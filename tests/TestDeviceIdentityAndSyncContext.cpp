#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>

#include <memory>

#include "Auth/DLClientAuthService.h"
#include "Auth/DLAuthSessionStore.h"
#include "Sync/DLDeviceIdentityStore.h"
#include "Sync/DLSyncRequestBuilder.h"

class TestDeviceIdentityAndSyncContext : public QObject
{
    Q_OBJECT

private slots:
    void generatedDeviceIdSurvivesStoreReload();
    void registeredDeviceContextPersistsWithSession();
    void signInRegistersAndPersistsDeviceContext();
    void syncRequestsRequireRegisteredDevice();
    void syncRequestsAttachBearerAndDeviceContext();
};

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
    if (headerEnd < 0) {
        return {};
    }

    return QJsonDocument::fromJson(request.mid(headerEnd + 4)).object();
}

void writeJsonResponse(QTcpSocket* socket, int statusCode, const QByteArray& body)
{
    const QByteArray reason = statusCode == 201 ? QByteArray("Created") : QByteArray("OK");
    const QByteArray response = QByteArray("HTTP/1.1 ")
        + QByteArray::number(statusCode)
        + QByteArray(" ")
        + reason
        + QByteArray("\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size())
        + QByteArray("\r\nConnection: close\r\n\r\n")
        + body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

DLAuthSession validSession()
{
    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("cached-token");
    session.tokenType = QStringLiteral("Bearer");
    session.deviceId = QStringLiteral("d783fc93-d487-4d99-b566-886a018403a1");
    session.deviceDisplayName = QStringLiteral("Laptop");
    session.priorSuccessfulAuthentication = true;
    return session;
}

void TestDeviceIdentityAndSyncContext::generatedDeviceIdSurvivesStoreReload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("device.json"));

    DLDeviceIdentityStore firstStore(path);
    const QString firstDeviceId = firstStore.deviceId();
    QVERIFY2(firstStore.lastError().isEmpty(), qPrintable(firstStore.lastError()));
    QVERIFY(!QUuid(firstDeviceId).isNull());
    QVERIFY(!firstDeviceId.contains(QLatin1Char('{')));
    QVERIFY(QFile::exists(path));

    DLDeviceIdentityStore reloadedStore(path);
    QCOMPARE(reloadedStore.deviceId(), firstDeviceId);
    QVERIFY2(reloadedStore.lastError().isEmpty(), qPrintable(reloadedStore.lastError()));
}

void TestDeviceIdentityAndSyncContext::registeredDeviceContextPersistsWithSession()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.json"));

    const DLAuthSession session = validSession();
    DLAuthSessionStore store(path);
    QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));

    const std::optional<DLAuthSession> loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->deviceId, session.deviceId);
    QCOMPARE(loaded->deviceDisplayName, session.deviceDisplayName);
    QVERIFY(loaded->hasRegisteredDevice());
}

void TestDeviceIdentityAndSyncContext::signInRegistersAndPersistsDeviceContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sessionPath = dir.filePath(QStringLiteral("session.json"));

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    DLClientAuthService auth(
        sessionPath,
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
    QSignalSpy successSpy(&auth, &DLClientAuthService::authSucceeded);
    QSignalSpy failureSpy(&auth, &DLClientAuthService::authFailed);

    auth.signIn(QStringLiteral("learner@example.com"), QStringLiteral("password"));

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> authSocket(server.nextPendingConnection());
    const QByteArray authRequest = readHttpRequest(authSocket.get());
    QVERIFY(authRequest.startsWith("POST /api/v1/auth/sign-in "));
    const QByteArray authBody =
        QByteArray(R"({"sessionToken":"server-token","tokenType":"Bearer","userId":"df5cb428-b235-4b56-9a21-137f1169015b"})");
    writeJsonResponse(authSocket.get(), 200, authBody);

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> deviceSocket(server.nextPendingConnection());
    const QByteArray deviceRequest = readHttpRequest(deviceSocket.get());
    QVERIFY(deviceRequest.startsWith("POST /api/v1/devices/register "));
    QVERIFY(deviceRequest.toLower().contains("authorization: bearer server-token"));

    const QJsonObject deviceBody = httpJsonBody(deviceRequest);
    const QString requestedDeviceId = deviceBody.value(QStringLiteral("deviceId")).toString();
    QVERIFY(!QUuid(requestedDeviceId).isNull());

    QJsonObject deviceResponse;
    deviceResponse.insert(QStringLiteral("deviceId"), requestedDeviceId);
    deviceResponse.insert(QStringLiteral("displayName"), QStringLiteral("Laptop"));
    writeJsonResponse(
        deviceSocket.get(),
        201,
        QJsonDocument(deviceResponse).toJson(QJsonDocument::Compact));

    QTRY_COMPARE(successSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 0);

    DLAuthSessionStore store(sessionPath);
    const std::optional<DLAuthSession> loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->userId, QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b"));
    QCOMPARE(loaded->sessionToken, QStringLiteral("server-token"));
    QCOMPARE(loaded->deviceId, requestedDeviceId);
    QVERIFY(loaded->hasRegisteredDevice());

    DLClientAuthService restarted(sessionPath);
    QCOMPARE(restarted.deviceId(), requestedDeviceId);
    QVERIFY(restarted.hasRegisteredDevice());
}

void TestDeviceIdentityAndSyncContext::syncRequestsRequireRegisteredDevice()
{
    DLAuthSession session = validSession();
    session.deviceId.clear();

    QString error;
    const QByteArray body = DLSyncRequestBuilder::cursorAdvanceBody(session, 1, &error);
    QVERIFY(body.isEmpty());
    QCOMPARE(error, QStringLiteral("Authenticated sync requests require a registered device id."));
}

void TestDeviceIdentityAndSyncContext::syncRequestsAttachBearerAndDeviceContext()
{
    const DLAuthSession session = validSession();

    QString error;
    const QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(
        QUrl(QStringLiteral("http://127.0.0.1:8080")),
        QStringLiteral("/events/cursor"),
        session,
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(request.url(), QUrl(QStringLiteral("http://127.0.0.1:8080/events/cursor")));
    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer cached-token"));
    QCOMPARE(request.header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/json"));

    const QByteArray cursorBody = DLSyncRequestBuilder::cursorAdvanceBody(session, 42, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonObject cursor = QJsonDocument::fromJson(cursorBody).object();
    QCOMPARE(cursor.value(QStringLiteral("deviceId")).toString(), session.deviceId);
    QCOMPARE(cursor.value(QStringLiteral("consumedSequence")).toInteger(), 42);

    DLSyncEventEnvelope event;
    event.eventId = QStringLiteral("de59e7da-086e-4f64-86e6-99d34e465b96");
    event.entityType = QStringLiteral("group");
    event.entityId = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
    event.operation = QStringLiteral("create");
    event.updatedAt = QStringLiteral("2026-01-15T08:30:00Z");
    event.payload = {
        {QStringLiteral("id"), event.entityId},
        {QStringLiteral("ownerUserId"), session.userId},
        {QStringLiteral("name"), QStringLiteral("Travel")},
        {QStringLiteral("color_hex"), QStringLiteral("#2F80ED")},
        {QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:30:00Z")},
        {QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:30:00Z")},
    };

    const DLSyncEventEnvelope contextualEvent =
        DLSyncRequestBuilder::eventWithSessionContext(event, session, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(contextualEvent.deviceId, session.deviceId);
    QCOMPARE(contextualEvent.authenticatedUserId, session.userId);
    QVERIFY2(DLSyncEventSerializer::validateEvent(contextualEvent, &error), qPrintable(error));
}

QTEST_MAIN(TestDeviceIdentityAndSyncContext)
#include "TestDeviceIdentityAndSyncContext.moc"
