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
    void outboundQueueTracksReviewStatsChanges();
    void deletingWordRetainsReviewStatsForSync();

private:
    QString m_dbPath;

    QString insertWord();
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

QString TestReviewStatsRepository::insertWord()
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
    const QString wordId = insertWord();
    QVERIFY(!wordId.isEmpty());

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
    const QString wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementCorrectAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchStats(wordId).correctAnswers, 1);
}

void TestReviewStatsRepository::incrementWrongAnswerIncrementsWrongCount()
{
    const QString wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementWrongAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchStats(wordId).wrongAnswers, 1);
}

void TestReviewStatsRepository::lastReviewedAtIsUpdatedAfterAnswerIncrement()
{
    const QString wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementCorrectAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    const DLWordReviewStats stats = repository.fetchStats(wordId);
    QVERIFY(stats.lastReviewedAt.isValid());
    QVERIFY(!stats.lastReviewedAt.isNull());
    QVERIFY(stats.lastReviewedAt.toLongLong() > 0);
}

void TestReviewStatsRepository::outboundQueueTracksReviewStatsChanges()
{
    const QString wordId = insertWord();
    DLReviewStatsRepository repository(DLDatabaseManager::instance());

    QVERIFY2(repository.incrementCorrectAnswer(wordId), qPrintable(DLDatabaseManager::instance().lastError()));

    const QVariantList rows = DLDatabaseManager::instance().selectRows(QStringLiteral(R"(
        SELECT entity_type, entity_id, operation, payload_json
        FROM outbound_sync_queue
        WHERE entity_type = 'word_review_stats'
        ORDER BY rowid;
    )"));
    QCOMPARE(rows.size(), 1);

    const DLWordReviewStats stats = repository.fetchStats(wordId);
    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value(QStringLiteral("entity_id")).toString(), stats.id);
    QCOMPARE(row.value(QStringLiteral("operation")).toString(), QStringLiteral("update"));
    QVERIFY(row.value(QStringLiteral("payload_json")).toString().contains(QStringLiteral("correct_answers"))
            || row.value(QStringLiteral("payload_json")).toString().contains(QStringLiteral("incrementedColumn")));
}

void TestReviewStatsRepository::deletingWordRetainsReviewStatsForSync()
{
    const QString wordId = insertWord();
    DLReviewStatsRepository stats(DLDatabaseManager::instance());
    DLWordRepository words(DLDatabaseManager::instance());

    QVERIFY(stats.incrementWrongAnswer(wordId));
    QVERIFY2(words.deleteWord(wordId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral(R"(
                     SELECT COUNT(*)
                     FROM word_review_stats rs
                     INNER JOIN words w ON w.id = rs.word_id
                     WHERE w.sync_id = :word_id;
                 )"),
                 {{ QStringLiteral(":word_id"), wordId }}),
             1);
}

QTEST_MAIN(TestReviewStatsRepository)

#include "TestReviewStatsRepository.moc"
