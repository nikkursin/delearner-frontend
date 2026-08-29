#include "DLDatabaseManager.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
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
constexpr int kSqliteSchemaMajor = 1;
constexpr int kSqliteSchemaMinor = 1;
constexpr int kSqliteUserVersion = kSqliteSchemaMajor * 1000 + kSqliteSchemaMinor;

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

QString createUuidString()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool execSimpleSql(QSqlDatabase& db, const QString& sql, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (error) {
            *error = query.lastError().text();
        }
        logSqlFailure("migration/exec", sql, {}, {}, {}, query.lastError());
        return false;
    }
    return true;
}

bool backfillUuidColumn(QSqlDatabase& db,
                        const QString& tableName,
                        const QString& idColumnName,
                        const QString& uuidColumnName,
                        QString* error)
{
    QSqlQuery select(db);
    const QString selectSql = QStringLiteral("SELECT %1 FROM %2 WHERE %3 IS NULL OR TRIM(%3) = '';")
                                  .arg(idColumnName, tableName, uuidColumnName);
    if (!select.exec(selectSql)) {
        if (error) {
            *error = select.lastError().text();
        }
        logSqlFailure("migration/selectUuidBackfill", selectSql, {}, {}, {}, select.lastError());
        return false;
    }

    QList<int> rowIds;
    while (select.next()) {
        rowIds.append(select.value(0).toInt());
    }

    const QString updateSql = QStringLiteral("UPDATE %1 SET %2 = :uuid WHERE %3 = :id;")
                                  .arg(tableName, uuidColumnName, idColumnName);
    QSqlQuery update(db);
    if (!update.prepare(updateSql)) {
        if (error) {
            *error = update.lastError().text();
        }
        logSqlFailure("migration/prepareUuidBackfill", updateSql, {}, {}, {}, update.lastError());
        return false;
    }

    for (int id : rowIds) {
        update.bindValue(QStringLiteral(":uuid"), createUuidString());
        update.bindValue(QStringLiteral(":id"), id);
        if (!update.exec()) {
            if (error) {
                *error = update.lastError().text();
            }
            logSqlFailure("migration/updateUuidBackfill", updateSql, {}, {}, {}, update.lastError());
            return false;
        }
    }

    return true;
}

bool recordCurrentSqliteSchemaVersion(QSqlDatabase& db, QString* error)
{
    if (!execMigrationSql(db, QStringLiteral(R"(
        INSERT INTO schema_version (component, major, minor, updated_at)
        VALUES ('sqlite', :major, :minor, :updated_at)
        ON CONFLICT(component) DO UPDATE SET
            major = excluded.major,
            minor = excluded.minor,
            updated_at = excluded.updated_at;
    )"), error, {
            { QStringLiteral(":major"), kSqliteSchemaMajor },
            { QStringLiteral(":minor"), kSqliteSchemaMinor },
            { QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTime() }
        })) {
        return false;
    }

    return execSimpleSql(db, QStringLiteral("PRAGMA user_version = %1;").arg(kSqliteUserVersion), error);
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

    return createTablesIfNeeded() && migrateSchemaIfNeeded() && createIndexesIfNeeded();
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
}

bool DLDatabaseManager::createTablesIfNeeded()
{
    qCDebug(dlDb) << "Creating database tables if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS schema_version (
                component TEXT PRIMARY KEY,
                major INTEGER NOT NULL,
                minor INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS groups (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sync_id TEXT NOT NULL UNIQUE,
                name TEXT NOT NULL,
                color_hex TEXT DEFAULT '#3366CC',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS words (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                sync_id TEXT NOT NULL UNIQUE,
                german_word TEXT NOT NULL,
                normalized_german_word TEXT NOT NULL,
                article TEXT,
                part_of_speech TEXT NOT NULL DEFAULT 'Andere',
                native_translation TEXT NOT NULL,
                normalized_native_translation TEXT NOT NULL,
                example_phrase_de TEXT,
                example_phrase_native TEXT,
                group_id INTEGER,
                group_sync_id TEXT,
                notes TEXT,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE SET NULL,
                FOREIGN KEY(group_sync_id) REFERENCES groups(sync_id) ON DELETE SET NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS word_review_stats (
                word_id INTEGER PRIMARY KEY,
                word_sync_id TEXT NOT NULL UNIQUE,
                correct_answers INTEGER NOT NULL DEFAULT 0,
                wrong_answers INTEGER NOT NULL DEFAULT 0,
                last_reviewed_at INTEGER,
                ease_factor REAL DEFAULT 2.5,
                interval_days INTEGER DEFAULT 0,
                due_at INTEGER,
                updated_at INTEGER NOT NULL,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE,
                FOREIGN KEY(word_sync_id) REFERENCES words(sync_id) ON DELETE CASCADE
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
            CREATE TABLE IF NOT EXISTS sync_outbox_events (
                event_id TEXT PRIMARY KEY,
                contract_version TEXT NOT NULL,
                device_id TEXT,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                operation TEXT NOT NULL CHECK(operation IN ('create', 'update', 'delete')),
                updated_at INTEGER NOT NULL,
                envelope_json TEXT NOT NULL,
                payload_json TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                send_attempt_count INTEGER NOT NULL DEFAULT 0 CHECK(send_attempt_count >= 0),
                last_attempted_at INTEGER,
                next_attempt_after INTEGER,
                last_error TEXT
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS sync_acknowledged_event_diagnostics (
                event_id TEXT PRIMARY KEY,
                contract_version TEXT NOT NULL,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                operation TEXT NOT NULL CHECK(operation IN ('create', 'update', 'delete')),
                updated_at INTEGER NOT NULL,
                created_at INTEGER NOT NULL,
                acknowledged_at INTEGER NOT NULL,
                server_sequence INTEGER,
                canonical_result_json TEXT,
                diagnostic_json TEXT
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

        if (!execMigrationSql(db, QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS schema_version (
                component TEXT PRIMARY KEY,
                major INTEGER NOT NULL,
                minor INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
        )"), error)) {
            return false;
        }

        if (!execMigrationSql(db, QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS sync_outbox_events (
                event_id TEXT PRIMARY KEY,
                contract_version TEXT NOT NULL,
                device_id TEXT,
                entity_type TEXT NOT NULL,
                entity_id TEXT NOT NULL,
                operation TEXT NOT NULL CHECK(operation IN ('create', 'update', 'delete')),
                updated_at INTEGER NOT NULL,
                envelope_json TEXT NOT NULL,
                payload_json TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                send_attempt_count INTEGER NOT NULL DEFAULT 0 CHECK(send_attempt_count >= 0),
                last_attempted_at INTEGER,
                next_attempt_after INTEGER,
                last_error TEXT
            );
        )"), error)
            || !execMigrationSql(db, QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS sync_acknowledged_event_diagnostics (
                    event_id TEXT PRIMARY KEY,
                    contract_version TEXT NOT NULL,
                    entity_type TEXT NOT NULL,
                    entity_id TEXT NOT NULL,
                    operation TEXT NOT NULL CHECK(operation IN ('create', 'update', 'delete')),
                    updated_at INTEGER NOT NULL,
                    created_at INTEGER NOT NULL,
                    acknowledged_at INTEGER NOT NULL,
                    server_sequence INTEGER,
                    canonical_result_json TEXT,
                    diagnostic_json TEXT
                );
            )"), error)) {
            return false;
        }

        const bool groupsHasUpdatedAt = tableHasColumn(db, QStringLiteral("groups"), QStringLiteral("updated_at"), error);
        if (!error->isEmpty()) {
            return false;
        }

        if (!groupsHasUpdatedAt) {
            qCInfo(dlDb) << "Adding missing groups.updated_at column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE groups ADD COLUMN updated_at INTEGER;"), error)
                || !execMigrationSql(db,
                                     QStringLiteral("UPDATE groups SET updated_at = COALESCE(created_at, :now) WHERE updated_at IS NULL;"),
                                     error,
                                     {{ QStringLiteral(":now"), now }})) {
                return false;
            }
        }

        const bool groupsHasDeletedAt = tableHasColumn(db, QStringLiteral("groups"), QStringLiteral("deleted_at"), error);
        if (!error->isEmpty()) {
            return false;
        }

        if (!groupsHasDeletedAt) {
            qCInfo(dlDb) << "Adding missing groups.deleted_at column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE groups ADD COLUMN deleted_at INTEGER;"), error)) {
                return false;
            }
        }

        const bool groupsHasSyncId = tableHasColumn(db, QStringLiteral("groups"), QStringLiteral("sync_id"), error);
        if (!error->isEmpty()) {
            return false;
        }
        if (!groupsHasSyncId) {
            qCInfo(dlDb) << "Adding missing groups.sync_id column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE groups ADD COLUMN sync_id TEXT;"), error)
                || !execMigrationSql(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_groups_sync_id ON groups(sync_id);"), error)
                || !backfillUuidColumn(db, QStringLiteral("groups"), QStringLiteral("id"), QStringLiteral("sync_id"), error)) {
                return false;
            }
        }

        const bool wordsHasSyncId = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("sync_id"), error);
        if (!error->isEmpty()) {
            return false;
        }
        if (!wordsHasSyncId) {
            qCInfo(dlDb) << "Adding missing words.sync_id column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE words ADD COLUMN sync_id TEXT;"), error)
                || !execMigrationSql(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_words_sync_id ON words(sync_id);"), error)
                || !backfillUuidColumn(db, QStringLiteral("words"), QStringLiteral("id"), QStringLiteral("sync_id"), error)) {
                return false;
            }
        }

        const bool wordsHasGroupSyncId = tableHasColumn(db, QStringLiteral("words"), QStringLiteral("group_sync_id"), error);
        if (!error->isEmpty()) {
            return false;
        }
        if (!wordsHasGroupSyncId) {
            qCInfo(dlDb) << "Adding missing words.group_sync_id column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE words ADD COLUMN group_sync_id TEXT;"), error)) {
                return false;
            }
        }
        if (!execMigrationSql(db, QStringLiteral(R"(
            UPDATE words
            SET group_sync_id = (
                SELECT groups.sync_id
                FROM groups
                WHERE groups.id = words.group_id
            )
            WHERE group_id IS NOT NULL
              AND (group_sync_id IS NULL OR TRIM(group_sync_id) = '');
        )"), error)) {
            return false;
        }

        const bool statsHasWordSyncId = tableHasColumn(db, QStringLiteral("word_review_stats"), QStringLiteral("word_sync_id"), error);
        if (!error->isEmpty()) {
            return false;
        }
        if (!statsHasWordSyncId) {
            qCInfo(dlDb) << "Adding missing word_review_stats.word_sync_id column";
            if (!execMigrationSql(db, QStringLiteral("ALTER TABLE word_review_stats ADD COLUMN word_sync_id TEXT;"), error)) {
                return false;
            }
        }
        if (!execMigrationSql(db, QStringLiteral(R"(
            UPDATE word_review_stats
            SET word_sync_id = (
                SELECT words.sync_id
                FROM words
                WHERE words.id = word_review_stats.word_id
            )
            WHERE word_sync_id IS NULL OR TRIM(word_sync_id) = '';
        )"), error)) {
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

        return recordCurrentSqliteSchemaVersion(db, error);
    });
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    qCDebug(dlDb) << "Creating database indexes if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_groups_sync_id ON groups(sync_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_groups_deleted_at ON groups(deleted_at);"), {} },
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_words_sync_id ON words(sync_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_group_id ON words(group_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_group_sync_id ON words(group_sync_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_normalized_german ON words(normalized_german_word);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_part_of_speech ON words(part_of_speech);"), {} },
        { QStringLiteral(R"(
            CREATE UNIQUE INDEX IF NOT EXISTS idx_words_unique_active_translation
            ON words(normalized_german_word, normalized_native_translation)
            WHERE deleted_at IS NULL;
        )"), {} },
        { QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_word_review_stats_word_sync_id ON word_review_stats(word_sync_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_due_at ON word_review_stats(due_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_events_created_at ON sync_outbox_events(created_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_events_next_attempt_after ON sync_outbox_events(next_attempt_after);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_events_entity ON sync_outbox_events(entity_type, entity_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_acknowledged_event_diagnostics_acknowledged_at ON sync_acknowledged_event_diagnostics(acknowledged_at);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_acknowledged_event_diagnostics_entity ON sync_acknowledged_event_diagnostics(entity_type, entity_id);"), {} }
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

void DLDatabaseManager::setSyncContext(const QString& userId, const QString& deviceId)
{
    QMutexLocker locker(&m_mutex);
    m_syncUserId = userId.trimmed();
    m_syncDeviceId = deviceId.trimmed();
}

void DLDatabaseManager::clearSyncContext()
{
    QMutexLocker locker(&m_mutex);
    m_syncUserId.clear();
    m_syncDeviceId.clear();
    m_failAfterNextSyncOutboxWriteForTesting = false;
}

bool DLDatabaseManager::hasSyncContext() const
{
    QMutexLocker locker(&m_mutex);
    return !QUuid(m_syncUserId).isNull() && !QUuid(m_syncDeviceId).isNull();
}

bool DLDatabaseManager::recordSyncOutboxEvent(QSqlDatabase& db, QString* error, DLSyncEventEnvelope event)
{
    if (QUuid(m_syncUserId).isNull() || QUuid(m_syncDeviceId).isNull()) {
        if (error) {
            *error = QStringLiteral("Sync outbox requires authenticated user and registered device context.");
        }
        return false;
    }

    event.contractVersion = DLSyncEventSerializer::contractVersion();
    event.eventId = event.eventId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : event.eventId.trimmed();
    event.deviceId = m_syncDeviceId;
    event.authenticatedUserId = m_syncUserId;
    if (event.operation == QStringLiteral("delete")) {
        event.tombstone.insert(QStringLiteral("ownerUserId"), m_syncUserId);
    } else {
        event.payload.insert(QStringLiteral("ownerUserId"), m_syncUserId);
    }

    QString serializationError;
    const QByteArray envelopeJson = DLSyncEventSerializer::serializeContractJson(event, &serializationError);
    if (envelopeJson.isEmpty()) {
        if (error) {
            *error = serializationError;
        }
        return false;
    }

    const QByteArray payloadJson = DLSyncEventSerializer::serializeBodyJson(event, &serializationError);
    if (payloadJson.isEmpty()) {
        if (error) {
            *error = serializationError;
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO sync_outbox_events
            (event_id, contract_version, device_id, entity_type, entity_id, operation,
             updated_at, envelope_json, payload_json, created_at)
        VALUES
            (:event_id, :contract_version, :device_id, :entity_type, :entity_id, :operation,
             :updated_at, :envelope_json, :payload_json, :created_at);
    )"));
    query.bindValue(QStringLiteral(":event_id"), event.eventId);
    query.bindValue(QStringLiteral(":contract_version"), event.contractVersion);
    query.bindValue(QStringLiteral(":device_id"), event.deviceId);
    query.bindValue(QStringLiteral(":entity_type"), event.entityType);
    query.bindValue(QStringLiteral(":entity_id"), event.entityId);
    query.bindValue(QStringLiteral(":operation"), event.operation);
    query.bindValue(QStringLiteral(":updated_at"), event.updatedAt);
    query.bindValue(QStringLiteral(":envelope_json"), QString::fromUtf8(envelopeJson));
    query.bindValue(QStringLiteral(":payload_json"), QString::fromUtf8(payloadJson));
    query.bindValue(QStringLiteral(":created_at"), DLDatabaseManager::currentUnixTime());

    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        logSqlFailure("syncOutbox/insert", query.lastQuery(), {}, {}, {}, query.lastError());
        return false;
    }

    if (m_failAfterNextSyncOutboxWriteForTesting) {
        m_failAfterNextSyncOutboxWriteForTesting = false;
        if (error) {
            *error = QStringLiteral("Injected sync outbox failure after write.");
        }
        return false;
    }

    return true;
}

void DLDatabaseManager::failAfterNextSyncOutboxWriteForTesting()
{
    QMutexLocker locker(&m_mutex);
    m_failAfterNextSyncOutboxWriteForTesting = true;
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

qint64 DLDatabaseManager::currentUnixTime()
{
    return QDateTime::currentSecsSinceEpoch();
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

bool DLDatabaseManager::updateGroup(const QString& syncId, const QString& name, const QString& colorHex)
{
    DLWordGroup group;
    group.syncId = syncId;
    group.name = name;
    group.colorHex = colorHex;
    return DLGroupRepository(*this).updateGroup(group);
}

bool DLDatabaseManager::deleteGroup(const QString& syncId)
{
    return DLGroupRepository(*this).deleteGroup(syncId);
}

QVariantList DLDatabaseManager::fetchAllGroups()
{
    return DLModelMappers::groupsToList(DLGroupRepository(*this).fetchAllGroups());
}

QVariantMap DLDatabaseManager::fetchGroupById(const QString& syncId)
{
    const DLWordGroup group = DLGroupRepository(*this).fetchGroupById(syncId);
    return group.id < 0 ? QVariantMap() : DLModelMappers::groupToMap(group);
}

QString DLDatabaseManager::insertWord(const QString& germanWord,
                                      const QString& article,
                                      const QString& partOfSpeech,
                                      const QString& nativeTranslation,
                                      const QString& examplePhraseDe,
                                      const QString& examplePhraseNative,
                                      const QString& groupSyncId,
                                      const QString& syncId,
                                      const QString& pluralForm,
                                      const QString& praeteritumForm,
                                      const QString& partizipIIForm,
                                      const QString& positiveForm,
                                      const QString& comparativeForm,
                                      const QString& superlativeForm)
{
    DLWord word;
    word.syncId = syncId;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupSyncId = groupSyncId;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).insertWord(word);
}

bool DLDatabaseManager::updateWord(const QString& syncId,
                                   const QString& germanWord,
                                   const QString& article,
                                   const QString& partOfSpeech,
                                   const QString& nativeTranslation,
                                   const QString& examplePhraseDe,
                                   const QString& examplePhraseNative,
                                   const QString& groupSyncId,
                                   const QString& pluralForm,
                                   const QString& praeteritumForm,
                                   const QString& partizipIIForm,
                                   const QString& positiveForm,
                                   const QString& comparativeForm,
                                   const QString& superlativeForm)
{
    DLWord word;
    word.syncId = syncId;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupSyncId = groupSyncId;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).updateWord(word);
}

bool DLDatabaseManager::deleteWord(const QString& syncId)
{
    return DLWordRepository(*this).deleteWord(syncId);
}

QVariantMap DLDatabaseManager::fetchWordById(const QString& syncId)
{
    const DLWord word = DLWordRepository(*this).fetchWordById(syncId);
    return word.id < 0 ? QVariantMap() : DLModelMappers::wordToMap(word);
}

bool DLDatabaseManager::wordExists(const QString& germanWord, const QString& nativeTranslation, const QString& excludingSyncId)
{
    return DLWordRepository(*this).wordExists(germanWord, nativeTranslation, excludingSyncId);
}

QVariantList DLDatabaseManager::fetchAllWords(const QString& sortMode, const QString& groupSyncId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).fetchAllWords(sortMode, groupSyncId));
}

QVariantList DLDatabaseManager::searchWords(const QString& query, const QString& groupSyncId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).searchWords(query, groupSyncId));
}

QVariantList DLDatabaseManager::fetchWordsByGroup(const QString& groupSyncId)
{
    return fetchAllWords(QStringLiteral("newest"), groupSyncId);
}

QVariantList DLDatabaseManager::fetchRandomWords(int limit, const QString& groupSyncId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchRandomWords(limit, groupSyncId));
}

QVariantList DLDatabaseManager::fetchTranslationQuizWords(int limit, const QString& groupSyncId, const QString& partOfSpeech)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchTranslationQuizWords(limit, groupSyncId, partOfSpeech));
}

QVariantList DLDatabaseManager::fetchNouns(const QString& groupSyncId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchNouns(groupSyncId));
}

int DLDatabaseManager::getWordCount(const QString& groupSyncId)
{
    return DLWordRepository(*this).getWordCount(groupSyncId);
}

int DLDatabaseManager::getTranslationQuizWordCount(const QString& groupSyncId, const QString& partOfSpeech)
{
    return DLQuizRepository(*this).getTranslationQuizWordCount(groupSyncId, partOfSpeech);
}

int DLDatabaseManager::getGroupCount()
{
    return DLGroupRepository(*this).getGroupCount();
}

int DLDatabaseManager::getNounCount(const QString& groupSyncId)
{
    return DLQuizRepository(*this).getNounCount(groupSyncId);
}

bool DLDatabaseManager::incrementCorrectAnswer(const QString& wordSyncId)
{
    return DLReviewStatsRepository(*this).incrementCorrectAnswer(wordSyncId);
}

bool DLDatabaseManager::incrementWrongAnswer(const QString& wordSyncId)
{
    return DLReviewStatsRepository(*this).incrementWrongAnswer(wordSyncId);
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
    return DLDatabaseMaintenanceService(*this).deleteAllData();
}
