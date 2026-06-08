#ifndef DLREVIEWSTATSREPOSITORY_H
#define DLREVIEWSTATSREPOSITORY_H

#include "../Models/DLWordReviewStats.h"

class DLDatabaseManager;
class QString;

class DLReviewStatsRepository
{
public:
    explicit DLReviewStatsRepository(DLDatabaseManager& database);

    bool incrementCorrectAnswer(int wordId);
    bool incrementWrongAnswer(int wordId);
    DLWordReviewStats fetchStats(int wordId);
    bool upsertStats(const DLWordReviewStats& stats);

private:
    bool incrementAnswer(int wordId, const QString& columnName);

    DLDatabaseManager& m_database;
};

#endif // DLREVIEWSTATSREPOSITORY_H
