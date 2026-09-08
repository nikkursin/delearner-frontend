#include "DLClientAuthService.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QtGlobal>
#include <QUuid>
#include <utility>

namespace {
QUrl resolvedUrl(QUrl baseUrl, const QString& path)
{
    if (!baseUrl.path().endsWith(QLatin1Char('/'))) {
        baseUrl.setPath(baseUrl.path() + QLatin1Char('/'));
    }
    return baseUrl.resolved(QUrl(path.startsWith(QLatin1Char('/')) ? path.mid(1) : path));
}

QString normalizedUuid(const QString& value)
{
    const QUuid uuid(value.trimmed());
    return uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
}
}

DLClientAuthService::DLClientAuthService(QString storagePath, QUrl apiBaseUrl, QObject* parent)
    : QObject(parent)
    , m_store(std::move(storagePath))
    , m_deviceIdentityStore(QStringLiteral("%1.device.json").arg(m_store.storagePath()))
    , m_apiBaseUrl(std::move(apiBaseUrl))
    , m_network(std::make_unique<QNetworkAccessManager>())
{
}

DLClientAuthService::~DLClientAuthService() = default;

QUrl DLClientAuthService::defaultApiBaseUrl()
{
    const QByteArray configured = qgetenv("DELEARNER_API_BASE_URL");
    if (!configured.trimmed().isEmpty()) {
        return QUrl(QString::fromUtf8(configured.trimmed()));
    }

    return QUrl(QStringLiteral("http://192.168.178.75:8080"));
}

QString DLClientAuthService::stateName(StartupState state)
{
    switch (state) {
    case StartupState::AuthenticationRequired:
        return QStringLiteral("authentication_required");
    case StartupState::AuthenticatedSession:
        return QStringLiteral("authenticated_session");
    case StartupState::ReauthenticationRequired:
        return QStringLiteral("reauthentication_required");
    case StartupState::OfflineFreeCore:
        return QStringLiteral("offline_free_core");
    }

    return QStringLiteral("authentication_required");
}

QString DLClientAuthService::lastError() const
{
    return m_lastError;
}

QString DLClientAuthService::sessionToken() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() ? session->sessionToken : QString();
}

QString DLClientAuthService::userId() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() ? session->userId : QString();
}

QString DLClientAuthService::email() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() ? session->email : QString();
}

QString DLClientAuthService::deviceId() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() ? session->deviceId : QString();
}

bool DLClientAuthService::hasPriorSuccessfulAuthentication() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() && session->allowsOfflineFreeCore();
}

bool DLClientAuthService::hasSessionCredentials() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() && session->hasSessionCredentials();
}

bool DLClientAuthService::hasRegisteredDevice() const
{
    const std::optional<DLAuthSession> session = currentSession();
    return session.has_value() && session->hasRegisteredDevice();
}

bool DLClientAuthService::canUseFreeCoreOffline() const
{
    return hasPriorSuccessfulAuthentication();
}

bool DLClientAuthService::clearSavedSession()
{
    if (!m_store.clearAll()) {
        setLastError(m_store.lastError());
        return false;
    }

    setLastError(QString());
    return true;
}

DLClientAuthService::StartupState DLClientAuthService::startupState(bool networkAvailable) const
{
    const std::optional<DLAuthSession> session = currentSession();
    if (!session.has_value() || !session->allowsOfflineFreeCore()) {
        return StartupState::AuthenticationRequired;
    }

    if (!networkAvailable) {
        return StartupState::OfflineFreeCore;
    }

    return session->hasSessionCredentials()
        ? StartupState::AuthenticatedSession
        : StartupState::ReauthenticationRequired;
}

void DLClientAuthService::signIn(const QString& email, const QString& password)
{
    submitEmailPassword(QStringLiteral("/api/v1/auth/sign-in"), email, password);
}

void DLClientAuthService::registerAccount(const QString& email, const QString& password)
{
    submitEmailPassword(QStringLiteral("/api/v1/auth/register"), email, password);
}

void DLClientAuthService::recordOnlineSessionRejected()
{
    if (!m_store.clearSessionCredentialsPreservingOfflineAccess()) {
        setLastError(m_store.lastError());
        return;
    }

    setLastError(QString());
}

void DLClientAuthService::submitEmailPassword(const QString& path, const QString& email, const QString& password)
{
    const QString trimmedEmail = email.trimmed();
    if (trimmedEmail.isEmpty() || password.isEmpty()) {
        setLastError(QStringLiteral("Email and password are required."));
        emit authFailed(m_lastError);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("email"), trimmedEmail);
    body.insert(QStringLiteral("password"), password);

    QNetworkRequest request(resolvedUrl(m_apiBaseUrl, path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmedEmail]() {
        handleAuthReply(reply, trimmedEmail);
    });
}

void DLClientAuthService::handleAuthReply(QNetworkReply* reply, const QString& email)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(
        reply,
        [](QNetworkReply* guardedReply) { guardedReply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
        setLastError(reply->errorString());
        emit authFailed(m_lastError);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Authentication response was not valid JSON."));
        emit authFailed(m_lastError);
        return;
    }

    const QJsonObject object = document.object();
    DLAuthSession session;
    session.userId = object.value(QStringLiteral("userId")).toString();
    session.email = email;
    session.sessionToken = object.value(QStringLiteral("sessionToken")).toString();
    session.tokenType = object.value(QStringLiteral("tokenType")).toString(QStringLiteral("Bearer"));
    session.lastAuthenticatedAtUtc = QDateTime::currentDateTimeUtc();
    session.priorSuccessfulAuthentication = true;

    if (!session.hasSessionCredentials()) {
        setLastError(QStringLiteral("Authentication response did not include valid session credentials."));
        emit authFailed(m_lastError);
        return;
    }

    registerAuthenticatedDevice(session);
}

void DLClientAuthService::registerAuthenticatedDevice(const DLAuthSession& session)
{
    const QString deviceId = m_deviceIdentityStore.deviceId();
    if (deviceId.isEmpty()) {
        setLastError(m_deviceIdentityStore.lastError());
        emit authFailed(m_lastError);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("deviceId"), deviceId);
    body.insert(QStringLiteral("displayName"), defaultDeviceDisplayName());

    QNetworkRequest request(resolvedUrl(m_apiBaseUrl, QStringLiteral("/api/v1/devices/register")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", session.authorizationHeader().toUtf8());

    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, session]() {
        handleDeviceRegistrationReply(reply, session);
    });
}

void DLClientAuthService::handleDeviceRegistrationReply(QNetworkReply* reply, DLAuthSession session)
{
    const std::unique_ptr<QNetworkReply, void (*)(QNetworkReply*)> replyGuard(
        reply,
        [](QNetworkReply* guardedReply) { guardedReply->deleteLater(); });

    if (reply->error() != QNetworkReply::NoError) {
        setLastError(reply->errorString());
        emit authFailed(m_lastError);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Device registration response was not valid JSON."));
        emit authFailed(m_lastError);
        return;
    }

    const QJsonObject object = document.object();
    const QString registeredDeviceId = normalizedUuid(object.value(QStringLiteral("deviceId")).toString());
    if (registeredDeviceId.isEmpty()) {
        setLastError(QStringLiteral("Device registration response did not include a valid device id."));
        emit authFailed(m_lastError);
        return;
    }

    session.deviceId = registeredDeviceId;
    session.deviceDisplayName = object.value(QStringLiteral("displayName")).toString();

    if (!m_store.saveSuccessfulSession(session)) {
        setLastError(m_store.lastError());
        emit authFailed(m_lastError);
        return;
    }

    setLastError(QString());
    emit authSucceeded();
}

QString DLClientAuthService::defaultDeviceDisplayName() const
{
    const QString hostName = QSysInfo::machineHostName().trimmed();
    return hostName.isEmpty() ? QStringLiteral("DE Learner device") : hostName;
}

std::optional<DLAuthSession> DLClientAuthService::currentSession() const
{
    return m_store.load();
}

void DLClientAuthService::setLastError(const QString& error)
{
    m_lastError = error;
}
