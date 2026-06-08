#include <QtTest>

#include <QDir>
#include <QFile>
#include <QUuid>

#include "Managers/DLDatabaseManager.h"
#include "Models/DLWord.h"
#include "Models/DLWordReviewStats.h"
#include "Repositories/DLReviewStatsRepository.h"
#include "Repositories/DLWordRepository.h"

class TestReviewStatsRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void statsRowIsCreatedOrUpsertedForWord();
    void incrementCorrectAnswerIncrementsCorrectCount();
    void incrementWrongAnswerIncrementsWrongCount();
    void lastReviewedAtIsUpdatedAfterAnswerIncrement();
    void deletingWordDeletesReviewStats();

private:
    QString m_dbPath;

    int insertWord();
};

void TestReviewStatsRepository::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestReviewStatsRepository::cleanup()
{
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

int TestReviewStatsRepository::insertWord()
{
    DLWord word;
    word.germanWord = QStringLiteral("Haus");
    word.nativeTranslation = QStringLiteral("house");
    word.partOfSpeech = QStringLiteral("Nomen");
    word.article = QStringLiteral("das");
    return DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
}

void TestReviewStatsRepository::statsRowIsCreatedOrUpsertedForWord()
{
    const int wordId = insertWord();
    QVERIFY(wordId > 0);

    DLReviewStatsRepository repository(DLDatabaseManager::instance());
    QCOMPARE(repository.fetchStats(wordId).wordId, wordId);

    DLWordReviewStats stats;
    stats.wordId = wordId;
    stats.correctAnswers = 4;
    stats.wrongAnswers = 2;
    stats.easeFactor = 2.7;
    stats.intervalDays = 3;

    QVERIFY2(repository.upsertStats(stats), qPrintable(DLDatabaseManager::instance().lastError()));
    const DLWordReviewStats fetched = repository.fetchStats(wordId);
    QCOMPARE(fetched.correctAnswers, 4);
    QCOMPARE(fetched.wrongAnswers, 2);
    QCOMPARE(fetched.intervalDays, 3);
    QCOMPARE(fetched.easeFactor, 2.7);
}

void TestReviewStatsRepository::incrementCorrectAnswerIncrementsCorrectCount()
{
    const int wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementCorrectAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchStats(wordId).correctAnswers, 1);
}

void TestReviewStatsRepository::incrementWrongAnswerIncrementsWrongCount()
{
    const int wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementWrongAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchStats(wordId).wrongAnswers, 1);
}

void TestReviewStatsRepository::lastReviewedAtIsUpdatedAfterAnswerIncrement()
{
    const int wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementCorrectAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    const DLWordReviewStats stats = repository.fetchStats(wordId);
    QVERIFY(stats.lastReviewedAt.isValid());
    QVERIFY(!stats.lastReviewedAt.isNull());
    QVERIFY(stats.lastReviewedAt.toLongLong() > 0);
}

void TestReviewStatsRepository::deletingWordDeletesReviewStats()
{
    const int wordId = insertWord();
    DLReviewStatsRepository stats(DLDatabaseManager::instance());
    DLWordRepository words(DLDatabaseManager::instance());

    QVERIFY(stats.incrementWrongAnswer(wordId));
    QVERIFY2(words.deleteWord(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM word_review_stats WHERE word_id = :word_id;"),
                 {{ QStringLiteral(":word_id"), wordId }}),
             0);
}

QTEST_MAIN(TestReviewStatsRepository)

#include "TestReviewStatsRepository.moc"
