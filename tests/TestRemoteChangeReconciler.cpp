#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QUuid>

#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLReviewStatsRepository.h"
#include "Repositories/DLWordRepository.h"
#include "Sync/DLRemoteChangeReconciler.h"

class TestRemoteChangeReconciler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void remoteGroupAndWordPayloadsApplyWithoutOutboxFeedback();
    void remoteGroupDeleteTombstonesGroupAndUnlinksWordsWithoutOutboxFeedback();
    void twoDeviceWordGroupCrudAndMembershipReconcilesBothDirections();
    void remoteReviewStatsPayloadReconcilesWithoutOutboxFeedback();
    void cursorAdvancesOnlyAfterWholeRemoteBatchApplies();

private:
    QString m_dbPath;
};

namespace {
qint64 isoSeconds(const QString& text)
{
    return QDateTime::fromString(text, Qt::ISODate).toUTC().toSecsSinceEpoch();
}

DLSyncEventEnvelope baseEvent(const QString& entityType,
                              const QString& entityId,
                              const QString& operation,
                              const QString& updatedAt,
                              const QString& deviceId = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4"))
{
    DLSyncEventEnvelope event;
    event.contractVersion = DLSyncEventSerializer::contractVersion();
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.deviceId = deviceId;
    event.entityType = entityType;
    event.entityId = entityId;
    event.operation = operation;
    event.updatedAt = updatedAt;
    event.authenticatedUserId = DLTestSupport::testUserId();
    return event;
}

DLSyncEventEnvelope groupPayloadEvent(const QString& groupId,
                                      const QString& name,
                                      const QString& colorHex,
                                      const QString& updatedAt,
                                      const QString& operation = QStringLiteral("update"),
                                      const QString& deviceId = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4"))
{
    DLSyncEventEnvelope event = baseEvent(QStringLiteral("group"), groupId, operation, updatedAt, deviceId);
    event.payload = {
        { QStringLiteral("id"), groupId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("name"), name },
        { QStringLiteral("color_hex"), colorHex },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:30:00Z") },
        { QStringLiteral("updated_at"), updatedAt }
    };
    return event;
}

DLSyncEventEnvelope wordPayloadEvent(const QString& wordId,
                                     const QString& groupId,
                                     const QString& nativeTranslation,
                                     const QString& updatedAt,
                                     const QString& operation = QStringLiteral("update"),
                                     const QString& deviceId = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4"))
{
    DLSyncEventEnvelope event = baseEvent(QStringLiteral("word"), wordId, operation, updatedAt, deviceId);
    event.payload = {
        { QStringLiteral("id"), wordId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("german_word"), QStringLiteral("die Fahrkarte") },
        { QStringLiteral("normalized_german_word"), QStringLiteral("fahrkarte") },
        { QStringLiteral("article"), QStringLiteral("die") },
        { QStringLiteral("part_of_speech"), QStringLiteral("Nomen") },
        { QStringLiteral("native_translation"), nativeTranslation },
        { QStringLiteral("normalized_native_translation"), nativeTranslation.toLower() },
        { QStringLiteral("example_phrase_de"), QStringLiteral("Die Fahrkarte gilt heute bis Mitternacht.") },
        { QStringLiteral("example_phrase_native"), QStringLiteral("The ticket is valid until midnight today.") },
        { QStringLiteral("group_id"), groupId },
        { QStringLiteral("notes"), QStringLiteral("Pulled canonical value.") },
        { QStringLiteral("plural_form"), QStringLiteral("die Fahrkarten") },
        { QStringLiteral("praeteritum_form"), QVariant() },
        { QStringLiteral("partizip_ii_form"), QVariant() },
        { QStringLiteral("positive_form"), QVariant() },
        { QStringLiteral("comparative_form"), QVariant() },
        { QStringLiteral("superlative_form"), QVariant() },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:35:00Z") },
        { QStringLiteral("updated_at"), updatedAt }
    };
    return event;
}

DLSyncEventEnvelope tombstoneEvent(const QString& entityType,
                                   const QString& entityId,
                                   const QString& deletedAt,
                                   const QString& deviceId = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4"))
{
    DLSyncEventEnvelope event = baseEvent(entityType, entityId, QStringLiteral("delete"), deletedAt, deviceId);
    event.tombstone = DLSyncEventSerializer::tombstoneForEntity(
        entityType,
        entityId,
        DLTestSupport::testUserId(),
        deletedAt);
    return event;
}
}

void TestRemoteChangeReconciler::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestRemoteChangeReconciler::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

void TestRemoteChangeReconciler::remoteGroupAndWordPayloadsApplyWithoutOutboxFeedback()
{
    const QString groupId = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
    const QString wordId = QStringLiteral("471e206c-9c4f-4212-ae90-e90a2ee04ce4");
    const QString updatedAt = QStringLiteral("2026-01-16T10:15:00Z");

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(reconciler.applyRemoteEvent(groupPayloadEvent(groupId, QStringLiteral("Travel"), QStringLiteral("#2F80ED"), updatedAt), &error),
             qPrintable(error));
    QVERIFY2(reconciler.applyRemoteEvent(wordPayloadEvent(wordId, groupId, QStringLiteral("train ticket"), updatedAt), &error),
             qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    QCOMPARE(DLDatabaseManager::instance().fetchAllGroups().size(), 1);

    const QVariantMap word = DLDatabaseManager::instance().fetchWordById(wordId);
    QCOMPARE(word.value(QStringLiteral("native_translation")).toString(), QStringLiteral("train ticket"));
    QCOMPARE(word.value(QStringLiteral("group_id")).toString(), groupId);
    QCOMPARE(word.value(QStringLiteral("plural_form")).toString(), QStringLiteral("die Fahrkarten"));
    QCOMPARE(word.value(QStringLiteral("updated_at")).toLongLong(), isoSeconds(updatedAt));
}

void TestRemoteChangeReconciler::remoteGroupDeleteTombstonesGroupAndUnlinksWordsWithoutOutboxFeedback()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordRepository words(DLDatabaseManager::instance());

    DLWordGroup group;
    group.name = QStringLiteral("Travel");
    const QString groupId = groups.insertGroup(group);
    QVERIFY(!groupId.isEmpty());

    DLWord word;
    word.germanWord = QStringLiteral("Bahnhof");
    word.nativeTranslation = QStringLiteral("station");
    word.groupSyncId = groupId;
    const QString wordId = words.insertWord(word);
    QVERIFY(!wordId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral("DELETE FROM sync_outbox_events;")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(reconciler.applyRemoteEvent(tombstoneEvent(QStringLiteral("group"), groupId, QStringLiteral("2026-01-25T13:00:00Z")), &error),
             qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    QCOMPARE(DLDatabaseManager::instance().fetchAllGroups().size(), 0);

    const QVariantMap fetchedWord = DLDatabaseManager::instance().fetchWordById(wordId);
    QVERIFY(fetchedWord.value(QStringLiteral("group_id")).toString().isEmpty());
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM groups WHERE sync_id = :sync_id AND deleted_at IS NOT NULL;"),
                 {{ QStringLiteral(":sync_id"), groupId }}),
             1);
}

void TestRemoteChangeReconciler::twoDeviceWordGroupCrudAndMembershipReconcilesBothDirections()
{
    const QString deviceA = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4");
    const QString deviceB = QStringLiteral("d91f7d5c-86ee-458f-bd95-9a39f020174a");
    const QString groupA = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
    const QString wordA = QStringLiteral("471e206c-9c4f-4212-ae90-e90a2ee04ce4");
    const QString groupB = QStringLiteral("f20c8544-3f0b-4767-9618-32aecf56d7e9");
    const QString wordB = QStringLiteral("a30d9655-4a1c-4878-a729-43bfd067e8fa");

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QString error;

    const QList<DLSyncEventEnvelope> fromDeviceACreate = {
        groupPayloadEvent(groupA, QStringLiteral("Travel A"), QStringLiteral("#2F80ED"),
                          QStringLiteral("2026-01-15T09:00:00Z"), QStringLiteral("create"), deviceA),
        wordPayloadEvent(wordA, groupA, QStringLiteral("ticket A"),
                         QStringLiteral("2026-01-15T09:01:00Z"), QStringLiteral("create"), deviceA),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceACreate, 2, &error), qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().fetchWordById(wordA).value(QStringLiteral("group_id")).toString(), groupA);

    const QList<DLSyncEventEnvelope> fromDeviceAUpdate = {
        groupPayloadEvent(groupA, QStringLiteral("Travel A updated"), QStringLiteral("#27AE60"),
                          QStringLiteral("2026-01-15T09:02:00Z"), QStringLiteral("update"), deviceA),
        wordPayloadEvent(wordA, QString(), QStringLiteral("train ticket A"),
                         QStringLiteral("2026-01-15T09:03:00Z"), QStringLiteral("update"), deviceA),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceAUpdate, 4, &error), qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().fetchGroupById(groupA).value(QStringLiteral("name")).toString(),
             QStringLiteral("Travel A updated"));
    QVERIFY(DLDatabaseManager::instance().fetchWordById(wordA).value(QStringLiteral("group_id")).toString().isEmpty());

    const QList<DLSyncEventEnvelope> fromDeviceADelete = {
        tombstoneEvent(QStringLiteral("word"), wordA, QStringLiteral("2026-01-15T09:04:00Z"), deviceA),
        tombstoneEvent(QStringLiteral("group"), groupA, QStringLiteral("2026-01-15T09:05:00Z"), deviceA),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceADelete, 6, &error), qPrintable(error));
    QVERIFY(DLDatabaseManager::instance().fetchWordById(wordA).isEmpty());
    QVERIFY(DLDatabaseManager::instance().fetchGroupById(groupA).isEmpty());

    const QList<DLSyncEventEnvelope> fromDeviceBCreate = {
        groupPayloadEvent(groupB, QStringLiteral("Travel B"), QStringLiteral("#9B51E0"),
                          QStringLiteral("2026-01-15T10:00:00Z"), QStringLiteral("create"), deviceB),
        wordPayloadEvent(wordB, groupB, QStringLiteral("ticket B"),
                         QStringLiteral("2026-01-15T10:01:00Z"), QStringLiteral("create"), deviceB),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceBCreate, 8, &error), qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().fetchWordById(wordB).value(QStringLiteral("group_id")).toString(), groupB);

    const QList<DLSyncEventEnvelope> fromDeviceBUpdate = {
        groupPayloadEvent(groupB, QStringLiteral("Travel B updated"), QStringLiteral("#EB5757"),
                          QStringLiteral("2026-01-15T10:02:00Z"), QStringLiteral("update"), deviceB),
        wordPayloadEvent(wordB, QString(), QStringLiteral("train ticket B"),
                         QStringLiteral("2026-01-15T10:03:00Z"), QStringLiteral("update"), deviceB),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceBUpdate, 10, &error), qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().fetchGroupById(groupB).value(QStringLiteral("name")).toString(),
             QStringLiteral("Travel B updated"));
    QVERIFY(DLDatabaseManager::instance().fetchWordById(wordB).value(QStringLiteral("group_id")).toString().isEmpty());

    const QList<DLSyncEventEnvelope> fromDeviceBDelete = {
        tombstoneEvent(QStringLiteral("word"), wordB, QStringLiteral("2026-01-15T10:04:00Z"), deviceB),
        tombstoneEvent(QStringLiteral("group"), groupB, QStringLiteral("2026-01-15T10:05:00Z"), deviceB),
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(fromDeviceBDelete, 12, &error), qPrintable(error));
    QVERIFY(DLDatabaseManager::instance().fetchWordById(wordB).isEmpty());
    QVERIFY(DLDatabaseManager::instance().fetchGroupById(groupB).isEmpty());
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(12));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
}

void TestRemoteChangeReconciler::remoteReviewStatsPayloadReconcilesWithoutOutboxFeedback()
{
    DLWord word;
    word.germanWord = QStringLiteral("gehen");
    word.nativeTranslation = QStringLiteral("go");
    const QString wordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!wordId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral("DELETE FROM sync_outbox_events;")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    const QString reviewedAt = QStringLiteral("2026-01-20T07:40:00Z");
    DLSyncEventEnvelope event = baseEvent(QStringLiteral("word_review_stats"), wordId, QStringLiteral("update"), reviewedAt);
    event.payload = {
        { QStringLiteral("word_id"), wordId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("correct_answers"), 4 },
        { QStringLiteral("wrong_answers"), 2 },
        { QStringLiteral("last_reviewed_at"), reviewedAt },
        { QStringLiteral("ease_factor"), 2.6 },
        { QStringLiteral("interval_days"), 4 },
        { QStringLiteral("due_at"), QStringLiteral("2026-01-24T07:40:00Z") },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-16T12:20:00Z") },
        { QStringLiteral("updated_at"), reviewedAt }
    };

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(reconciler.applyRemoteEvent(event, &error), qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    const DLWordReviewStats stats = DLReviewStatsRepository(DLDatabaseManager::instance()).fetchStats(wordId);
    QCOMPARE(stats.correctAnswers, 4);
    QCOMPARE(stats.wrongAnswers, 2);
    QCOMPARE(stats.lastReviewedAt.toLongLong(), isoSeconds(reviewedAt));
    QCOMPARE(stats.intervalDays, 4);
}

void TestRemoteChangeReconciler::cursorAdvancesOnlyAfterWholeRemoteBatchApplies()
{
    const QString groupId = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
    const QString wordId = QStringLiteral("471e206c-9c4f-4212-ae90-e90a2ee04ce4");
    const QString missingGroupId = QStringLiteral("848f84f6-aef0-463c-9087-08046262e9d2");
    const QString updatedAt = QStringLiteral("2026-01-16T10:15:00Z");

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QString error;
    const QList<DLSyncEventEnvelope> interruptedBatch = {
        groupPayloadEvent(groupId, QStringLiteral("Travel"), QStringLiteral("#2F80ED"), updatedAt),
        wordPayloadEvent(wordId, missingGroupId, QStringLiteral("train ticket"), updatedAt)
    };

    QVERIFY(!reconciler.applyRemoteEventsAndAdvanceCursor(interruptedBatch, 42, &error));
    QVERIFY2(error.contains(QStringLiteral("Referenced remote group does not exist locally")),
             qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(0));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM groups WHERE sync_id = :sync_id;"),
                 {{ QStringLiteral(":sync_id"), groupId }}),
             0);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM words WHERE sync_id = :sync_id;"),
                 {{ QStringLiteral(":sync_id"), wordId }}),
             0);

    error.clear();
    const QList<DLSyncEventEnvelope> replayBatch = {
        groupPayloadEvent(groupId, QStringLiteral("Travel"), QStringLiteral("#2F80ED"), updatedAt),
        wordPayloadEvent(wordId, groupId, QStringLiteral("train ticket"), updatedAt)
    };
    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(replayBatch, 42, &error),
             qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(42));

    QVERIFY2(reconciler.applyRemoteEventsAndAdvanceCursor(replayBatch, 42, &error),
             qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(42));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM groups WHERE sync_id = :sync_id;"),
                 {{ QStringLiteral(":sync_id"), groupId }}),
             1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM words WHERE sync_id = :sync_id;"),
                 {{ QStringLiteral(":sync_id"), wordId }}),
             1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM word_review_stats WHERE word_sync_id = :word_sync_id;"),
                 {{ QStringLiteral(":word_sync_id"), wordId }}),
             1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
}

QTEST_MAIN(TestRemoteChangeReconciler)
#include "TestRemoteChangeReconciler.moc"
