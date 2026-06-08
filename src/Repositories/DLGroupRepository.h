#ifndef DLGROUPREPOSITORY_H
#define DLGROUPREPOSITORY_H

#include <QList>

#include "../Models/DLWordGroup.h"

class DLDatabaseManager;

class DLGroupRepository
{
public:
    explicit DLGroupRepository(DLDatabaseManager& database);

    int insertGroup(const DLWordGroup& group);
    bool updateGroup(const DLWordGroup& group);
    bool deleteGroup(int id);
    QList<DLWordGroup> fetchAllGroups();
    DLWordGroup fetchGroupById(int id);
    int getGroupCount();

private:
    DLDatabaseManager& m_database;
};

#endif // DLGROUPREPOSITORY_H
