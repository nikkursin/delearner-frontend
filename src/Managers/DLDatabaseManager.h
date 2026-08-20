#ifndef DLDATABASEMANAGER_H
#define DLDATABASEMANAGER_H

#include <functional>

#include <QList>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

struct DLSqlCommand
{
    QString sql;
    QVariantMap args;
};

class DLDatabaseManager
{
private:
    DLDatabaseManager();
    ~DLDatabaseManager();

public:
    static DLDatabaseManager& instance();

    DLDatabaseManager(const DLDatabaseManager& other) = delete;
    DLDatabaseManager& operator=(const DLDatabaseManager& other) = delete;

    bool openDatabase(const QString& databasePath);
    void closeDatabase();

    bool createTablesIfNeeded();
    bool migrateSchemaIfNeeded();
    bool createIndexesIfNeeded();

    QString databasePath() const;
    QString lastError() const;
    void setLastError(const QString& error);

    bool executeSql(const QString& sql, const QVariantMap& args = {});
    bool executeSqlBatch(const QList<DLSqlCommand>& commands);
    int executeInsert(const QString& sql, const QVariantMap& args = {});
    QVariantList selectRows(const QString& sql, const QVariantMap& args = {});
    QVariantMap selectOneRow(const QString& sql, const QVariantMap& args = {});
    int selectInt(const QString& sql, const QVariantMap& args = {}, int fallback = 0);
    bool transaction(const std::function<bool(QSqlDatabase&, QString*)>& callback);

    static qint64 currentUnixTime();
    static QString normalizedText(const QString& value);
    static QVariant nullVariant();

    QString insertGroup(const QString& name,
                        const QString& colorHex = QStringLiteral("#3366CC"));
    bool updateGroup(const QString& syncId,
                     const QString& name,
                     const QString& colorHex);
    bool deleteGroup(const QString& syncId);
    QVariantList fetchAllGroups();
    QVariantMap fetchGroupById(const QString& syncId);

    QString insertWord(const QString& germanWord,
                       const QString& article,
                       const QString& partOfSpeech,
                       const QString& nativeTranslation,
                       const QString& examplePhraseDe = QString(),
                       const QString& examplePhraseNative = QString(),
                       const QString& groupSyncId = QString(),
                       const QString& syncId = QString(),
                       const QString& pluralForm = QString(),
                       const QString& praeteritumForm = QString(),
                       const QString& partizipIIForm = QString(),
                       const QString& positiveForm = QString(),
                       const QString& comparativeForm = QString(),
                       const QString& superlativeForm = QString());

    bool updateWord(const QString& syncId,
                    const QString& germanWord,
                    const QString& article,
                    const QString& partOfSpeech,
                    const QString& nativeTranslation,
                    const QString& examplePhraseDe = QString(),
                    const QString& examplePhraseNative = QString(),
                    const QString& groupSyncId = QString(),
                    const QString& pluralForm = QString(),
                    const QString& praeteritumForm = QString(),
                    const QString& partizipIIForm = QString(),
                    const QString& positiveForm = QString(),
                    const QString& comparativeForm = QString(),
                    const QString& superlativeForm = QString());

    bool deleteWord(const QString& syncId);
    QVariantMap fetchWordById(const QString& syncId);
    bool wordExists(const QString& germanWord,
                    const QString& nativeTranslation,
                    const QString& excludingSyncId = QString());
    QVariantList fetchAllWords(const QString& sortMode = QStringLiteral("newest"),
                               const QString& groupSyncId = QString());
    QVariantList searchWords(const QString& query,
                             const QString& groupSyncId = QString());
    QVariantList fetchWordsByGroup(const QString& groupSyncId);
    QVariantList fetchRandomWords(int limit,
                                  const QString& groupSyncId = QString());
    QVariantList fetchTranslationQuizWords(int limit,
                                           const QString& groupSyncId = QString(),
                                           const QString& partOfSpeech = QString());
    QVariantList fetchNouns(const QString& groupSyncId = QString());
    int getWordCount(const QString& groupSyncId = QString());
    int getTranslationQuizWordCount(const QString& groupSyncId = QString(),
                                    const QString& partOfSpeech = QString());
    int getGroupCount();
    int getNounCount(const QString& groupSyncId = QString());
    bool incrementCorrectAnswer(int wordId);
    bool incrementWrongAnswer(int wordId);
    QVariantMap getDatabaseStats();
    bool importDatabaseMerge(const QString& sourceDatabasePath);
    bool deleteAllData();

private:
    mutable QMutex m_mutex;
    QSqlDatabase m_db;
    QString m_lastError;
    QString m_connectionName;
};

#endif // DLDATABASEMANAGER_H
