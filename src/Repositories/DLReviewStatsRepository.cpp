#include "DLReviewStatsRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"

DLReviewStatsRepository::DLReviewStatsRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLReviewStatsRepository::incrementCorrectAnswer(int wordId)
{
    return incrementAnswer(wordId, QStringLiteral("correct_answers"));
}

bool DLReviewStatsRepository::incrementWrongAnswer(int wordId)
{
    return incrementAnswer(wordId, QStringLiteral("wrong_answers"));
}

DLWordReviewStats DLReviewStatsRepository::fetchStats(int wordId)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT word_id, correct_answers, wrong_answers, last_reviewed_at,
                   ease_factor, interval_days, due_at, updated_at
            FROM word_review_stats
            WHERE word_id = :word_id;
        )"),
        {{ QStringLiteral(":word_id"), wordId }});

    DLWordReviewStats stats;
    stats.wordId = row.value(QStringLiteral("word_id"), wordId).toInt();
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
    const qint64 now = DLDatabaseManager::currentUnixTime();
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:word_id, :correct_answers, :wrong_answers, :last_reviewed_at,
                 :ease_factor, :interval_days, :due_at, :updated_at)
            ON CONFLICT(word_id) DO UPDATE SET
                correct_answers = excluded.correct_answers,
                wrong_answers = excluded.wrong_answers,
                last_reviewed_at = excluded.last_reviewed_at,
                ease_factor = excluded.ease_factor,
                interval_days = excluded.interval_days,
                due_at = excluded.due_at,
                updated_at = excluded.updated_at;
        )"),
        {
            { QStringLiteral(":word_id"), stats.wordId },
            { QStringLiteral(":correct_answers"), stats.correctAnswers },
            { QStringLiteral(":wrong_answers"), stats.wrongAnswers },
            { QStringLiteral(":last_reviewed_at"), stats.lastReviewedAt },
            { QStringLiteral(":ease_factor"), stats.easeFactor },
            { QStringLiteral(":interval_days"), stats.intervalDays },
            { QStringLiteral(":due_at"), stats.dueAt },
            { QStringLiteral(":updated_at"), stats.updatedAt > 0 ? stats.updatedAt : now }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to upsert review stats for word" << stats.wordId << ":" << m_database.lastError();
    }
    return success;
}

bool DLReviewStatsRepository::incrementAnswer(int wordId, const QString& columnName)
{
    const qint64 now = DLDatabaseManager::currentUnixTime();
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:word_id, %1, %2, :reviewed_at, 2.5, 0, NULL, :reviewed_at)
            ON CONFLICT(word_id) DO UPDATE SET
                %3 = word_review_stats.%3 + 1,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at = excluded.updated_at;
        )").arg(columnName == QStringLiteral("correct_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName == QStringLiteral("wrong_answers") ? QStringLiteral("1") : QStringLiteral("0"),
               columnName),
        {
            { QStringLiteral(":word_id"), wordId },
            { QStringLiteral(":reviewed_at"), now }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to increment review stats for word" << wordId << ":" << m_database.lastError();
    }
    return success;
}
