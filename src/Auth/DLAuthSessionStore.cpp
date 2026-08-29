#include "DLAuthSessionStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <utility>

namespace {
QString dateToString(const QDateTime& value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime dateFromString(const QString& value)
{
    return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}
}

DLAuthSessionStore::DLAuthSessionStore(QString storagePath)
    : m_storagePath(std::move(storagePath))
{
}

QString DLAuthSessionStore::storagePath() const
{
    return m_storagePath;
}

QString DLAuthSessionStore::lastError() const
{
    return m_lastError;
}

std::optional<DLAuthSession> DLAuthSessionStore::load() const
{
    setLastError(QString());

    QFile file(m_storagePath);
    if (!file.exists()) {
        return std::nullopt;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("Could not read saved authentication session."));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Saved authentication session is invalid."));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    DLAuthSession session;
    session.userId = object.value(QStringLiteral("userId")).toString();
    session.email = object.value(QStringLiteral("email")).toString();
    session.sessionToken = object.value(QStringLiteral("sessionToken")).toString();
    session.tokenType = object.value(QStringLiteral("tokenType")).toString(QStringLiteral("Bearer"));
    session.deviceId = object.value(QStringLiteral("deviceId")).toString();
    session.deviceDisplayName = object.value(QStringLiteral("deviceDisplayName")).toString();
    session.lastAuthenticatedAtUtc = dateFromString(object.value(QStringLiteral("lastAuthenticatedAtUtc")).toString());
    session.priorSuccessfulAuthentication =
        object.value(QStringLiteral("priorSuccessfulAuthentication")).toBool(false);

    if (!session.allowsOfflineFreeCore()) {
        setLastError(QStringLiteral("Saved authentication session does not contain prior successful authentication."));
        return std::nullopt;
    }

    return session;
}

bool DLAuthSessionStore::saveSuccessfulSession(const DLAuthSession& session)
{
    DLAuthSession sessionToSave = session;
    sessionToSave.priorSuccessfulAuthentication = true;
    sessionToSave.tokenType = sessionToSave.tokenType.trimmed().isEmpty()
        ? QStringLiteral("Bearer")
        : sessionToSave.tokenType.trimmed();

    if (!sessionToSave.hasSessionCredentials()) {
        setLastError(QStringLiteral("Authentication session requires a user id and session token."));
        return false;
    }

    if (!sessionToSave.lastAuthenticatedAtUtc.isValid()) {
        sessionToSave.lastAuthenticatedAtUtc = QDateTime::currentDateTimeUtc();
    }

    return writeSession(sessionToSave);
}

bool DLAuthSessionStore::clearSessionCredentialsPreservingOfflineAccess()
{
    const std::optional<DLAuthSession> current = load();
    if (!current.has_value()) {
        return true;
    }

    DLAuthSession session = current.value();
    session.sessionToken.clear();
    session.tokenType = QStringLiteral("Bearer");
    session.priorSuccessfulAuthentication = true;
    return writeSession(session);
}

bool DLAuthSessionStore::clearAll()
{
    setLastError(QString());

    QFile file(m_storagePath);
    if (!file.exists()) {
        return true;
    }

    if (!file.remove()) {
        setLastError(QStringLiteral("Could not clear saved authentication session."));
        return false;
    }

    return true;
}

bool DLAuthSessionStore::writeSession(const DLAuthSession& session)
{
    setLastError(QString());

    const QFileInfo info(m_storagePath);
    const QDir parentDir = info.absoluteDir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        setLastError(QStringLiteral("Could not create authentication session directory."));
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("userId"), session.userId);
    object.insert(QStringLiteral("email"), session.email);
    object.insert(QStringLiteral("sessionToken"), session.sessionToken);
    object.insert(QStringLiteral("tokenType"), session.tokenType);
    object.insert(QStringLiteral("deviceId"), session.deviceId);
    object.insert(QStringLiteral("deviceDisplayName"), session.deviceDisplayName);
    object.insert(QStringLiteral("lastAuthenticatedAtUtc"), dateToString(session.lastAuthenticatedAtUtc));
    object.insert(QStringLiteral("priorSuccessfulAuthentication"), session.priorSuccessfulAuthentication);

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("Could not write authentication session."));
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setLastError(QStringLiteral("Could not commit authentication session."));
        return false;
    }

    return true;
}

void DLAuthSessionStore::setLastError(const QString& error) const
{
    m_lastError = error;
}
