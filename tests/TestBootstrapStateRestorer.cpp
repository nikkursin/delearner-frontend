#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QVariantList>
#include <QUuid>

#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLReviewStatsRepository.h"
#include "Repositories/DLWordRepository.h"
#include "Sync/DLBootstrapStateRestorer.h"
#include "Sync/DLRemoteChangeReconciler.h"

class TestBootstrapStateRestorer : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void restoresBootstrapStateForFreeCoreRepositories();
    void restoreThenDownloadedCatchUpAppliesPostBootstrapMutationBeforeReady();
    void preservesAndReplaysPendingOutboxWhenReplacingState();

private:
    QVariantMap bootstrapResponse(qint64 serverSequence) const;
    QVariantMap catchUpDownloadResponse(qint64 serverSequence) const;
    QString m_dbPath;
};

namespace {
const QString kGroupId = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
const QString kWordId = QStringLiteral("471e206c-9c4f-4212-ae90-e90a2ee04ce4");
const QString kDeletedGroupId = QStringLiteral("e9f23d57-0f7b-474e-9e71-227f8753bc96");

qint64 isoSeconds(const QString& text)
{
    return QDateTime::fromString(text, Qt::ISODate).toUTC().toSecsSinceEpoch();
}
}

void TestBootstrapStateRestorer::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_bootstrap_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestBootstrapStateRestorer::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

QVariantMap TestBootstrapStateRestorer::bootstrapResponse(qint64 serverSequence) const
{
    QVariantMap group{
        { QStringLiteral("id"), kGroupId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("name"), QStringLiteral("Travel") },
        { QStringLiteral("color_hex"), QStringLiteral("#2F80ED") },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:30:00Z") },
        { QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:30:00Z") }
    };

    QVariantMap word{
        { QStringLiteral("id"), kWordId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("german_word"), QStringLiteral("die Fahrkarte") },
        { QStringLiteral("normalized_german_word"), QStringLiteral("fahrkarte") },
        { QStringLiteral("article"), QStringLiteral("die") },
        { QStringLiteral("part_of_speech"), QStringLiteral("Nomen") },
        { QStringLiteral("native_translation"), QStringLiteral("ticket") },
        { QStringLiteral("normalized_native_translation"), QStringLiteral("ticket") },
        { QStringLiteral("example_phrase_de"), QStringLiteral("Ich kaufe eine Fahrkarte nach Berlin.") },
        { QStringLiteral("example_phrase_native"), QStringLiteral("I am buying a ticket to Berlin.") },
        { QStringLiteral("group_id"), kGroupId },
        { QStringLiteral("notes"), QStringLiteral("Common travel vocabulary.") },
        { QStringLiteral("plural_form"), QStringLiteral("die Fahrkarten") },
        { QStringLiteral("praeteritum_form"), QVariant() },
        { QStringLiteral("partizip_ii_form"), QVariant() },
        { QStringLiteral("positive_form"), QVariant() },
        { QStringLiteral("comparative_form"), QVariant() },
        { QStringLiteral("superlative_form"), QVariant() },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:35:00Z") },
        { QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:35:00Z") }
    };

    QVariantMap stats{
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("word_id"), kWordId },
        { QStringLiteral("correct_answers"), 4 },
        { QStringLiteral("wrong_answers"), 1 },
        { QStringLiteral("last_reviewed_at"), QStringLiteral("2026-01-16T12:20:00Z") },
        { QStringLiteral("ease_factor"), 2.5 },
        { QStringLiteral("interval_days"), 3 },
        { QStringLiteral("due_at"), QStringLiteral("2026-01-19T12:20:00Z") },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-16T12:20:00Z") },
        { QStringLiteral("updated_at"), QStringLiteral("2026-01-16T12:20:00Z") }
    };

    QVariantMap learningSetting{
        { QStringLiteral("id"), QStringLiteral("0dc0f0a1-6e93-4c2e-8a3d-b46d91845cdb") },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("setting_key"), QStringLiteral("learning.native_language") },
        { QStringLiteral("setting_value"), QStringLiteral("en") },
        { QStringLiteral("value_type"), QStringLiteral("string") },
        { QStringLiteral("scope"), QStringLiteral("account_learning") },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:50:00Z") },
        { QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:50:00Z") }
    };

    QVariantMap deletedGroup{
        { QStringLiteral("entity_type"), QStringLiteral("group") },
        { QStringLiteral("entity_id"), kDeletedGroupId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("deletedAt"), QStringLiteral("2026-01-25T13:00:00Z") },
        { QStringLiteral("updatedAt"), QStringLiteral("2026-01-25T13:00:00Z") }
    };

    QVariantMap deletedSetting{
        { QStringLiteral("entity_type"), QStringLiteral("app_setting") },
        { QStringLiteral("entity_id"), QStringLiteral("0dc0f0a1-6e93-4c2e-8a3d-b46d91845cdb") },
        { QStringLiteral("setting_key"), QStringLiteral("learning.native_language") },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("deletedAt"), QStringLiteral("2026-01-25T13:30:00Z") },
        { QStringLiteral("updatedAt"), QStringLiteral("2026-01-25T13:30:00Z") }
    };

    QVariantMap state;
    state.insert(QStringLiteral("groups"), QVariantList{group});
    state.insert(QStringLiteral("words"), QVariantList{word});
    state.insert(QStringLiteral("reviewStats"), QVariantList{stats});
    state.insert(QStringLiteral("learningSettings"), QVariantList{learningSetting});
    state.insert(QStringLiteral("tombstones"), QVariantList{deletedGroup, deletedSetting});

    QVariantMap response;
    response.insert(QStringLiteral("serverSequence"), serverSequence);
    response.insert(QStringLiteral("state"), state);
    return response;
}

QVariantMap TestBootstrapStateRestorer::catchUpDownloadResponse(qint64 serverSequence) const
{
    QVariantMap payload{
        { QStringLiteral("id"), kGroupId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("name"), QStringLiteral("Travel updated during bootstrap") },
        { QStringLiteral("color_hex"), QStringLiteral("#27AE60") },
        { QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:30:00Z") },
        { QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:45:00Z") }
    };

    QVariantMap event;
    event.insert(QStringLiteral("serverSequence"), serverSequence);
    event.insert(QStringLiteral("eventId"), QStringLiteral("d13567cf-5daf-4fa3-9208-112e9aee3520"));
    event.insert(QStringLiteral("deviceId"), QStringLiteral("b8e96c4a-f2cb-4ef3-8412-d6694a8358e4"));
    event.insert(QStringLiteral("entityType"), QStringLiteral("group"));
    event.insert(QStringLiteral("entityId"), kGroupId);
    event.insert(QStringLiteral("operation"), QStringLiteral("update"));
    event.insert(QStringLiteral("baseVersion"), 0);
    event.insert(QStringLiteral("occurredAt"), 456);
    event.insert(QStringLiteral("payload"), payload);

    QVariantMap response;
    response.insert(QStringLiteral("events"), QVariantList{event});
    response.insert(QStringLiteral("nextSequence"), serverSequence);
    response.insert(QStringLiteral("hasMore"), false);
    response.insert(QStringLiteral("cursorGapDetected"), false);
    response.insert(QStringLiteral("firstAvailableSequence"), 0);
    return response;
}

void TestBootstrapStateRestorer::restoresBootstrapStateForFreeCoreRepositories()
{
    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordRepository words(DLDatabaseManager::instance());

    DLWordGroup staleGroup;
    staleGroup.name = QStringLiteral("Stale local group");
    const QString staleGroupId = groups.insertGroup(staleGroup);
    QVERIFY(!staleGroupId.isEmpty());

    DLWord staleWord;
    staleWord.germanWord = QStringLiteral("alt");
    staleWord.nativeTranslation = QStringLiteral("old");
    const QString staleWordId = words.insertWord(staleWord);
    QVERIFY(!staleWordId.isEmpty());

    QVERIFY2(DLDatabaseManager::instance().executeSql(QStringLiteral("DELETE FROM sync_outbox_events;")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    DLBootstrapStateRestorer restorer(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(restorer.restoreFromResponse(bootstrapResponse(77), &error), qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(77));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    QCOMPARE(DLDatabaseManager::instance().fetchAllGroups().size(), 1);
    QCOMPARE(DLDatabaseManager::instance().getWordCount(), 1);
    QCOMPARE(DLDatabaseManager::instance().fetchWordById(staleWordId).isEmpty(), true);

    const QVariantMap restoredGroup = DLDatabaseManager::instance().fetchGroupById(kGroupId);
    QCOMPARE(restoredGroup.value(QStringLiteral("name")).toString(), QStringLiteral("Travel"));

    const QVariantMap restoredWord = DLDatabaseManager::instance().fetchWordById(kWordId);
    QCOMPARE(restoredWord.value(QStringLiteral("german_word")).toString(), QStringLiteral("die Fahrkarte"));
    QCOMPARE(restoredWord.value(QStringLiteral("group_id")).toString(), kGroupId);
    QCOMPARE(restoredWord.value(QStringLiteral("plural_form")).toString(), QStringLiteral("die Fahrkarten"));

    const DLWordReviewStats restoredStats = DLReviewStatsRepository(DLDatabaseManager::instance()).fetchStats(kWordId);
    QCOMPARE(restoredStats.correctAnswers, 4);
    QCOMPARE(restoredStats.wrongAnswers, 1);
    QCOMPARE(restoredStats.lastReviewedAt.toLongLong(), isoSeconds(QStringLiteral("2026-01-16T12:20:00Z")));

    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM groups WHERE sync_id = :sync_id AND deleted_at IS NOT NULL;"),
                 {{ QStringLiteral(":sync_id"), kDeletedGroupId }}),
             1);
}

void TestBootstrapStateRestorer::restoreThenDownloadedCatchUpAppliesPostBootstrapMutationBeforeReady()
{
    DLBootstrapStateRestorer restorer(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(restorer.restoreFromResponse(bootstrapResponse(77), &error), qPrintable(error));
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(77));
    QCOMPARE(DLDatabaseManager::instance().fetchGroupById(kGroupId).value(QStringLiteral("name")).toString(),
             QStringLiteral("Travel"));

    DLRemoteChangeReconciler reconciler(DLDatabaseManager::instance());
    QVERIFY2(reconciler.applyDownloadedEventsAndAdvanceCursor(catchUpDownloadResponse(78), &error),
             qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(78));
    QCOMPARE(DLDatabaseManager::instance().fetchGroupById(kGroupId).value(QStringLiteral("name")).toString(),
             QStringLiteral("Travel updated during bootstrap"));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
}

void TestBootstrapStateRestorer::preservesAndReplaysPendingOutboxWhenReplacingState()
{
    DLWord word;
    word.germanWord = QStringLiteral("gehen");
    word.nativeTranslation = QStringLiteral("go");
    const QString localWordId = DLWordRepository(DLDatabaseManager::instance()).insertWord(word);
    QVERIFY(!localWordId.isEmpty());
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 1);

    const QVariantMap pendingBefore = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral(R"(
            SELECT event_id, envelope_json
            FROM sync_outbox_events
            WHERE entity_id = :entity_id;
        )"),
        {{ QStringLiteral(":entity_id"), localWordId }});
    QVERIFY(!pendingBefore.isEmpty());
    QVERIFY2(DLDatabaseManager::instance().recordSyncOutboxSendAttempt(
                 pendingBefore.value(QStringLiteral("event_id")).toString(),
                 1234,
                 QStringLiteral("offline")),
             qPrintable(DLDatabaseManager::instance().lastError()));

    DLBootstrapStateRestorer restorer(DLDatabaseManager::instance());
    QString error;
    QVERIFY2(restorer.restoreFromResponse(bootstrapResponse(88), &error), qPrintable(error));

    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(88));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 1);

    const QVariantMap pendingAfter = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral(R"(
            SELECT event_id, envelope_json, send_attempt_count, next_attempt_after, last_error
            FROM sync_outbox_events
            WHERE entity_id = :entity_id;
        )"),
        {{ QStringLiteral(":entity_id"), localWordId }});
    QCOMPARE(pendingAfter.value(QStringLiteral("event_id")).toString(),
             pendingBefore.value(QStringLiteral("event_id")).toString());
    QCOMPARE(pendingAfter.value(QStringLiteral("envelope_json")).toString(),
             pendingBefore.value(QStringLiteral("envelope_json")).toString());
    QCOMPARE(pendingAfter.value(QStringLiteral("send_attempt_count")).toInt(), 1);
    QCOMPARE(pendingAfter.value(QStringLiteral("next_attempt_after")).toLongLong(), qint64(1234));
    QCOMPARE(pendingAfter.value(QStringLiteral("last_error")).toString(), QStringLiteral("offline"));

    const QVariantMap replayedWord = DLDatabaseManager::instance().fetchWordById(localWordId);
    QCOMPARE(replayedWord.value(QStringLiteral("german_word")).toString(), QStringLiteral("gehen"));
    QCOMPARE(replayedWord.value(QStringLiteral("native_translation")).toString(), QStringLiteral("go"));
    QCOMPARE(DLDatabaseManager::instance().fetchGroupById(kGroupId).value(QStringLiteral("name")).toString(),
             QStringLiteral("Travel"));
}

QTEST_MAIN(TestBootstrapStateRestorer)
#include "TestBootstrapStateRestorer.moc"
