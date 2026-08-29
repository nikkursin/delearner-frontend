#ifndef DLCLIENTAUTHSERVICE_H
#define DLCLIENTAUTHSERVICE_H

#include <memory>
#include <optional>

#include <QObject>
#include <QUrl>

#include "DLAuthSession.h"
#include "DLAuthSessionStore.h"
#include "Sync/DLDeviceIdentityStore.h"

class QNetworkAccessManager;
class QNetworkReply;

class DLClientAuthService : public QObject
{
    Q_OBJECT

public:
    enum class StartupState
    {
        AuthenticationRequired,
        AuthenticatedSession,
        ReauthenticationRequired,
        OfflineFreeCore
    };
    Q_ENUM(StartupState)

    explicit DLClientAuthService(QString storagePath,
                                 QUrl apiBaseUrl = defaultApiBaseUrl(),
                                 QObject* parent = nullptr);
    ~DLClientAuthService() override;

    static QUrl defaultApiBaseUrl();
    static QString stateName(StartupState state);

    QString lastError() const;
    QString sessionToken() const;
    QString userId() const;
    QString email() const;
    QString deviceId() const;
    bool hasPriorSuccessfulAuthentication() const;
    bool hasSessionCredentials() const;
    bool hasRegisteredDevice() const;
    bool canUseFreeCoreOffline() const;
    std::optional<DLAuthSession> currentSession() const;
    bool clearSavedSession();

    StartupState startupState(bool networkAvailable) const;

public slots:
    void signIn(const QString& email, const QString& password);
    void registerAccount(const QString& email, const QString& password);
    void recordOnlineSessionRejected();

signals:
    void authSucceeded();
    void authFailed(const QString& message);

private:
    void submitEmailPassword(const QString& path, const QString& email, const QString& password);
    void handleAuthReply(QNetworkReply* reply, const QString& email);
    void registerAuthenticatedDevice(const DLAuthSession& session);
    void handleDeviceRegistrationReply(QNetworkReply* reply, DLAuthSession session);
    QString defaultDeviceDisplayName() const;
    void setLastError(const QString& error);

    DLAuthSessionStore m_store;
    DLDeviceIdentityStore m_deviceIdentityStore;
    QUrl m_apiBaseUrl;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QString m_lastError;
};

#endif // DLCLIENTAUTHSERVICE_H
