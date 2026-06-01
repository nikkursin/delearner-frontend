#ifndef DLDATABASEMANAGER_H
#define DLDATABASEMANAGER_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlDatabase>
#include <QMutex>
#include <QList>

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
    bool createIndexesIfNeeded();

    QString lastError() const;

    int insertGroup(const QString& name,
                    const QString& colorHex = QStringLiteral("#3366CC"));

    bool updateGroup(int id,
                     const QString& name,
                     const QString& colorHex);

    bool deleteGroup(int id);

    QVariantList fetchAllGroups();
    QVariantMap fetchGroupById(int id);

    int insertWord(const QString& germanWord,
                   const QString& article,
                   const QString& partOfSpeech,
                   const QString& nativeTranslation,
                   const QString& examplePhraseDe = QString(),
                   const QString& examplePhraseNative = QString(),
                   int groupId = -1);

    bool updateWord(int id,
                    const QString& germanWord,
                    const QString& article,
                    const QString& partOfSpeech,
                    const QString& nativeTranslation,
                    const QString& examplePhraseDe = QString(),
                    const QString& examplePhraseNative = QString(),
                    int groupId = -1);

    bool deleteWord(int id);

    QVariantMap fetchWordById(int id);

    bool wordExists(const QString& germanWord,
                    const QString& nativeTranslation,
                    int excludingId = -1);

    QVariantList fetchAllWords(const QString& sortMode = QStringLiteral("newest"),
                               int groupId = -1);

    QVariantList searchWords(const QString& query,
                             int groupId = -1);

    QVariantList fetchWordsByGroup(int groupId);

    QVariantList fetchRandomWords(int limit,
                                  int groupId = -1);

    QVariantList fetchNouns(int groupId = -1);

    int getWordCount(int groupId = -1);
    int getGroupCount();
    int getNounCount(int groupId = -1);

    bool incrementCorrectAnswer(int wordId);
    bool incrementWrongAnswer(int wordId);

    QVariantMap getDatabaseStats();

private:
    bool executeSql(const QString& sql,
                    const QVariantMap& args = {});

    bool executeSqlBatch(const QList<DLSqlCommand>& commands);

    QVariantList selectRows(const QString& sql,
                            const QVariantMap& args = {});

    QVariantMap selectOneRow(const QString& sql,
                             const QVariantMap& args = {});

    int selectInt(const QString& sql,
                  const QVariantMap& args = {},
                  int fallback = 0);

private:
    bool executeSqlNoLock(const QString& sql,
                          const QVariantMap& args = {});

    bool executeSqlBatchNoLock(const QList<DLSqlCommand>& commands);

    QVariantList selectRowsNoLock(const QString& sql,
                                  const QVariantMap& args = {});

    QVariantMap selectOneRowNoLock(const QString& sql,
                                   const QVariantMap& args = {});

    int selectIntNoLock(const QString& sql,
                        const QVariantMap& args = {},
                        int fallback = 0);

    bool wordExistsNoLock(const QString& germanWord,
                          const QString& nativeTranslation,
                          int excludingId = -1);

    QString sortClause(const QString& sortMode) const;
    QVariant nullVariant() const;

private:
    mutable QMutex m_mutex;

    QSqlDatabase m_db;
    QString m_lastError;
    QString m_connectionName;
};

#endif // DLDATABASEMANAGER_H
