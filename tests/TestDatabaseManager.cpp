#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

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
}

void TestDatabaseManager::cleanup()
{
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
        QStringLiteral("groups"),
        QStringLiteral("words"),
        QStringLiteral("word_review_stats"),
        QStringLiteral("noun_forms"),
        QStringLiteral("verb_forms"),
        QStringLiteral("adjective_forms"),
        QStringLiteral("app_settings"),
        QStringLiteral("sync_state"),
        QStringLiteral("device_identity"),
        QStringLiteral("outbound_sync_queue")
    };

    for (const QString& tableName : tables) {
        QVERIFY2(tableExists(tableName), qPrintable(QStringLiteral("Missing table: %1").arg(tableName)));
    }

    const QStringList syncColumns = {
        QStringLiteral("sync_id"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
        QStringLiteral("deleted_at"),
        QStringLiteral("server_updated_at"),
        QStringLiteral("server_version"),
        QStringLiteral("device_id"),
        QStringLiteral("dirty")
    };

    for (const QString& columnName : syncColumns) {
        QVERIFY2(columnExists(QStringLiteral("words"), columnName),
                 qPrintable(QStringLiteral("Missing words.%1").arg(columnName)));
        QVERIFY2(columnExists(QStringLiteral("groups"), columnName),
                 qPrintable(QStringLiteral("Missing groups.%1").arg(columnName)));
    }

    QVERIFY(columnExists(QStringLiteral("words"), QStringLiteral("plural_form")));
    QVERIFY(columnExists(QStringLiteral("words"), QStringLiteral("notes")));

    for (const QString& columnName : syncColumns) {
        QVERIFY2(columnExists(QStringLiteral("word_review_stats"), columnName),
                 qPrintable(QStringLiteral("Missing word_review_stats.%1").arg(columnName)));
    }
}

void TestDatabaseManager::createIndexesIfNeededCreatesExpectedIndexes()
{
    QVERIFY2(DLDatabaseManager::instance().createIndexesIfNeeded(),
             qPrintable(DLDatabaseManager::instance().lastError()));

    const QStringList indexes = {
        QStringLiteral("idx_words_group_id"),
        QStringLiteral("idx_words_sync_id"),
        QStringLiteral("idx_words_dirty"),
        QStringLiteral("idx_words_deleted_at"),
        QStringLiteral("idx_words_server_updated_at"),
        QStringLiteral("idx_words_normalized_german"),
        QStringLiteral("idx_words_part_of_speech"),
        QStringLiteral("idx_words_unique_active_translation"),
        QStringLiteral("idx_groups_sync_id"),
        QStringLiteral("idx_groups_dirty"),
        QStringLiteral("idx_groups_deleted_at"),
        QStringLiteral("idx_groups_server_updated_at"),
        QStringLiteral("idx_word_review_stats_sync_id"),
        QStringLiteral("idx_word_review_stats_due_at"),
        QStringLiteral("idx_word_review_stats_dirty"),
        QStringLiteral("idx_word_review_stats_deleted_at"),
        QStringLiteral("idx_word_review_stats_server_updated_at"),
        QStringLiteral("idx_app_settings_dirty"),
        QStringLiteral("idx_app_settings_deleted_at"),
        QStringLiteral("idx_app_settings_server_updated_at"),
        QStringLiteral("idx_sync_state_scope"),
        QStringLiteral("idx_device_identity_device_id"),
        QStringLiteral("idx_outbound_sync_queue_record"),
        QStringLiteral("idx_outbound_sync_queue_created_at")
    };

    for (const QString& indexName : indexes) {
        QVERIFY2(indexExists(indexName), qPrintable(QStringLiteral("Missing index: %1").arg(indexName)));
    }
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
                 QStringLiteral("INSERT INTO groups (name, color_hex, created_at, updated_at) VALUES (:name, :color_hex, :created_at, :updated_at);"),
                 {
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

    const QVariantList groups = DLDatabaseManager::instance().fetchAllGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Basics"));
    QVERIFY(groups.first().toMap().value(QStringLiteral("updated_at")).toLongLong() > 0);

    const QVariantList words = DLDatabaseManager::instance().fetchAllWords(QStringLiteral("newest"), -1);
    QCOMPARE(words.size(), 1);
    const QVariantMap word = words.first().toMap();
    QCOMPARE(word.value(QStringLiteral("german_word")).toString(), QStringLiteral("gehen"));
    QCOMPARE(word.value(QStringLiteral("praeteritum_form")).toString(), QStringLiteral("ging"));
    QCOMPARE(word.value(QStringLiteral("partizip_ii_form")).toString(), QStringLiteral("gegangen"));
}

void TestDatabaseManager::deleteAllDataRemovesStoredData()
{
    const int groupId = DLDatabaseManager::instance().insertGroup(QStringLiteral("Basics"), QStringLiteral("#112233"));
    QVERIFY(groupId > 0);

    const int wordId = DLDatabaseManager::instance().insertWord(
        QStringLiteral("Haus"),
        QStringLiteral("das"),
        QStringLiteral("Nomen"),
        QStringLiteral("house"),
        QString(),
        QString(),
        groupId);
    QVERIFY(wordId > 0);

    QVERIFY2(DLDatabaseManager::instance().deleteAllData(),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().getGroupCount(), 0);
    QCOMPARE(DLDatabaseManager::instance().getWordCount(), 0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM word_review_stats;")), 0);
}

QTEST_MAIN(TestDatabaseManager)

#include "TestDatabaseManager.moc"
