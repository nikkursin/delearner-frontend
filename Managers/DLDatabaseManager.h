#ifndef DLDATABASEMANAGER_H
#define DLDATABASEMANAGER_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlDatabase>
#include <QMutex>

class DLDatabaseManager
{
private:
    DLDatabaseManager();
    ~DLDatabaseManager();

public:
    void init();

    DLDatabaseManager(const DLDatabaseManager& other) = delete;
    DLDatabaseManager& operator= (const DLDatabaseManager& other) = delete;

    static DLDatabaseManager& instance();

    bool openDatabase(const QString &databasePath);
    void closeDatabase();
    bool createTablesIfNeeded();
    bool createIndexesIfNeeded();
    QString lastError() const;

    int insertGroup(const QString &name, const QString &colorHex = QStringLiteral("#3366CC"));
    bool updateGroup(int id, const QString &name, const QString &colorHex);
    bool deleteGroup(int id);

    QVariantList fetchAllGroups();
    QVariantMap  fetchGroupById(int id);

    int insertWord(const QString &germanWord,
                   const QString &article,
                   const QString &partOfSpeech,
                   const QString &nativeTranslation,
                   const QString &examplePhraseDe    = QString(),
                   const QString &examplePhraseNative = QString(),
                   int            groupId             = -1);

    bool updateWord(int            id,
                    const QString &germanWord,
                    const QString &article,
                    const QString &partOfSpeech,
                    const QString &nativeTranslation,
                    const QString &examplePhraseDe    = QString(),
                    const QString &examplePhraseNative = QString(),
                    int            groupId             = -1);

    bool deleteWord(int id);

    QVariantMap  fetchWordById(int id);

    bool wordExists(const QString &germanWord,
                    const QString &nativeTranslation,
                    int            excludingId = -1);

    QVariantList fetchAllWords(const QString &sortMode = QStringLiteral("newest"),
                               int groupId = -1);

    QVariantList searchWords(const QString &query, int groupId = -1);

    QVariantList fetchWordsByGroup(int groupId);

    QVariantList fetchRandomWords(int limit, int groupId = -1);

    QVariantList fetchNouns(int groupId = -1);

    int  getWordCount(int groupId = -1);
    int  getGroupCount();
    int  getNounCount(int groupId = -1);

    bool incrementCorrectAnswer(int wordId);
    bool incrementWrongAnswer(int wordId);

    QVariantMap getDatabaseStats();

private:
    QString sortClause(const QString &sortMode) const;

    QVariantMap rowToMap() const;

    QSqlDatabase m_db;
    QString      m_lastError;
    QString      m_connectionName;

    static DLDatabaseManager* m_instance;
    static std::once_flag m_initFlag;
    static QMutex m_mutex;
};

#endif // DLDATABASEMANAGER_H
