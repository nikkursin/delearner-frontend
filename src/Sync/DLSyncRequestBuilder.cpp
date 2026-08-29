#include "DLSyncRequestBuilder.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace {
void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

QUrl resolvedUrl(QUrl baseUrl, const QString& path)
{
    if (!baseUrl.path().endsWith(QLatin1Char('/'))) {
        baseUrl.setPath(baseUrl.path() + QLatin1Char('/'));
    }
    return baseUrl.resolved(QUrl(path.startsWith(QLatin1Char('/')) ? path.mid(1) : path));
}
}

QNetworkRequest DLSyncRequestBuilder::jsonRequest(const QUrl& apiBaseUrl,
                                                  const QString& path,
                                                  const DLAuthSession& session,
                                                  QString* error)
{
    if (!validateSessionContext(session, error)) {
        return {};
    }

    QNetworkRequest request(resolvedUrl(apiBaseUrl, path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", session.authorizationHeader().toUtf8());
    return request;
}

QByteArray DLSyncRequestBuilder::cursorAdvanceBody(const DLAuthSession& session,
                                                   qint64 consumedSequence,
                                                   QString* error)
{
    if (!validateSessionContext(session, error)) {
        return {};
    }
    if (consumedSequence < 0) {
        setError(error, QStringLiteral("Consumed sequence must be non-negative."));
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("deviceId"), session.deviceId.trimmed());
    body.insert(QStringLiteral("consumedSequence"), consumedSequence);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

DLSyncEventEnvelope DLSyncRequestBuilder::eventWithSessionContext(DLSyncEventEnvelope event,
                                                                  const DLAuthSession& session,
                                                                  QString* error)
{
    if (!validateSessionContext(session, error)) {
        return {};
    }

    event.deviceId = session.deviceId.trimmed();
    event.authenticatedUserId = session.userId.trimmed();
    return event;
}

bool DLSyncRequestBuilder::validateSessionContext(const DLAuthSession& session, QString* error)
{
    if (!session.hasSessionCredentials()) {
        setError(error, QStringLiteral("Authenticated sync requests require session credentials."));
        return false;
    }
    if (!session.hasRegisteredDevice()) {
        setError(error, QStringLiteral("Authenticated sync requests require a registered device id."));
        return false;
    }

    setError(error, QString());
    return true;
}
