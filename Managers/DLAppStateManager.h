#ifndef DLAPPSTATEMANAGER_H
#define DLAPPSTATEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

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
    Q_INVOKABLE void goQuizSetupPage(const QString& quizType = QString());
    Q_INVOKABLE void goTranslationQuizSessionPage();
    Q_INVOKABLE void goArticleQuizSessionPage();
    Q_INVOKABLE void goQuizResults();
    Q_INVOKABLE void goSettingsPage();

    Q_INVOKABLE QVariantList availableGroups();
    Q_INVOKABLE int createGroup(const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    Q_INVOKABLE bool updateGroup(int id, const QString& name, const QString& colorHex = QStringLiteral("#337fe6"));
    Q_INVOKABLE bool deleteGroup(int id);
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

    Q_INVOKABLE QVariantList availableQuizModes();
    Q_INVOKABLE int availableQuizQuestionCount(const QString& type,
                                               int groupId = -1,
                                               const QString& partOfSpeech = QString());
    Q_INVOKABLE QString selectedQuizType() const;
    Q_INVOKABLE bool canStartQuiz(const QString& type,
                                  int groupId = -1,
                                  int questionCount = 10,
                                  const QString& partOfSpeech = QString());
    Q_INVOKABLE bool startQuiz(const QString& type,
                               int groupId = -1,
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
    void wordsChanged();
    void groupsChanged();
    void quizStateChanged();

private:
    enum QuizType
    {
        UnknownQuiz,
        TranslationQuiz,
        ArticleQuiz
    };

    void navigateTo(const DLScreen& screen);
    void setLastError(const QString& error);
    void setSelectedWordId(int id);
    QuizType quizTypeFromString(const QString& type) const;
    QString quizTypeToString(QuizType type) const;
    QString quizTypeTitle(QuizType type) const;
    int availableQuestionCount(QuizType type, int groupId, const QString& partOfSpeech = QString()) const;
    QVariantList answerOptionsForTranslation(const QVariantMap& word, const QVariantList& pool) const;
    QVariantMap buildQuestion(QuizType type, const QVariantMap& word, const QVariantList& pool) const;
    void updateQuizResultCache();
    QVariantList sortedWords(const QVariantList& words, const QString& sortMode) const;
    int groupIdFromWordData(const QVariantMap& wordData) const;
    QString trimmedStringValue(const QVariantMap& wordData, const QString& key) const;
    QString localPathFromUrlOrPath(const QString& value) const;

    DLScreen m_currentScreen = StartupLoadingPage;
    QString m_lastError;
    QString m_databasePath;
    int m_selectedWordId = -1;
    QuizType m_selectedQuizType = UnknownQuiz;
    QuizType m_activeQuizType = UnknownQuiz;
    int m_quizGroupId = -1;
    int m_currentQuestionIndex = 0;
    int m_correctAnswerCount = 0;
    int m_wrongAnswerCount = 0;
    bool m_quizFinished = false;
    QVector<QVariantMap> m_quizQuestions;
    QVariantMap m_quizResultCache;

};

#endif // DLAPPSTATEMANAGER_H
