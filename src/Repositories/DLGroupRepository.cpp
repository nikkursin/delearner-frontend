#include "DLGroupRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "DLWordRepository.h"
#include "../Models/DLModelMappers.h"
#include "../Sync/DLSyncEventSerializer.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace {
bool execRepositoryQuery(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    if (error) {
        *error = query.lastError().text();
    }
    qCWarning(dlRepo) << "Failed repository query:" << query.lastError().text();
    return false;
}

QVariantMap currentRow(QSqlQuery& query)
{
    QVariantMap row;
    const QSqlRecord record = query.record();
    for (int i = 0; i < record.count(); ++i) {
        row.insert(record.fieldName(i), query.value(i));
    }
    return row;
}

DLWordGroup fetchGroupForOutbox(QSqlDatabase& db, QString* error, const QString& syncId, bool includeDeleted = false)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        SELECT id, name, color_hex, created_at, updated_at, deleted_at, sync_id
        FROM groups
        WHERE sync_id = :sync_id %1;
    )").arg(includeDeleted ? QString() : QStringLiteral("AND deleted_at IS NULL")));
    query.bindValue(QStringLiteral(":sync_id"), syncId);
    if (!execRepositoryQuery(query, error)) {
        return {};
    }
    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("Group not found.");
        }
        return {};
    }
    return DLModelMappers::groupFromMap(currentRow(query));
}

QList<DLWord> fetchActiveWordsForGroup(QSqlDatabase& db, QString* error, const QString& groupSyncId)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE w.group_sync_id = :group_sync_id AND w.deleted_at IS NULL;")
                      .arg(DLWordRepository::wordSelectColumns(), DLWordRepository::wordFromClause()));
    query.bindValue(QStringLiteral(":group_sync_id"), groupSyncId);
    if (!execRepositoryQuery(query, error)) {
        return {};
    }

    QList<DLWord> words;
    while (query.next()) {
        words.append(DLModelMappers::wordFromMap(currentRow(query)));
    }
    return words;
}

DLSyncEventEnvelope groupEvent(const DLWordGroup& group, const QString& operation)
{
    DLSyncEventEnvelope event;
    event.entityType = QStringLiteral("group");
    event.entityId = group.syncId;
    event.operation = operation;
    event.updatedAt = group.updatedAt;
    event.payload = DLSyncEventSerializer::payloadForGroup(group, QString());
    return event;
}

DLSyncEventEnvelope groupDeleteEvent(const QString& syncId, qint64 deletedAt)
{
    DLSyncEventEnvelope event;
    event.entityType = QStringLiteral("group");
    event.entityId = syncId;
    event.operation = QStringLiteral("delete");
    event.updatedAt = deletedAt;
    event.tombstone = DLSyncEventSerializer::tombstoneForEntity(
        event.entityType,
        syncId,
        QString(),
        deletedAt);
    return event;
}

DLSyncEventEnvelope wordUpdateEvent(const DLWord& word)
{
    DLSyncEventEnvelope event;
    event.entityType = QStringLiteral("word");
    event.entityId = word.syncId;
    event.operation = QStringLiteral("update");
    event.updatedAt = word.updatedAt;
    event.payload = DLSyncEventSerializer::payloadForWord(word, QString());
    return event;
}
}

DLGroupRepository::DLGroupRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLGroupRepository::insertGroup(const DLWordGroup& group)
{
    const qint64 now = DLDatabaseManager::currentUnixTime();
    const QString syncId = group.syncId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : group.syncId.trimmed();

    int newId = -1;
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO groups (sync_id, name, color_hex, created_at, updated_at)
            VALUES (:sync_id, :name, :color_hex, :created_at, :updated_at);
        )"));
        query.bindValue(QStringLiteral(":sync_id"), syncId);
        query.bindValue(QStringLiteral(":name"), group.name);
        query.bindValue(QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex);
        query.bindValue(QStringLiteral(":created_at"), now);
        query.bindValue(QStringLiteral(":updated_at"), now);
        if (!execRepositoryQuery(query, error)) {
            return false;
        }

        newId = static_cast<int>(query.lastInsertId().toLongLong());
        DLWordGroup stored = group;
        stored.id = newId;
        stored.syncId = syncId;
        stored.colorHex = stored.colorHex.isEmpty() ? QStringLiteral("#3366CC") : stored.colorHex;
        stored.createdAt = now;
        stored.updatedAt = now;
        return m_database.recordSyncOutboxEvent(db, error, groupEvent(stored, QStringLiteral("create")));
    });

    if (!success) {
        qCWarning(dlRepo) << "Failed to insert group:" << m_database.lastError();
        return {};
    }

    qCDebug(dlRepo) << "Inserted group with sync id" << syncId;
    return syncId;
}

bool DLGroupRepository::updateGroup(const DLWordGroup& group)
{
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTime();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            UPDATE groups
            SET name = :name,
                color_hex = :color_hex,
                updated_at = :updated_at
            WHERE sync_id = :sync_id
              AND deleted_at IS NULL;
        )"));
        query.bindValue(QStringLiteral(":sync_id"), group.syncId);
        query.bindValue(QStringLiteral(":name"), group.name);
        query.bindValue(QStringLiteral(":color_hex"), group.colorHex.isEmpty() ? QStringLiteral("#3366CC") : group.colorHex);
        query.bindValue(QStringLiteral(":updated_at"), now);
        if (!execRepositoryQuery(query, error)) {
            return false;
        }
        if (query.numRowsAffected() <= 0) {
            if (error) {
                *error = QStringLiteral("Group not found.");
            }
            return false;
        }

        const DLWordGroup stored = fetchGroupForOutbox(db, error, group.syncId);
        return stored.id >= 0
            && m_database.recordSyncOutboxEvent(db, error, groupEvent(stored, QStringLiteral("update")));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update group" << group.syncId << ":" << m_database.lastError();
    }
    return success;
}

bool DLGroupRepository::deleteGroup(const QString& syncId)
{
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTime();
        const DLWordGroup existing = fetchGroupForOutbox(db, error, syncId);
        if (existing.id < 0) {
            return false;
        }
        QList<DLWord> affectedWords = fetchActiveWordsForGroup(db, error, syncId);
        if (!error->isEmpty()) {
            return false;
        }

        QSqlQuery tombstone(db);
        tombstone.prepare(QStringLiteral(R"(
            UPDATE groups
            SET deleted_at = :deleted_at,
                updated_at = :updated_at
            WHERE sync_id = :sync_id
              AND deleted_at IS NULL;
        )"));
        tombstone.bindValue(QStringLiteral(":sync_id"), syncId);
        tombstone.bindValue(QStringLiteral(":deleted_at"), now);
        tombstone.bindValue(QStringLiteral(":updated_at"), now);
        if (!execRepositoryQuery(tombstone, error)) {
            return false;
        }
        if (tombstone.numRowsAffected() <= 0) {
            if (error) {
                *error = QStringLiteral("Group not found.");
            }
            return false;
        }

        QSqlQuery unlinkWords(db);
        unlinkWords.prepare(QStringLiteral(R"(
            UPDATE words
            SET group_id = NULL,
                group_sync_id = NULL,
                updated_at = :updated_at
            WHERE group_sync_id = :sync_id
              AND deleted_at IS NULL;
        )"));
        unlinkWords.bindValue(QStringLiteral(":sync_id"), syncId);
        unlinkWords.bindValue(QStringLiteral(":updated_at"), now);
        if (!execRepositoryQuery(unlinkWords, error)) {
            return false;
        }

        if (!m_database.recordSyncOutboxEvent(db, error, groupDeleteEvent(syncId, now))) {
            return false;
        }

        for (DLWord& word : affectedWords) {
            word.groupId = -1;
            word.groupSyncId.clear();
            word.updatedAt = now;
            if (!m_database.recordSyncOutboxEvent(db, error, wordUpdateEvent(word))) {
                return false;
            }
        }

        return true;
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete group" << syncId << ":" << m_database.lastError();
    }
    return success;
}

QList<DLWordGroup> DLGroupRepository::fetchAllGroups()
{
    const QVariantList rows = m_database.selectRows(QStringLiteral(R"(
        SELECT g.id, g.name, g.color_hex, g.created_at, g.updated_at, g.deleted_at,
               g.sync_id,
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

DLWordGroup DLGroupRepository::fetchGroupById(const QString& syncId)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral(R"(
            SELECT g.id, g.name, g.color_hex, g.created_at, g.updated_at, g.deleted_at,
                   g.sync_id,
                   (SELECT COUNT(*) FROM words WHERE group_id = g.id AND deleted_at IS NULL) AS word_count
            FROM groups g
            WHERE g.sync_id = :sync_id
              AND g.deleted_at IS NULL;
        )"),
        {{ QStringLiteral(":sync_id"), syncId }});

    return DLModelMappers::groupFromMap(row);
}

int DLGroupRepository::getGroupCount()
{
    return m_database.selectInt(QStringLiteral("SELECT COUNT(*) FROM groups WHERE deleted_at IS NULL;"));
}
