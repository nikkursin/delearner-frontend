#ifndef DLQUIZREPOSITORY_H
#define DLQUIZREPOSITORY_H

#include <QList>
#include <QString>
#include <QVariantMap>

#include "../Models/DLWord.h"

class DLDatabaseManager;

class DLQuizRepository
{
public:
    explicit DLQuizRepository(DLDatabaseManager& database);

    QList<DLWord> fetchRandomWords(int limit, const QString& groupSyncId = QString());
    QList<DLWord> fetchTranslationQuizWords(int limit, const QString& groupSyncId = QString(), const QString& partOfSpeech = QString());
    QList<DLWord> fetchNouns(const QString& groupSyncId = QString());
    int getNounCount(const QString& groupSyncId = QString());
    int getTranslationQuizWordCount(const QString& groupSyncId = QString(), const QString& partOfSpeech = QString());

private:
    QList<DLWord> fetchWords(const QString& sql, const QVariantMap& args);

    DLDatabaseManager& m_database;
};

#endif // DLQUIZREPOSITORY_H
