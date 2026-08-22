#include "DLReviewStatsRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"

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
    const int wordId = localWordIdForSyncId(wordSyncId);
    if (wordId < 0) {
        qCWarning(dlRepo) << "Cannot upsert review stats for missing word sync id" << wordSyncId;
        return false;
    }

    const qint64 now = DLDatabaseManager::currentUnixTime();
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
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
        )"),
        {
            { QStringLiteral(":word_id"), wordId },
            { QStringLiteral(":word_sync_id"), wordSyncId },
            { QStringLiteral(":correct_answers"), stats.correctAnswers },
            { QStringLiteral(":wrong_answers"), stats.wrongAnswers },
            { QStringLiteral(":last_reviewed_at"), stats.lastReviewedAt },
            { QStringLiteral(":ease_factor"), stats.easeFactor },
            { QStringLiteral(":interval_days"), stats.intervalDays },
            { QStringLiteral(":due_at"), stats.dueAt },
            { QStringLiteral(":updated_at"), stats.updatedAt > 0 ? stats.updatedAt : now }
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to upsert review stats for word" << wordSyncId << ":" << m_database.lastError();
    }
    return success;
}

bool DLReviewStatsRepository::incrementAnswer(const QString& wordSyncId, const QString& columnName)
{
    const QString trimmedSyncId = wordSyncId.trimmed();
    const int wordId = localWordIdForSyncId(trimmedSyncId);
    if (wordId < 0) {
        qCWarning(dlRepo) << "Cannot increment review stats for missing word sync id" << trimmedSyncId;
        return false;
    }

    const qint64 now = DLDatabaseManager::currentUnixTime();
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
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
               columnName),
        {
            { QStringLiteral(":word_id"), wordId },
            { QStringLiteral(":word_sync_id"), trimmedSyncId },
            { QStringLiteral(":reviewed_at"), now }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to increment review stats for word" << trimmedSyncId << ":" << m_database.lastError();
    }
    return success;
}

int DLReviewStatsRepository::localWordIdForSyncId(const QString& wordSyncId)
{
    if (wordSyncId.trimmed().isEmpty()) {
        return -1;
    }

    return m_database.selectInt(
        QStringLiteral("SELECT id FROM words WHERE sync_id = :word_sync_id AND deleted_at IS NULL;"),
        {{ QStringLiteral(":word_sync_id"), wordSyncId.trimmed() }},
        -1);
}
