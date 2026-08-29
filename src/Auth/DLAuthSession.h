#ifndef DLAUTHSESSION_H
#define DLAUTHSESSION_H

#include <QDateTime>
#include <QString>

struct DLAuthSession
{
    QString userId;
    QString email;
    QString sessionToken;
    QString tokenType = QStringLiteral("Bearer");
    QString deviceId;
    QString deviceDisplayName;
    QDateTime lastAuthenticatedAtUtc;
    bool priorSuccessfulAuthentication = false;

    bool hasSessionCredentials() const;
    bool allowsOfflineFreeCore() const;
    bool hasRegisteredDevice() const;
    QString authorizationHeader() const;
};

#endif // DLAUTHSESSION_H
