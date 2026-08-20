#ifndef DLQUIZSESSION_H
#define DLQUIZSESSION_H

#include <QList>
#include <QVariantMap>

#include "../Models/DLQuizQuestion.h"
#include "../Models/DLQuizResult.h"

class DLDatabaseManager;

class DLQuizSession
{
public:
    explicit DLQuizSession(DLDatabaseManager& database);

    void start(const QString& quizType,
               const QString& quizTitle,
               const QString& groupSyncId,
               const QList<DLQuizQuestion>& questions);
    void reset();

    QString activeQuizType() const;
    bool isActive() const;
    bool isFinished() const;
    QVariantMap currentQuestion() const;
    QVariantMap submitAnswer(const QString& answer);
    bool nextQuestion();
    QVariantMap progress() const;
    QVariantMap result() const;
    QString lastError() const;

private:
    QVariantMap questionToMap(const DLQuizQuestion& question) const;
    QVariantMap resultToMap(const DLQuizResult& result) const;
    DLQuizResult buildResult() const;

    DLDatabaseManager& m_database;
    QString m_lastError;
    QString m_activeQuizType;
    QString m_activeQuizTitle;
    QString m_groupId;
    int m_currentQuestionIndex = 0;
    int m_correctAnswerCount = 0;
    int m_wrongAnswerCount = 0;
    bool m_finished = false;
    QList<DLQuizQuestion> m_questions;
    QVariantMap m_resultCache;
};

#endif // DLQUIZSESSION_H
