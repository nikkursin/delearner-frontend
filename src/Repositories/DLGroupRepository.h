#ifndef DLGROUPREPOSITORY_H
#define DLGROUPREPOSITORY_H

#include <QList>

#include "../Models/DLWordGroup.h"

class DLDatabaseManager;

class DLGroupRepository
{
public:
    explicit DLGroupRepository(DLDatabaseManager& database);

    QString insertGroup(const DLWordGroup& group);
    bool updateGroup(const DLWordGroup& group);
    bool deleteGroup(const QString& id);
    QList<DLWordGroup> fetchAllGroups();
    DLWordGroup fetchGroupById(const QString& id);
    int getGroupCount();

private:
    DLDatabaseManager& m_database;
};

#endif // DLGROUPREPOSITORY_H
