#include "DLGroupRepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "DLOutboundSyncQueueRepository.h"
#include "../Models/DLModelMappers.h"

namespace {
QString groupPayloadJson(const DLWordGroup& group, const QString& id)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("id"), id);
    payload.insert(QStringLiteral("name"), group.name);
    payload.insert(QStringLiteral("colorHex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex);
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

bool execGroupQuery(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    if (error) {
        *error = query.lastError().text();
    }
    qCWarning(dlRepo) << "Failed group repository query:" << query.lastError().text();
    return false;
}
}

DLGroupRepository::DLGroupRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLGroupRepository::insertGroup(const DLWordGroup& group)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString id = group.id.trimmed().isEmpty() ? DLDatabaseManager::generateUuid() : group.id.trimmed();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO groups
                (sync_id, name, color_hex, created_at, updated_at,
                 deleted_at, server_updated_at, server_version, device_id, dirty)
            VALUES
                (:sync_id, :name, :color_hex, :created_at, :updated_at,
                 NULL, NULL, 0, :device_id, 1);
        )"));
        query.bindValue(QStringLiteral(":sync_id"), id);
        query.bindValue(QStringLiteral(":name"), group.name);
        query.bindValue(QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex);
        query.bindValue(QStringLiteral(":created_at"), now);
        query.bindValue(QStringLiteral(":updated_at"), now);
        query.bindValue(QStringLiteral(":device_id"), deviceId);

        return execGroupQuery(query, error)
            && DLOutboundSyncQueueRepository::enqueue(db,
                                                      error,
                                                      QStringLiteral("groups"),
                                                      id,
                                                      QStringLiteral("create"),
                                                      groupPayloadJson(group, id));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to insert group:" << m_database.lastError();
        return {};
    }

    qCDebug(dlRepo) << "Inserted group with id" << id;
    return id;
}

bool DLGroupRepository::updateGroup(const DLWordGroup& group)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            UPDATE groups
            SET name = :name,
                color_hex = :color_hex,
                updated_at = :updated_at,
                device_id = :device_id,
                dirty = 1
            WHERE sync_id = :id;
        )"));
        query.bindValue(QStringLiteral(":id"), group.id);
        query.bindValue(QStringLiteral(":name"), group.name);
        query.bindValue(QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex);
        query.bindValue(QStringLiteral(":updated_at"), now);
        query.bindValue(QStringLiteral(":device_id"), deviceId);

        if (!execGroupQuery(query, error)) {
            return false;
        }
        if (query.numRowsAffected() <= 0) {
            return true;
        }

        return DLOutboundSyncQueueRepository::enqueue(db,
                                                      error,
                                                      QStringLiteral("groups"),
                                                      group.id,
                                                      QStringLiteral("update"),
                                                      groupPayloadJson(group, group.id));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update group" << group.id << ":" << m_database.lastError();
    }
    return success;
}

bool DLGroupRepository::deleteGroup(const QString& id)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery deleteGroup(db);
        deleteGroup.prepare(QStringLiteral(R"(
            UPDATE groups
            SET deleted_at = COALESCE(deleted_at, :deleted_at),
                updated_at = :deleted_at,
                device_id = :device_id,
                dirty = 1
            WHERE sync_id = :id
              AND deleted_at IS NULL;
        )"));
        deleteGroup.bindValue(QStringLiteral(":id"), id);
        deleteGroup.bindValue(QStringLiteral(":deleted_at"), now);
        deleteGroup.bindValue(QStringLiteral(":device_id"), deviceId);

        if (!execGroupQuery(deleteGroup, error)) {
            return false;
        }
        const bool groupChanged = deleteGroup.numRowsAffected() > 0;

        QSqlQuery updateWords(db);
        updateWords.prepare(QStringLiteral(R"(
            UPDATE words
            SET group_id = NULL,
                updated_at = :updated_at,
                device_id = :device_id,
                dirty = 1
            WHERE group_id = (
                SELECT id
                FROM groups
                WHERE sync_id = :id
            )
              AND deleted_at IS NULL;
        )"));
        updateWords.bindValue(QStringLiteral(":id"), id);
        updateWords.bindValue(QStringLiteral(":updated_at"), now);
        updateWords.bindValue(QStringLiteral(":device_id"), deviceId);

        if (!execGroupQuery(updateWords, error) || !groupChanged) {
            return !error || error->isEmpty();
        }

        return DLOutboundSyncQueueRepository::enqueue(db,
                                                      error,
                                                      QStringLiteral("groups"),
                                                      id,
                                                      QStringLiteral("delete"));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete group" << id << ":" << m_database.lastError();
    }
    return success;
}

QList<DLWordGroup> DLGroupRepository::fetchAllGroups()
{
    const QVariantList rows = m_database.selectRows(QStringLiteral(R"(
        SELECT g.sync_id AS id, g.id AS local_id, g.name, g.color_hex,
               g.created_at, g.updated_at, g.deleted_at, g.server_updated_at,
               g.server_version, g.device_id, g.dirty,
               (SELECT COUNT(*) FROM words WHERE group_id = g.id AND deleted_at IS NULL) AS word_count
        FROM groups g
        WHERE g.deleted_at IS NULL
        ORDER BY g.name;
    )"));

    QList<DLWordGroup> groups;
    for (const QVariant& row : rows) {
        groups.append(DLModelMappers::groupFromMap(row.toMap()));
    }
    return groups;
}

DLWordGroup DLGroupRepository::fetchGroupById(const QString& id)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT g.sync_id AS id, g.id AS local_id, g.name, g.color_hex,
                   g.created_at, g.updated_at, g.deleted_at, g.server_updated_at,
                   g.server_version, g.device_id, g.dirty,
                   (SELECT COUNT(*) FROM words WHERE group_id = g.id AND deleted_at IS NULL) AS word_count
            FROM groups g
            WHERE g.sync_id = :id
              AND g.deleted_at IS NULL;
        )"),
        {{ QStringLiteral(":id"), id }});

    return DLModelMappers::groupFromMap(row);
}

int DLGroupRepository::getGroupCount()
{
    return m_database.selectInt(QStringLiteral("SELECT COUNT(*) FROM groups WHERE deleted_at IS NULL;"));
}
