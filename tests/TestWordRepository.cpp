#include <QtTest>

#include <QDir>
#include <QFile>
#include <QUuid>

#include "Managers/DLDatabaseManager.h"
#include "Models/DLWord.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLWordRepository.h"

class TestWordRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void insertWordReturnsValidId();
    void fetchWordByIdReturnsCorrectData();
    void updateWordChangesCoreFields();
    void deleteWordRemovesWord();
    void duplicateDetectionUsesNormalizedWords();
    void duplicateDetectionAllowsDifferentTranslations();
    void wordExistsDoesNotReportDuplicateOnQueryFailure();
    void fetchAllWordsReturnsInsertedUngroupedWord();
    void fetchAllWordsExcludesSoftDeletedRows();
    void searchFindsGermanWordAndNativeTranslation_data();
    void searchFindsGermanWordAndNativeTranslation();
    void groupFilteringWorks();
    void sortModesWork_data();
    void sortModesWork();
    void insertingWordWithGroupStoresRelation();
    void deletingGroupSetsRelatedWordGroupIdToNull();

private:
    QString m_dbPath;

    static DLWord word(const QString& german,
                       const QString& native,
                       const QString& partOfSpeech = QStringLiteral("Andere"),
                       const QString& article = QString(),
                       const QString& groupId = QString());
};

void TestWordRepository::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestWordRepository::cleanup()
{
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

DLWord TestWordRepository::word(const QString& german,
                                const QString& native,
                                const QString& partOfSpeech,
                                const QString& article,
                                const QString& groupId)
{
    DLWord value;
    value.germanWord = german;
    value.nativeTranslation = native;
    value.partOfSpeech = partOfSpeech;
    value.article = article;
    value.groupSyncId = groupId;
    return value;
}

void TestWordRepository::insertWordReturnsValidId()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    const QString wordId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das")));
    QVERIFY2(!wordId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY(!QUuid(wordId).isNull());
}

void TestWordRepository::fetchWordByIdReturnsCorrectData()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    DLWord original = word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"));
    original.nounForms.pluralForm = QStringLiteral("Haeuser");

    const QString wordId = repository.insertWord(original);
    const DLWord fetched = repository.fetchWordById(wordId);

    QCOMPARE(fetched.syncId, wordId);
    QCOMPARE(fetched.germanWord, original.germanWord);
    QCOMPARE(fetched.nativeTranslation, original.nativeTranslation);
    QCOMPARE(fetched.partOfSpeech, original.partOfSpeech);
    QCOMPARE(fetched.article, original.article);
    QCOMPARE(fetched.nounForms.pluralForm, original.nounForms.pluralForm);
}

void TestWordRepository::updateWordChangesCoreFields()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    const QString wordId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house")));

    DLWord update = word(QStringLiteral("Baum"), QStringLiteral("tree"), QStringLiteral("Nomen"), QStringLiteral("der"));
    update.syncId = wordId;
    update.nounForms.pluralForm = QStringLiteral("Baeume");

    QVERIFY2(repository.updateWord(update), qPrintable(DLDatabaseManager::instance().lastError()));
    const DLWord fetched = repository.fetchWordById(wordId);
    QCOMPARE(fetched.germanWord, update.germanWord);
    QCOMPARE(fetched.nativeTranslation, update.nativeTranslation);
    QCOMPARE(fetched.partOfSpeech, update.partOfSpeech);
    QCOMPARE(fetched.article, update.article);
    QCOMPARE(fetched.nounForms.pluralForm, update.nounForms.pluralForm);
}

void TestWordRepository::deleteWordRemovesWord()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    const QString wordId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house")));

    QVERIFY2(repository.deleteWord(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchWordById(wordId).id, -1);
    QCOMPARE(repository.getWordCount(), 0);
}

void TestWordRepository::duplicateDetectionUsesNormalizedWords()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    QVERIFY(!repository.insertWord(word(QStringLiteral(" Haus "), QStringLiteral(" House "))).isEmpty());

    QVERIFY(repository.wordExists(QStringLiteral("haus"), QStringLiteral("house")));
    QVERIFY(repository.insertWord(word(QStringLiteral("haus"), QStringLiteral("house"))).isEmpty());
    QVERIFY(DLDatabaseManager::instance().lastError().contains(QStringLiteral("Duplicate")));
}

void TestWordRepository::duplicateDetectionAllowsDifferentTranslations()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Bank"), QStringLiteral("bench"))).isEmpty());

    QVERIFY(!repository.wordExists(QStringLiteral("Bank"), QStringLiteral("bank")));
    QVERIFY2(!repository.insertWord(word(QStringLiteral("Bank"), QStringLiteral("bank"))).isEmpty(),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestWordRepository::wordExistsDoesNotReportDuplicateOnQueryFailure()
{
    DLDatabaseManager::instance().closeDatabase();
    DLWordRepository repository(DLDatabaseManager::instance());

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*Failed SQL operation:.*")));
    QVERIFY(!repository.wordExists(QStringLiteral("Haus"), QStringLiteral("house")));
}

void TestWordRepository::fetchAllWordsReturnsInsertedUngroupedWord()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"))).isEmpty());

    const QList<DLWord> words = repository.fetchAllWords(QStringLiteral("newest"));
    QCOMPARE(words.size(), 1);
    QCOMPARE(words.first().germanWord, QStringLiteral("Haus"));
    QCOMPARE(words.first().groupId, -1);
}

void TestWordRepository::fetchAllWordsExcludesSoftDeletedRows()
{
    DLWordRepository repository(DLDatabaseManager::instance());
    const QString activeId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house")));
    const QString deletedId = repository.insertWord(word(QStringLiteral("Baum"), QStringLiteral("tree")));
    QVERIFY(!activeId.isEmpty());
    QVERIFY(!deletedId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral("UPDATE words SET deleted_at = :deleted_at WHERE sync_id = :id;"),
                 {{ QStringLiteral(":deleted_at"), 10 }, { QStringLiteral(":id"), deletedId }}),
             qPrintable(DLDatabaseManager::instance().lastError()));

    const QList<DLWord> words = repository.fetchAllWords(QStringLiteral("newest"));
    QCOMPARE(words.size(), 1);
    QCOMPARE(words.first().syncId, activeId);
}

void TestWordRepository::searchFindsGermanWordAndNativeTranslation_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("expectedGermanWord");

    QTest::newRow("german") << QStringLiteral("Hau") << QStringLiteral("Haus");
    QTest::newRow("translation") << QStringLiteral("tree") << QStringLiteral("Baum");
}

void TestWordRepository::searchFindsGermanWordAndNativeTranslation()
{
    QFETCH(QString, query);
    QFETCH(QString, expectedGermanWord);

    DLWordRepository repository(DLDatabaseManager::instance());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"))).isEmpty());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Baum"), QStringLiteral("tree"))).isEmpty());

    const QList<DLWord> words = repository.searchWords(query);
    QCOMPARE(words.size(), 1);
    QCOMPARE(words.first().germanWord, expectedGermanWord);
}

void TestWordRepository::groupFilteringWorks()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup basics;
    basics.name = QStringLiteral("Basics");
    DLWordGroup travel;
    travel.name = QStringLiteral("Travel");
    const QString basicsId = groups.insertGroup(basics);
    const QString travelId = groups.insertGroup(travel);

    DLWordRepository repository(DLDatabaseManager::instance());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"), basicsId)).isEmpty());
    QVERIFY(!repository.insertWord(word(QStringLiteral("Zug"), QStringLiteral("train"), QStringLiteral("Nomen"), QStringLiteral("der"), travelId)).isEmpty());

    const QList<DLWord> basicsWords = repository.fetchAllWords(QStringLiteral("newest"), basicsId);
    QCOMPARE(basicsWords.size(), 1);
    QCOMPARE(basicsWords.first().germanWord, QStringLiteral("Haus"));
}

void TestWordRepository::sortModesWork_data()
{
    QTest::addColumn<QString>("sortMode");
    QTest::addColumn<QString>("firstGermanWord");

    QTest::newRow("newest") << QStringLiteral("newest") << QStringLiteral("C Wort");
    QTest::newRow("oldest") << QStringLiteral("oldest") << QStringLiteral("B Wort");
    QTest::newRow("alphabetical") << QStringLiteral("az") << QStringLiteral("A Wort");
    QTest::newRow("reverse alphabetical") << QStringLiteral("za") << QStringLiteral("C Wort");
}

void TestWordRepository::sortModesWork()
{
    QFETCH(QString, sortMode);
    QFETCH(QString, firstGermanWord);

    DLWordRepository repository(DLDatabaseManager::instance());
    const QString bId = repository.insertWord(word(QStringLiteral("B Wort"), QStringLiteral("b")));
    const QString aId = repository.insertWord(word(QStringLiteral("A Wort"), QStringLiteral("a")));
    const QString cId = repository.insertWord(word(QStringLiteral("C Wort"), QStringLiteral("c")));
    QVERIFY(!bId.isEmpty());
    QVERIFY(!aId.isEmpty());
    QVERIFY(!cId.isEmpty());

    QVERIFY(DLDatabaseManager::instance().executeSql(QStringLiteral("UPDATE words SET created_at = :created_at WHERE sync_id = :id;"),
                                                     {{ QStringLiteral(":created_at"), 10 }, { QStringLiteral(":id"), bId }}));
    QVERIFY(DLDatabaseManager::instance().executeSql(QStringLiteral("UPDATE words SET created_at = :created_at WHERE sync_id = :id;"),
                                                     {{ QStringLiteral(":created_at"), 20 }, { QStringLiteral(":id"), aId }}));
    QVERIFY(DLDatabaseManager::instance().executeSql(QStringLiteral("UPDATE words SET created_at = :created_at WHERE sync_id = :id;"),
                                                     {{ QStringLiteral(":created_at"), 30 }, { QStringLiteral(":id"), cId }}));

    const QList<DLWord> words = repository.fetchAllWords(sortMode);
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.first().germanWord, firstGermanWord);
}

void TestWordRepository::insertingWordWithGroupStoresRelation()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = groups.insertGroup(group);

    DLWordRepository repository(DLDatabaseManager::instance());
    const QString wordId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"), groupId));

    QCOMPARE(repository.fetchWordById(wordId).groupSyncId, groupId);
}

void TestWordRepository::deletingGroupSetsRelatedWordGroupIdToNull()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = groups.insertGroup(group);

    DLWordRepository repository(DLDatabaseManager::instance());
    const QString wordId = repository.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"), groupId));

    QVERIFY2(groups.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY(repository.fetchWordById(wordId).groupSyncId.isEmpty());
}

QTEST_MAIN(TestWordRepository)

#include "TestWordRepository.moc"
