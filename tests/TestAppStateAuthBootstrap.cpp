#include <QtTest>

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <memory>

#include "Auth/DLAuthSessionStore.h"
#include "Managers/DLAppStateManager.h"

class TestAppStateAuthBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void freshInstallRoutesToAuthPageWithoutOpeningDatabase();
    void priorSuccessfulAuthenticationOpensFreeCore();
    void activeConnectedStartupPullsRemoteChangesWithoutPolling();
    void activeConnectedLocalMutationStartsQueuedSync();
    void resumeStartsOneSyncAfterInactiveStartup();
    void networkRestorationStartsOneSyncAfterOfflineStartup();
    void overlappingResumeAndNetworkRestorationCoalesceOneFollowUpSync();
    void manualDiagnosticSyncUsesExistingCoordinator();
    void offlineManualSyncDoesNotBypassNetworkRestoration();
    void repeatedManualDiagnosticTriggersCoalesceOneFollowUpSync();

private:
    QByteArray m_previousApiBaseUrl;
    bool m_hadPreviousApiBaseUrl = false;
};

namespace {
DLAuthSession registeredSession()
{
    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("cached-token");
    session.deviceId = QStringLiteral("6a521372-48d8-4fd4-a474-a77a0fc2cdcb");
    session.priorSuccessfulAuthentication = true;
    return session;
}

QByteArray readHttpRequest(QTcpSocket* socket)
{
    QByteArray request;
    int contentLength = 0;
    qsizetype headerEnd = -1;

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (socket->bytesAvailable() == 0) {
            socket->waitForReadyRead(20);
        }
        request.append(socket->readAll());

        headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd >= 0 && contentLength == 0) {
            const QList<QByteArray> headerLines = request.left(headerEnd).split('\n');
            for (QByteArray line : headerLines) {
                line = line.trimmed();
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(QByteArray("content-length:").size()).trimmed().toInt();
                    break;
                }
            }
        }

        if (headerEnd >= 0 && request.size() >= headerEnd + 4 + contentLength) {
            return request;
        }
    }

    return request;
}

QJsonObject httpJsonBody(const QByteArray& request)
{
    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
    return headerEnd >= 0 ? QJsonDocument::fromJson(request.mid(headerEnd + 4)).object() : QJsonObject();
}

void writeJsonResponse(QTcpSocket* socket, int statusCode, const QJsonObject& object)
{
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const QByteArray response = QByteArray("HTTP/1.1 ")
        + QByteArray::number(statusCode)
        + QByteArray(" OK\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size())
        + QByteArray("\r\nConnection: close\r\n\r\n")
        + body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void writeEmptyPullResponse(QTcpSocket* socket)
{
    writeJsonResponse(socket, 200, QJsonObject{
        { QStringLiteral("events"), QJsonArray{} },
        { QStringLiteral("nextSequence"), 0 },
        { QStringLiteral("hasMore"), false },
        { QStringLiteral("cursorGapDetected"), false },
        { QStringLiteral("firstAvailableSequence"), 0 }
    });
}
}

void TestAppStateAuthBootstrap::init()
{
    m_hadPreviousApiBaseUrl = qEnvironmentVariableIsSet("DELEARNER_API_BASE_URL");
    m_previousApiBaseUrl = qgetenv("DELEARNER_API_BASE_URL");
}

void TestAppStateAuthBootstrap::cleanup()
{
    if (m_hadPreviousApiBaseUrl) {
        qputenv("DELEARNER_API_BASE_URL", m_previousApiBaseUrl);
    } else {
        qunsetenv("DELEARNER_API_BASE_URL");
    }
}

void TestAppStateAuthBootstrap::freshInstallRoutesToAuthPageWithoutOpeningDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAppStateManager manager;
    manager.init(databasePath);

    QCOMPARE(manager.currentScreen(), DLAppStateManager::AuthPage);
    QCOMPARE(manager.authState(), QStringLiteral("authentication_required"));
    QVERIFY(!QFile::exists(databasePath));
}

void TestAppStateAuthBootstrap::priorSuccessfulAuthenticationOpensFreeCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("cached-token");

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QCOMPARE(manager.currentScreen(), DLAppStateManager::AddEditWordPage);
    QCOMPARE(manager.authState(), QStringLiteral("authenticated_session"));
    QVERIFY(QFile::exists(databasePath));
}

void TestAppStateAuthBootstrap::activeConnectedStartupPullsRemoteChangesWithoutPolling()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> pullSocket(server.nextPendingConnection());
    const QByteArray pullRequest = readHttpRequest(pullSocket.get());
    QVERIFY(pullRequest.startsWith("GET /events?after=0 "));
    QVERIFY(pullRequest.toLower().contains("authorization: bearer cached-token"));
    writeEmptyPullResponse(pullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::activeConnectedLocalMutationStartsQueuedSync()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> startupPullSocket(server.nextPendingConnection());
    QVERIFY(readHttpRequest(startupPullSocket.get()).startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(startupPullSocket.get());

    const QString groupId = manager.createGroup(QStringLiteral("Travel"));
    QVERIFY(!groupId.isEmpty());

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> uploadSocket(server.nextPendingConnection());
    const QByteArray uploadRequest = readHttpRequest(uploadSocket.get());
    QVERIFY(uploadRequest.startsWith("POST /events "));

    const QJsonArray uploadedEvents = httpJsonBody(uploadRequest).value(QStringLiteral("events")).toArray();
    QCOMPARE(uploadedEvents.size(), 1);
    const QString eventId = uploadedEvents.at(0).toObject().value(QStringLiteral("eventId")).toString();
    QVERIFY(!eventId.isEmpty());

    QJsonObject acceptedEvent;
    acceptedEvent.insert(QStringLiteral("eventId"), eventId);
    acceptedEvent.insert(QStringLiteral("serverSequence"), 1);
    acceptedEvent.insert(QStringLiteral("replayed"), false);
    acceptedEvent.insert(QStringLiteral("canonical"), QJsonObject{{QStringLiteral("accepted"), true}});
    writeJsonResponse(uploadSocket.get(), 200, QJsonObject{
        { QStringLiteral("accepted"), QJsonArray{acceptedEvent} },
        { QStringLiteral("rejected"), QJsonArray{} }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> followUpPullSocket(server.nextPendingConnection());
    QVERIFY(readHttpRequest(followUpPullSocket.get()).startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(followUpPullSocket.get());
}

void TestAppStateAuthBootstrap::resumeStartsOneSyncAfterInactiveStartup()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.setApplicationActive(false);
    manager.init(databasePath);

    QVERIFY(!server.waitForNewConnection(100));

    manager.setApplicationActive(true);
    manager.setApplicationActive(true);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> pullSocket(server.nextPendingConnection());
    const QByteArray pullRequest = readHttpRequest(pullSocket.get());
    QVERIFY(pullRequest.startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(pullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::networkRestorationStartsOneSyncAfterOfflineStartup()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.setNetworkAvailable(false);
    manager.init(databasePath);

    QVERIFY(!server.waitForNewConnection(100));

    manager.setNetworkAvailable(true);
    manager.setNetworkAvailable(true);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> pullSocket(server.nextPendingConnection());
    const QByteArray pullRequest = readHttpRequest(pullSocket.get());
    QVERIFY(pullRequest.startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(pullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::overlappingResumeAndNetworkRestorationCoalesceOneFollowUpSync()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> firstPullSocket(server.nextPendingConnection());
    const QByteArray firstPullRequest = readHttpRequest(firstPullSocket.get());
    QVERIFY(firstPullRequest.startsWith("GET /events?after=0 "));

    manager.setApplicationActive(false);
    manager.setApplicationActive(true);
    manager.setApplicationActive(true);
    manager.setNetworkAvailable(false);
    manager.setNetworkAvailable(true);
    manager.setNetworkAvailable(true);

    QVERIFY(!server.waitForNewConnection(100));

    writeEmptyPullResponse(firstPullSocket.get());

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> followUpPullSocket(server.nextPendingConnection());
    const QByteArray followUpPullRequest = readHttpRequest(followUpPullSocket.get());
    QVERIFY(followUpPullRequest.startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(followUpPullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::manualDiagnosticSyncUsesExistingCoordinator()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> startupPullSocket(server.nextPendingConnection());
    QVERIFY(readHttpRequest(startupPullSocket.get()).startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(startupPullSocket.get());

    QVERIFY(manager.requestManualSync());

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> manualPullSocket(server.nextPendingConnection());
    const QByteArray manualPullRequest = readHttpRequest(manualPullSocket.get());
    QVERIFY(manualPullRequest.startsWith("GET /events?after=0 "));
    QVERIFY(manualPullRequest.toLower().contains("authorization: bearer cached-token"));
    writeEmptyPullResponse(manualPullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::offlineManualSyncDoesNotBypassNetworkRestoration()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.setNetworkAvailable(false);
    manager.init(databasePath);

    QVERIFY(!manager.requestManualSync());
    QCOMPARE(manager.lastError(), QStringLiteral("Sync is unavailable while offline, inactive or unauthenticated."));
    QVERIFY(!server.waitForNewConnection(100));

    manager.setNetworkAvailable(true);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> pullSocket(server.nextPendingConnection());
    const QByteArray pullRequest = readHttpRequest(pullSocket.get());
    QVERIFY(pullRequest.startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(pullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

void TestAppStateAuthBootstrap::repeatedManualDiagnosticTriggersCoalesceOneFollowUpSync()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("DELEARNER_API_BASE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(registeredSession()), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> firstPullSocket(server.nextPendingConnection());
    const QByteArray firstPullRequest = readHttpRequest(firstPullSocket.get());
    QVERIFY(firstPullRequest.startsWith("GET /events?after=0 "));

    QVERIFY(manager.requestManualSync());
    QVERIFY(manager.requestManualSync());
    QVERIFY(!server.waitForNewConnection(100));

    writeEmptyPullResponse(firstPullSocket.get());

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> followUpPullSocket(server.nextPendingConnection());
    const QByteArray followUpPullRequest = readHttpRequest(followUpPullSocket.get());
    QVERIFY(followUpPullRequest.startsWith("GET /events?after=0 "));
    writeEmptyPullResponse(followUpPullSocket.get());

    QVERIFY(!server.waitForNewConnection(100));
}

QTEST_MAIN(TestAppStateAuthBootstrap)
#include "TestAppStateAuthBootstrap.moc"
