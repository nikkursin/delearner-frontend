#ifndef DLGROUPSERVICE_H
#define DLGROUPSERVICE_H

#include <QString>
#include <QVariantList>

class DLDatabaseManager;

class DLGroupService
{
public:
    explicit DLGroupService(DLDatabaseManager& database);

    QVariantList availableGroups();
    QString createGroup(const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    bool updateGroup(const QString& syncId, const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    bool deleteGroup(const QString& syncId);
    int groupCount();
    QString lastError() const;

private:
    QString normalizedColor(const QString& colorHex) const;

    DLDatabaseManager& m_database;
    QString m_lastError;
};

#endif // DLGROUPSERVICE_H
