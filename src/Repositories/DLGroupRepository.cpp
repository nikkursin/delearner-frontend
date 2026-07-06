#include "DLGroupRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Models/DLModelMappers.h"

DLGroupRepository::DLGroupRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLGroupRepository::insertGroup(const DLWordGroup& group)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString id = group.id.trimmed().isEmpty() ? DLDatabaseManager::generateUuid() : group.id.trimmed();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const int newId = m_database.executeInsert(
        QStringLiteral(R"(
            INSERT INTO groups
                (sync_id, name, color_hex, created_at, updated_at,
                 deleted_at, server_updated_at, server_version, device_id, dirty)
            VALUES
                (:sync_id, :name, :color_hex, :created_at, :updated_at,
                 NULL, NULL, 0, :device_id, 1);
        )"),
        {
            { QStringLiteral(":sync_id"), id },
            { QStringLiteral(":name"), group.name },
            { QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex },
            { QStringLiteral(":created_at"), now },
            { QStringLiteral(":updated_at"), now },
            { QStringLiteral(":device_id"), deviceId }
        });
    if (newId < 0) {
        qCWarning(dlRepo) << "Failed to insert group:" << m_database.lastError();
        return {};
    } else {
        qCDebug(dlRepo) << "Inserted group with id" << id;
    }
    return id;
}

bool DLGroupRepository::updateGroup(const DLWordGroup& group)
{
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
            UPDATE groups
            SET name = :name,
                color_hex = :color_hex,
                updated_at = :updated_at,
                device_id = :device_id,
                dirty = 1
            WHERE sync_id = :id;
        )"),
        {
            { QStringLiteral(":id"), group.id },
            { QStringLiteral(":name"), group.name },
            { QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex },
            { QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTimeMs() },
            { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update group" << group.id << ":" << m_database.lastError();
    }
    return success;
}

bool DLGroupRepository::deleteGroup(const QString& id)
{
    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const bool success = m_database.executeSqlBatch({
        {
            QStringLiteral(R"(
                UPDATE groups
                SET deleted_at = COALESCE(deleted_at, :deleted_at),
                    updated_at = :deleted_at,
                    device_id = :device_id,
                    dirty = 1
                WHERE sync_id = :id
                  AND deleted_at IS NULL;
            )"),
            {
                { QStringLiteral(":id"), id },
                { QStringLiteral(":deleted_at"), now },
                { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
            }
        },
        {
            QStringLiteral(R"(
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
            )"),
            {
                { QStringLiteral(":id"), id },
                { QStringLiteral(":updated_at"), now },
                { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
            }
        }
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
