#ifndef DLBOOTSTRAPSTATERESTORER_H
#define DLBOOTSTRAPSTATERESTORER_H

#include <QByteArray>
#include <QVariantMap>

class DLDatabaseManager;

class DLBootstrapStateRestorer
{
public:
    explicit DLBootstrapStateRestorer(DLDatabaseManager& database);

    bool restoreFromJson(const QByteArray& json, QString* error = nullptr);
    bool restoreFromResponse(const QVariantMap& response, QString* error = nullptr);

private:
    DLDatabaseManager& m_database;
};

#endif // DLBOOTSTRAPSTATERESTORER_H
