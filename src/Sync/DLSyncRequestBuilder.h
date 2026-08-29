#ifndef DLSYNCREQUESTBUILDER_H
#define DLSYNCREQUESTBUILDER_H

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

#include "Auth/DLAuthSession.h"
#include "DLSyncEventSerializer.h"

class DLSyncRequestBuilder
{
public:
    static QNetworkRequest jsonRequest(const QUrl& apiBaseUrl,
                                       const QString& path,
                                       const DLAuthSession& session,
                                       QString* error = nullptr);
    static QByteArray cursorAdvanceBody(const DLAuthSession& session,
                                        qint64 consumedSequence,
                                        QString* error = nullptr);
    static DLSyncEventEnvelope eventWithSessionContext(DLSyncEventEnvelope event,
                                                       const DLAuthSession& session,
                                                       QString* error = nullptr);

private:
    static bool validateSessionContext(const DLAuthSession& session, QString* error);
};

#endif // DLSYNCREQUESTBUILDER_H
