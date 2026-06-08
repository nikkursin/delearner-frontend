#ifndef DLWORDGROUP_H
#define DLWORDGROUP_H

#include <QString>
#include <QtGlobal>

struct DLWordGroup
{
    int id = -1;
    QString name;
    QString colorHex = QStringLiteral("#3366CC");
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    int wordCount = 0;
};

#endif // DLWORDGROUP_H
