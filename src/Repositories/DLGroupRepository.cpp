#include "DLGroupRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Models/DLModelMappers.h"

#include <QUuid>

DLGroupRepository::DLGroupRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

int DLGroupRepository::insertGroup(const DLWordGroup& group)
{
    const qint64 now = DLDatabaseManager::currentUnixTime();
    const QString syncId = group.syncId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : group.syncId.trimmed();
    const int newId = m_database.executeInsert(
        QStringLiteral(R"(
            INSERT INTO groups (sync_id, name, color_hex, created_at, updated_at)
            VALUES (:sync_id, :name, :color_hex, :created_at, :updated_at);
        )"),
        {
            { QStringLiteral(":sync_id"), syncId },
            { QStringLiteral(":name"), group.name },
            { QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex },
            { QStringLiteral(":created_at"), now },
            { QStringLiteral(":updated_at"), now }
        });
    if (newId < 0) {
        qCWarning(dlRepo) << "Failed to insert group:" << m_database.lastError();
    } else {
        qCDebug(dlRepo) << "Inserted group with id" << newId;
    }
    return newId;
}

bool DLGroupRepository::updateGroup(const DLWordGroup& group)
{
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
            UPDATE groups
            SET name = :name,
                color_hex = :color_hex,
                updated_at = :updated_at
            WHERE id = :id;
        )"),
        {
            { QStringLiteral(":id"), group.id },
            { QStringLiteral(":name"), group.name },
            { QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex },
            { QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTime() }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update group" << group.id << ":" << m_database.lastError();
    }
    return success;
}

bool DLGroupRepository::deleteGroup(int id)
{
    const bool success = m_database.executeSql(
        QStringLiteral("DELETE FROM groups WHERE id = :id;"),
        {{ QStringLiteral(":id"), id }});
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete group" << id << ":" << m_database.lastError();
    }
    return success;
}

QList<DLWordGroup> DLGroupRepository::fetchAllGroups()
{
    const QVariantList rows = m_database.selectRows(QStringLiteral(R"(
        SELECT g.id, g.name, g.color_hex, g.created_at, g.updated_at,
               g.sync_id,
               (SELECT COUNT(*) FROM words WHERE group_id = g.id AND deleted_at IS NULL) AS word_count
        FROM groups g
        ORDER BY g.name;
    )"));

    QList<DLWordGroup> groups;
    for (const QVariant& row : rows) {
        groups.append(DLModelMappers::groupFromMap(row.toMap()));
    }
    return groups;
}

DLWordGroup DLGroupRepository::fetchGroupById(int id)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT g.id, g.name, g.color_hex, g.created_at, g.updated_at,
                   g.sync_id,
                   (SELECT COUNT(*) FROM words WHERE group_id = g.id AND deleted_at IS NULL) AS word_count
            FROM groups g
            WHERE g.id = :id;
        )"),
        {{ QStringLiteral(":id"), id }});

    return DLModelMappers::groupFromMap(row);
}

int DLGroupRepository::getGroupCount()
{
    return m_database.selectInt(QStringLiteral("SELECT COUNT(*) FROM groups;"));
}
