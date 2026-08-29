#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "Auth/DLAuthSessionStore.h"
#include "Auth/DLClientAuthService.h"

class TestAuthSessionStore : public QObject
{
    Q_OBJECT

private slots:
    void firstUseOfflineRequiresAuthentication();
    void successfulSessionPersistsOfflineAccess();
    void rejectedOnlineSessionPreservesOfflineFreeCore();
    void startupStatesMatchRootAuthBehaviorFixture();

private:
    QString fixturePath() const;
};

QString TestAuthSessionStore::fixturePath() const
{
#ifdef DELEARNER_ROOT_DIR
    return QStringLiteral(DELEARNER_ROOT_DIR)
        + QStringLiteral("/contracts/fixtures/v1/auth-behavior-contract.json");
#else
    return QString();
#endif
}

void TestAuthSessionStore::firstUseOfflineRequiresAuthentication()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DLClientAuthService auth(dir.filePath(QStringLiteral("session.json")));
    QCOMPARE(auth.hasPriorSuccessfulAuthentication(), false);
    QCOMPARE(auth.startupState(false), DLClientAuthService::StartupState::AuthenticationRequired);
    QCOMPARE(DLClientAuthService::stateName(auth.startupState(false)),
             QStringLiteral("authentication_required"));
    QCOMPARE(auth.canUseFreeCoreOffline(), false);
}

void TestAuthSessionStore::successfulSessionPersistsOfflineAccess()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.json"));

    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("token");
    session.tokenType = QStringLiteral("Bearer");

    DLAuthSessionStore store(path);
    QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));

    DLClientAuthService auth(path);
    QCOMPARE(auth.hasPriorSuccessfulAuthentication(), true);
    QCOMPARE(auth.hasSessionCredentials(), true);
    QCOMPARE(auth.userId(), session.userId);
    QCOMPARE(auth.email(), session.email);
    QCOMPARE(auth.startupState(false), DLClientAuthService::StartupState::OfflineFreeCore);
    QCOMPARE(auth.canUseFreeCoreOffline(), true);
}

void TestAuthSessionStore::rejectedOnlineSessionPreservesOfflineFreeCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.json"));

    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("expired-token");

    DLAuthSessionStore store(path);
    QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));

    DLClientAuthService auth(path);
    auth.recordOnlineSessionRejected();

    QCOMPARE(auth.startupState(true), DLClientAuthService::StartupState::ReauthenticationRequired);
    QCOMPARE(DLClientAuthService::stateName(auth.startupState(true)),
             QStringLiteral("reauthentication_required"));
    QCOMPARE(auth.startupState(false), DLClientAuthService::StartupState::OfflineFreeCore);
    QCOMPARE(auth.hasSessionCredentials(), false);
    QCOMPARE(auth.canUseFreeCoreOffline(), true);
}

void TestAuthSessionStore::startupStatesMatchRootAuthBehaviorFixture()
{
    const QString path = fixturePath();
    QVERIFY2(!path.isEmpty(), "DELEARNER_ROOT_DIR must be provided for auth fixture coverage.");

    QFile fixture(path);
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(QStringLiteral("Missing fixture: %1").arg(path)));

    const QJsonDocument document = QJsonDocument::fromJson(fixture.readAll());
    QVERIFY(document.isObject());
    const QJsonArray states = document.object().value(QStringLiteral("states")).toArray();
    QVERIFY(!states.isEmpty());

    for (const QJsonValue& value : states) {
        const QJsonObject state = value.toObject();
        const QString name = state.value(QStringLiteral("name")).toString();
        const QString expected = state.value(QStringLiteral("expectedState")).toString();
        const bool freeCoreAccess = state.value(QStringLiteral("freeCoreAccess")).toBool();

        if (name == QStringLiteral("first_use_offline")) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            DLClientAuthService auth(dir.filePath(QStringLiteral("session.json")));
            QCOMPARE(DLClientAuthService::stateName(auth.startupState(false)), expected);
            QCOMPARE(auth.canUseFreeCoreOffline(), freeCoreAccess);
        } else if (name == QStringLiteral("previously_authenticated_offline_reopen")) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            const QString sessionPath = dir.filePath(QStringLiteral("session.json"));
            DLAuthSession session;
            session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
            session.email = QStringLiteral("learner@example.com");
            session.sessionToken = QStringLiteral("cached-token");
            DLAuthSessionStore store(sessionPath);
            QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));
            DLClientAuthService auth(sessionPath);
            QCOMPARE(DLClientAuthService::stateName(auth.startupState(false)), expected);
            QCOMPARE(auth.canUseFreeCoreOffline(), freeCoreAccess);
        } else if (name == QStringLiteral("invalid_or_expired_session_online")) {
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            const QString sessionPath = dir.filePath(QStringLiteral("session.json"));
            DLAuthSession session;
            session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
            session.email = QStringLiteral("learner@example.com");
            session.sessionToken = QStringLiteral("expired-token");
            DLAuthSessionStore store(sessionPath);
            QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));
            DLClientAuthService auth(sessionPath);
            auth.recordOnlineSessionRejected();
            QCOMPARE(DLClientAuthService::stateName(auth.startupState(true)), expected);
            QCOMPARE(auth.canUseFreeCoreOffline(), freeCoreAccess);
        }
    }
}

QTEST_MAIN(TestAuthSessionStore)
#include "TestAuthSessionStore.moc"
