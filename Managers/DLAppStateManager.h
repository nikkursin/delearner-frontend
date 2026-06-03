#ifndef DLAPPSTATEMANAGER_H
#define DLAPPSTATEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "DLDatabaseManager.h"

class DLAppStateManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(DLScreen currentScreen READ currentScreen NOTIFY currentScreenChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int selectedWordId READ selectedWordId NOTIFY selectedWordIdChanged)
public:
    enum DLScreen
    {
        StartupLoadingPage,
        WordsPage,
        WordDetails,
        AddEditWordPage,
        GroupsPage,
        GroupEditPage,
        QuizHomePage,
        QuizSetupPage,
        TranslationQuizSessionPage,
        ArticleQuizSessionPage,
        QuizResults,
        SettingsPage
    };

    Q_ENUM(DLScreen)

    explicit DLAppStateManager(QObject *parent = nullptr);
    ~DLAppStateManager();

    void init(const QString& databasePath);

    DLScreen currentScreen() const;
    QString lastError() const;
    int selectedWordId() const;

    Q_INVOKABLE void goStartupLoadingPage();
    Q_INVOKABLE void goWordsPage();
    Q_INVOKABLE void goWordDetails();
    Q_INVOKABLE void goAddEditWordPage();
    Q_INVOKABLE void goGroupsPage();
    Q_INVOKABLE void goGroupEditPage();
    Q_INVOKABLE void goQuizHomePage();
    Q_INVOKABLE void goQuizSetupPage();
    Q_INVOKABLE void goTranslationQuizSessionPage();
    Q_INVOKABLE void goArticleQuizSessionPage();
    Q_INVOKABLE void goQuizResults();
    Q_INVOKABLE void goSettingsPage();

    Q_INVOKABLE QVariantList availableGroups();
    Q_INVOKABLE QVariantList loadWords(const QString& sortMode = QStringLiteral("newest"), int groupId = -1);
    Q_INVOKABLE QVariantList searchWords(const QString& query, const QString& sortMode = QStringLiteral("newest"), int groupId = -1);
    Q_INVOKABLE int wordCount(int groupId = -1);
    Q_INVOKABLE int groupCount();
    Q_INVOKABLE qint64 databaseSize();
    Q_INVOKABLE QVariantMap databaseStats();
    Q_INVOKABLE QVariantMap wordById(int id);
    Q_INVOKABLE int createWord(const QVariantMap& wordData);
    Q_INVOKABLE bool updateWord(int id, const QVariantMap& wordData);
    Q_INVOKABLE bool deleteWord(int id);
    Q_INVOKABLE bool exportDatabase(const QString& targetPath);
    Q_INVOKABLE bool importDatabaseReplace(const QString& sourcePath);
    Q_INVOKABLE bool importDatabaseMerge(const QString& sourcePath);
    Q_INVOKABLE bool deleteAllData();
    Q_INVOKABLE void openWordDetails(int id);
    Q_INVOKABLE void openEditWord(int id);

signals:

    void currentScreenChanged();
    void lastErrorChanged();
    void selectedWordIdChanged();
    void wordsChanged();

private:
    void navigateTo(const DLScreen& screen);
    void setLastError(const QString& error);
    void setSelectedWordId(int id);
    QVariantList sortedWords(const QVariantList& words, const QString& sortMode) const;
    int groupIdFromWordData(const QVariantMap& wordData) const;
    QString trimmedStringValue(const QVariantMap& wordData, const QString& key) const;
    QString localPathFromUrlOrPath(const QString& value) const;

    DLScreen m_currentScreen = StartupLoadingPage;
    QString m_lastError;
    QString m_databasePath;
    int m_selectedWordId = -1;

};

#endif // DLAPPSTATEMANAGER_H
