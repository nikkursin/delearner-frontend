#ifndef DLOUTBOUNDSYNCQUEUEREPOSITORY_H
#define DLOUTBOUNDSYNCQUEUEREPOSITORY_H

#include <QString>

class QSqlDatabase;

class DLOutboundSyncQueueRepository
{
public:
    static bool enqueue(QSqlDatabase& db,
                        QString* error,
                        const QString& entityType,
                        const QString& entityId,
                        const QString& operation,
                        const QString& payloadJson = QString());

    static bool queueingEnabled();
    static void setQueueingEnabled(bool enabled);
};

class DLOutboundSyncQueueScope
{
public:
    explicit DLOutboundSyncQueueScope(bool enabled);
    ~DLOutboundSyncQueueScope();

    DLOutboundSyncQueueScope(const DLOutboundSyncQueueScope&) = delete;
    DLOutboundSyncQueueScope& operator=(const DLOutboundSyncQueueScope&) = delete;

private:
    bool m_previousEnabled;
};

#endif // DLOUTBOUNDSYNCQUEUEREPOSITORY_H
