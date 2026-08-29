#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

#include <memory>

#include "Auth/DLAuthSession.h"
#include "DLTestSupport.h"
#include "Managers/DLDatabaseManager.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"
#include "Sync/DLSyncCoordinator.h"

class TestSyncCoordinator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rejectsOverlappingCycleAndKeepsLocalCrudAvailable();

private:
    QString m_dbPath;
};

namespace {
DLAuthSession validSession()
{
    DLAuthSession session;
    session.userId = DLTestSupport::testUserId();
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("cached-token");
    session.tokenType = QStringLiteral("Bearer");
    session.deviceId = DLTestSupport::testDeviceId();
    session.deviceDisplayName = QStringLiteral("Laptop");
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
    const QByteArray reason = statusCode == 201 ? QByteArray("Created") : QByteArray("OK");
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
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
}

void TestSyncCoordinator::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_sync_coordinator_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
    DLTestSupport::installSyncContext();
}

void TestSyncCoordinator::cleanup()
{
    DLDatabaseManager::instance().clearSyncContext();
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

void TestSyncCoordinator::rejectsOverlappingCycleAndKeepsLocalCrudAvailable()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    DLGroupRepository groups(DLDatabaseManager::instance());
    DLWordGroup first;
    first.name = QStringLiteral("Travel");
    const QString firstGroupId = groups.insertGroup(first);
    QVERIFY(!firstGroupId.isEmpty());

    QNetworkAccessManager network;
    DLSyncCoordinator coordinator(
        DLDatabaseManager::instance(),
        &network,
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
    QSignalSpy finishedSpy(&coordinator, &DLSyncCoordinator::syncFinished);

    QVERIFY(coordinator.startSync(validSession()));
    QVERIFY(coordinator.syncInProgress());

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> uploadSocket(server.nextPendingConnection());
    const QByteArray uploadRequest = readHttpRequest(uploadSocket.get());
    QVERIFY(uploadRequest.startsWith("POST /events "));
    QVERIFY(uploadRequest.toLower().contains("authorization: bearer cached-token"));

    QVERIFY(!coordinator.startSync(validSession()));
    QCOMPARE(coordinator.lastError(), QStringLiteral("Sync is already in progress."));
    QVERIFY(!server.waitForNewConnection(100));

    DLWordGroup second;
    second.name = QStringLiteral("Local while syncing");
    const QString secondGroupId = groups.insertGroup(second);
    QVERIFY2(!secondGroupId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 2);

    const QJsonArray uploadedEvents = httpJsonBody(uploadRequest).value(QStringLiteral("events")).toArray();
    QCOMPARE(uploadedEvents.size(), 1);
    const QString uploadedEventId = uploadedEvents.at(0).toObject().value(QStringLiteral("eventId")).toString();
    QVERIFY(!uploadedEventId.isEmpty());

    QJsonObject acceptedEvent;
    acceptedEvent.insert(QStringLiteral("eventId"), uploadedEventId);
    acceptedEvent.insert(QStringLiteral("serverSequence"), 1);
    acceptedEvent.insert(QStringLiteral("replayed"), false);
    acceptedEvent.insert(QStringLiteral("canonical"), QJsonObject{{QStringLiteral("accepted"), true}});
    writeJsonResponse(uploadSocket.get(), 200, QJsonObject{
        { QStringLiteral("accepted"), QJsonArray{acceptedEvent} },
        { QStringLiteral("rejected"), QJsonArray{} }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> pullSocket(server.nextPendingConnection());
    const QByteArray pullRequest = readHttpRequest(pullSocket.get());
    QVERIFY(pullRequest.startsWith("GET /events?after=0 "));
    writeJsonResponse(pullSocket.get(), 200, QJsonObject{
        { QStringLiteral("events"), QJsonArray{} },
        { QStringLiteral("nextSequence"), 0 },
        { QStringLiteral("hasMore"), false },
        { QStringLiteral("cursorGapDetected"), false },
        { QStringLiteral("firstAvailableSequence"), 0 }
    });

    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), true);
    QVERIFY(!coordinator.syncInProgress());
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events WHERE entity_id = :entity_id;"),
                 {{ QStringLiteral(":entity_id"), secondGroupId }}),
             1);
}

QTEST_MAIN(TestSyncCoordinator)
#include "TestSyncCoordinator.moc"
