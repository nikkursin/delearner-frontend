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
                              const QString& updatedAt)
{
    DLSyncEventEnvelope event;
    event.contractVersion = DLSyncEventSerializer::contractVersion();
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.deviceId = QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4");
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
                                      const QString& updatedAt)
{
    DLSyncEventEnvelope event = baseEvent(QStringLiteral("group"), groupId, QStringLiteral("update"), updatedAt);
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
                                     const QString& updatedAt)
{
    DLSyncEventEnvelope event = baseEvent(QStringLiteral("word"), wordId, QStringLiteral("update"), updatedAt);
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
                                   const QString& deletedAt)
{
    DLSyncEventEnvelope event = baseEvent(entityType, entityId, QStringLiteral("delete"), deletedAt);
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
