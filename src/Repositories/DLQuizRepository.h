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

    QList<DLWord> fetchRandomWords(int limit, int groupId = -1);
    QList<DLWord> fetchTranslationQuizWords(int limit, int groupId = -1, const QString& partOfSpeech = QString());
    QList<DLWord> fetchNouns(int groupId = -1);
    int getNounCount(int groupId = -1);
    int getTranslationQuizWordCount(int groupId = -1, const QString& partOfSpeech = QString());

private:
    QList<DLWord> fetchWords(const QString& sql, const QVariantMap& args);

    DLDatabaseManager& m_database;
};

#endif // DLQUIZREPOSITORY_H
