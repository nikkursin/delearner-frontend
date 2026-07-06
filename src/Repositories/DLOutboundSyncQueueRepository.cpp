#include "DLOutboundSyncQueueRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "DLDatabaseManager.h"
#include "DLLogging.h"

namespace {
thread_local bool s_queueingEnabled = true;

bool isValidOperation(const QString& operation)
{
    static const QStringList operations = {
        QStringLiteral("create"),
        QStringLiteral("update"),
        QStringLiteral("delete")
    };
    return operations.contains(operation);
}
}

bool DLOutboundSyncQueueRepository::enqueue(QSqlDatabase& db,
                                            QString* error,
                                            const QString& entityType,
                                            const QString& entityId,
                                            const QString& operation,
                                            const QString& payloadJson)
{
    if (!s_queueingEnabled) {
        return true;
    }

    const QString trimmedEntityType = entityType.trimmed();
    const QString trimmedEntityId = entityId.trimmed();
    const QString trimmedOperation = operation.trimmed().toLower();

    if (trimmedEntityType.isEmpty() || trimmedEntityId.isEmpty() || !isValidOperation(trimmedOperation)) {
        if (error) {
            *error = QStringLiteral("Invalid outbound sync queue entry.");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO outbound_sync_queue
            (id, entity_type, entity_id, operation, payload_json, created_at,
             retry_count, last_error, pushed_at)
        VALUES
            (:id, :entity_type, :entity_id, :operation, :payload_json, :created_at,
             0, NULL, NULL);
    )"));
    query.bindValue(QStringLiteral(":id"), DLDatabaseManager::generateUuid());
    query.bindValue(QStringLiteral(":entity_type"), trimmedEntityType);
    query.bindValue(QStringLiteral(":entity_id"), trimmedEntityId);
    query.bindValue(QStringLiteral(":operation"), trimmedOperation);
    query.bindValue(QStringLiteral(":payload_json"), payloadJson.isNull() ? DLDatabaseManager::nullVariant() : QVariant(payloadJson));
    query.bindValue(QStringLiteral(":created_at"), DLDatabaseManager::currentUnixTimeMs());

    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        qCWarning(dlRepo) << "Failed to enqueue outbound sync operation:"
                          << trimmedEntityType << trimmedEntityId << trimmedOperation
                          << query.lastError().text();
        return false;
    }

    return true;
}

bool DLOutboundSyncQueueRepository::queueingEnabled()
{
    return s_queueingEnabled;
}

void DLOutboundSyncQueueRepository::setQueueingEnabled(bool enabled)
{
    s_queueingEnabled = enabled;
}

DLOutboundSyncQueueScope::DLOutboundSyncQueueScope(bool enabled)
    : m_previousEnabled(DLOutboundSyncQueueRepository::queueingEnabled())
{
    DLOutboundSyncQueueRepository::setQueueingEnabled(enabled);
}

DLOutboundSyncQueueScope::~DLOutboundSyncQueueScope()
{
    DLOutboundSyncQueueRepository::setQueueingEnabled(m_previousEnabled);
}
