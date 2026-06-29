#ifndef DLMODELMAPPERS_H
#define DLMODELMAPPERS_H

#include <QList>
#include <QVariantList>
#include <QVariantMap>

#include "DLWord.h"
#include "DLWordGroup.h"

namespace DLModelMappers
{
DLWord wordFromMap(const QVariantMap& map);
QVariantMap wordToMap(const DLWord& word);
QVariantList wordsToList(const QList<DLWord>& words);

DLWordGroup groupFromMap(const QVariantMap& map);
QVariantMap groupToMap(const DLWordGroup& group);
QVariantList groupsToList(const QList<DLWordGroup>& groups);
}

#endif // DLMODELMAPPERS_H
