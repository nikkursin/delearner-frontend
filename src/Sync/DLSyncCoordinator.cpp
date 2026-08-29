#include "DLSyncCoordinator.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariantMap>

#include "Managers/DLDatabaseManager.h"
#include "Sync/DLBootstrapStateRestorer.h"
#include "Sync/DLRemoteChangeReconciler.h"
#include "Sync/DLSyncRequestBuilder.h"

namespace {
void deleteReplyLater(QNetworkReply* reply)
{
    if (reply) {
        reply->deleteLater();
    }
}

QString networkReplyError(QNetworkReply* reply, const QByteArray& body, const QString& fallback)
{
    if (!reply) {
        return fallback;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QString message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
        const QString error = object.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            return error;
        }
    }

    const QString replyError = reply->errorString();
    return replyError.isEmpty() ? fallback : replyError;
}

bool httpSucceeded(QNetworkReply* reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return reply->error() == QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 300));
}

qint64 secondsFromContractTimestamp(const QJsonValue& value)
{
    if (value.isDouble()) {
        const qint64 seconds = value.toVariant().toLongLong();
        return seconds > 0 ? seconds : 0;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return 0;
    }

    QDateTime timestamp = QDateTime::fromString(text, Qt::ISODate);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(text, Qt::ISODateWithMs);
    }

    const qint64 seconds = timestamp.isValid() ? timestamp.toUTC().toSecsSinceEpoch() : 0;
    return seconds > 0 ? seconds : 0;
}

qint64 occurredAtForEvent(const QJsonObject& event)
{
    qint64 seconds = secondsFromContractTimestamp(event.value(QStringLiteral("updatedAt")));
    if (seconds > 0) {
        return seconds;
    }

    const QJsonObject body = event.value(event.value(QStringLiteral("operation")).toString() == QStringLiteral("delete")
                                        ? QStringLiteral("tombstone")
                                        : QStringLiteral("payload")).toObject();
    seconds = secondsFromContractTimestamp(body.value(QStringLiteral("updated_at")));
    if (seconds > 0) {
        return seconds;
    }
    return secondsFromContractTimestamp(body.value(QStringLiteral("updatedAt")));
}

bool appendBackendUploadEvent(const QString& envelopeJson,
                              QJsonArray* events,
                              QStringList* eventIds,
                              QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(envelopeJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Pending sync outbox event is not valid JSON.");
        }
        return false;
    }

    QJsonObject event = document.object();
    const QString eventId = event.value(QStringLiteral("eventId")).toString().trimmed();
    if (eventId.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Pending sync outbox event is missing eventId.");
        }
        return false;
    }

    if (!event.contains(QStringLiteral("baseVersion"))) {
        event.insert(QStringLiteral("baseVersion"), 0);
    }
    if (!event.contains(QStringLiteral("occurredAt"))) {
        const qint64 occurredAt = occurredAtForEvent(event);
        if (occurredAt <= 0) {
            if (error) {
                *error = QStringLiteral("Pending sync outbox event is missing a valid occurredAt timestamp.");
            }
            return false;
        }
        event.insert(QStringLiteral("occurredAt"), occurredAt);
    }

    events->append(event);
    eventIds->append(eventId);
    return true;
}

QByteArray compactJson(const QJsonValue& value)
{
    if (value.isObject()) {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }
    if (value.isArray()) {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    }
    return {};
}
}

DLSyncCoordinator::DLSyncCoordinator(DLDatabaseManager& database,
                                     const QUrl& apiBaseUrl,
                                     QObject* parent)
    : DLSyncCoordinator(database, nullptr, apiBaseUrl, parent)
{
    m_ownedNetwork = std::make_unique<QNetworkAccessManager>(this);
    m_network = m_ownedNetwork.get();
}

DLSyncCoordinator::DLSyncCoordinator(DLDatabaseManager& database,
                                     QNetworkAccessManager* network,
                                     const QUrl& apiBaseUrl,
                                     QObject* parent)
    : QObject(parent)
    , m_database(database)
    , m_apiBaseUrl(apiBaseUrl)
    , m_network(network)
{
}

DLSyncCoordinator::~DLSyncCoordinator() = default;

bool DLSyncCoordinator::syncInProgress() const
{
    return m_syncInProgress;
}

QString DLSyncCoordinator::lastError() const
{
    return m_lastError;
}

bool DLSyncCoordinator::startSync(const DLAuthSession& session)
{
    if (m_syncInProgress) {
        setLastError(QStringLiteral("Sync is already in progress."));
        return false;
    }
    if (!m_network) {
        setLastError(QStringLiteral("Sync coordinator requires a network access manager."));
        return false;
    }

    QString error;
    const QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(m_apiBaseUrl, QStringLiteral("/events"), session, &error);
    if (!error.isEmpty() || !request.url().isValid()) {
        setLastError(error.isEmpty() ? QStringLiteral("Sync coordinator could not build an authenticated request.") : error);
        return false;
    }

    m_session = session;
    m_pullHasMore = false;
    setLastError(QString());
    setSyncInProgress(true);
    emit syncStarted();
    pushPendingOutboxEvents();
    return true;
}

bool DLSyncCoordinator::startBootstrapThenSync(const DLAuthSession& session)
{
    if (m_syncInProgress) {
        setLastError(QStringLiteral("Sync is already in progress."));
        return false;
    }
    if (!m_network) {
        setLastError(QStringLiteral("Sync coordinator requires a network access manager."));
        return false;
    }

    QString error;
    const QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(m_apiBaseUrl, QStringLiteral("/bootstrap"), session, &error);
    if (!error.isEmpty() || !request.url().isValid()) {
        setLastError(error.isEmpty() ? QStringLiteral("Sync coordinator could not build an authenticated bootstrap request.") : error);
        return false;
    }

    m_session = session;
    m_pullHasMore = false;
    setLastError(QString());
    setSyncInProgress(true);
    emit syncStarted();

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleBootstrapReply(reply);
    });
    return true;
}

void DLSyncCoordinator::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

void DLSyncCoordinator::setSyncInProgress(bool syncInProgress)
{
    if (m_syncInProgress == syncInProgress) {
        return;
    }
    m_syncInProgress = syncInProgress;
    emit syncInProgressChanged();
}

void DLSyncCoordinator::finishSync(bool success, const QString& error)
{
    setLastError(success ? QString() : error);
    setSyncInProgress(false);
    emit syncFinished(success);
}

void DLSyncCoordinator::pushPendingOutboxEvents()
{
    const QVariantList rows = m_database.selectRows(QStringLiteral(R"(
        SELECT event_id, envelope_json
        FROM sync_outbox_events
        WHERE next_attempt_after IS NULL OR next_attempt_after <= :now
        ORDER BY created_at ASC, event_id ASC;
    )"), {{ QStringLiteral(":now"), DLDatabaseManager::currentUnixTime() }});

    if (!m_database.lastError().isEmpty()) {
        finishSync(false, m_database.lastError());
        return;
    }
    if (rows.isEmpty()) {
        pullRemoteChanges();
        return;
    }

    QJsonArray events;
    QStringList eventIds;
    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        QString error;
        if (!appendBackendUploadEvent(row.value(QStringLiteral("envelope_json")).toString(), &events, &eventIds, &error)) {
            finishSync(false, error);
            return;
        }
    }

    QJsonObject body;
    body.insert(QStringLiteral("events"), events);

    QString error;
    QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(m_apiBaseUrl, QStringLiteral("/events"), m_session, &error);
    if (!error.isEmpty()) {
        finishSync(false, error);
        return;
    }

    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, eventIds]() {
        handlePushReply(reply, eventIds);
    });
}

void DLSyncCoordinator::handlePushReply(QNetworkReply* reply, const QStringList& eventIds)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(reply, deleteReplyLater);
    const QByteArray body = reply->readAll();
    if (!httpSucceeded(reply)) {
        const QString error = networkReplyError(reply, body, QStringLiteral("Sync upload failed."));
        for (const QString& eventId : eventIds) {
            m_database.recordSyncOutboxSendAttempt(eventId, 0, error);
        }
        finishSync(false, error);
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        finishSync(false, QStringLiteral("Sync upload response was not valid JSON."));
        return;
    }

    bool hasRejectedEvents = false;
    const QJsonObject response = document.object();
    const QJsonArray accepted = response.value(QStringLiteral("accepted")).toArray();
    for (const QJsonValue& value : accepted) {
        const QJsonObject acceptedEvent = value.toObject();
        const QString eventId = acceptedEvent.value(QStringLiteral("eventId")).toString().trimmed();
        const qint64 serverSequence = acceptedEvent.value(QStringLiteral("serverSequence")).toVariant().toLongLong();
        if (eventId.isEmpty()) {
            finishSync(false, QStringLiteral("Sync upload acknowledgement was missing eventId."));
            return;
        }

        const QString canonicalResult = QString::fromUtf8(compactJson(acceptedEvent.value(QStringLiteral("canonical"))));
        const QString diagnostic = QString::fromUtf8(compactJson(value));
        if (!m_database.acknowledgeSyncOutboxEvent(eventId, serverSequence, canonicalResult, diagnostic)) {
            finishSync(false, m_database.lastError());
            return;
        }
    }

    const QJsonArray rejected = response.value(QStringLiteral("rejected")).toArray();
    for (const QJsonValue& value : rejected) {
        hasRejectedEvents = true;
        const QJsonObject rejectedEvent = value.toObject();
        const QString eventId = rejectedEvent.value(QStringLiteral("eventId")).toString().trimmed();
        const QString reason = rejectedEvent.value(QStringLiteral("reason")).toString(QStringLiteral("rejected"));
        if (!eventId.isEmpty()) {
            m_database.recordSyncOutboxSendAttempt(eventId, 0, reason);
        }
    }

    if (hasRejectedEvents) {
        finishSync(false, QStringLiteral("One or more sync events were rejected by the backend."));
        return;
    }

    pullRemoteChanges();
}

void DLSyncCoordinator::handleBootstrapReply(QNetworkReply* reply)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(reply, deleteReplyLater);
    const QByteArray body = reply->readAll();
    if (!httpSucceeded(reply)) {
        finishSync(false, networkReplyError(reply, body, QStringLiteral("Bootstrap state download failed.")));
        return;
    }

    DLBootstrapStateRestorer restorer(m_database);
    QString error;
    if (!restorer.restoreFromJson(body, &error)) {
        finishSync(false, error);
        return;
    }

    pullRemoteChanges();
}

void DLSyncCoordinator::pullRemoteChanges()
{
    const qint64 currentCursor = m_database.remoteCursor();
    if (!m_database.lastError().isEmpty()) {
        finishSync(false, m_database.lastError());
        return;
    }

    QString error;
    QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(
        m_apiBaseUrl,
        QStringLiteral("/events?after=%1").arg(currentCursor),
        m_session,
        &error);
    if (!error.isEmpty()) {
        finishSync(false, error);
        return;
    }

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handlePullReply(reply);
    });
}

void DLSyncCoordinator::handlePullReply(QNetworkReply* reply)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(reply, deleteReplyLater);
    const QByteArray body = reply->readAll();
    if (!httpSucceeded(reply)) {
        finishSync(false, networkReplyError(reply, body, QStringLiteral("Sync download failed.")));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        finishSync(false, QStringLiteral("Sync download response was not valid JSON."));
        return;
    }

    const QVariantMap response = document.object().toVariantMap();
    const qint64 previousCursor = m_database.remoteCursor();
    const qint64 nextSequence = response.value(QStringLiteral("nextSequence")).toLongLong();
    m_pullHasMore = response.value(QStringLiteral("hasMore")).toBool();
    DLRemoteChangeReconciler reconciler(m_database);
    QString error;
    if (!reconciler.applyDownloadedEventsAndAdvanceCursor(response, &error)) {
        finishSync(false, error);
        return;
    }

    if (nextSequence > previousCursor) {
        acknowledgeRemoteCursor(nextSequence);
        return;
    }

    finishSync(true);
}

void DLSyncCoordinator::acknowledgeRemoteCursor(qint64 consumedSequence)
{
    QString error;
    QNetworkRequest request = DLSyncRequestBuilder::jsonRequest(m_apiBaseUrl, QStringLiteral("/events/cursor"), m_session, &error);
    if (!error.isEmpty()) {
        finishSync(false, error);
        return;
    }

    const QByteArray body = DLSyncRequestBuilder::cursorAdvanceBody(m_session, consumedSequence, &error);
    if (!error.isEmpty() || body.isEmpty()) {
        finishSync(false, error.isEmpty() ? QStringLiteral("Could not build sync cursor acknowledgement.") : error);
        return;
    }

    QNetworkReply* reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCursorReply(reply);
    });
}

void DLSyncCoordinator::handleCursorReply(QNetworkReply* reply)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(reply, deleteReplyLater);
    const QByteArray body = reply->readAll();
    if (!httpSucceeded(reply)) {
        finishSync(false, networkReplyError(reply, body, QStringLiteral("Sync cursor acknowledgement failed.")));
        return;
    }

    if (m_pullHasMore) {
        pullRemoteChanges();
        return;
    }

    finishSync(true);
}
