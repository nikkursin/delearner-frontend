#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Repositories/DLAppSettingRepository.h"
#include "Sync/DLRemoteChangeReconciler.h"
#include "Sync/DLBootstrapStateRestorer.h"

class TestAppSettingRepository : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        auto& db = DLDatabaseManager::instance();
        db.closeDatabase();
        QVERIFY(db.openDatabase(m_dir.filePath(QUuid::createUuid().toString() + ".sqlite")));
        DLTestSupport::installSyncContext();
    }
    void cleanup() { DLDatabaseManager::instance().closeDatabase(); }
    void localMutationsAreAtomicAndExcludeUiSettings()
    {
        auto& db = DLDatabaseManager::instance();
        DLAppSettingRepository settings(db);
        for (const QString& key : {QStringLiteral("learning.native_language"), QStringLiteral("learning.quiz_defaults"), QStringLiteral("learning.preferences")}) {
            QVERIFY(settings.setSetting(key, "en"));
            const QString id = settings.fetchSetting(key).value("id").toString();
            QVERIFY(!QUuid(id).isNull());
            QVERIFY(settings.setSetting(key, "de"));
            QCOMPARE(settings.fetchSetting(key).value("id").toString(), id);
            QVERIFY(settings.deleteSetting(key));
            QVERIFY(settings.fetchSetting(key).isEmpty());
            QVERIFY(settings.setSetting(key, "fr"));
            QCOMPARE(settings.fetchSetting(key).value("id").toString(), id);
        }
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 12);
        for (const QString& key : {QStringLiteral("ui.theme"), QStringLiteral("navigation"), QStringLiteral("search")}) {
            QVERIFY(!settings.setSetting(key, "local"));
        }
        db.failAfterNextSyncOutboxWriteForTesting();
        QVERIFY(!settings.setSetting("learning.native_language", "failed"));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("setting_value").toString(), QStringLiteral("fr"));
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 12);
    }
    void canonicalAcknowledgementPreservesPendingRetriesAndUuid()
    {
        auto& db = DLDatabaseManager::instance();
        DLAppSettingRepository settings(db);
        QVERIFY(settings.setSetting("learning.native_language", "en"));
        const auto original = db.selectOneRow("SELECT event_id, envelope_json FROM sync_outbox_events ORDER BY rowid LIMIT 1");
        QVERIFY(settings.setSetting("learning.native_language", "fr"));
        const auto pending = db.selectOneRow("SELECT event_id, envelope_json FROM sync_outbox_events ORDER BY rowid DESC LIMIT 1");
        QString error;
        auto event = DLSyncEventSerializer::parseContractJson(original.value("envelope_json").toString().toUtf8(), "1.0", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QString canonicalId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto body = event.payload;
        body["id"] = canonicalId;
        body["setting_value"] = "de";
        const QJsonObject canonical{{"entityType", "app_setting"}, {"entityId", canonicalId}, {"operation", "update"}, {"state", QJsonObject::fromVariantMap(body)}};
        QVERIFY2(db.acknowledgeSyncOutboxEvent(original.value("event_id").toString(), 1,
            QString::fromUtf8(QJsonDocument(canonical).toJson())), qPrintable(db.lastError()));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("id").toString(), canonicalId);
        QCOMPARE(settings.fetchSetting("learning.native_language").value("setting_value").toString(), QStringLiteral("fr"));
        QCOMPARE(db.selectOneRow("SELECT event_id, envelope_json FROM sync_outbox_events"), pending);
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM account_learning_settings"), 1);
        QVERIFY(settings.setSetting("learning.native_language", "it"));
        QCOMPARE(db.selectOneRow("SELECT entity_id FROM sync_outbox_events ORDER BY rowid DESC LIMIT 1").value("entity_id").toString(), canonicalId);
        QVERIFY(!db.acknowledgeSyncOutboxEvent(pending.value("event_id").toString(), 2, "{}"));
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 2);
    }
    void canonicalDeleteAndAcknowledgementRollbackAreAtomic()
    {
        auto& db = DLDatabaseManager::instance();
        DLAppSettingRepository settings(db);
        QVERIFY(settings.setSetting("learning.native_language", "en"));
        const auto original = db.selectOneRow("SELECT event_id, envelope_json FROM sync_outbox_events");
        QString error;
        const auto event = DLSyncEventSerializer::parseContractJson(original.value("envelope_json").toString().toUtf8(), "1.0", &error);
        QVERIFY(error.isEmpty());
        const QString canonicalId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto tombstone = DLSyncEventSerializer::tombstoneForEntity("app_setting", canonicalId,
            DLTestSupport::testUserId(), event.updatedAt, {{"setting_key", "learning.native_language"}});
        const QJsonObject canonical{{"entityType", "app_setting"}, {"entityId", canonicalId},
            {"operation", "delete"}, {"tombstone", QJsonObject::fromVariantMap(tombstone)}};
        const QString json = QString::fromUtf8(QJsonDocument(canonical).toJson());
        QVERIFY(db.executeSql("CREATE TRIGGER fail_setting BEFORE UPDATE ON account_learning_settings BEGIN SELECT RAISE(ABORT, 'test failure'); END"));
        QVERIFY(!db.acknowledgeSyncOutboxEvent(original.value("event_id").toString(), 1, json));
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 1);
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_acknowledged_event_diagnostics"), 0);
        QVERIFY(!settings.fetchSetting("learning.native_language").isEmpty());
        QVERIFY(db.executeSql("DROP TRIGGER fail_setting"));
        QVERIFY2(db.acknowledgeSyncOutboxEvent(original.value("event_id").toString(), 1, json), qPrintable(db.lastError()));
        QVERIFY(settings.fetchSetting("learning.native_language").isEmpty());
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 0);
        QCOMPARE(db.selectOneRow("SELECT sync_id FROM account_learning_settings").value("sync_id").toString(), canonicalId);
    }
    void pullBootstrapAndRecoveryConvergeByKey()
    {
        auto& db = DLDatabaseManager::instance();
        DLAppSettingRepository settings(db);
        QVERIFY(db.executeSql("CREATE TABLE local_ui (theme TEXT, navigation TEXT)"));
        QVERIFY(db.executeSql("INSERT INTO local_ui VALUES ('dark', 'words')"));
        QVERIFY(settings.setSetting("learning.native_language", "offline"));
        const auto saved = db.selectOneRow("SELECT envelope_json FROM sync_outbox_events");
        QString error;
        auto event = DLSyncEventSerializer::parseContractJson(saved.value("envelope_json").toString().toUtf8(), "1.0", &error);
        QVERIFY(error.isEmpty());
        event.entityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.payload["id"] = event.entityId;
        event.payload["setting_value"] = "canonical";
        DLRemoteChangeReconciler reconciler(db);
        QVERIFY2(reconciler.applyRemoteEvent(event, &error), qPrintable(error));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("id").toString(), event.entityId);
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 1);
        DLBootstrapStateRestorer restorer(db);
        const QVariantMap bootstrap{{"serverSequence", 3}, {"state", QVariantMap{{"learningSettings", QVariantList{event.payload}}}}};
        QVERIFY2(restorer.restoreFromResponse(bootstrap, &error), qPrintable(error));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("setting_value").toString(), QStringLiteral("offline"));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("id").toString(), event.entityId);
        QCOMPARE(db.selectOneRow("SELECT envelope_json FROM sync_outbox_events"), saved);
        QCOMPARE(db.selectOneRow("SELECT theme FROM local_ui").value("theme").toString(), QStringLiteral("dark"));
        QVERIFY(db.executeSql("DELETE FROM sync_outbox_events"));
        event.operation = "delete";
        event.tombstone = DLSyncEventSerializer::tombstoneForEntity("app_setting", event.entityId,
            DLTestSupport::testUserId(), event.updatedAt, {{"setting_key", "learning.native_language"}});
        event.payload.clear();
        QVERIFY2(reconciler.applyRemoteEvent(event, &error), qPrintable(error));
        QVERIFY(settings.fetchSetting("learning.native_language").isEmpty());
        QVERIFY2(restorer.restoreFromResponse(bootstrap, &error), qPrintable(error));
        QCOMPARE(settings.fetchSetting("learning.native_language").value("setting_value").toString(), QStringLiteral("canonical"));
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM sync_outbox_events"), 0);
        QVERIFY(db.deleteAllData());
        QCOMPARE(db.selectInt("SELECT COUNT(*) FROM account_learning_settings"), 0);
    }
private:
    QTemporaryDir m_dir;
};
QTEST_GUILESS_MAIN(TestAppSettingRepository)
#include "TestAppSettingRepository.moc"
