#ifndef DLWORDGROUP_H
#define DLWORDGROUP_H

#include <QString>
#include <QVariant>
#include <QtGlobal>

struct DLWordGroup
{
    QString id;
    QString name;
    QString colorHex = QStringLiteral("#3366CC");
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVariant deletedAt;
    QVariant serverUpdatedAt;
    int serverVersion = 0;
    QString deviceId;
    bool dirty = true;
    int wordCount = 0;
};

#endif // DLWORDGROUP_H
