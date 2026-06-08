#ifndef DLWORDREPOSITORY_H
#define DLWORDREPOSITORY_H

#include <QList>
#include <QString>
#include <QVariantMap>

#include "../Models/DLWord.h"

class DLDatabaseManager;
class QSqlDatabase;
class QSqlQuery;

class DLWordRepository
{
public:
    explicit DLWordRepository(DLDatabaseManager& database);

    int insertWord(const DLWord& word);
    bool updateWord(const DLWord& word);
    bool deleteWord(int id);
    DLWord fetchWordById(int id);
    QList<DLWord> fetchAllWords(const QString& sortMode = QStringLiteral("newest"), int groupId = -1);
    QList<DLWord> searchWords(const QString& query, int groupId = -1);
    bool wordExists(const QString& germanWord, const QString& nativeTranslation, int excludingId = -1);
    int getWordCount(int groupId = -1);

    static QString wordSelectColumns();
    static QString wordFromClause();

private:
    QString sortClause(const QString& sortMode) const;
    QList<DLWord> fetchWords(const QString& sql, const QVariantMap& args);
    bool saveForms(QSqlDatabase& db, QString* error, int wordId, const DLWord& word);
    bool createPhraseFromExample(QSqlDatabase& db, QString* error, const DLWord& word);
    bool bindAndExec(QSqlQuery& query, QString* error);

    DLDatabaseManager& m_database;
};

#endif // DLWORDREPOSITORY_H
