#include <QtTest>

#include <QDir>
#include <QFile>
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
    void deleteAllDataRemovesStoredData();

private:
    QString m_dbPath;

    bool tableExists(const QString& tableName);
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
        QStringLiteral("adjective_forms")
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
        QStringLiteral("idx_words_group_id"),
        QStringLiteral("idx_words_normalized_german"),
        QStringLiteral("idx_words_part_of_speech"),
        QStringLiteral("idx_words_unique_active_translation"),
        QStringLiteral("idx_word_review_stats_due_at")
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
    QVERIFY(!DLDatabaseManager::instance().executeSql(QStringLiteral("INSERT INTO missing_table(value) VALUES (1);")));
    QVERIFY2(!DLDatabaseManager::instance().lastError().isEmpty(), "lastError should describe the SQL failure");
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
