#ifndef DLWORDSERVICE_H
#define DLWORDSERVICE_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class DLDatabaseManager;

class DLWordService
{
public:
    explicit DLWordService(DLDatabaseManager& database);

    QString createWord(const QVariantMap& wordData);
    bool updateWord(const QString& syncId, const QVariantMap& wordData);
    bool deleteWord(const QString& syncId);
    QVariantMap wordById(const QString& syncId);
    QVariantList loadWords(const QString& sortMode, const QString& groupSyncId);
    QVariantList searchWords(const QString& query, const QString& sortMode, const QString& groupSyncId);
    int wordCount(const QString& groupSyncId);
    QString lastError() const;

private:
    QString trimmedStringValue(const QVariantMap& wordData, const QString& key) const;
    QString groupSyncIdFromWordData(const QVariantMap& wordData) const;
    QVariantList sortedWords(const QVariantList& words, const QString& sortMode) const;

    DLDatabaseManager& m_database;
    QString m_lastError;
};

#endif // DLWORDSERVICE_H
