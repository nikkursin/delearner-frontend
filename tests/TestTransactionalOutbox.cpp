#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Models/DLWord.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLReviewStatsRepository.h"
#include "Repositories/DLWordRepository.h"

class TestTransactionalOutbox : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void wordInsertCreatesContractOutboxEvent();
    void domainMutationRollsBackWhenOutboxWriteFails();
    void outboxWriteRollsBackWhenTransactionFailsAfterEventInsert();
    void groupDeleteRecordsTombstoneAndAffectedWordUpdate();
    void reviewStatsIncrementCreatesOutboxEvent();

private:
    QString m_dbPath;
};

void TestTransactionalOutbox::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestTransactionalOutbox::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

void TestTransactionalOutbox::wordInsertCreatesContractOutboxEvent()
{
    DLWord word;
    word.germanWord = QStringLiteral("Haus");
    word.nativeTranslation = QStringLiteral("house");
    word.partOfSpeech = QStringLiteral("Nomen");
    word.article = QStringLiteral("das");

    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY2(!wordId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));

    const QVariantMap outbox = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT * FROM sync_outbox_events WHERE entity_id = :entity_id;"),
        {{ QStringLiteral(":entity_id"), wordId }});
    QCOMPARE(outbox.value(QStringLiteral("entity_type")).toString(), QStringLiteral("word"));
    QCOMPARE(outbox.value(QStringLiteral("operation")).toString(), QStringLiteral("create"));
    QCOMPARE(outbox.value(QStringLiteral("device_id")).toString(), DLTestSupport::testDeviceId());
    QVERIFY(!QUuid(outbox.value(QStringLiteral("event_id")).toString()).isNull());

    const QJsonObject envelope = QJsonDocument::fromJson(outbox.value(QStringLiteral("envelope_json")).toString().toUtf8()).object();
    QCOMPARE(envelope.value(QStringLiteral("deviceId")).toString(), DLTestSupport::testDeviceId());
    QCOMPARE(envelope.value(QStringLiteral("context")).toObject().value(QStringLiteral("authenticatedUserId")).toString(),
             DLTestSupport::testUserId());
    QCOMPARE(envelope.value(QStringLiteral("payload")).toObject().value(QStringLiteral("ownerUserId")).toString(),
             DLTestSupport::testUserId());
}

void TestTransactionalOutbox::domainMutationRollsBackWhenOutboxWriteFails()
{
    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral("DROP TABLE sync_outbox_events;")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = DLGroupRepository(DLDatabaseManager::instance()).insertGroup(group);

    QVERIFY(groupId.isEmpty());
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM groups;")), 0);
}

void TestTransactionalOutbox::outboxWriteRollsBackWhenTransactionFailsAfterEventInsert()
{
    DLDatabaseManager::instance().failAfterNextSyncOutboxWriteForTesting();

    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = DLGroupRepository(DLDatabaseManager::instance()).insertGroup(group);

    QVERIFY(groupId.isEmpty());
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM groups;")), 0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
}

void TestTransactionalOutbox::groupDeleteRecordsTombstoneAndAffectedWordUpdate()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = groups.insertGroup(group);
    QVERIFY(!groupId.isEmpty());

    DLWord word;
    word.germanWord = QStringLiteral("Baum");
    word.nativeTranslation = QStringLiteral("tree");
    word.groupSyncId = groupId;
    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!wordId.isEmpty());

    QVERIFY2(groups.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events WHERE entity_type = 'group' AND entity_id = :id AND operation = 'delete';"),
                 {{ QStringLiteral(":id"), groupId }}),
             1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events WHERE entity_type = 'word' AND entity_id = :id AND operation = 'update';"),
                 {{ QStringLiteral(":id"), wordId }}),
             1);
}

void TestTransactionalOutbox::reviewStatsIncrementCreatesOutboxEvent()
{
    DLWord word;
    word.germanWord = QStringLiteral("gehen");
    word.nativeTranslation = QStringLiteral("go");
    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!wordId.isEmpty());

    QVERIFY2(DLReviewStatsRepository(DLDatabaseManager::instance()).incrementCorrectAnswer(wordId),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events WHERE entity_type = 'word_review_stats' AND entity_id = :id;"),
                 {{ QStringLiteral(":id"), wordId }}),
             1);
}

QTEST_MAIN(TestTransactionalOutbox)
#include "TestTransactionalOutbox.moc"
