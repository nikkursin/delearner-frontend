#ifndef DLWORDREVIEWSTATS_H
#define DLWORDREVIEWSTATS_H

#include <QVariant>
#include <QString>
#include <QtGlobal>

struct DLWordReviewStats
{
    int wordId = -1;
    QString wordSyncId;
    int correctAnswers = 0;
    int wrongAnswers = 0;
    QVariant lastReviewedAt;
    double easeFactor = 2.5;
    int intervalDays = 0;
    QVariant dueAt;
    qint64 updatedAt = 0;
};

#endif // DLWORDREVIEWSTATS_H
