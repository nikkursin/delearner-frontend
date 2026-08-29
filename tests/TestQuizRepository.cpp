#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QUuid>

#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Models/DLWord.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLQuizRepository.h"
#include "Repositories/DLWordRepository.h"

class TestQuizRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void getTranslationQuizWordCountReturnsCorrectCount();
    void fetchTranslationQuizWordsReturnsExpectedNumberOfWords();
    void fetchNounsReturnsOnlyArticleEligibleWords();
    void getNounCountReturnsCorrectCount();
    void nounQueriesAcceptCurrentNounLabels();
    void groupFilteringWorksForQuizQueries();
    void partOfSpeechFilteringWorks();

private:
    QString m_dbPath;

    static DLWord word(const QString& german,
                       const QString& native,
                       const QString& partOfSpeech,
                       const QString& article = QString(),
                       const QString& groupId = QString());
    void seedWords(const QString& groupA = QString(), const QString& groupB = QString());
};

void TestQuizRepository::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestQuizRepository::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

DLWord TestQuizRepository::word(const QString& german,
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

void TestQuizRepository::seedWords(const QString& groupA, const QString& groupB)
{
    DLWordRepository words(DLDatabaseManager::instance());
    QVERIFY(!words.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"), groupA)).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("Baum"), QStringLiteral("tree"), QStringLiteral("Nomen"), QStringLiteral("der"), groupA)).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("gehen"), QStringLiteral("go"), QStringLiteral("Verb"), QString(), groupB)).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("schoen"), QStringLiteral("beautiful"), QStringLiteral("Adjektiv"), QString(), groupB)).isEmpty());
}

void TestQuizRepository::getTranslationQuizWordCountReturnsCorrectCount()
{
    seedWords();
    DLQuizRepository repository(DLDatabaseManager::instance());

    QCOMPARE(repository.getTranslationQuizWordCount(), 4);
}

void TestQuizRepository::fetchTranslationQuizWordsReturnsExpectedNumberOfWords()
{
    seedWords();
    DLQuizRepository repository(DLDatabaseManager::instance());

    const QList<DLWord> words = repository.fetchTranslationQuizWords(3);
    QCOMPARE(words.size(), 3);
    for (const DLWord& word : words) {
        QVERIFY(!word.nativeTranslation.trimmed().isEmpty());
    }
}

void TestQuizRepository::fetchNounsReturnsOnlyArticleEligibleWords()
{
    seedWords();
    DLQuizRepository repository(DLDatabaseManager::instance());

    const QList<DLWord> nouns = repository.fetchNouns();
    QCOMPARE(nouns.size(), 2);
    for (const DLWord& noun : nouns) {
        QVERIFY(QSet<QString>({ QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("das") }).contains(noun.article));
    }
}

void TestQuizRepository::getNounCountReturnsCorrectCount()
{
    seedWords();
    DLQuizRepository repository(DLDatabaseManager::instance());

    QCOMPARE(repository.getNounCount(), 2);
}

void TestQuizRepository::nounQueriesAcceptCurrentNounLabels()
{
    DLWordRepository words(DLDatabaseManager::instance());
    QVERIFY(!words.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"))).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("Baum"), QStringLiteral("tree"), QStringLiteral("Substantiv"), QStringLiteral("der"))).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("Tuer"), QStringLiteral("door"), QStringLiteral("noun"), QStringLiteral("die"))).isEmpty());
    QVERIFY(!words.insertWord(word(QStringLiteral("gehen"), QStringLiteral("go"), QStringLiteral("Verb"))).isEmpty());

    DLQuizRepository repository(DLDatabaseManager::instance());
    QCOMPARE(repository.getNounCount(), 3);
    QCOMPARE(repository.fetchNouns().size(), 3);
}

void TestQuizRepository::groupFilteringWorksForQuizQueries()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup a;
    a.name = QStringLiteral("Basics");
    DLWordGroup b;
    b.name = QStringLiteral("Travel");
    const QString groupA = groups.insertGroup(a);
    const QString groupB = groups.insertGroup(b);
    seedWords(groupA, groupB);

    DLQuizRepository repository(DLDatabaseManager::instance());
    QCOMPARE(repository.getTranslationQuizWordCount(groupA), 2);
    QCOMPARE(repository.getNounCount(groupA), 2);
    QCOMPARE(repository.fetchTranslationQuizWords(10, groupB).size(), 2);
    QCOMPARE(repository.fetchNouns(groupB).size(), 0);
}

void TestQuizRepository::partOfSpeechFilteringWorks()
{
    seedWords();
    DLQuizRepository repository(DLDatabaseManager::instance());

    QCOMPARE(repository.getTranslationQuizWordCount(QString(), QStringLiteral("Nomen")), 2);
    QCOMPARE(repository.getTranslationQuizWordCount(QString(), QStringLiteral("Verb")), 1);

    const QList<DLWord> verbs = repository.fetchTranslationQuizWords(10, QString(), QStringLiteral("Verb"));
    QCOMPARE(verbs.size(), 1);
    QCOMPARE(verbs.first().partOfSpeech, QStringLiteral("Verb"));
}

QTEST_MAIN(TestQuizRepository)

#include "TestQuizRepository.moc"
