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
    Q_INVOKABLE QVariantMap wordById(int id);
    Q_INVOKABLE int createWord(const QVariantMap& wordData);
    Q_INVOKABLE bool updateWord(int id, const QVariantMap& wordData);

signals:

    void currentScreenChanged();
    void lastErrorChanged();
    void wordsChanged();

private:
    void navigateTo(const DLScreen& screen);
    void setLastError(const QString& error);
    int groupIdFromWordData(const QVariantMap& wordData) const;
    QString trimmedStringValue(const QVariantMap& wordData, const QString& key) const;

    DLScreen m_currentScreen = StartupLoadingPage;
    QString m_lastError;

};

#endif // DLAPPSTATEMANAGER_H
