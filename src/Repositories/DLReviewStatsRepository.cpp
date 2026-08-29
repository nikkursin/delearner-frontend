#include "DLReviewStatsRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Sync/DLSyncEventSerializer.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool execRepositoryQuery(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    if (error) {
        *error = query.lastError().text();
    }
    qCWarning(dlRepo) << "Failed repository query:" << query.lastError().text();
    return false;
}

DLSyncEventEnvelope reviewStatsEvent(const DLWordReviewStats& stats, const QString& operation)
{
    DLSyncEventEnvelope event;
    event.entityType = QStringLiteral("word_review_stats");
    event.entityId = stats.wordSyncId;
    event.operation = operation;
    event.updatedAt = stats.updatedAt;
    event.payload = DLSyncEventSerializer::payloadForReviewStats(stats, QString());
    return event;
}
}

DLReviewStatsRepository::DLReviewStatsRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLReviewStatsRepository::incrementCorrectAnswer(const QString& wordSyncId)
{
    return incrementAnswer(wordSyncId, QStringLiteral("correct_answers"));
}

bool DLReviewStatsRepository::incrementWrongAnswer(const QString& wordSyncId)
{
    return incrementAnswer(wordSyncId, QStringLiteral("wrong_answers"));
}

DLWordReviewStats DLReviewStatsRepository::fetchStats(const QString& wordSyncId)
{
    const QString trimmedSyncId = wordSyncId.trimmed();
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT w.id AS word_id,
                   w.sync_id AS word_sync_id,
                   COALESCE(rs.correct_answers, 0) AS correct_answers,
                   COALESCE(rs.wrong_answers, 0) AS wrong_answers,
                   rs.last_reviewed_at,
                   COALESCE(rs.ease_factor, 2.5) AS ease_factor,
                   COALESCE(rs.interval_days, 0) AS interval_days,
                   rs.due_at,
                   COALESCE(rs.updated_at, 0) AS updated_at
            FROM words w
            LEFT JOIN word_review_stats rs ON rs.word_sync_id = w.sync_id
            WHERE w.sync_id = :word_sync_id
              AND w.deleted_at IS NULL;
        )"),
        {{ QStringLiteral(":word_sync_id"), trimmedSyncId }});

    DLWordReviewStats stats;
    stats.wordId = row.value(QStringLiteral("word_id"), -1).toInt();
    stats.wordSyncId = row.value(QStringLiteral("word_sync_id"), trimmedSyncId).toString();
    stats.correctAnswers = row.value(QStringLiteral("correct_answers"), 0).toInt();
    stats.wrongAnswers = row.value(QStringLiteral("wrong_answers"), 0).toInt();
    stats.lastReviewedAt = row.value(QStringLiteral("last_reviewed_at"));
    stats.easeFactor = row.value(QStringLiteral("ease_factor"), 2.5).toDouble();
    stats.intervalDays = row.value(QStringLiteral("interval_days"), 0).toInt();
    stats.dueAt = row.value(QStringLiteral("due_at"));
    stats.updatedAt = row.value(QStringLiteral("updated_at")).toLongLong();
    return stats;
}

bool DLReviewStatsRepository::upsertStats(const DLWordReviewStats& stats)
{
    const QString wordSyncId = stats.wordSyncId.trimmed();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const int wordId = localWordIdForSyncId(db, error, wordSyncId);
        if (wordId < 0) {
            return false;
        }

        const qint64 now = DLDatabaseManager::currentUnixTime();
        const qint64 updatedAt = stats.updatedAt > 0 ? stats.updatedAt : now;
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:word_id, :word_sync_id,
                 :correct_answers, :wrong_answers, :last_reviewed_at,
                 :ease_factor, :interval_days, :due_at, :updated_at)
            ON CONFLICT(word_sync_id) DO UPDATE SET
                word_id = excluded.word_id,
                word_sync_id = excluded.word_sync_id,
                correct_answers = excluded.correct_answers,
                wrong_answers = excluded.wrong_answers,
                last_reviewed_at = excluded.last_reviewed_at,
                ease_factor = excluded.ease_factor,
                interval_days = excluded.interval_days,
                due_at = excluded.due_at,
                updated_at = excluded.updated_at;
        )"));
        query.bindValue(QStringLiteral(":word_id"), wordId);
        query.bindValue(QStringLiteral(":word_sync_id"), wordSyncId);
        query.bindValue(QStringLiteral(":correct_answers"), stats.correctAnswers);
        query.bindValue(QStringLiteral(":wrong_answers"), stats.wrongAnswers);
        query.bindValue(QStringLiteral(":last_reviewed_at"), stats.lastReviewedAt);
        query.bindValue(QStringLiteral(":ease_factor"), stats.easeFactor);
        query.bindValue(QStringLiteral(":interval_days"), stats.intervalDays);
        query.bindValue(QStringLiteral(":due_at"), stats.dueAt);
        query.bindValue(QStringLiteral(":updated_at"), updatedAt);
        if (!execRepositoryQuery(query, error)) {
            return false;
        }

        DLWordReviewStats stored = stats;
        stored.wordId = wordId;
        stored.wordSyncId = wordSyncId;
        stored.updatedAt = updatedAt;
        return m_database.recordSyncOutboxEvent(db, error, reviewStatsEvent(stored, QStringLiteral("update")));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to upsert review stats for word" << wordSyncId << ":" << m_database.lastError();
    }
    return success;
}

bool DLReviewStatsRepository::incrementAnswer(const QString& wordSyncId, const QString& columnName)
{
    const QString trimmedSyncId = wordSyncId.trimmed();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const int wordId = localWordIdForSyncId(db, error, trimmedSyncId);
        if (wordId < 0) {
            return false;
        }

        const qint64 now = DLDatabaseManager::currentUnixTime();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:word_id, :word_sync_id, %1, %2, :reviewed_at, 2.5, 0, NULL, :reviewed_at)
            ON CONFLICT(word_sync_id) DO UPDATE SET
                word_id = excluded.word_id,
                word_sync_id = excluded.word_sync_id,
                %3 = word_review_stats.%3 + 1,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at = excluded.updated_at;
        )").arg(columnName == QStringLiteral("correct_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName == QStringLiteral("wrong_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName));
        query.bindValue(QStringLiteral(":word_id"), wordId);
        query.bindValue(QStringLiteral(":word_sync_id"), trimmedSyncId);
        query.bindValue(QStringLiteral(":reviewed_at"), now);
        if (!execRepositoryQuery(query, error)) {
            return false;
        }

        DLWordReviewStats stored;
        stored.wordId = wordId;
        stored.wordSyncId = trimmedSyncId;
        stored.updatedAt = now;
        stored.lastReviewedAt = now;
        QSqlQuery select(db);
        select.prepare(QStringLiteral(R"(
            SELECT correct_answers, wrong_answers, ease_factor, interval_days, due_at
            FROM word_review_stats
            WHERE word_sync_id = :word_sync_id;
        )"));
        select.bindValue(QStringLiteral(":word_sync_id"), trimmedSyncId);
        if (!execRepositoryQuery(select, error) || !select.next()) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Review stats not found.");
            }
            return false;
        }
        stored.correctAnswers = select.value(0).toInt();
        stored.wrongAnswers = select.value(1).toInt();
        stored.easeFactor = select.value(2).toDouble();
        stored.intervalDays = select.value(3).toInt();
        stored.dueAt = select.value(4);
        return m_database.recordSyncOutboxEvent(db, error, reviewStatsEvent(stored, QStringLiteral("update")));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to increment review stats for word" << trimmedSyncId << ":" << m_database.lastError();
    }
    return success;
}

int DLReviewStatsRepository::localWordIdForSyncId(QSqlDatabase& db, QString* error, const QString& wordSyncId)
{
    if (wordSyncId.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Word not found.");
        }
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM words WHERE sync_id = :word_sync_id AND deleted_at IS NULL;"));
    query.bindValue(QStringLiteral(":word_sync_id"), wordSyncId.trimmed());
    if (!execRepositoryQuery(query, error)) {
        return -1;
    }
    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("Word not found.");
        }
        return -1;
    }
    return query.value(0).toInt();
}
