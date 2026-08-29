#ifndef DLAUTHSESSIONSTORE_H
#define DLAUTHSESSIONSTORE_H

#include <optional>

#include <QString>

#include "DLAuthSession.h"

class DLAuthSessionStore
{
public:
    explicit DLAuthSessionStore(QString storagePath);

    QString storagePath() const;
    QString lastError() const;

    std::optional<DLAuthSession> load() const;
    bool saveSuccessfulSession(const DLAuthSession& session);
    bool clearSessionCredentialsPreservingOfflineAccess();
    bool clearAll();

private:
    bool writeSession(const DLAuthSession& session);
    void setLastError(const QString& error) const;

    QString m_storagePath;
    mutable QString m_lastError;
};

#endif // DLAUTHSESSIONSTORE_H
