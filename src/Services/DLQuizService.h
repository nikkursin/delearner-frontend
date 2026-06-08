#ifndef DLQUIZSERVICE_H
#define DLQUIZSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "DLQuizSession.h"
#include "../Models/DLWord.h"

class DLDatabaseManager;

class DLQuizService
{
public:
    explicit DLQuizService(DLDatabaseManager& database);

    QVariantList availableQuizModes();
    int availableQuizQuestionCount(const QString& type, int groupId = -1, const QString& partOfSpeech = QString());
    bool canStartQuiz(const QString& type, int groupId = -1, int questionCount = 10, const QString& partOfSpeech = QString());
    bool startQuiz(const QString& type, int groupId = -1, int questionCount = 10, const QString& partOfSpeech = QString());
    void resetQuiz();

    bool selectQuizType(const QString& type);
    QString selectedQuizType() const;
    QString activeQuizType() const;
    DLQuizSession& session();
    const DLQuizSession& session() const;
    QString lastError() const;

private:
    enum QuizType
    {
        UnknownQuiz,
        TranslationQuiz,
        ArticleQuiz
    };

    QuizType quizTypeFromString(const QString& type) const;
    QString quizTypeToString(QuizType type) const;
    QString quizTypeTitle(QuizType type) const;
    int availableQuestionCount(QuizType type, int groupId, const QString& partOfSpeech = QString()) const;
    QList<DLQuizQuestion> buildQuestions(QuizType type, const QList<DLWord>& questionWords, const QList<DLWord>& pool, int limit) const;
    DLQuizQuestion buildQuestion(QuizType type, const DLWord& word, const QList<DLWord>& pool) const;
    QStringList answerOptionsForTranslation(const DLWord& word, QList<DLWord> pool) const;

    DLDatabaseManager& m_database;
    DLQuizSession m_session;
    QuizType m_selectedQuizType = UnknownQuiz;
    QString m_lastError;
};

#endif // DLQUIZSERVICE_H
