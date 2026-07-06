#ifndef DLWORDREVIEWSTATS_H
#define DLWORDREVIEWSTATS_H

#include <QVariant>
#include <QtGlobal>

struct DLWordReviewStats
{
    QString id;
    QString wordId;
    int correctAnswers = 0;
    int wrongAnswers = 0;
    QVariant lastReviewedAt;
    double easeFactor = 2.5;
    int intervalDays = 0;
    QVariant dueAt;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVariant deletedAt;
    QVariant serverUpdatedAt;
    int serverVersion = 0;
    QString deviceId;
    bool dirty = true;
};

#endif // DLWORDREVIEWSTATS_H
