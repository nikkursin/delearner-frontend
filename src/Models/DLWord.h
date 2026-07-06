#ifndef DLWORD_H
#define DLWORD_H

#include <QString>
#include <QVariant>
#include <QtGlobal>

#include "DLWordForms.h"
#include "DLWordReviewStats.h"

struct DLWord
{
    QString id;
    QString germanWord;
    QString normalizedGermanWord;
    QString article;
    QString partOfSpeech = QStringLiteral("Andere");
    QString nativeTranslation;
    QString normalizedNativeTranslation;
    QString examplePhraseDe;
    QString examplePhraseNative;
    QString groupId;
    QString pluralForm;
    QString notes;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QVariant deletedAt;
    QVariant serverUpdatedAt;
    int serverVersion = 0;
    QString deviceId;
    bool dirty = true;
    DLWordReviewStats reviewStats;
    DLNounForms nounForms;
    DLVerbForms verbForms;
    DLAdjectiveForms adjectiveForms;
};

#endif // DLWORD_H
