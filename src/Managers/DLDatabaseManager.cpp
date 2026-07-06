#include "DLDatabaseManager.h"

#include <QDateTime>
#include <QSysInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include "DLDatabaseMaintenanceService.h"
#include "DLGroupRepository.h"
#include "DLLogging.h"
#include "DLQuizRepository.h"
#include "DLReviewStatsRepository.h"
#include "DLWordRepository.h"
#include "../Models/DLModelMappers.h"

namespace {
QStringList argumentKeys(const QVariantMap& args)
{
    QStringList keys;
    keys.reserve(args.size());
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        keys.append(it.key());
    }
    return keys;
}

QStringList uniquePlaceholderNames(const QSqlQuery& query)
{
    QStringList names;
    QSet<QString> seen;
    for (const QString& name : query.boundValueNames()) {
        if (!seen.contains(name)) {
            names.append(name);
            seen.insert(name);
        }
    }
    return names;
}

bool bindNamedValues(QSqlQuery& query, const QVariantMap& args, QStringList* missingKeys = nullptr, QStringList* unusedKeys = nullptr)
{
    const QStringList placeholders = uniquePlaceholderNames(query);
    QSet<QString> usedKeys;

    for (const QString& placeholder : placeholders) {
        const QString colonless = placeholder.startsWith(QLatin1Char(':')) ? placeholder.mid(1) : placeholder;
        const QString colonKey = placeholder.startsWith(QLatin1Char(':')) ? placeholder : QStringLiteral(":%1").arg(placeholder);

        QString argKey;
        if (args.contains(placeholder)) {
            argKey = placeholder;
        } else if (args.contains(colonless)) {
            argKey = colonless;
        } else if (args.contains(colonKey)) {
            argKey = colonKey;
        }

        if (argKey.isEmpty()) {
            if (missingKeys) {
                missingKeys->append(placeholder);
            }
            continue;
        }

        query.bindValue(placeholder, args.value(argKey));
        usedKeys.insert(argKey);
    }

    if (unusedKeys) {
        for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
            if (!usedKeys.contains(it.key())) {
                unusedKeys->append(it.key());
            }
        }
    }

    return missingKeys == nullptr || missingKeys->isEmpty();
}

void logSqlFailure(const char* operation,
                   const QString& sql,
                   const QVariantMap& args,
                   const QStringList& missingKeys,
                   const QStringList& unusedKeys,
                   const QSqlError& error);

bool tableHasColumn(QSqlDatabase& db, const QString& tableName, const QString& columnName, QString* error)
{
    if (error) {
        error->clear();
    }

    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info(:table_name) WHERE name = :column_name;"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.bindValue(QStringLiteral(":table_name"), tableName);
    query.bindValue(QStringLiteral(":column_name"), columnName);
    if (!query.exec() || !query.next()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    return query.value(0).toInt() > 0;
}

bool tableExists(QSqlDatabase& db, const QString& tableName, QString* error)
{
    if (error) {
        error->clear();
    }

    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = :table_name;"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.bindValue(QStringLiteral(":table_name"), tableName);
    if (!query.exec() || !query.next()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    return query.value(0).toInt() > 0;
}

bool execMigrationSql(QSqlDatabase& db, const QString& sql, QString* error, const QVariantMap& args = {})
{
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        if (error) {
            *error = query.lastError().text();
        }
        logSqlFailure("migration/prepare", sql, args, {}, argumentKeys(args), query.lastError());
        return false;
    }

    QStringList missingKeys;
    QStringList unusedKeys;
    bindNamedValues(query, args, &missingKeys, &unusedKeys);
    if (!missingKeys.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
        }
        logSqlFailure("migration/bind", sql, args, missingKeys, unusedKeys, query.lastError());
        return false;
    }

    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        logSqlFailure("migration/exec", sql, args, missingKeys, unusedKeys, query.lastError());
        return false;
    }

    return true;
}

bool migrationAlreadyApplied(QSqlDatabase& db, int version, QString* error)
{
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = :version;"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.bindValue(QStringLiteral(":version"), version);
    if (!query.exec() || !query.next()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    return query.value(0).toInt() > 0;
}

bool recordMigration(QSqlDatabase& db, int version, const QString& name, qint64 appliedAt, QString* error)
{
    return execMigrationSql(db,
                            QStringLiteral(R"(
                                INSERT INTO schema_migrations (version, name, applied_at)
                                VALUES (:version, :name, :applied_at);
                            )"),
                            error,
                            {
                                { QStringLiteral(":version"), version },
                                { QStringLiteral(":name"), name },
                                { QStringLiteral(":applied_at"), appliedAt }
                            });
}

QString sqliteUuidExpression()
{
    return QStringLiteral(R"(
        lower(hex(randomblob(4))) || '-' ||
        lower(hex(randomblob(2))) || '-' ||
        '4' || substr(lower(hex(randomblob(2))), 2) || '-' ||
        substr('89ab', abs(random()) % 4 + 1, 1) ||
        substr(lower(hex(randomblob(2))), 2) || '-' ||
        lower(hex(randomblob(6)))
    )");
}

bool addColumnIfMissing(QSqlDatabase& db,
                        const QString& tableName,
                        const QString& columnName,
                        const QString& definition,
                        QString* error)
{
    const bool hasColumn = tableHasColumn(db, tableName, columnName, error);
    if (error && !error->isEmpty()) {
        return false;
    }

    if (hasColumn) {
        return true;
    }

    qCInfo(dlDb) << "Adding missing" << tableName + QStringLiteral(".") + columnName << "column";
    return execMigrationSql(db,
                            QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3;")
                                .arg(tableName, columnName, definition),
                            error);
}

bool createSyncStateTable(QSqlDatabase& db, QString* error)
{
    return execMigrationSql(db,
                            QStringLiteral(R"(
                                CREATE TABLE IF NOT EXISTS sync_state (
                                    key TEXT PRIMARY KEY,
                                    value TEXT NOT NULL,
                                    updated_at INTEGER NOT NULL
                                );
                            )"),
                            error);
}

bool createDeviceIdentityTable(QSqlDatabase& db, QString* error)
{
    return execMigrationSql(db,
                            QStringLiteral(R"(
                                CREATE TABLE IF NOT EXISTS device_identity (
                                    id TEXT PRIMARY KEY,
                                    device_name TEXT,
                                    created_at INTEGER NOT NULL,
                                    last_seen_at INTEGER
                                );
                            )"),
                            error);
}

bool createOutboundSyncQueueTable(QSqlDatabase& db, QString* error)
{
    return execMigrationSql(db,
                            QStringLiteral(R"(
                                CREATE TABLE IF NOT EXISTS outbound_sync_queue (
                                    id TEXT PRIMARY KEY,
                                    entity_type TEXT NOT NULL,
                                    entity_id TEXT NOT NULL,
                                    operation TEXT NOT NULL,
                                    payload_json TEXT,
                                    created_at INTEGER NOT NULL,
                                    retry_count INTEGER NOT NULL DEFAULT 0,
                                    last_error TEXT,
                                    pushed_at INTEGER
                                );
                            )"),
                            error);
}

bool migrateLocalIdentityAndSyncStateTables(QSqlDatabase& db, qint64 now, QString* error)
{
    QString migrationError;
    const bool alreadyApplied = migrationAlreadyApplied(db, 2, &migrationError);
    if (!migrationError.isEmpty()) {
        if (error) {
            *error = migrationError;
        }
        return false;
    }
    if (alreadyApplied) {
        return true;
    }

    const bool syncStateExists = tableExists(db, QStringLiteral("sync_state"), error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (syncStateExists) {
        const bool hasKey = tableHasColumn(db, QStringLiteral("sync_state"), QStringLiteral("key"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasValue = tableHasColumn(db, QStringLiteral("sync_state"), QStringLiteral("value"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasScope = tableHasColumn(db, QStringLiteral("sync_state"), QStringLiteral("scope"), error);
        if (error && !error->isEmpty()) {
            return false;
        }

        if (!hasKey || !hasValue || hasScope) {
            const QString legacyTable = QStringLiteral("sync_state_legacy_%1").arg(now);
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE sync_state RENAME TO %1;").arg(legacyTable), error)
                || !createSyncStateTable(db, error)) {
                return false;
            }

            if (hasScope) {
                if (!execMigrationSql(db,
                                      QStringLiteral(R"(
                                          INSERT OR REPLACE INTO sync_state (key, value, updated_at)
                                          SELECT COALESCE(NULLIF(TRIM(scope), ''), NULLIF(TRIM(id), '')),
                                                 COALESCE(cursor, ''),
                                                 COALESCE(updated_at, created_at, :now)
                                          FROM %1
                                          WHERE COALESCE(NULLIF(TRIM(scope), ''), NULLIF(TRIM(id), '')) IS NOT NULL
                                            AND cursor IS NOT NULL;
                                      )").arg(legacyTable),
                                      error,
                                      {{ QStringLiteral(":now"), now }})) {
                    return false;
                }
            } else if (hasKey && hasValue) {
                if (!execMigrationSql(db,
                                      QStringLiteral(R"(
                                          INSERT OR REPLACE INTO sync_state (key, value, updated_at)
                                          SELECT key, value, COALESCE(updated_at, :now)
                                          FROM %1
                                          WHERE TRIM(COALESCE(key, '')) != ''
                                            AND value IS NOT NULL;
                                      )").arg(legacyTable),
                                      error,
                                      {{ QStringLiteral(":now"), now }})) {
                    return false;
                }
            }

            if (!execMigrationSql(db, QStringLiteral("DROP TABLE %1;").arg(legacyTable), error)) {
                return false;
            }
        }
    } else if (!createSyncStateTable(db, error)) {
        return false;
    }

    const bool deviceIdentityExists = tableExists(db, QStringLiteral("device_identity"), error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (deviceIdentityExists) {
        const bool hasDeviceName = tableHasColumn(db, QStringLiteral("device_identity"), QStringLiteral("device_name"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasLastSeenAt = tableHasColumn(db, QStringLiteral("device_identity"), QStringLiteral("last_seen_at"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasDeviceId = tableHasColumn(db, QStringLiteral("device_identity"), QStringLiteral("device_id"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasDisplayName = tableHasColumn(db, QStringLiteral("device_identity"), QStringLiteral("display_name"), error);
        if (error && !error->isEmpty()) {
            return false;
        }

        if (!hasDeviceName || !hasLastSeenAt || hasDeviceId || hasDisplayName) {
            const QString legacyTable = QStringLiteral("device_identity_legacy_%1").arg(now);
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE device_identity RENAME TO %1;").arg(legacyTable), error)
                || !createDeviceIdentityTable(db, error)) {
                return false;
            }

            if (hasDeviceId) {
                if (!execMigrationSql(db,
                                      QStringLiteral(R"(
                                          INSERT OR IGNORE INTO device_identity (id, device_name, created_at, last_seen_at)
                                          SELECT COALESCE(NULLIF(TRIM(device_id), ''), NULLIF(TRIM(id), '')),
                                                 %2,
                                                 COALESCE(created_at, :now),
                                                 COALESCE(updated_at, :now)
                                          FROM %1
                                          WHERE COALESCE(NULLIF(TRIM(device_id), ''), NULLIF(TRIM(id), '')) IS NOT NULL;
                                      )").arg(legacyTable,
                                             hasDisplayName ? QStringLiteral("display_name") : QStringLiteral("NULL")),
                                      error,
                                      {{ QStringLiteral(":now"), now }})) {
                    return false;
                }
            }

            if (!execMigrationSql(db, QStringLiteral("DROP TABLE %1;").arg(legacyTable), error)) {
                return false;
            }
        }
    } else if (!createDeviceIdentityTable(db, error)) {
        return false;
    }

    return recordMigration(db, 2, QStringLiteral("local_device_identity_and_sync_state"), now, error);
}

bool migrateOutboundSyncQueueTable(QSqlDatabase& db, qint64 now, QString* error)
{
    QString migrationError;
    const bool alreadyApplied = migrationAlreadyApplied(db, 3, &migrationError);
    if (!migrationError.isEmpty()) {
        if (error) {
            *error = migrationError;
        }
        return false;
    }
    if (alreadyApplied) {
        return true;
    }

    const bool queueExists = tableExists(db, QStringLiteral("outbound_sync_queue"), error);
    if (error && !error->isEmpty()) {
        return false;
    }
    if (queueExists) {
        const bool hasEntityType = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("entity_type"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasEntityId = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("entity_id"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasRetryCount = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("retry_count"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasPushedAt = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("pushed_at"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasTableName = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("table_name"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasRecordId = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("record_id"), error);
        if (error && !error->isEmpty()) {
            return false;
        }
        const bool hasAttempts = tableHasColumn(db, QStringLiteral("outbound_sync_queue"), QStringLiteral("attempts"), error);
        if (error && !error->isEmpty()) {
            return false;
        }

        if (!hasEntityType || !hasEntityId || !hasRetryCount || !hasPushedAt || hasTableName || hasRecordId || hasAttempts) {
            const QString legacyTable = QStringLiteral("outbound_sync_queue_legacy_%1").arg(now);
            if (!execMigrationSql(db, QStringLiteral("DROP TRIGGER IF EXISTS trg_outbound_sync_queue_id_after_insert;"), error)
                || !execMigrationSql(db, QStringLiteral("ALTER TABLE outbound_sync_queue RENAME TO %1;").arg(legacyTable), error)
                || !createOutboundSyncQueueTable(db, error)) {
                return false;
            }

            if ((hasTableName || hasEntityType) && (hasRecordId || hasEntityId)) {
                const QString entityTypeExpression = hasEntityType
                    ? QStringLiteral("entity_type")
                    : QStringLiteral("table_name");
                const QString entityIdExpression = hasEntityId
                    ? QStringLiteral("entity_id")
                    : QStringLiteral("record_id");
                const QString retryCountExpression = hasRetryCount
                    ? QStringLiteral("retry_count")
                    : (hasAttempts ? QStringLiteral("attempts") : QStringLiteral("0"));
                const QString pushedAtExpression = hasPushedAt
                    ? QStringLiteral("pushed_at")
                    : QStringLiteral("NULL");

                if (!execMigrationSql(db,
                                      QStringLiteral(R"(
                                          INSERT OR IGNORE INTO outbound_sync_queue
                                              (id, entity_type, entity_id, operation, payload_json,
                                               created_at, retry_count, last_error, pushed_at)
                                          SELECT COALESCE(NULLIF(TRIM(id), ''), %6),
                                                 %2,
                                                 %3,
                                                 operation,
                                                 payload_json,
                                                 COALESCE(created_at, :now),
                                                 COALESCE(%4, 0),
                                                 last_error,
                                                 %5
                                          FROM %1
                                          WHERE TRIM(COALESCE(%2, '')) != ''
                                            AND TRIM(COALESCE(%3, '')) != ''
                                            AND TRIM(COALESCE(operation, '')) != '';
                                      )").arg(legacyTable,
                                             entityTypeExpression,
                                             entityIdExpression,
                                             retryCountExpression,
                                             pushedAtExpression,
                                             sqliteUuidExpression()),
                                      error,
                                      {{ QStringLiteral(":now"), now }})) {
                    return false;
                }
            }

            if (!execMigrationSql(db, QStringLiteral("DROP TABLE %1;").arg(legacyTable), error)) {
                return false;
            }
        }
    } else if (!createOutboundSyncQueueTable(db, error)) {
        return false;
    }

    return recordMigration(db, 3, QStringLiteral("outbound_sync_queue"), now, error);
}

void logSqlFailure(const char* operation,
                   const QString& sql,
                   const QVariantMap& args,
                   const QStringList& missingKeys,
                   const QStringList& unusedKeys,
                   const QSqlError& error)
{
    qCWarning(dlDb) << "Failed SQL operation:" << operation
                    << "error:" << error.text()
                    << "sql:" << sql.simplified()
                    << "argKeys:" << argumentKeys(args)
                    << "argCount:" << args.size()
                    << "missingKeys:" << missingKeys
                    << "unusedKeys:" << unusedKeys;
}
}

DLDatabaseManager::DLDatabaseManager()
    : m_connectionName(QStringLiteral("delearner_main_connection"))
{
}

DLDatabaseManager::~DLDatabaseManager()
{
    closeDatabase();
}

DLDatabaseManager& DLDatabaseManager::instance()
{
    static DLDatabaseManager instance;
    return instance;
}

bool DLDatabaseManager::openDatabase(const QString& databasePath)
{
    {
        QMutexLocker locker(&m_mutex);

        qCInfo(dlDb) << "Opening database at" << databasePath;

        if (QSqlDatabase::contains(m_connectionName)) {
            m_db = QSqlDatabase::database(m_connectionName);
        } else {
            m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        }

        if (m_db.isOpen() && m_db.databaseName() == databasePath) {
            return true;
        }

        if (m_db.isOpen()) {
            m_db.close();
        }

        m_db.setDatabaseName(databasePath);
        if (!m_db.open()) {
            m_lastError = m_db.lastError().text();
            qCCritical(dlDb) << "Failed to open database:" << m_lastError;
            return false;
        }

        QSqlQuery pragma(m_db);
        if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
            m_lastError = pragma.lastError().text();
            qCCritical(dlDb) << "Failed to enable SQLite foreign keys:" << m_lastError;
            return false;
        }
    }

    if (!createTablesIfNeeded() || !migrateSchemaIfNeeded() || !createIndexesIfNeeded()) {
        return false;
    }

    return !localDeviceId().isEmpty();
}

void DLDatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        qCInfo(dlDb) << "Closing database";
        m_db.close();
    }

    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_deviceId.clear();
}

bool DLDatabaseManager::createTablesIfNeeded()
{
    qCDebug(dlDb) << "Creating database tables if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS schema_migrations (
                version INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                applied_at INTEGER NOT NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS groups (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sync_id TEXT,
                name TEXT NOT NULL,
                color_hex TEXT DEFAULT '#3366CC',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                server_updated_at INTEGER,
                server_version INTEGER NOT NULL DEFAULT 0,
                device_id TEXT,
                dirty INTEGER NOT NULL DEFAULT 1
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS words (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sync_id TEXT,
                german_word TEXT NOT NULL,
                normalized_german_word TEXT NOT NULL,
                article TEXT,
                part_of_speech TEXT NOT NULL DEFAULT 'Andere',
                native_translation TEXT NOT NULL,
                normalized_native_translation TEXT NOT NULL,
                example_phrase_de TEXT,
                example_phrase_native TEXT,
                group_id INTEGER,
                plural_form TEXT,
                notes TEXT,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                server_updated_at INTEGER,
                server_version INTEGER NOT NULL DEFAULT 0,
                device_id TEXT,
                dirty INTEGER NOT NULL DEFAULT 1,
                FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE SET NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS word_review_stats (
                word_id INTEGER PRIMARY KEY,
                sync_id TEXT,
                correct_answers INTEGER NOT NULL DEFAULT 0,
                wrong_answers INTEGER NOT NULL DEFAULT 0,
                last_reviewed_at INTEGER,
                ease_factor REAL DEFAULT 2.5,
                interval_days INTEGER DEFAULT 0,
                due_at INTEGER,
                created_at INTEGER NOT NULL DEFAULT 0,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                server_updated_at INTEGER,
                server_version INTEGER NOT NULL DEFAULT 0,
                device_id TEXT,
                dirty INTEGER NOT NULL DEFAULT 1,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS noun_forms (
                word_id INTEGER PRIMARY KEY,
                plural_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS verb_forms (
                word_id INTEGER PRIMARY KEY,
                praeteritum_form TEXT,
                partizip_ii_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS adjective_forms (
                word_id INTEGER PRIMARY KEY,
                positive_form TEXT,
                comparative_form TEXT,
                superlative_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS app_settings (
                id TEXT PRIMARY KEY,
                setting_key TEXT NOT NULL UNIQUE,
                setting_value TEXT,
                value_type TEXT NOT NULL DEFAULT 'string',
                notes TEXT,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                server_updated_at INTEGER,
                server_version INTEGER NOT NULL DEFAULT 0,
                device_id TEXT,
                dirty INTEGER NOT NULL DEFAULT 1
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS sync_state (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL,
                updated_at INTEGER NOT NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS device_identity (
                id TEXT PRIMARY KEY,
                device_name TEXT,
                created_at INTEGER NOT NULL,
                last_seen_at INTEGER
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS outbound_sync_queue (
                id TEXT PRIMARY KEY,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                operation TEXT NOT NULL,
                payload_json TEXT,
                created_at INTEGER NOT NULL,
                retry_count INTEGER NOT NULL DEFAULT 0,
                last_error TEXT,
                pushed_at INTEGER
            );
        )"), {} }
    };

    return executeSqlBatch(commands);
}

bool DLDatabaseManager::migrateSchemaIfNeeded()
{
    qCDebug(dlDb) << "Migrating database schema if needed";
    return transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTime();
        const QString uuidExpression = sqliteUuidExpression();

        if (!execMigrationSql(db, QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS schema_migrations (
                    version INTEGER PRIMARY KEY,
                    name TEXT NOT NULL,
                    applied_at INTEGER NOT NULL
                );
            )"),
            error)) {
            return false;
        }

        QString migrationError;
        const bool alreadyApplied = migrationAlreadyApplied(db, 1, &migrationError);
        if (!migrationError.isEmpty()) {
            if (error) {
                *error = migrationError;
            }
            return false;
        }

        if (alreadyApplied) {
            return migrateLocalIdentityAndSyncStateTables(db, now, error)
                && migrateOutboundSyncQueueTable(db, now, error);
        }

        if (!addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("sync_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("created_at"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("updated_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("deleted_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("server_updated_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("server_version"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("device_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("groups"), QStringLiteral("dirty"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"), error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE groups SET updated_at = COALESCE(updated_at, created_at, :now) WHERE updated_at IS NULL;"),
                                 error,
                                 {{ QStringLiteral(":now"), now }})
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE groups SET sync_id = %1 WHERE sync_id IS NULL OR TRIM(sync_id) = '';").arg(uuidExpression),
                                 error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE groups SET server_version = COALESCE(server_version, 0), dirty = COALESCE(dirty, 1);"),
                                 error)) {
            return false;
        }

        if (!addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("sync_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("created_at"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("updated_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("plural_form"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("notes"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("deleted_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("server_updated_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("server_version"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("device_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("words"), QStringLiteral("dirty"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"), error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE words SET sync_id = %1 WHERE sync_id IS NULL OR TRIM(sync_id) = '';").arg(uuidExpression),
                                 error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE words SET updated_at = COALESCE(updated_at, created_at, :now) WHERE updated_at IS NULL;"),
                                 error,
                                 {{ QStringLiteral(":now"), now }})
            || !execMigrationSql(db,
                                 QStringLiteral(R"(
                                     UPDATE words
                                     SET plural_form = (
                                         SELECT nf.plural_form
                                         FROM noun_forms nf
                                         WHERE nf.word_id = words.id
                                     )
                                     WHERE (plural_form IS NULL OR TRIM(plural_form) = '')
                                       AND EXISTS (
                                           SELECT 1
                                           FROM noun_forms nf
                                           WHERE nf.word_id = words.id
                                             AND TRIM(COALESCE(nf.plural_form, '')) != ''
                                       );
                                 )"),
                                 error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE words SET server_version = COALESCE(server_version, 0), dirty = COALESCE(dirty, 1);"),
                                 error)) {
            return false;
        }

        const bool hasPraeteritum = tableHasColumn(db, QStringLiteral("verb_forms"), QStringLiteral("praeteritum_form"), error);
        if (!error->isEmpty()) {
            return false;
        }
        const bool hasPartizip = tableHasColumn(db, QStringLiteral("verb_forms"), QStringLiteral("partizip_ii_form"), error);
        if (!error->isEmpty()) {
            return false;
        }

        if (!hasPraeteritum || !hasPartizip) {
            const bool hasLegacyFormKey = tableHasColumn(db, QStringLiteral("verb_forms"), QStringLiteral("form_key"), error);
            if (!error->isEmpty()) {
                return false;
            }
            const QString legacyTable = QStringLiteral("verb_forms_legacy_%1").arg(now);
            qCInfo(dlDb) << "Migrating legacy verb_forms table";

            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE verb_forms RENAME TO %1;").arg(legacyTable), error)
                || !execMigrationSql(db, QStringLiteral(R"(
                    CREATE TABLE verb_forms (
                        word_id INTEGER PRIMARY KEY,
                        praeteritum_form TEXT,
                        partizip_ii_form TEXT,
                        FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
                    );
                )"), error)) {
                return false;
            }

            if (hasLegacyFormKey) {
                const QString copySql = QStringLiteral(R"(
                    INSERT OR REPLACE INTO verb_forms (word_id, praeteritum_form, partizip_ii_form)
                    SELECT word_id,
                           MAX(CASE
                               WHEN LOWER(TRIM(form_key)) IN ('praeteritum', 'präteritum', 'praeteritum_form', 'preterite', 'simple_past')
                               THEN form_value
                           END) AS praeteritum_form,
                           MAX(CASE
                               WHEN LOWER(TRIM(form_key)) IN ('partizip ii', 'partizip_ii', 'partizip_ii_form', 'partizipii', 'past_participle')
                               THEN form_value
                           END) AS partizip_ii_form
                    FROM %1
                    WHERE COALESCE(deleted_at, 0) = 0
                    GROUP BY word_id
                    HAVING TRIM(COALESCE(praeteritum_form, '')) != ''
                        OR TRIM(COALESCE(partizip_ii_form, '')) != '';
                )").arg(legacyTable);
                if (!execMigrationSql(db, copySql, error)) {
                    return false;
                }
            }

            if (!execMigrationSql(db, QStringLiteral("DROP TABLE %1;").arg(legacyTable), error)) {
                return false;
            }
        }

        if (!addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("sync_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("created_at"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("deleted_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("server_updated_at"), QStringLiteral("INTEGER"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("server_version"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("device_id"), QStringLiteral("TEXT"), error)
            || !addColumnIfMissing(db, QStringLiteral("word_review_stats"), QStringLiteral("dirty"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"), error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE word_review_stats SET sync_id = %1 WHERE sync_id IS NULL OR TRIM(sync_id) = '';").arg(uuidExpression),
                                 error)
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE word_review_stats SET created_at = COALESCE(NULLIF(created_at, 0), updated_at, :now) WHERE created_at IS NULL OR created_at = 0;"),
                                 error,
                                 {{ QStringLiteral(":now"), now }})
            || !execMigrationSql(db,
                                 QStringLiteral("UPDATE word_review_stats SET server_version = COALESCE(server_version, 0), dirty = COALESCE(dirty, 1);"),
                                 error)) {
            return false;
        }

        QString statsColumnError;
        const bool wordsHaveCorrectAnswers = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("correct_answers"), &statsColumnError);
        if (!statsColumnError.isEmpty()) {
            if (error) {
                *error = statsColumnError;
            }
            return false;
        }
        const bool wordsHaveWrongAnswers = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("wrong_answers"), &statsColumnError);
        if (!statsColumnError.isEmpty()) {
            if (error) {
                *error = statsColumnError;
            }
            return false;
        }
        const bool wordsHaveLastReviewedAt = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("last_reviewed_at"), &statsColumnError);
        if (!statsColumnError.isEmpty()) {
            if (error) {
                *error = statsColumnError;
            }
            return false;
        }

        const QString correctAnswersExpression = wordsHaveCorrectAnswers
            ? QStringLiteral("COALESCE(w.correct_answers, 0)")
            : QStringLiteral("0");
        const QString wrongAnswersExpression = wordsHaveWrongAnswers
            ? QStringLiteral("COALESCE(w.wrong_answers, 0)")
            : QStringLiteral("0");
        const QString lastReviewedAtExpression = wordsHaveLastReviewedAt
            ? QStringLiteral("w.last_reviewed_at")
            : QStringLiteral("NULL");
        const QString deletedAtExpression = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("deleted_at"), &statsColumnError)
            ? QStringLiteral("w.deleted_at")
            : QStringLiteral("NULL");
        if (!statsColumnError.isEmpty()) {
            if (error) {
                *error = statsColumnError;
            }
            return false;
        }

        if (!execMigrationSql(db,
                              QStringLiteral(R"(
                                  INSERT INTO word_review_stats
                                      (word_id, sync_id, correct_answers, wrong_answers, last_reviewed_at,
                                       ease_factor, interval_days, due_at, created_at, updated_at, deleted_at,
                                       server_updated_at, server_version, device_id, dirty)
                                  SELECT w.id,
                                         %1,
                                         %2,
                                         %3,
                                         %4,
                                         2.5,
                                         0,
                                         NULL,
                                         COALESCE(w.created_at, :now),
                                         COALESCE(w.updated_at, w.created_at, :now),
                                         %5,
                                         NULL,
                                         0,
                                         NULL,
                                         1
                                  FROM words w
                                  WHERE NOT EXISTS (
                                      SELECT 1
                                      FROM word_review_stats rs
                                      WHERE rs.word_id = w.id
                                  );
                              )").arg(uuidExpression,
                                      correctAnswersExpression,
                                      wrongAnswersExpression,
                                      lastReviewedAtExpression,
                                      deletedAtExpression),
                              error,
                              {{ QStringLiteral(":now"), now }})) {
            return false;
        }

        if (wordsHaveCorrectAnswers
            && !execMigrationSql(db,
                                 QStringLiteral(R"(
                                     UPDATE word_review_stats
                                     SET correct_answers = CASE
                                             WHEN correct_answers = 0 THEN COALESCE((
                                                 SELECT w.correct_answers
                                                 FROM words w
                                                 WHERE w.id = word_review_stats.word_id
                                             ), correct_answers)
                                             ELSE correct_answers
                                         END
                                     WHERE EXISTS (
                                         SELECT 1
                                         FROM words w
                                         WHERE w.id = word_review_stats.word_id
                                     );
                                 )"),
                                 error)) {
            return false;
        }

        if (wordsHaveWrongAnswers
            && !execMigrationSql(db,
                                 QStringLiteral(R"(
                                     UPDATE word_review_stats
                                     SET wrong_answers = CASE
                                             WHEN wrong_answers = 0 THEN COALESCE((
                                                 SELECT w.wrong_answers
                                                 FROM words w
                                                 WHERE w.id = word_review_stats.word_id
                                             ), wrong_answers)
                                             ELSE wrong_answers
                                         END
                                     WHERE EXISTS (
                                         SELECT 1
                                         FROM words w
                                         WHERE w.id = word_review_stats.word_id
                                     );
                                 )"),
                                 error)) {
            return false;
        }

        if (wordsHaveLastReviewedAt
            && !execMigrationSql(db,
                                 QStringLiteral(R"(
                                     UPDATE word_review_stats
                                     SET last_reviewed_at = COALESCE(last_reviewed_at, (
                                         SELECT w.last_reviewed_at
                                         FROM words w
                                         WHERE w.id = word_review_stats.word_id
                                     ))
                                     WHERE EXISTS (
                                         SELECT 1
                                         FROM words w
                                         WHERE w.id = word_review_stats.word_id
                                     );
                                 )"),
                                 error)) {
            return false;
        }

        if (!recordMigration(db, 1, QStringLiteral("sync_metadata_and_legacy_stats"), now, error)
            || !migrateLocalIdentityAndSyncStateTables(db, now, error)
            || !migrateOutboundSyncQueueTable(db, now, error)) {
            return false;
        }

        return true;
    });
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    qCDebug(dlDb) << "Creating database indexes if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral("DROP TRIGGER IF EXISTS trg_sync_state_id_after_insert;"), {} },
        { QStringLiteral("DROP TRIGGER IF EXISTS trg_device_identity_id_after_insert;"), {} },
        { QStringLiteral("DROP TRIGGER IF EXISTS trg_outbound_sync_queue_id_after_insert;"), {} },
        { QStringLiteral("DROP INDEX IF EXISTS idx_sync_state_scope;"), {} },
        { QStringLiteral("DROP INDEX IF EXISTS idx_device_identity_device_id;"), {} },
        { QStringLiteral("DROP INDEX IF EXISTS idx_outbound_sync_queue_record;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_group_id ON words(group_id);"), {} },
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_words_sync_id ON words(sync_id) WHERE sync_id IS NOT NULL;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_dirty ON words(dirty) WHERE dirty = 1;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_deleted_at ON words(deleted_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_server_updated_at ON words(server_updated_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_normalized_german ON words(normalized_german_word);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_part_of_speech ON words(part_of_speech);"), {} },
        { QStringLiteral(R"(
            CREATE UNIQUE INDEX IF NOT EXISTS idx_words_unique_active_translation
            ON words(normalized_german_word, normalized_native_translation)
            WHERE deleted_at IS NULL;
        )"), {} },
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_groups_sync_id ON groups(sync_id) WHERE sync_id IS NOT NULL;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_groups_dirty ON groups(dirty) WHERE dirty = 1;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_groups_deleted_at ON groups(deleted_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_groups_server_updated_at ON groups(server_updated_at);"), {} },
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_word_review_stats_sync_id ON word_review_stats(sync_id) WHERE sync_id IS NOT NULL;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_due_at ON word_review_stats(due_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_dirty ON word_review_stats(dirty) WHERE dirty = 1;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_deleted_at ON word_review_stats(deleted_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_server_updated_at ON word_review_stats(server_updated_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_app_settings_dirty ON app_settings(dirty) WHERE dirty = 1;"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_app_settings_deleted_at ON app_settings(deleted_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_app_settings_server_updated_at ON app_settings(server_updated_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outbound_sync_queue_entity ON outbound_sync_queue(entity_type, entity_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outbound_sync_queue_created_at ON outbound_sync_queue(created_at);"), {} },
        { QString(R"SQL(
            CREATE TRIGGER IF NOT EXISTS trg_groups_sync_id_after_insert
            AFTER INSERT ON groups
            FOR EACH ROW
            WHEN NEW.sync_id IS NULL OR TRIM(NEW.sync_id) = ''
            BEGIN
                UPDATE groups SET sync_id = )SQL") + sqliteUuidExpression() + QString(R"SQL(
                WHERE id = NEW.id;
            END;
        )SQL"), {} },
        { QString(R"SQL(
            CREATE TRIGGER IF NOT EXISTS trg_words_sync_id_after_insert
            AFTER INSERT ON words
            FOR EACH ROW
            WHEN NEW.sync_id IS NULL OR TRIM(NEW.sync_id) = ''
            BEGIN
                UPDATE words SET sync_id = )SQL") + sqliteUuidExpression() + QString(R"SQL(
                WHERE id = NEW.id;
            END;
        )SQL"), {} },
        { QString(R"SQL(
            CREATE TRIGGER IF NOT EXISTS trg_word_review_stats_sync_after_insert
            AFTER INSERT ON word_review_stats
            FOR EACH ROW
            WHEN NEW.sync_id IS NULL OR TRIM(NEW.sync_id) = '' OR NEW.created_at = 0
            BEGIN
                UPDATE word_review_stats
                SET sync_id = CASE
                        WHEN NEW.sync_id IS NULL OR TRIM(NEW.sync_id) = '' THEN )SQL") + sqliteUuidExpression() + QString(R"SQL(
                        ELSE NEW.sync_id
                    END,
                    created_at = CASE
                        WHEN NEW.created_at = 0 THEN COALESCE(NEW.updated_at, CAST(strftime('%s', 'now') AS INTEGER))
                        ELSE NEW.created_at
                    END
                WHERE word_id = NEW.word_id;
            END;
        )SQL"), {} },
        { QString(R"SQL(
            CREATE TRIGGER IF NOT EXISTS trg_app_settings_id_after_insert
            AFTER INSERT ON app_settings
            FOR EACH ROW
            WHEN NEW.id IS NULL OR TRIM(NEW.id) = ''
            BEGIN
                UPDATE app_settings SET id = )SQL") + sqliteUuidExpression() + QString(R"SQL(
                WHERE rowid = NEW.rowid;
            END;
        )SQL"), {} },
    };

    return executeSqlBatch(commands);
}

QString DLDatabaseManager::databasePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_db.databaseName();
}

QString DLDatabaseManager::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void DLDatabaseManager::setLastError(const QString& error)
{
    QMutexLocker locker(&m_mutex);
    m_lastError = error;
}

QString DLDatabaseManager::localDeviceId()
{
    QMutexLocker locker(&m_mutex);
    return ensureDeviceIdentityLocked();
}

QVariantMap DLDatabaseManager::localDeviceIdentity()
{
    const QString deviceId = localDeviceId();
    if (deviceId.isEmpty()) {
        return {};
    }

    return selectOneRow(
        QStringLiteral(R"(
            SELECT id, device_name, created_at, last_seen_at
            FROM device_identity
            WHERE id = :id;
        )"),
        {{ QStringLiteral(":id"), deviceId }});
}

bool DLDatabaseManager::setSyncStateValue(const QString& key, const QString& value)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        setLastError(QStringLiteral("Sync state key cannot be empty."));
        return false;
    }

    return executeSql(
        QStringLiteral(R"(
            INSERT INTO sync_state (key, value, updated_at)
            VALUES (:key, :value, :updated_at)
            ON CONFLICT(key) DO UPDATE SET
                value = excluded.value,
                updated_at = excluded.updated_at;
        )"),
        {
            { QStringLiteral(":key"), trimmedKey },
            { QStringLiteral(":value"), value },
            { QStringLiteral(":updated_at"), currentUnixTimeMs() }
        });
}

QString DLDatabaseManager::syncStateValue(const QString& key, const QString& fallback)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        return fallback;
    }

    const QVariantMap row = selectOneRow(
        QStringLiteral("SELECT value FROM sync_state WHERE key = :key;"),
        {{ QStringLiteral(":key"), trimmedKey }});
    return row.isEmpty() ? fallback : row.value(QStringLiteral("value")).toString();
}

bool DLDatabaseManager::executeSql(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        m_lastError = query.lastError().text();
        logSqlFailure("execute/prepare", sql, args, {}, argumentKeys(args), query.lastError());
        return false;
    }

    QStringList missingKeys;
    QStringList unusedKeys;
    bindNamedValues(query, args, &missingKeys, &unusedKeys);
    if (!missingKeys.isEmpty()) {
        m_lastError = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
        logSqlFailure("execute/bind", sql, args, missingKeys, unusedKeys, query.lastError());
        return false;
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logSqlFailure("execute/exec", sql, args, missingKeys, unusedKeys, query.lastError());
        return false;
    }

    m_lastError.clear();
    return true;
}

bool DLDatabaseManager::executeSqlBatch(const QList<DLSqlCommand>& commands)
{
    return transaction([&](QSqlDatabase& db, QString* error) {
        for (const DLSqlCommand& command : commands) {
            QSqlQuery query(db);
            if (!query.prepare(command.sql)) {
                if (error) {
                    *error = query.lastError().text();
                }
                logSqlFailure("batch/prepare", command.sql, command.args, {}, argumentKeys(command.args), query.lastError());
                return false;
            }

            QStringList missingKeys;
            QStringList unusedKeys;
            bindNamedValues(query, command.args, &missingKeys, &unusedKeys);
            if (!missingKeys.isEmpty()) {
                if (error) {
                    *error = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
                }
                logSqlFailure("batch/bind", command.sql, command.args, missingKeys, unusedKeys, query.lastError());
                return false;
            }

            if (!query.exec()) {
                if (error) {
                    *error = query.lastError().text();
                }
                logSqlFailure("batch/exec", command.sql, command.args, missingKeys, unusedKeys, query.lastError());
                return false;
            }
        }
        return true;
    });
}

int DLDatabaseManager::executeInsert(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        m_lastError = query.lastError().text();
        logSqlFailure("insert/prepare", sql, args, {}, argumentKeys(args), query.lastError());
        return -1;
    }

    QStringList missingKeys;
    QStringList unusedKeys;
    bindNamedValues(query, args, &missingKeys, &unusedKeys);
    if (!missingKeys.isEmpty()) {
        m_lastError = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
        logSqlFailure("insert/bind", sql, args, missingKeys, unusedKeys, query.lastError());
        return -1;
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logSqlFailure("insert/exec", sql, args, missingKeys, unusedKeys, query.lastError());
        return -1;
    }

    m_lastError.clear();
    return static_cast<int>(query.lastInsertId().toLongLong());
}

QVariantList DLDatabaseManager::selectRows(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        m_lastError = query.lastError().text();
        logSqlFailure("selectRows/prepare", sql, args, {}, argumentKeys(args), query.lastError());
        return {};
    }

    QStringList missingKeys;
    QStringList unusedKeys;
    bindNamedValues(query, args, &missingKeys, &unusedKeys);
    if (!missingKeys.isEmpty()) {
        m_lastError = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
        logSqlFailure("selectRows/bind", sql, args, missingKeys, unusedKeys, query.lastError());
        return {};
    }

    QVariantList rows;
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logSqlFailure("selectRows/exec", sql, args, missingKeys, unusedKeys, query.lastError());
        return rows;
    }

    m_lastError.clear();
    const QSqlRecord record = query.record();
    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < record.count(); ++i) {
            row.insert(record.fieldName(i), query.value(i));
        }
        rows.append(row);
    }

    return rows;
}

QVariantMap DLDatabaseManager::selectOneRow(const QString& sql, const QVariantMap& args)
{
    const QVariantList rows = selectRows(sql, args);
    return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}

int DLDatabaseManager::selectInt(const QString& sql, const QVariantMap& args, int fallback)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        m_lastError = query.lastError().text();
        logSqlFailure("selectInt/prepare", sql, args, {}, argumentKeys(args), query.lastError());
        return fallback;
    }

    QStringList missingKeys;
    QStringList unusedKeys;
    bindNamedValues(query, args, &missingKeys, &unusedKeys);
    if (!missingKeys.isEmpty()) {
        m_lastError = QStringLiteral("Missing SQL parameter binding(s): %1").arg(missingKeys.join(QStringLiteral(", ")));
        logSqlFailure("selectInt/bind", sql, args, missingKeys, unusedKeys, query.lastError());
        return fallback;
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logSqlFailure("selectInt/exec", sql, args, missingKeys, unusedKeys, query.lastError());
        return fallback;
    }

    m_lastError.clear();
    return query.next() ? query.value(0).toInt() : fallback;
}

bool DLDatabaseManager::transaction(const std::function<bool(QSqlDatabase&, QString*)>& callback)
{
    QMutexLocker locker(&m_mutex);

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        qCWarning(dlDb) << "Failed to start database transaction:" << m_lastError;
        return false;
    }

    QString callbackError;
    if (!callback(m_db, &callbackError)) {
        if (!callbackError.isEmpty()) {
            m_lastError = callbackError;
        }
        qCWarning(dlDb) << "Rolling back database transaction:" << m_lastError;
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        qCWarning(dlDb) << "Failed to commit database transaction:" << m_lastError;
        m_db.rollback();
        return false;
    }

    m_lastError.clear();
    return true;
}

QString DLDatabaseManager::ensureDeviceIdentityLocked()
{
    if (!m_deviceId.isEmpty()) {
        return m_deviceId;
    }

    if (!m_db.isOpen()) {
        m_lastError = QStringLiteral("Database is not open.");
        return {};
    }

    const qint64 now = currentUnixTimeMs();
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"(
            SELECT id
            FROM device_identity
            WHERE TRIM(COALESCE(id, '')) != ''
            ORDER BY created_at
            LIMIT 1;
        )"))) {
        m_lastError = query.lastError().text();
        logSqlFailure("deviceIdentity/select/prepare", QString(), {}, {}, {}, query.lastError());
        return {};
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logSqlFailure("deviceIdentity/select/exec", QStringLiteral("SELECT id FROM device_identity"), {}, {}, {}, query.lastError());
        return {};
    }

    if (query.next()) {
        m_deviceId = query.value(0).toString();

        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE device_identity SET last_seen_at = :last_seen_at WHERE id = :id;"));
        update.bindValue(QStringLiteral(":last_seen_at"), now);
        update.bindValue(QStringLiteral(":id"), m_deviceId);
        if (!update.exec()) {
            m_lastError = update.lastError().text();
            logSqlFailure("deviceIdentity/updateLastSeen", QStringLiteral("UPDATE device_identity SET last_seen_at = :last_seen_at WHERE id = :id;"), {}, {}, {}, update.lastError());
            m_deviceId.clear();
            return {};
        }

        m_lastError.clear();
        return m_deviceId;
    }

    const QString id = generateUuid();
    const QString deviceName = QSysInfo::machineHostName().trimmed().isEmpty()
        ? QSysInfo::prettyProductName()
        : QSysInfo::machineHostName().trimmed();

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(R"(
        INSERT INTO device_identity (id, device_name, created_at, last_seen_at)
        VALUES (:id, :device_name, :created_at, :last_seen_at);
    )"));
    insert.bindValue(QStringLiteral(":id"), id);
    insert.bindValue(QStringLiteral(":device_name"), deviceName.trimmed().isEmpty() ? nullVariant() : QVariant(deviceName));
    insert.bindValue(QStringLiteral(":created_at"), now);
    insert.bindValue(QStringLiteral(":last_seen_at"), now);
    if (!insert.exec()) {
        m_lastError = insert.lastError().text();
        logSqlFailure("deviceIdentity/insert", QStringLiteral("INSERT INTO device_identity"), {}, {}, {}, insert.lastError());
        return {};
    }

    m_deviceId = id;
    m_lastError.clear();
    return m_deviceId;
}

qint64 DLDatabaseManager::currentUnixTime()
{
    return QDateTime::currentSecsSinceEpoch();
}

qint64 DLDatabaseManager::currentUnixTimeMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString DLDatabaseManager::currentDeviceId()
{
    return DLDatabaseManager::instance().localDeviceId();
}

QString DLDatabaseManager::generateUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString DLDatabaseManager::normalizedText(const QString& value)
{
    return value.trimmed().toLower();
}

QVariant DLDatabaseManager::nullVariant()
{
    return QVariant();
}

QString DLDatabaseManager::insertGroup(const QString& name, const QString& colorHex)
{
    DLWordGroup group;
    group.name = name;
    group.colorHex = colorHex;
    return DLGroupRepository(*this).insertGroup(group);
}

bool DLDatabaseManager::updateGroup(const QString& id, const QString& name, const QString& colorHex)
{
    DLWordGroup group;
    group.id = id;
    group.name = name;
    group.colorHex = colorHex;
    return DLGroupRepository(*this).updateGroup(group);
}

bool DLDatabaseManager::deleteGroup(const QString& id)
{
    return DLGroupRepository(*this).deleteGroup(id);
}

QVariantList DLDatabaseManager::fetchAllGroups()
{
    return DLModelMappers::groupsToList(DLGroupRepository(*this).fetchAllGroups());
}

QVariantMap DLDatabaseManager::fetchGroupById(const QString& id)
{
    const DLWordGroup group = DLGroupRepository(*this).fetchGroupById(id);
    return group.id.isEmpty() ? QVariantMap() : DLModelMappers::groupToMap(group);
}

QString DLDatabaseManager::insertWord(const QString& germanWord,
                                      const QString& article,
                                      const QString& partOfSpeech,
                                      const QString& nativeTranslation,
                                      const QString& examplePhraseDe,
                                      const QString& examplePhraseNative,
                                      const QString& groupId,
                                      const QString& syncId,
                                      const QString& notes,
                                      const QString& pluralForm,
                                      const QString& praeteritumForm,
                                      const QString& partizipIIForm,
                                      const QString& positiveForm,
                                      const QString& comparativeForm,
                                      const QString& superlativeForm)
{
    DLWord word;
    word.id = syncId;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupId = groupId;
    word.notes = notes;
    word.pluralForm = pluralForm;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).insertWord(word);
}

bool DLDatabaseManager::updateWord(const QString& id,
                                   const QString& germanWord,
                                   const QString& article,
                                   const QString& partOfSpeech,
                                   const QString& nativeTranslation,
                                   const QString& examplePhraseDe,
                                   const QString& examplePhraseNative,
                                   const QString& groupId,
                                   const QString& syncId,
                                   const QString& notes,
                                   const QString& pluralForm,
                                   const QString& praeteritumForm,
                                   const QString& partizipIIForm,
                                   const QString& positiveForm,
                                   const QString& comparativeForm,
                                   const QString& superlativeForm)
{
    DLWord word;
    word.id = id.trimmed().isEmpty() ? syncId : id;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupId = groupId;
    word.notes = notes;
    word.pluralForm = pluralForm;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).updateWord(word);
}

bool DLDatabaseManager::deleteWord(const QString& id)
{
    return DLWordRepository(*this).deleteWord(id);
}

QVariantMap DLDatabaseManager::fetchWordById(const QString& id)
{
    const DLWord word = DLWordRepository(*this).fetchWordById(id);
    return word.id.isEmpty() ? QVariantMap() : DLModelMappers::wordToMap(word);
}

bool DLDatabaseManager::wordExists(const QString& germanWord, const QString& nativeTranslation, const QString& excludingId)
{
    return DLWordRepository(*this).wordExists(germanWord, nativeTranslation, excludingId);
}

QVariantList DLDatabaseManager::fetchAllWords(const QString& sortMode, const QString& groupId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).fetchAllWords(sortMode, groupId));
}

QVariantList DLDatabaseManager::searchWords(const QString& query, const QString& groupId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).searchWords(query, groupId));
}

QVariantList DLDatabaseManager::fetchWordsByGroup(const QString& groupId)
{
    return fetchAllWords(QStringLiteral("newest"), groupId);
}

QVariantList DLDatabaseManager::fetchRandomWords(int limit, const QString& groupId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchRandomWords(limit, groupId));
}

QVariantList DLDatabaseManager::fetchTranslationQuizWords(int limit, const QString& groupId, const QString& partOfSpeech)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchTranslationQuizWords(limit, groupId, partOfSpeech));
}

QVariantList DLDatabaseManager::fetchNouns(const QString& groupId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchNouns(groupId));
}

int DLDatabaseManager::getWordCount(const QString& groupId)
{
    return DLWordRepository(*this).getWordCount(groupId);
}

int DLDatabaseManager::getTranslationQuizWordCount(const QString& groupId, const QString& partOfSpeech)
{
    return DLQuizRepository(*this).getTranslationQuizWordCount(groupId, partOfSpeech);
}

int DLDatabaseManager::getGroupCount()
{
    return DLGroupRepository(*this).getGroupCount();
}

int DLDatabaseManager::getNounCount(const QString& groupId)
{
    return DLQuizRepository(*this).getNounCount(groupId);
}

bool DLDatabaseManager::incrementCorrectAnswer(const QString& wordId)
{
    return DLReviewStatsRepository(*this).incrementCorrectAnswer(wordId);
}

bool DLDatabaseManager::incrementWrongAnswer(const QString& wordId)
{
    return DLReviewStatsRepository(*this).incrementWrongAnswer(wordId);
}

QVariantMap DLDatabaseManager::getDatabaseStats()
{
    return DLDatabaseMaintenanceService(*this).getDatabaseStats();
}

bool DLDatabaseManager::importDatabaseMerge(const QString& sourceDatabasePath)
{
    return DLDatabaseMaintenanceService(*this).importDatabaseMerge(sourceDatabasePath);
}

bool DLDatabaseManager::deleteAllData()
{
    const bool success = DLDatabaseMaintenanceService(*this).deleteAllData();
    if (success) {
        QMutexLocker locker(&m_mutex);
        m_deviceId.clear();
    }
    return success;
}
