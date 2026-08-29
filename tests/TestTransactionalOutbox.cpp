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
    void sendAttemptKeepsActiveOutboxEventForRetry();
    void acknowledgementMovesEventToBoundedDiagnosticHistory();

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

void TestTransactionalOutbox::sendAttemptKeepsActiveOutboxEventForRetry()
{
    DLWord word;
    word.germanWord = QStringLiteral("Fenster");
    word.nativeTranslation = QStringLiteral("window");
    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!wordId.isEmpty());

    const QVariantMap initialOutbox = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT event_id FROM sync_outbox_events WHERE entity_id = :entity_id;"),
        {{ QStringLiteral(":entity_id"), wordId }});
    const QString eventId = initialOutbox.value(QStringLiteral("event_id")).toString();
    QVERIFY(!eventId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().recordSyncOutboxSendAttempt(eventId, 1234, QStringLiteral("timeout")),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QVariantMap retriableOutbox = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral(R"(
            SELECT event_id, send_attempt_count, last_attempted_at, next_attempt_after, last_error
            FROM sync_outbox_events
            WHERE event_id = :event_id;
        )"),
        {{ QStringLiteral(":event_id"), eventId }});
    QCOMPARE(retriableOutbox.value(QStringLiteral("event_id")).toString(), eventId);
    QCOMPARE(retriableOutbox.value(QStringLiteral("send_attempt_count")).toInt(), 1);
    QVERIFY(retriableOutbox.value(QStringLiteral("last_attempted_at")).toLongLong() > 0);
    QCOMPARE(retriableOutbox.value(QStringLiteral("next_attempt_after")).toLongLong(), 1234);
    QCOMPARE(retriableOutbox.value(QStringLiteral("last_error")).toString(), QStringLiteral("timeout"));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_acknowledged_event_diagnostics;")), 0);

    QVERIFY2(DLDatabaseManager::instance().recordSyncOutboxSendAttempt(eventId),
             qPrintable(DLDatabaseManager::instance().lastError()));
    retriableOutbox = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT event_id, send_attempt_count FROM sync_outbox_events WHERE event_id = :event_id;"),
        {{ QStringLiteral(":event_id"), eventId }});
    QCOMPARE(retriableOutbox.value(QStringLiteral("event_id")).toString(), eventId);
    QCOMPARE(retriableOutbox.value(QStringLiteral("send_attempt_count")).toInt(), 2);
}

void TestTransactionalOutbox::acknowledgementMovesEventToBoundedDiagnosticHistory()
{
    DLWord word;
    word.germanWord = QStringLiteral("Tisch");
    word.nativeTranslation = QStringLiteral("table");
    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!wordId.isEmpty());

    const QVariantMap initialOutbox = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT event_id FROM sync_outbox_events WHERE entity_id = :entity_id;"),
        {{ QStringLiteral(":entity_id"), wordId }});
    const QString eventId = initialOutbox.value(QStringLiteral("event_id")).toString();
    QVERIFY(!eventId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().recordSyncOutboxSendAttempt(eventId),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(DLDatabaseManager::instance().acknowledgeSyncOutboxEvent(
                 eventId,
                 42,
                 QStringLiteral("{\"accepted\":true}"),
                 QStringLiteral("{\"source\":\"test\"}")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events WHERE event_id = :event_id;"),
                 {{ QStringLiteral(":event_id"), eventId }}),
             0);

    QVariantMap diagnostic = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral(R"(
            SELECT event_id, entity_type, entity_id, operation, server_sequence, canonical_result_json, diagnostic_json
            FROM sync_acknowledged_event_diagnostics
            WHERE event_id = :event_id;
        )"),
        {{ QStringLiteral(":event_id"), eventId }});
    QCOMPARE(diagnostic.value(QStringLiteral("event_id")).toString(), eventId);
    QCOMPARE(diagnostic.value(QStringLiteral("entity_type")).toString(), QStringLiteral("word"));
    QCOMPARE(diagnostic.value(QStringLiteral("entity_id")).toString(), wordId);
    QCOMPARE(diagnostic.value(QStringLiteral("operation")).toString(), QStringLiteral("create"));
    QCOMPARE(diagnostic.value(QStringLiteral("server_sequence")).toLongLong(), 42);
    QCOMPARE(diagnostic.value(QStringLiteral("canonical_result_json")).toString(), QStringLiteral("{\"accepted\":true}"));
    QCOMPARE(diagnostic.value(QStringLiteral("diagnostic_json")).toString(), QStringLiteral("{\"source\":\"test\"}"));

    for (int i = 0; i < 2; ++i) {
        const QString oldEventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral(R"(
            INSERT INTO sync_acknowledged_event_diagnostics
                (event_id, contract_version, entity_type, entity_id, operation,
                 updated_at, created_at, acknowledged_at)
            VALUES
                (:event_id, '1.0', 'word', :entity_id, 'update',
                 10, 10, :acknowledged_at);
        )"), {
                { QStringLiteral(":event_id"), oldEventId },
                { QStringLiteral(":entity_id"), wordId },
                { QStringLiteral(":acknowledged_at"), i + 1 }
            }),
            qPrintable(DLDatabaseManager::instance().lastError()));
    }

    QVERIFY2(DLDatabaseManager::instance().purgeAcknowledgedSyncEventDiagnostics(1),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_acknowledged_event_diagnostics;")), 1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_acknowledged_event_diagnostics WHERE event_id = :event_id;"),
                 {{ QStringLiteral(":event_id"), eventId }}),
             1);
}

QTEST_MAIN(TestTransactionalOutbox)
#include "TestTransactionalOutbox.moc"
