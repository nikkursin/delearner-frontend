#include <QtTest>

#include <QDateTime>
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
    void staleDeviceRebootstrapsReplaysOriginalEventAndConverges();

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

void TestSyncCoordinator::staleDeviceRebootstrapsReplaysOriginalEventAndConverges()
{
    const QString olderTimestamp = QStringLiteral("2026-01-15T09:00:00Z");
    const QString newerTimestamp = QStringLiteral("2026-01-15T10:00:00Z");
    const qint64 olderSeconds = QDateTime::fromString(olderTimestamp, Qt::ISODate).toSecsSinceEpoch();

    DLWordGroup offlineGroup;
    offlineGroup.syncId = QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507");
    offlineGroup.name = QStringLiteral("Older offline edit");
    offlineGroup.colorHex = QStringLiteral("#3366CC");
    QCOMPARE(DLGroupRepository(DLDatabaseManager::instance()).insertGroup(offlineGroup), offlineGroup.syncId);

    QVariantMap pending = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT event_id, envelope_json FROM sync_outbox_events WHERE entity_id = :entity_id;"),
        {{ QStringLiteral(":entity_id"), offlineGroup.syncId }});
    QVERIFY(!pending.isEmpty());
    const QString originalEventId = pending.value(QStringLiteral("event_id")).toString();
    QJsonObject originalEnvelope = QJsonDocument::fromJson(
        pending.value(QStringLiteral("envelope_json")).toString().toUtf8()).object();
    QJsonObject olderPayload = originalEnvelope.value(QStringLiteral("payload")).toObject();
    olderPayload.insert(QStringLiteral("created_at"), olderTimestamp);
    olderPayload.insert(QStringLiteral("updated_at"), olderTimestamp);
    originalEnvelope.insert(QStringLiteral("updatedAt"), olderTimestamp);
    originalEnvelope.insert(QStringLiteral("payload"), olderPayload);
    const QString originalEnvelopeJson = QString::fromUtf8(
        QJsonDocument(originalEnvelope).toJson(QJsonDocument::Compact));
    const QString originalPayloadJson = QString::fromUtf8(
        QJsonDocument(olderPayload).toJson(QJsonDocument::Compact));
    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral(R"(
                    UPDATE sync_outbox_events
                    SET updated_at = :updated_at,
                        envelope_json = :envelope_json,
                        payload_json = :payload_json,
                        next_attempt_after = :next_attempt_after
                    WHERE event_id = :event_id;
                 )"),
                 {
                     { QStringLiteral(":updated_at"), olderSeconds },
                     { QStringLiteral(":envelope_json"), originalEnvelopeJson },
                     { QStringLiteral(":payload_json"), originalPayloadJson },
                     { QStringLiteral(":next_attempt_after"), DLDatabaseManager::currentUnixTime() + 3600 },
                     { QStringLiteral(":event_id"), originalEventId }
                 }),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral("UPDATE groups SET created_at = :timestamp, updated_at = :timestamp WHERE sync_id = :entity_id;"),
                 {
                     { QStringLiteral(":timestamp"), olderSeconds },
                     { QStringLiteral(":entity_id"), offlineGroup.syncId }
                 }),
             qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(DLDatabaseManager::instance().advanceRemoteCursor(4),
             qPrintable(DLDatabaseManager::instance().lastError()));

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QNetworkAccessManager network;
    DLSyncCoordinator coordinator(
        DLDatabaseManager::instance(),
        &network,
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
    QSignalSpy finishedSpy(&coordinator, &DLSyncCoordinator::syncFinished);

    QVERIFY(coordinator.startSync(validSession()));

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> stalePullSocket(server.nextPendingConnection());
    QCOMPARE(readHttpRequest(stalePullSocket.get()).startsWith("GET /events?after=4 "), true);
    writeJsonResponse(stalePullSocket.get(), 409, QJsonObject{
        { QStringLiteral("error"), QStringLiteral("stale_cursor") },
        { QStringLiteral("message"), QStringLiteral("Re-bootstrap is required.") },
        { QStringLiteral("rebootstrapRequired"), true },
        { QStringLiteral("firstAvailableSequence"), 8 }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> bootstrapSocket(server.nextPendingConnection());
    QCOMPARE(readHttpRequest(bootstrapSocket.get()).startsWith("GET /bootstrap "), true);
    const QJsonObject newerPayload{
        { QStringLiteral("id"), offlineGroup.syncId },
        { QStringLiteral("ownerUserId"), DLTestSupport::testUserId() },
        { QStringLiteral("name"), QStringLiteral("Newer server edit") },
        { QStringLiteral("color_hex"), QStringLiteral("#2F80ED") },
        { QStringLiteral("created_at"), olderTimestamp },
        { QStringLiteral("updated_at"), newerTimestamp }
    };
    writeJsonResponse(bootstrapSocket.get(), 200, QJsonObject{
        { QStringLiteral("serverSequence"), 10 },
        { QStringLiteral("state"), QJsonObject{
            { QStringLiteral("groups"), QJsonArray{newerPayload} },
            { QStringLiteral("words"), QJsonArray{} },
            { QStringLiteral("reviewStats"), QJsonArray{} },
            { QStringLiteral("learningSettings"), QJsonArray{} },
            { QStringLiteral("tombstones"), QJsonArray{} }
        } }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> replaySocket(server.nextPendingConnection());
    const QByteArray replayRequest = readHttpRequest(replaySocket.get());
    QVERIFY(replayRequest.startsWith("POST /events "));
    const QJsonArray replayedEvents = httpJsonBody(replayRequest).value(QStringLiteral("events")).toArray();
    QCOMPARE(replayedEvents.size(), 1);
    const QJsonObject replayedEvent = replayedEvents.at(0).toObject();
    QCOMPARE(replayedEvent.value(QStringLiteral("eventId")).toString(), originalEventId);
    QCOMPARE(replayedEvent.value(QStringLiteral("updatedAt")).toString(), olderTimestamp);
    QCOMPARE(replayedEvent.value(QStringLiteral("payload")).toObject().value(QStringLiteral("updated_at")).toString(),
             olderTimestamp);
    QCOMPARE(replayedEvent.value(QStringLiteral("occurredAt")).toInteger(), olderSeconds);
    writeJsonResponse(replaySocket.get(), 200, QJsonObject{
        { QStringLiteral("accepted"), QJsonArray{QJsonObject{
            { QStringLiteral("eventId"), originalEventId },
            { QStringLiteral("serverSequence"), 11 },
            { QStringLiteral("replayed"), false },
            { QStringLiteral("canonical"), QJsonObject{
                { QStringLiteral("operation"), QStringLiteral("update") },
                { QStringLiteral("state"), newerPayload }
            } }
        }} },
        { QStringLiteral("rejected"), QJsonArray{} }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> catchUpSocket(server.nextPendingConnection());
    QCOMPARE(readHttpRequest(catchUpSocket.get()).startsWith("GET /events?after=10 "), true);
    writeJsonResponse(catchUpSocket.get(), 200, QJsonObject{
        { QStringLiteral("events"), QJsonArray{QJsonObject{
            { QStringLiteral("serverSequence"), 11 },
            { QStringLiteral("eventId"), originalEventId },
            { QStringLiteral("deviceId"), DLTestSupport::testDeviceId() },
            { QStringLiteral("entityType"), QStringLiteral("group") },
            { QStringLiteral("entityId"), offlineGroup.syncId },
            { QStringLiteral("operation"), QStringLiteral("update") },
            { QStringLiteral("baseVersion"), 0 },
            { QStringLiteral("occurredAt"), QDateTime::fromString(newerTimestamp, Qt::ISODate).toSecsSinceEpoch() },
            { QStringLiteral("payload"), newerPayload }
        }} },
        { QStringLiteral("nextSequence"), 11 },
        { QStringLiteral("hasMore"), false },
        { QStringLiteral("cursorGapDetected"), false },
        { QStringLiteral("firstAvailableSequence"), 8 }
    });

    QTRY_VERIFY(server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> cursorSocket(server.nextPendingConnection());
    const QByteArray cursorRequest = readHttpRequest(cursorSocket.get());
    QVERIFY(cursorRequest.startsWith("POST /events/cursor "));
    QCOMPARE(httpJsonBody(cursorRequest).value(QStringLiteral("consumedSequence")).toInteger(), qint64(11));
    writeJsonResponse(cursorSocket.get(), 200, QJsonObject{
        { QStringLiteral("consumedSequence"), 11 }
    });

    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(DLDatabaseManager::instance().remoteCursor(), qint64(11));
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM sync_outbox_events;")), 0);
    const QVariantMap converged = DLDatabaseManager::instance().fetchGroupById(offlineGroup.syncId);
    QCOMPARE(converged.value(QStringLiteral("name")).toString(), QStringLiteral("Newer server edit"));
    QCOMPARE(converged.value(QStringLiteral("updated_at")).toLongLong(),
             QDateTime::fromString(newerTimestamp, Qt::ISODate).toSecsSinceEpoch());
}

QTEST_MAIN(TestSyncCoordinator)
#include "TestSyncCoordinator.moc"
