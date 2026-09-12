#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"

class TestDatabaseManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void openDatabaseOpensTemporarySqliteDatabase();
    void createTablesIfNeededCreatesRequiredTables();
    void createIndexesIfNeededCreatesExpectedIndexes();
    void openDatabaseRecordsSqliteSchemaVersion();
    void cleanDatabaseCreatesUuidIdentityColumns();
    void cleanDatabaseCreatesSyncOutboxSchema();
    void cleanDatabaseCreatesSyncPullCursorSchema();
    void remoteCursorAdvancementPersistsAndDoesNotRegress();
    void insertsPopulateUuidIdentities();
    void closeDatabaseClosesSafely();
    void lastErrorIsSetWhenOperationFails();
    void selectRowsReturnsEmptyListForEmptyValidResult();
    void namedBindingsAcceptColonlessKeys();
    void missingNamedBindingFailsClearly();
    void invalidSelectSetsLastError();
    void openDatabaseMigratesLegacySchema();
    void deleteAllDataRemovesStoredData();

private:
    QString m_dbPath;

    bool tableExists(const QString& tableName);
    bool columnExists(const QString& tableName, const QString& columnName);
    bool indexExists(const QString& indexName);
};

void TestDatabaseManager::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestDatabaseManager::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

bool TestDatabaseManager::tableExists(const QString& tableName)
{
    return DLDatabaseManager::instance().selectInt(
               QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = :name;"),
               {{ QStringLiteral(":name"), tableName }}) == 1;
}

bool TestDatabaseManager::columnExists(const QString& tableName, const QString& columnName)
{
    return DLDatabaseManager::instance().selectInt(
               QStringLiteral("SELECT COUNT(*) FROM pragma_table_info(:table_name) WHERE name = :column_name;"),
               {
                   { QStringLiteral(":table_name"), tableName },
                   { QStringLiteral(":column_name"), columnName }
               }) == 1;
}

bool TestDatabaseManager::indexExists(const QString& indexName)
{
    return DLDatabaseManager::instance().selectInt(
               QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = :name;"),
               {{ QStringLiteral(":name"), indexName }}) == 1;
}

void TestDatabaseManager::openDatabaseOpensTemporarySqliteDatabase()
{
    QCOMPARE(DLDatabaseManager::instance().databasePath(), m_dbPath);
    QVERIFY(QFile::exists(m_dbPath));
}

void TestDatabaseManager::createTablesIfNeededCreatesRequiredTables()
{
    QVERIFY2(DLDatabaseManager::instance().createTablesIfNeeded(),
             qPrintable(DLDatabaseManager::instance().lastError()));

    const QStringList tables = {
        QStringLiteral("schema_version"),
        QStringLiteral("groups"),
        QStringLiteral("words"),
        QStringLiteral("word_review_stats"),
        QStringLiteral("noun_forms"),
        QStringLiteral("verb_forms"),
        QStringLiteral("adjective_forms"),
        QStringLiteral("sync_outbox_events"),
        QStringLiteral("sync_acknowledged_event_diagnostics"),
        QStringLiteral("sync_pull_cursor")
    };

    for (const QString& tableName : tables) {
        QVERIFY2(tableExists(tableName), qPrintable(QStringLiteral("Missing table: %1").arg(tableName)));
    }
}

void TestDatabaseManager::createIndexesIfNeededCreatesExpectedIndexes()
{
    QVERIFY2(DLDatabaseManager::instance().createIndexesIfNeeded(),
             qPrintable(DLDatabaseManager::instance().lastError()));

    const QStringList indexes = {
        QStringLiteral("idx_groups_sync_id"),
        QStringLiteral("idx_groups_deleted_at"),
        QStringLiteral("idx_words_sync_id"),
        QStringLiteral("idx_words_group_id"),
        QStringLiteral("idx_words_group_sync_id"),
        QStringLiteral("idx_words_normalized_german"),
        QStringLiteral("idx_words_part_of_speech"),
        QStringLiteral("idx_words_unique_active_translation"),
        QStringLiteral("idx_word_review_stats_word_sync_id"),
        QStringLiteral("idx_word_review_stats_due_at"),
        QStringLiteral("idx_sync_outbox_events_created_at"),
        QStringLiteral("idx_sync_outbox_events_next_attempt_after"),
        QStringLiteral("idx_sync_outbox_events_entity"),
        QStringLiteral("idx_sync_acknowledged_event_diagnostics_acknowledged_at"),
        QStringLiteral("idx_sync_acknowledged_event_diagnostics_entity")
    };

    for (const QString& indexName : indexes) {
        QVERIFY2(indexExists(indexName), qPrintable(QStringLiteral("Missing index: %1").arg(indexName)));
    }
}

void TestDatabaseManager::openDatabaseRecordsSqliteSchemaVersion()
{
    const QVariantMap row = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT major, minor FROM schema_version WHERE component = 'sqlite';"));
    QCOMPARE(row.value(QStringLiteral("major")).toInt(), 1);
    QCOMPARE(row.value(QStringLiteral("minor")).toInt(), 3);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("PRAGMA user_version;")), 1003);
}

void TestDatabaseManager::cleanDatabaseCreatesUuidIdentityColumns()
{
    QVERIFY(columnExists(QStringLiteral("groups"), QStringLiteral("sync_id")));
    QVERIFY(columnExists(QStringLiteral("groups"), QStringLiteral("deleted_at")));
    QVERIFY(columnExists(QStringLiteral("words"), QStringLiteral("sync_id")));
    QVERIFY(columnExists(QStringLiteral("words"), QStringLiteral("group_sync_id")));
    QVERIFY(columnExists(QStringLiteral("word_review_stats"), QStringLiteral("word_sync_id")));
}

void TestDatabaseManager::cleanDatabaseCreatesSyncOutboxSchema()
{
    const QStringList outboxColumns = {
        QStringLiteral("event_id"),
        QStringLiteral("contract_version"),
        QStringLiteral("device_id"),
        QStringLiteral("entity_type"),
        QStringLiteral("entity_id"),
        QStringLiteral("operation"),
        QStringLiteral("updated_at"),
        QStringLiteral("envelope_json"),
        QStringLiteral("payload_json"),
        QStringLiteral("created_at"),
        QStringLiteral("send_attempt_count"),
        QStringLiteral("last_attempted_at"),
        QStringLiteral("next_attempt_after"),
        QStringLiteral("last_error")
    };

    for (const QString& columnName : outboxColumns) {
        QVERIFY2(columnExists(QStringLiteral("sync_outbox_events"), columnName),
                 qPrintable(QStringLiteral("Missing sync_outbox_events.%1").arg(columnName)));
    }

    const QStringList diagnosticColumns = {
        QStringLiteral("event_id"),
        QStringLiteral("contract_version"),
        QStringLiteral("entity_type"),
        QStringLiteral("entity_id"),
        QStringLiteral("operation"),
        QStringLiteral("updated_at"),
        QStringLiteral("created_at"),
        QStringLiteral("acknowledged_at"),
        QStringLiteral("server_sequence"),
        QStringLiteral("canonical_result_json"),
        QStringLiteral("diagnostic_json")
    };

    for (const QString& columnName : diagnosticColumns) {
        QVERIFY2(columnExists(QStringLiteral("sync_acknowledged_event_diagnostics"), columnName),
                 qPrintable(QStringLiteral("Missing sync_acknowledged_event_diagnostics.%1").arg(columnName)));
    }
}

void TestDatabaseManager::cleanDatabaseCreatesSyncPullCursorSchema()
{
    const QStringList cursorColumns = {
        QStringLiteral("id"),
        QStringLiteral("consumed_sequence"),
        QStringLiteral("updated_at")
    };

    for (const QString& columnName : cursorColumns) {
        QVERIFY2(columnExists(QStringLiteral("sync_pull_cursor"), columnName),
                 qPrintable(QStringLiteral("Missing sync_pull_cursor.%1").arg(columnName)));
    }

    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(0));
}

void TestDatabaseManager::remoteCursorAdvancementPersistsAndDoesNotRegress()
{
    QVERIFY2(DLDatabaseManager::instance().advanceRemoteCursor(42),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(42));

    QVERIFY2(DLDatabaseManager::instance().advanceRemoteCursor(7),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(42));

    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(42));
}

void TestDatabaseManager::insertsPopulateUuidIdentities()
{
    const QString groupId = DLDatabaseManager::instance().insertGroup(QStringLiteral("Basics"), QStringLiteral("#112233"));
    QVERIFY(!groupId.isEmpty());

    const QString wordId = DLDatabaseManager::instance().insertWord(
        QStringLiteral("Haus"),
        QStringLiteral("das"),
        QStringLiteral("Nomen"),
        QStringLiteral("house"),
        QString(),
        QString(),
        groupId);
    QVERIFY(!wordId.isEmpty());

    const QVariantMap group = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT sync_id FROM groups WHERE sync_id = :id;"),
        {{ QStringLiteral(":id"), groupId }});
    const QVariantMap word = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT sync_id, group_sync_id FROM words WHERE sync_id = :id;"),
        {{ QStringLiteral(":id"), wordId }});
    const QVariantMap stats = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT word_sync_id FROM word_review_stats WHERE word_sync_id = :word_id;"),
        {{ QStringLiteral(":word_id"), wordId }});

    const QString groupSyncId = group.value(QStringLiteral("sync_id")).toString();
    const QString wordSyncId = word.value(QStringLiteral("sync_id")).toString();
    QVERIFY(!QUuid(groupSyncId).isNull());
    QVERIFY(!QUuid(wordSyncId).isNull());
    QCOMPARE(word.value(QStringLiteral("group_sync_id")).toString(), groupSyncId);
    QCOMPARE(stats.value(QStringLiteral("word_sync_id")).toString(), wordSyncId);
}

void TestDatabaseManager::closeDatabaseClosesSafely()
{
    DLDatabaseManager::instance().closeDatabase();
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY(DLDatabaseManager::instance().databasePath().isEmpty());
}

void TestDatabaseManager::lastErrorIsSetWhenOperationFails()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*Failed SQL operation:.*")));
    QVERIFY(!DLDatabaseManager::instance().executeSql(QStringLiteral("INSERT INTO missing_table(value) VALUES (1);")));
    QVERIFY2(!DLDatabaseManager::instance().lastError().isEmpty(), "lastError should describe the SQL failure");
}

void TestDatabaseManager::selectRowsReturnsEmptyListForEmptyValidResult()
{
    const QVariantList rows = DLDatabaseManager::instance().selectRows(
        QStringLiteral("SELECT id, name FROM groups WHERE name = :name;"),
        {{ QStringLiteral(":name"), QStringLiteral("Missing") }});

    QVERIFY(rows.isEmpty());
    QVERIFY2(DLDatabaseManager::instance().lastError().isEmpty(),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestDatabaseManager::namedBindingsAcceptColonlessKeys()
{
    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral("INSERT INTO groups (sync_id, name, color_hex, created_at, updated_at) VALUES (:sync_id, :name, :color_hex, :created_at, :updated_at);"),
                 {
                     { QStringLiteral("sync_id"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
                     { QStringLiteral("name"), QStringLiteral("Basics") },
                     { QStringLiteral("color_hex"), QStringLiteral("#112233") },
                     { QStringLiteral("created_at"), 10 },
                     { QStringLiteral("updated_at"), 10 }
                 }),
             qPrintable(DLDatabaseManager::instance().lastError()));

    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM groups WHERE name = :name;"),
                 {{ QStringLiteral("name"), QStringLiteral("Basics") }}),
             1);
}

void TestDatabaseManager::missingNamedBindingFailsClearly()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*Failed SQL operation:.*missingKeys.*:name.*")));
    QVERIFY(!DLDatabaseManager::instance().executeSql(
        QStringLiteral("INSERT INTO groups (name, color_hex, created_at, updated_at) VALUES (:name, '#112233', 10, 10);")));
    QVERIFY(DLDatabaseManager::instance().lastError().contains(QStringLiteral(":name")));
}

void TestDatabaseManager::invalidSelectSetsLastError()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*Failed SQL operation:.*selectRows.*")));
    const QVariantList rows = DLDatabaseManager::instance().selectRows(QStringLiteral("SELECT missing_column FROM groups;"));

    QVERIFY(rows.isEmpty());
    QVERIFY2(!DLDatabaseManager::instance().lastError().isEmpty(), "Invalid SELECT should set lastError");
}

void TestDatabaseManager::openDatabaseMigratesLegacySchema()
{
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);

    const QString connectionName = QStringLiteral("legacy_schema_seed_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(m_dbPath);
        QVERIFY(db.open());

        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(R"(
            CREATE TABLE groups (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                color_hex TEXT DEFAULT '#3366CC',
                created_at REAL NOT NULL
            );
        )")));
        QVERIFY(query.exec(QStringLiteral(R"(
            CREATE TABLE words (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                german_word TEXT NOT NULL,
                normalized_german_word TEXT NOT NULL,
                article TEXT,
                part_of_speech TEXT DEFAULT 'Andere',
                native_translation TEXT NOT NULL,
                normalized_native_translation TEXT NOT NULL,
                example_phrase_de TEXT,
                example_phrase_native TEXT,
                group_id INTEGER,
                notes TEXT,
                created_at REAL NOT NULL,
                updated_at REAL NOT NULL,
                deleted_at REAL
            );
        )")));
        QVERIFY(query.exec(QStringLiteral(R"(
            CREATE TABLE verb_forms (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                word_id INTEGER NOT NULL,
                form_key TEXT NOT NULL,
                form_value TEXT NOT NULL,
                source TEXT,
                created_at REAL NOT NULL,
                updated_at REAL NOT NULL,
                deleted_at REAL
            );
        )")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO groups (id, name, color_hex, created_at) VALUES (1, 'Basics', '#112233', 10);")));
        QVERIFY(query.exec(QStringLiteral(R"(
            INSERT INTO words
                (id, german_word, normalized_german_word, article, part_of_speech,
                 native_translation, normalized_native_translation, group_id, created_at, updated_at, deleted_at)
            VALUES
                (1, 'gehen', 'gehen', NULL, 'Verb', 'go', 'go', 1, 10, 10, NULL);
        )")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO verb_forms (word_id, form_key, form_value, created_at, updated_at, deleted_at) VALUES (1, 'praeteritum', 'ging', 10, 10, NULL);")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO verb_forms (word_id, form_key, form_value, created_at, updated_at, deleted_at) VALUES (1, 'partizip_ii', 'gegangen', 10, 10, NULL);")));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();

    const QVariantList groups = DLDatabaseManager::instance().fetchAllGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Basics"));
    QVERIFY(groups.first().toMap().value(QStringLiteral("updated_at")).toLongLong() > 0);

    const QVariantList words = DLDatabaseManager::instance().fetchAllWords(QStringLiteral("newest"));
    QCOMPARE(words.size(), 1);
    const QVariantMap word = words.first().toMap();
    QCOMPARE(word.value(QStringLiteral("german_word")).toString(), QStringLiteral("gehen"));
    QCOMPARE(word.value(QStringLiteral("praeteritum_form")).toString(), QStringLiteral("ging"));
    QCOMPARE(word.value(QStringLiteral("partizip_ii_form")).toString(), QStringLiteral("gegangen"));
    QVERIFY(tableExists(QStringLiteral("sync_outbox_events")));
    QVERIFY(tableExists(QStringLiteral("sync_acknowledged_event_diagnostics")));
    QVERIFY(tableExists(QStringLiteral("sync_pull_cursor")));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("PRAGMA user_version;")), 1003);
}

void TestDatabaseManager::deleteAllDataRemovesStoredData()
{
    const QString groupId = DLDatabaseManager::instance().insertGroup(QStringLiteral("Basics"), QStringLiteral("#112233"));
    QVERIFY(!groupId.isEmpty());

    const QString wordId = DLDatabaseManager::instance().insertWord(
        QStringLiteral("Haus"),
        QStringLiteral("das"),
        QStringLiteral("Nomen"),
        QStringLiteral("house"),
        QString(),
        QString(),
        groupId);
    QVERIFY(!wordId.isEmpty());

    const QString eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral(R"(
        INSERT INTO sync_outbox_events
            (event_id, contract_version, entity_type, entity_id, operation,
             updated_at, envelope_json, payload_json, created_at)
        VALUES
            (:event_id, '1.0', 'word', :entity_id, 'create',
             10, '{}', '{}', 10);
    )"), {
            { QStringLiteral(":event_id"), eventId },
            { QStringLiteral(":entity_id"), wordId }
        }),
        qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral(R"(
        INSERT INTO sync_acknowledged_event_diagnostics
            (event_id, contract_version, entity_type, entity_id, operation,
             updated_at, created_at, acknowledged_at)
        VALUES
            (:event_id, '1.0', 'word', :entity_id, 'create',
             10, 10, 11);
    )"), {
            { QStringLiteral(":event_id"), eventId },
            { QStringLiteral(":entity_id"), wordId }
        }),
        qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(DLDatabaseManager::instance().advanceRemoteCursor(42),
             qPrintable(DLDatabaseManager::instance().lastError()));

    QVERIFY2(DLDatabaseManager::instance().deleteAllData(),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().getGroupCount(), 0);
    QCOMPARE(DLDatabaseManager::instance().getWordCount(), 0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM word_review_stats;")), 0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_acknowledged_event_diagnostics;")), 0);
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(0));
}

QTEST_MAIN(TestDatabaseManager)

#include "TestDatabaseManager.moc"
