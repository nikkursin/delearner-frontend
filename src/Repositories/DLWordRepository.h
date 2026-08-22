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

    QString insertWord(const DLWord& word);
    bool updateWord(const DLWord& word);
    bool deleteWord(const QString& syncId);
    DLWord fetchWordById(const QString& syncId);
    QList<DLWord> fetchAllWords(const QString& sortMode = QStringLiteral("newest"), const QString& groupSyncId = QString());
    QList<DLWord> searchWords(const QString& query, const QString& groupSyncId = QString());
    bool wordExists(const QString& germanWord, const QString& nativeTranslation, const QString& excludingSyncId = QString());
    int getWordCount(const QString& groupSyncId = QString());

    static QString wordSelectColumns();
    static QString wordFromClause();

private:
    QString sortClause(const QString& sortMode) const;
    QList<DLWord> fetchWords(const QString& sql, const QVariantMap& args);
    int localWordIdForSyncId(QSqlDatabase& db, QString* error, const QString& syncId);
    int localGroupIdForSyncId(QSqlDatabase& db, QString* error, const QString& syncId);
    bool saveForms(QSqlDatabase& db, QString* error, int wordId, const DLWord& word);
    bool createPhraseFromExample(QSqlDatabase& db, QString* error, const DLWord& word);
    bool bindAndExec(QSqlQuery& query, QString* error);

    DLDatabaseManager& m_database;
};

#endif // DLWORDREPOSITORY_H
