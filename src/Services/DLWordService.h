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

    int createWord(const QVariantMap& wordData);
    bool updateWord(int id, const QVariantMap& wordData);
    bool deleteWord(int id);
    QVariantMap wordById(int id);
    QVariantList loadWords(const QString& sortMode, int groupId);
    QVariantList searchWords(const QString& query, const QString& sortMode, int groupId);
    int wordCount(int groupId);
    QString lastError() const;

private:
    QString trimmedStringValue(const QVariantMap& wordData, const QString& key) const;
    int groupIdFromWordData(const QVariantMap& wordData) const;
    QVariantList sortedWords(const QVariantList& words, const QString& sortMode) const;

    DLDatabaseManager& m_database;
    QString m_lastError;
};

#endif // DLWORDSERVICE_H
