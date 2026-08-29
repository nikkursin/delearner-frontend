#ifndef DLAPPSTATEMANAGER_H
#define DLAPPSTATEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DLGroupService;
class DLQuizService;
class DLWordService;
class DLClientAuthService;

class DLAppStateManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(DLScreen currentScreen READ currentScreen NOTIFY currentScreenChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString selectedWordId READ selectedWordId NOTIFY selectedWordIdChanged)
    Q_PROPERTY(QString authState READ authState NOTIFY authStateChanged)
    Q_PROPERTY(bool authBusy READ authBusy NOTIFY authBusyChanged)
public:
    enum DLScreen
    {
        StartupLoadingPage,
        AuthPage,
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
    QString selectedWordId() const;
    QString authState() const;
    bool authBusy() const;

    Q_INVOKABLE void goStartupLoadingPage();
    Q_INVOKABLE void goWordsPage();
    Q_INVOKABLE void goWordDetails();
    Q_INVOKABLE void goAddEditWordPage();
    Q_INVOKABLE void goGroupsPage();
    Q_INVOKABLE void goGroupEditPage();
    Q_INVOKABLE void goQuizHomePage();
    Q_INVOKABLE void goQuizSetupPage(const QString& quizType = QString());
    Q_INVOKABLE void goTranslationQuizSessionPage();
    Q_INVOKABLE void goArticleQuizSessionPage();
    Q_INVOKABLE void goQuizResults();
    Q_INVOKABLE void goSettingsPage();
    Q_INVOKABLE void signIn(const QString& email, const QString& password);
    Q_INVOKABLE void registerAccount(const QString& email, const QString& password);

    Q_INVOKABLE QVariantList availableGroups();
    Q_INVOKABLE QString createGroup(const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    Q_INVOKABLE bool updateGroup(const QString& syncId, const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    Q_INVOKABLE bool deleteGroup(const QString& syncId);
    Q_INVOKABLE QVariantList loadWords(const QString& sortMode = QStringLiteral("newest"), const QString& groupSyncId = QString());
    Q_INVOKABLE QVariantList searchWords(const QString& query, const QString& sortMode = QStringLiteral("newest"), const QString& groupSyncId = QString());
    Q_INVOKABLE int wordCount(const QString& groupSyncId = QString());
    Q_INVOKABLE int groupCount();
    Q_INVOKABLE qint64 databaseSize();
    Q_INVOKABLE QVariantMap databaseStats();
    Q_INVOKABLE QVariantMap wordById(const QString& syncId);
    Q_INVOKABLE QString createWord(const QVariantMap& wordData);
    Q_INVOKABLE bool updateWord(const QString& syncId, const QVariantMap& wordData);
    Q_INVOKABLE bool deleteWord(const QString& syncId);
    Q_INVOKABLE bool exportVocabularyDatabase();
    Q_INVOKABLE bool exportDatabase(const QString& targetPath);
    Q_INVOKABLE bool importDatabaseReplace(const QString& sourcePath);
    Q_INVOKABLE bool importDatabaseMerge(const QString& sourcePath);
    Q_INVOKABLE bool deleteAllData();
    Q_INVOKABLE void openWordDetails(const QString& syncId);
    Q_INVOKABLE void openEditWord(const QString& syncId);

    Q_INVOKABLE QVariantList availableQuizModes();
    Q_INVOKABLE int availableQuizQuestionCount(const QString& type,
                                               const QString& groupSyncId = QString(),
                                               const QString& partOfSpeech = QString());
    Q_INVOKABLE QString selectedQuizType() const;
    Q_INVOKABLE bool canStartQuiz(const QString& type,
                                  const QString& groupSyncId = QString(),
                                  int questionCount = 10,
                                  const QString& partOfSpeech = QString());
    Q_INVOKABLE bool startQuiz(const QString& type,
                               const QString& groupSyncId = QString(),
                               int questionCount = 10,
                               const QString& partOfSpeech = QString());
    Q_INVOKABLE QVariantMap currentQuizQuestion() const;
    Q_INVOKABLE QVariantMap submitQuizAnswer(const QString& answer);
    Q_INVOKABLE bool nextQuizQuestion();
    Q_INVOKABLE QVariantMap quizProgress() const;
    Q_INVOKABLE QVariantMap quizResult() const;
    Q_INVOKABLE void resetQuiz();

signals:

    void currentScreenChanged();
    void lastErrorChanged();
    void selectedWordIdChanged();
    void authStateChanged();
    void authBusyChanged();
    void wordsChanged();
    void groupsChanged();
    void quizStateChanged();

private:
    void navigateTo(const DLScreen& screen);
    void setLastError(const QString& error);
    void setSelectedWordId(const QString& syncId);
    void setAuthState(const QString& authState);
    void setAuthBusy(bool authBusy);
    bool openFreeCore();
    QString localPathFromUrlOrPath(const QString& value) const;

    DLScreen m_currentScreen = StartupLoadingPage;
    QString m_lastError;
    QString m_databasePath;
    QString m_selectedWordId;
    QString m_authState;
    bool m_authBusy = false;
    std::unique_ptr<DLClientAuthService> m_authService;
    std::unique_ptr<DLWordService> m_wordService;
    std::unique_ptr<DLGroupService> m_groupService;
    std::unique_ptr<DLQuizService> m_quizService;

};

#endif // DLAPPSTATEMANAGER_H
