#ifndef DLREMOTECHANGERECONCILER_H
#define DLREMOTECHANGERECONCILER_H

#include <QList>
#include <QString>
#include <QVariantMap>

#include "DLSyncEventSerializer.h"

class DLDatabaseManager;
class QSqlDatabase;

class DLRemoteChangeReconciler
{
public:
    explicit DLRemoteChangeReconciler(DLDatabaseManager& database);

    bool applyRemoteEvent(const DLSyncEventEnvelope& event, QString* error = nullptr);
    bool applyRemoteEvents(const QList<DLSyncEventEnvelope>& events, QString* error = nullptr);
    bool applyRemoteEventsAndAdvanceCursor(const QList<DLSyncEventEnvelope>& events,
                                           qint64 consumedSequence,
                                           QString* error = nullptr);
    bool applyDownloadedEventsAndAdvanceCursor(const QVariantMap& downloadResponse,
                                               QString* error = nullptr);
    bool replaceLocalStateWithRemoteEventsAndAdvanceCursor(const QList<DLSyncEventEnvelope>& events,
                                                           qint64 consumedSequence,
                                                           QString* error = nullptr);

private:
    bool applyRemoteEventInTransaction(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool recordRemoteCursor(QSqlDatabase& db, qint64 consumedSequence, QString* error);
    bool localOutboxIsEmpty(QSqlDatabase& db, QString* error);
    bool clearLocalSyncableState(QSqlDatabase& db, QString* error);

    bool applyPayloadEvent(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool applyTombstoneEvent(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);

    bool upsertGroup(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool upsertWord(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool upsertReviewStats(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);

    bool tombstoneGroup(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool tombstoneWord(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);
    bool deleteReviewStats(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error);

    DLDatabaseManager& m_database;
};

#endif // DLREMOTECHANGERECONCILER_H
