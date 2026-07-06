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

    QString localDeviceId();
    QVariantMap localDeviceIdentity();
    bool setSyncStateValue(const QString& key, const QString& value);
    QString syncStateValue(const QString& key, const QString& fallback = QString());

    bool executeSql(const QString& sql, const QVariantMap& args = {});
    bool executeSqlBatch(const QList<DLSqlCommand>& commands);
    int executeInsert(const QString& sql, const QVariantMap& args = {});
    QVariantList selectRows(const QString& sql, const QVariantMap& args = {});
    QVariantMap selectOneRow(const QString& sql, const QVariantMap& args = {});
    int selectInt(const QString& sql, const QVariantMap& args = {}, int fallback = 0);
    bool transaction(const std::function<bool(QSqlDatabase&, QString*)>& callback);

    static qint64 currentUnixTime();
    static qint64 currentUnixTimeMs();
    static QString currentDeviceId();
    static QString generateUuid();
    static QString normalizedText(const QString& value);
    static QVariant nullVariant();

    QString insertGroup(const QString& name,
                        const QString& colorHex = QStringLiteral("#3366CC"));
    bool updateGroup(const QString& id,
                     const QString& name,
                     const QString& colorHex);
    bool deleteGroup(const QString& id);
    QVariantList fetchAllGroups();
    QVariantMap fetchGroupById(const QString& id);

    QString insertWord(const QString& germanWord,
                       const QString& article,
                       const QString& partOfSpeech,
                       const QString& nativeTranslation,
                       const QString& examplePhraseDe = QString(),
                       const QString& examplePhraseNative = QString(),
                       const QString& groupId = QString(),
                       const QString& syncId = QString(),
                       const QString& notes = QString(),
                       const QString& pluralForm = QString(),
                       const QString& praeteritumForm = QString(),
                       const QString& partizipIIForm = QString(),
                       const QString& positiveForm = QString(),
                       const QString& comparativeForm = QString(),
                       const QString& superlativeForm = QString());

    bool updateWord(const QString& id,
                    const QString& germanWord,
                    const QString& article,
                    const QString& partOfSpeech,
                    const QString& nativeTranslation,
                    const QString& examplePhraseDe = QString(),
                    const QString& examplePhraseNative = QString(),
                    const QString& groupId = QString(),
                    const QString& syncId = QString(),
                    const QString& notes = QString(),
                    const QString& pluralForm = QString(),
                    const QString& praeteritumForm = QString(),
                    const QString& partizipIIForm = QString(),
                    const QString& positiveForm = QString(),
                    const QString& comparativeForm = QString(),
                    const QString& superlativeForm = QString());

    bool deleteWord(const QString& id);
    QVariantMap fetchWordById(const QString& id);
    bool wordExists(const QString& germanWord,
                    const QString& nativeTranslation,
                    const QString& excludingId = QString());
    QVariantList fetchAllWords(const QString& sortMode = QStringLiteral("newest"),
                               const QString& groupId = QString());
    QVariantList searchWords(const QString& query,
                             const QString& groupId = QString());
    QVariantList fetchWordsByGroup(const QString& groupId);
    QVariantList fetchRandomWords(int limit,
                                  const QString& groupId = QString());
    QVariantList fetchTranslationQuizWords(int limit,
                                           const QString& groupId = QString(),
                                           const QString& partOfSpeech = QString());
    QVariantList fetchNouns(const QString& groupId = QString());
    int getWordCount(const QString& groupId = QString());
    int getTranslationQuizWordCount(const QString& groupId = QString(),
                                    const QString& partOfSpeech = QString());
    int getGroupCount();
    int getNounCount(const QString& groupId = QString());
    bool incrementCorrectAnswer(const QString& wordId);
    bool incrementWrongAnswer(const QString& wordId);
    QVariantMap getDatabaseStats();
    bool importDatabaseMerge(const QString& sourceDatabasePath);
    bool deleteAllData();

private:
    QString ensureDeviceIdentityLocked();

    mutable QMutex m_mutex;
    QSqlDatabase m_db;
    QString m_lastError;
    QString m_connectionName;
    QString m_deviceId;
};

#endif // DLDATABASEMANAGER_H
