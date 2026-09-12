#include "DLAppSettingRepository.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Sync/DLSyncEventSerializer.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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
    qCWarning(dlRepo) << "Failed app-setting repository query:" << query.lastError().text();
    return false;
}

bool isAccountLearningKey(const QString& settingKey)
{
    return settingKey.trimmed().startsWith(QStringLiteral("learning."));
}

bool isSupportedValueType(const QString& valueType)
{
    static const QSet<QString> supported = {
        QStringLiteral("string"),
        QStringLiteral("integer"),
        QStringLiteral("boolean"),
        QStringLiteral("json"),
    };
    return supported.contains(valueType.trimmed());
}

DLSyncEventEnvelope settingEvent(const QVariantMap& setting, const QString& operation)
{
    DLSyncEventEnvelope event;
    event.entityType = QStringLiteral("app_setting");
    event.entityId = setting.value(QStringLiteral("id")).toString();
    event.operation = operation;
    event.updatedAt = setting.value(QStringLiteral("updated_at"));
    event.payload = DLSyncEventSerializer::payloadForAppSetting(setting, QString());
    return event;
}
}

DLAppSettingRepository::DLAppSettingRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLAppSettingRepository::setSetting(const QString& settingKey,
                                        const QString& settingValue,
                                        const QString& valueType)
{
    const QString key = settingKey.trimmed();
    const QString type = valueType.trimmed();
    if (!isAccountLearningKey(key) || !isSupportedValueType(type)) {
        m_database.setLastError(QStringLiteral("Only account-level learning settings with a supported value type can synchronize."));
        return false;
    }

    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral(
            "SELECT sync_id, created_at FROM account_learning_settings WHERE setting_key = :setting_key;"));
        existing.bindValue(QStringLiteral(":setting_key"), key);
        if (!execRepositoryQuery(existing, error)) {
            return false;
        }

        const bool exists = existing.next();
        const QString syncId = exists
            ? existing.value(0).toString()
            : QUuid::createUuid().toString(QUuid::WithoutBraces);
        const qint64 now = DLDatabaseManager::currentUnixTime();
        const qint64 createdAt = exists ? existing.value(1).toLongLong() : now;

        QSqlQuery upsert(db);
        upsert.prepare(QStringLiteral(R"(
            INSERT INTO account_learning_settings
                (sync_id, setting_key, setting_value, value_type, created_at, updated_at, deleted_at)
            VALUES
                (:sync_id, :setting_key, :setting_value, :value_type, :created_at, :updated_at, NULL)
            ON CONFLICT(setting_key) DO UPDATE SET
                setting_value = excluded.setting_value,
                value_type = excluded.value_type,
                updated_at = excluded.updated_at,
                deleted_at = NULL;
        )"));
        upsert.bindValue(QStringLiteral(":sync_id"), syncId);
        upsert.bindValue(QStringLiteral(":setting_key"), key);
        upsert.bindValue(QStringLiteral(":setting_value"), settingValue);
        upsert.bindValue(QStringLiteral(":value_type"), type);
        upsert.bindValue(QStringLiteral(":created_at"), createdAt);
        upsert.bindValue(QStringLiteral(":updated_at"), now);
        if (!execRepositoryQuery(upsert, error)) {
            return false;
        }

        const QVariantMap setting{
            { QStringLiteral("id"), syncId },
            { QStringLiteral("setting_key"), key },
            { QStringLiteral("setting_value"), settingValue },
            { QStringLiteral("value_type"), type },
            { QStringLiteral("scope"), QStringLiteral("account_learning") },
            { QStringLiteral("created_at"), createdAt },
            { QStringLiteral("updated_at"), now },
        };
        return m_database.recordSyncOutboxEvent(
            db, error, settingEvent(setting, exists ? QStringLiteral("update") : QStringLiteral("create")));
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to store account learning setting" << key << ":" << m_database.lastError();
    }
    return success;
}

bool DLAppSettingRepository::deleteSetting(const QString& settingKey)
{
    const QString key = settingKey.trimmed();
    if (!isAccountLearningKey(key)) {
        m_database.setLastError(QStringLiteral("Only account-level learning settings can synchronize."));
        return false;
    }

    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral(
            "SELECT sync_id FROM account_learning_settings WHERE setting_key = :setting_key AND deleted_at IS NULL;"));
        existing.bindValue(QStringLiteral(":setting_key"), key);
        if (!execRepositoryQuery(existing, error) || !existing.next()) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Account learning setting was not found.");
            }
            return false;
        }

        const QString syncId = existing.value(0).toString();
        const qint64 now = DLDatabaseManager::currentUnixTime();
        QSqlQuery tombstone(db);
        tombstone.prepare(QStringLiteral(R"(
            UPDATE account_learning_settings
            SET updated_at = :updated_at, deleted_at = :deleted_at
            WHERE sync_id = :sync_id AND deleted_at IS NULL;
        )"));
        tombstone.bindValue(QStringLiteral(":updated_at"), now);
        tombstone.bindValue(QStringLiteral(":deleted_at"), now);
        tombstone.bindValue(QStringLiteral(":sync_id"), syncId);
        if (!execRepositoryQuery(tombstone, error) || tombstone.numRowsAffected() != 1) {
            return false;
        }

        DLSyncEventEnvelope event;
        event.entityType = QStringLiteral("app_setting");
        event.entityId = syncId;
        event.operation = QStringLiteral("delete");
        event.updatedAt = now;
        event.tombstone = DLSyncEventSerializer::tombstoneForEntity(
            event.entityType,
            event.entityId,
            QString(),
            now,
            {{ QStringLiteral("setting_key"), key }});
        return m_database.recordSyncOutboxEvent(db, error, event);
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete account learning setting" << key << ":" << m_database.lastError();
    }
    return success;
}

QVariantMap DLAppSettingRepository::fetchSetting(const QString& settingKey)
{
    return m_database.selectOneRow(QStringLiteral(R"(
        SELECT sync_id AS id, setting_key, setting_value, value_type,
               'account_learning' AS scope, created_at, updated_at
        FROM account_learning_settings
        WHERE setting_key = :setting_key AND deleted_at IS NULL;
    )"), {{ QStringLiteral(":setting_key"), settingKey.trimmed() }});
}

QVariantList DLAppSettingRepository::fetchAllSettings()
{
    return m_database.selectRows(QStringLiteral(R"(
        SELECT sync_id AS id, setting_key, setting_value, value_type,
               'account_learning' AS scope, created_at, updated_at
        FROM account_learning_settings
        WHERE deleted_at IS NULL
        ORDER BY setting_key;
    )"));
}
