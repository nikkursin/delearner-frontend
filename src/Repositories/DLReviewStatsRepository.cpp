#include "DLReviewStatsRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"

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
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
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
        )"),
        {
            { QStringLiteral(":word_id"), stats.wordId },
            { QStringLiteral(":sync_id"), stats.id },
            { QStringLiteral(":correct_answers"), stats.correctAnswers },
            { QStringLiteral(":wrong_answers"), stats.wrongAnswers },
            { QStringLiteral(":last_reviewed_at"), stats.lastReviewedAt },
            { QStringLiteral(":ease_factor"), stats.easeFactor },
            { QStringLiteral(":interval_days"), stats.intervalDays },
            { QStringLiteral(":due_at"), stats.dueAt },
            { QStringLiteral(":created_at"), stats.createdAt > 0 ? stats.createdAt : now },
            { QStringLiteral(":updated_at"), stats.updatedAt > 0 ? stats.updatedAt : now },
            { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to upsert review stats for word" << stats.wordId << ":" << m_database.lastError();
    }
    return success;
}

bool DLReviewStatsRepository::incrementAnswer(const QString& wordId, const QString& columnName)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
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
               columnName),
        {
            { QStringLiteral(":word_id"), wordId },
            { QStringLiteral(":reviewed_at"), now },
            { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to increment review stats for word" << wordId << ":" << m_database.lastError();
    }
    return success;
}
