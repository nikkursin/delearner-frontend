#ifndef DLREVIEWSTATSREPOSITORY_H
#define DLREVIEWSTATSREPOSITORY_H

#include "../Models/DLWordReviewStats.h"

class DLDatabaseManager;
class QSqlDatabase;
class QString;

class DLReviewStatsRepository
{
public:
    explicit DLReviewStatsRepository(DLDatabaseManager& database);

    bool incrementCorrectAnswer(const QString& wordSyncId);
    bool incrementWrongAnswer(const QString& wordSyncId);
    DLWordReviewStats fetchStats(const QString& wordSyncId);
    bool upsertStats(const DLWordReviewStats& stats);

private:
    bool incrementAnswer(const QString& wordSyncId, const QString& columnName);
    int localWordIdForSyncId(QSqlDatabase& db, QString* error, const QString& wordSyncId);

    DLDatabaseManager& m_database;
};

#endif // DLREVIEWSTATSREPOSITORY_H
