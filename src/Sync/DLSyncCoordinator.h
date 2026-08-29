#ifndef DLSYNCCOORDINATOR_H
#define DLSYNCCOORDINATOR_H

#include <memory>

#include <QObject>
#include <QUrl>

#include "Auth/DLAuthSession.h"

class DLDatabaseManager;
class QNetworkAccessManager;
class QNetworkReply;

class DLSyncCoordinator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncInProgress READ syncInProgress NOTIFY syncInProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DLSyncCoordinator(DLDatabaseManager& database,
                               const QUrl& apiBaseUrl,
                               QObject* parent = nullptr);
    DLSyncCoordinator(DLDatabaseManager& database,
                      QNetworkAccessManager* network,
                      const QUrl& apiBaseUrl,
                      QObject* parent = nullptr);
    ~DLSyncCoordinator() override;

    bool syncInProgress() const;
    QString lastError() const;

    bool startSync(const DLAuthSession& session);
    bool startBootstrapThenSync(const DLAuthSession& session);

signals:
    void syncStarted();
    void syncFinished(bool success);
    void syncInProgressChanged();
    void lastErrorChanged();

private:
    void setLastError(const QString& error);
    void setSyncInProgress(bool syncInProgress);
    void finishSync(bool success, const QString& error = QString());

    void pushPendingOutboxEvents();
    void handlePushReply(QNetworkReply* reply, const QStringList& eventIds);
    void handleBootstrapReply(QNetworkReply* reply);
    void pullRemoteChanges();
    void handlePullReply(QNetworkReply* reply);
    void acknowledgeRemoteCursor(qint64 consumedSequence);
    void handleCursorReply(QNetworkReply* reply);

    DLDatabaseManager& m_database;
    QUrl m_apiBaseUrl;
    std::unique_ptr<QNetworkAccessManager> m_ownedNetwork;
    QNetworkAccessManager* m_network = nullptr;
    DLAuthSession m_session;
    bool m_syncInProgress = false;
    bool m_pullHasMore = false;
    QString m_lastError;
};

#endif // DLSYNCCOORDINATOR_H
