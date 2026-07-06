#include "DLReviewStatsRepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "DLOutboundSyncQueueRepository.h"

namespace {
QString statsPayloadJson(const DLWordReviewStats& stats)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), stats.id);
    payload.insert(QStringLiteral("wordId"), stats.wordId);
    payload.insert(QStringLiteral("correctAnswers"), stats.correctAnswers);
    payload.insert(QStringLiteral("wrongAnswers"), stats.wrongAnswers);
    payload.insert(QStringLiteral("lastReviewedAt"), QJsonValue::fromVariant(stats.lastReviewedAt));
    payload.insert(QStringLiteral("easeFactor"), stats.easeFactor);
    payload.insert(QStringLiteral("intervalDays"), stats.intervalDays);
    payload.insert(QStringLiteral("dueAt"), QJsonValue::fromVariant(stats.dueAt));
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

bool execStatsQuery(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    if (error) {
        *error = query.lastError().text();
    }
    qCWarning(dlRepo) << "Failed review stats repository query:" << query.lastError().text();
    return false;
}

QString statsSyncIdForWord(QSqlDatabase& db, const QString& wordId, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        SELECT rs.sync_id
        FROM word_review_stats rs
        JOIN words w ON w.id = rs.word_id
        WHERE w.sync_id = :word_id;
    )"));
    query.bindValue(QStringLiteral(":word_id"), wordId);
    if (!execStatsQuery(query, error)) {
        return {};
    }
    return query.next() ? query.value(0).toString() : QString();
}
}

DLReviewStatsRepository::DLReviewStatsRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLReviewStatsRepository::incrementCorrectAnswer(const QString& wordId)
{
    return incrementAnswer(wordId, QStringLiteral("correct_answers"));
}

bool DLReviewStatsRepository::incrementWrongAnswer(const QString& wordId)
{
    return incrementAnswer(wordId, QStringLiteral("wrong_answers"));
}

DLWordReviewStats DLReviewStatsRepository::fetchStats(const QString& wordId)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT rs.sync_id AS id, w.sync_id AS word_id,
                   rs.correct_answers, rs.wrong_answers, rs.last_reviewed_at,
                   rs.ease_factor, rs.interval_days, rs.due_at,
                   rs.created_at, rs.updated_at, rs.deleted_at,
                   rs.server_updated_at, rs.server_version, rs.device_id, rs.dirty
            FROM word_review_stats rs
            JOIN words w ON w.id = rs.word_id
            WHERE w.sync_id = :word_id
              AND rs.deleted_at IS NULL;
        )"),
        {{ QStringLiteral(":word_id"), wordId }});

    DLWordReviewStats stats;
    stats.id = row.value(QStringLiteral("id")).toString();
    stats.wordId = row.value(QStringLiteral("word_id"), wordId).toString();
    stats.correctAnswers = row.value(QStringLiteral("correct_answers"), 0).toInt();
    stats.wrongAnswers = row.value(QStringLiteral("wrong_answers"), 0).toInt();
    stats.lastReviewedAt = row.value(QStringLiteral("last_reviewed_at"));
    stats.easeFactor = row.value(QStringLiteral("ease_factor"), 2.5).toDouble();
    stats.intervalDays = row.value(QStringLiteral("interval_days"), 0).toInt();
    stats.dueAt = row.value(QStringLiteral("due_at"));
    stats.createdAt = row.value(QStringLiteral("created_at")).toLongLong();
    stats.updatedAt = row.value(QStringLiteral("updated_at")).toLongLong();
    stats.deletedAt = row.value(QStringLiteral("deleted_at"));
    stats.serverUpdatedAt = row.value(QStringLiteral("server_updated_at"));
    stats.serverVersion = row.value(QStringLiteral("server_version"), 0).toInt();
    stats.deviceId = row.value(QStringLiteral("device_id")).toString();
    stats.dirty = row.value(QStringLiteral("dirty"), true).toBool();
    return stats;
}

bool DLReviewStatsRepository::upsertStats(const DLWordReviewStats& stats)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral(R"(
            SELECT COUNT(*)
            FROM word_review_stats rs
            JOIN words w ON w.id = rs.word_id
            WHERE w.sync_id = :word_id;
        )"));
        exists.bindValue(QStringLiteral(":word_id"), stats.wordId);
        if (!execStatsQuery(exists, error) || !exists.next()) {
            return false;
        }
        const bool alreadyExists = exists.value(0).toInt() > 0;

        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, created_at, updated_at,
                 deleted_at, server_updated_at, server_version, device_id, dirty)
            VALUES
                ((SELECT id FROM words WHERE sync_id = :word_id),
                 COALESCE(NULLIF(:sync_id, ''), lower(hex(randomblob(4))) || '-' ||
                     lower(hex(randomblob(2))) || '-' ||
                     '4' || substr(lower(hex(randomblob(2))), 2) || '-' ||
                     substr('89ab', abs(random()) % 4 + 1, 1) ||
                     substr(lower(hex(randomblob(2))), 2) || '-' ||
                     lower(hex(randomblob(6)))),
                 :correct_answers, :wrong_answers, :last_reviewed_at,
                 :ease_factor, :interval_days, :due_at, :created_at, :updated_at,
                 NULL, NULL, 0, :device_id, 1)
            ON CONFLICT(word_id) DO UPDATE SET
                correct_answers = excluded.correct_answers,
                wrong_answers = excluded.wrong_answers,
                last_reviewed_at = excluded.last_reviewed_at,
                ease_factor = excluded.ease_factor,
                interval_days = excluded.interval_days,
                due_at = excluded.due_at,
                updated_at = excluded.updated_at,
                device_id = excluded.device_id,
                deleted_at = NULL,
                dirty = 1;
        )"));
        query.bindValue(QStringLiteral(":word_id"), stats.wordId);
        query.bindValue(QStringLiteral(":sync_id"), stats.id);
        query.bindValue(QStringLiteral(":correct_answers"), stats.correctAnswers);
        query.bindValue(QStringLiteral(":wrong_answers"), stats.wrongAnswers);
        query.bindValue(QStringLiteral(":last_reviewed_at"), stats.lastReviewedAt);
        query.bindValue(QStringLiteral(":ease_factor"), stats.easeFactor);
        query.bindValue(QStringLiteral(":interval_days"), stats.intervalDays);
        query.bindValue(QStringLiteral(":due_at"), stats.dueAt);
        query.bindValue(QStringLiteral(":created_at"), stats.createdAt > 0 ? stats.createdAt : now);
        query.bindValue(QStringLiteral(":updated_at"), stats.updatedAt > 0 ? stats.updatedAt : now);
        query.bindValue(QStringLiteral(":device_id"), deviceId);
        if (!execStatsQuery(query, error)) {
            return false;
        }

        DLWordReviewStats queuedStats = stats;
        queuedStats.id = statsSyncIdForWord(db, stats.wordId, error);
        if ((error && !error->isEmpty()) || queuedStats.id.isEmpty()) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Review stats not found after upsert.");
            }
            return false;
        }

        return DLOutboundSyncQueueRepository::enqueue(db,
                                                      error,
                                                      QStringLiteral("word_review_stats"),
                                                      queuedStats.id,
                                                      alreadyExists ? QStringLiteral("update") : QStringLiteral("create"),
                                                      statsPayloadJson(queuedStats));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to upsert review stats for word" << stats.wordId << ":" << m_database.lastError();
    }
    return success;
}

bool DLReviewStatsRepository::incrementAnswer(const QString& wordId, const QString& columnName)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral(R"(
            SELECT COUNT(*)
            FROM word_review_stats rs
            JOIN words w ON w.id = rs.word_id
            WHERE w.sync_id = :word_id;
        )"));
        exists.bindValue(QStringLiteral(":word_id"), wordId);
        if (!execStatsQuery(exists, error) || !exists.next()) {
            return false;
        }
        const bool alreadyExists = exists.value(0).toInt() > 0;

        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, created_at, updated_at,
                 deleted_at, server_updated_at, server_version, device_id, dirty)
            VALUES
                ((SELECT id FROM words WHERE sync_id = :word_id),
                 lower(hex(randomblob(4))) || '-' ||
                 lower(hex(randomblob(2))) || '-' ||
                 '4' || substr(lower(hex(randomblob(2))), 2) || '-' ||
                 substr('89ab', abs(random()) % 4 + 1, 1) ||
                 substr(lower(hex(randomblob(2))), 2) || '-' ||
                 lower(hex(randomblob(6))),
                 %1, %2, :reviewed_at, 2.5, 0, NULL,
                 :reviewed_at, :reviewed_at, NULL, NULL, 0, :device_id, 1)
            ON CONFLICT(word_id) DO UPDATE SET
                %3 = word_review_stats.%3 + 1,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at = excluded.updated_at,
                device_id = excluded.device_id,
                deleted_at = NULL,
                dirty = 1;
        )").arg(columnName == QStringLiteral("correct_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName == QStringLiteral("wrong_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName));
        query.bindValue(QStringLiteral(":word_id"), wordId);
        query.bindValue(QStringLiteral(":reviewed_at"), now);
        query.bindValue(QStringLiteral(":device_id"), deviceId);
        if (!execStatsQuery(query, error)) {
            return false;
        }

        const QString statsId = statsSyncIdForWord(db, wordId, error);
        if ((error && !error->isEmpty()) || statsId.isEmpty()) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Review stats not found after increment.");
            }
            return false;
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("wordId"), wordId);
        payload.insert(QStringLiteral("incrementedColumn"), columnName);
        payload.insert(QStringLiteral("reviewedAt"), now);
        return DLOutboundSyncQueueRepository::enqueue(
            db,
            error,
            QStringLiteral("word_review_stats"),
            statsId,
            alreadyExists ? QStringLiteral("update") : QStringLiteral("create"),
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to increment review stats for word" << wordId << ":" << m_database.lastError();
    }
    return success;
}
