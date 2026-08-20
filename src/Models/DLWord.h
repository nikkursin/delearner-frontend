#ifndef DLWORD_H
#define DLWORD_H

#include <QString>
#include <QVariant>
#include <QtGlobal>

#include "DLWordForms.h"
#include "DLWordReviewStats.h"

struct DLWord
{
    int id = -1;
    QString syncId;
    QString germanWord;
    QString normalizedGermanWord;
    QString article;
    QString partOfSpeech = QStringLiteral("Andere");
    QString nativeTranslation;
    QString normalizedNativeTranslation;
    QString examplePhraseDe;
    QString examplePhraseNative;
    int groupId = -1;
    QString groupSyncId;
    QString notes;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVariant deletedAt;
    DLWordReviewStats reviewStats;
    DLNounForms nounForms;
    DLVerbForms verbForms;
    DLAdjectiveForms adjectiveForms;
};

#endif // DLWORD_H
