#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QUuid>

#include "Managers/DLDatabaseManager.h"
#include "Managers/DLGroupRepository.h"
#include "Managers/DLQuizRepository.h"
#include "Managers/DLWordRepository.h"
#include "Models/DLWord.h"
#include "Models/DLWordGroup.h"

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
    void groupFilteringWorksForQuizQueries();
    void partOfSpeechFilteringWorks();

private:
    QString m_dbPath;

    static DLWord word(const QString& german,
                       const QString& native,
                       const QString& partOfSpeech,
                       const QString& article = QString(),
                       int groupId = -1);
    void seedWords(int groupA = -1, int groupB = -1);
};

void TestQuizRepository::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestQuizRepository::cleanup()
{
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

DLWord TestQuizRepository::word(const QString& german,
                                const QString& native,
                                const QString& partOfSpeech,
                                const QString& article,
                                int groupId)
{
    DLWord value;
    value.germanWord = german;
    value.nativeTranslation = native;
    value.partOfSpeech = partOfSpeech;
    value.article = article;
    value.groupId = groupId;
    return value;
}

void TestQuizRepository::seedWords(int groupA, int groupB)
{
    DLWordRepository words(DLDatabaseManager::instance());
    QVERIFY(words.insertWord(word(QStringLiteral("Haus"), QStringLiteral("house"), QStringLiteral("Nomen"), QStringLiteral("das"), groupA)) > 0);
    QVERIFY(words.insertWord(word(QStringLiteral("Baum"), QStringLiteral("tree"), QStringLiteral("Nomen"), QStringLiteral("der"), groupA)) > 0);
    QVERIFY(words.insertWord(word(QStringLiteral("gehen"), QStringLiteral("go"), QStringLiteral("Verb"), QString(), groupB)) > 0);
    QVERIFY(words.insertWord(word(QStringLiteral("schoen"), QStringLiteral("beautiful"), QStringLiteral("Adjektiv"), QString(), groupB)) > 0);
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

void TestQuizRepository::groupFilteringWorksForQuizQueries()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup a;
    a.name = QStringLiteral("Basics");
    DLWordGroup b;
    b.name = QStringLiteral("Travel");
    const int groupA = groups.insertGroup(a);
    const int groupB = groups.insertGroup(b);
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

    QCOMPARE(repository.getTranslationQuizWordCount(-1, QStringLiteral("Nomen")), 2);
    QCOMPARE(repository.getTranslationQuizWordCount(-1, QStringLiteral("Verb")), 1);

    const QList<DLWord> verbs = repository.fetchTranslationQuizWords(10, -1, QStringLiteral("Verb"));
    QCOMPARE(verbs.size(), 1);
    QCOMPARE(verbs.first().partOfSpeech, QStringLiteral("Verb"));
}

QTEST_MAIN(TestQuizRepository)

#include "TestQuizRepository.moc"
