#ifndef DLAPPSTATEMANAGER_H
#define DLAPPSTATEMANAGER_H

#include <QObject>

#include "DLDatabaseManager.h"

class DLAppStateManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(DLScreen currentScreen READ currentScreen NOTIFY currentScreenChanged)
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

signals:

    void currentScreenChanged();

private:
    void navigateTo(const DLScreen& screen);

    DLDatabaseManager* m_databaseManager;
    DLScreen m_currentScreen = StartupLoadingPage;

};

#endif // DLAPPSTATEMANAGER_H
