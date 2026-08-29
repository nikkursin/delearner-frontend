#include "DLAuthSession.h"

#include <QUuid>

bool DLAuthSession::hasSessionCredentials() const
{
    return priorSuccessfulAuthentication
        && !userId.trimmed().isEmpty()
        && !sessionToken.trimmed().isEmpty();
}

bool DLAuthSession::allowsOfflineFreeCore() const
{
    return priorSuccessfulAuthentication && !userId.trimmed().isEmpty();
}

bool DLAuthSession::hasRegisteredDevice() const
{
    return !QUuid(deviceId.trimmed()).isNull();
}

QString DLAuthSession::authorizationHeader() const
{
    if (!hasSessionCredentials()) {
        return {};
    }

    const QString normalizedTokenType = tokenType.trimmed().isEmpty()
        ? QStringLiteral("Bearer")
        : tokenType.trimmed();
    return QStringLiteral("%1 %2").arg(normalizedTokenType, sessionToken.trimmed());
}
