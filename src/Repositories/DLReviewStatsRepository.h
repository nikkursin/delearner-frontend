#ifndef DLREVIEWSTATSREPOSITORY_H
#define DLREVIEWSTATSREPOSITORY_H

#include "../Models/DLWordReviewStats.h"

class DLDatabaseManager;
class QString;

class DLReviewStatsRepository
{
public:
    explicit DLReviewStatsRepository(DLDatabaseManager& database);

    bool incrementCorrectAnswer(const QString& wordId);
    bool incrementWrongAnswer(const QString& wordId);
    DLWordReviewStats fetchStats(const QString& wordId);
    bool upsertStats(const DLWordReviewStats& stats);

private:
    bool incrementAnswer(const QString& wordId, const QString& columnName);

    DLDatabaseManager& m_database;
};

#endif // DLREVIEWSTATSREPOSITORY_H
