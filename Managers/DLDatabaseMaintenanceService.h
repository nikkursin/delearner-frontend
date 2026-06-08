#ifndef DLDATABASEMAINTENANCESERVICE_H
#define DLDATABASEMAINTENANCESERVICE_H

#include <QVariantMap>

class DLDatabaseManager;
class QString;

class DLDatabaseMaintenanceService
{
public:
    explicit DLDatabaseMaintenanceService(DLDatabaseManager& database);

    QVariantMap getDatabaseStats();
    bool importDatabaseMerge(const QString& sourceDatabasePath);
    bool deleteAllData();

private:
    DLDatabaseManager& m_database;
};

#endif // DLDATABASEMAINTENANCESERVICE_H
