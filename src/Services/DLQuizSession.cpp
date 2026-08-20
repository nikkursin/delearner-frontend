#include "DLQuizSession.h"

#include <QtMath>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "DLReviewStatsRepository.h"

DLQuizSession::DLQuizSession(DLDatabaseManager& database)
    : m_database(database)
{
}

void DLQuizSession::start(const QString& quizType,
                          const QString& quizTitle,
                          const QString& groupSyncId,
                          const QList<DLQuizQuestion>& questions)
{
    m_activeQuizType = quizType;
    m_activeQuizTitle = quizTitle;
    m_groupId = groupSyncId;
    m_questions = questions;
    m_currentQuestionIndex = 0;
    m_correctAnswerCount = 0;
    m_wrongAnswerCount = 0;
    m_finished = false;
    m_resultCache.clear();
    m_lastError.clear();
    qCDebug(dlQuiz) << "Started quiz session with question count" << m_questions.size();
}

void DLQuizSession::reset()
{
    m_activeQuizType.clear();
    m_activeQuizTitle.clear();
    m_groupId.clear();
    m_currentQuestionIndex = 0;
    m_correctAnswerCount = 0;
    m_wrongAnswerCount = 0;
    m_finished = false;
    m_questions.clear();
    m_resultCache.clear();
    m_lastError.clear();
    qCDebug(dlQuiz) << "Reset quiz session";
}

QString DLQuizSession::activeQuizType() const
{
    return m_activeQuizType;
}

bool DLQuizSession::isActive() const
{
    return !m_questions.isEmpty();
}

bool DLQuizSession::isFinished() const
{
    return m_finished;
}

QVariantMap DLQuizSession::currentQuestion() const
{
    if (m_questions.isEmpty()
        || m_currentQuestionIndex < 0
        || m_currentQuestionIndex >= m_questions.size()) {
        return {};
    }

    QVariantMap question = questionToMap(m_questions.at(m_currentQuestionIndex));
    question.insert(QStringLiteral("index"), m_currentQuestionIndex);
    question.insert(QStringLiteral("number"), m_currentQuestionIndex + 1);
    question.insert(QStringLiteral("total"), m_questions.size());
    question.insert(QStringLiteral("quizType"), m_activeQuizType);
    question.insert(QStringLiteral("quizTitle"), m_activeQuizTitle);
    return question;
}

QVariantMap DLQuizSession::submitAnswer(const QString& answer)
{
    if (m_questions.isEmpty()
        || m_currentQuestionIndex < 0
        || m_currentQuestionIndex >= m_questions.size()) {
        m_lastError = QStringLiteral("No active quiz question.");
        qCWarning(dlQuiz) << "Rejected quiz answer: no active question";
        return {};
    }

    DLQuizQuestion& question = m_questions[m_currentQuestionIndex];
    if (question.isAnswered) {
        return currentQuestion();
    }

    question.selectedAnswer = answer.trimmed();
    question.isAnswered = true;
    question.isCorrect = QString::compare(question.selectedAnswer, question.answer, Qt::CaseInsensitive) == 0;
    question.feedback = question.isCorrect
        ? QStringLiteral("Correct")
        : QStringLiteral("Correct answer: %1").arg(question.answer);

    DLReviewStatsRepository reviewStats(m_database);
    const bool statsUpdated = question.isCorrect
        ? reviewStats.incrementCorrectAnswer(question.localWordId)
        : reviewStats.incrementWrongAnswer(question.localWordId);

    if (question.isCorrect) {
        ++m_correctAnswerCount;
    } else {
        ++m_wrongAnswerCount;
    }

    m_resultCache.clear();
    m_lastError = statsUpdated ? QString() : m_database.lastError();
    return currentQuestion();
}

bool DLQuizSession::nextQuestion()
{
    if (m_questions.isEmpty()) {
        m_lastError = QStringLiteral("No active quiz.");
        qCWarning(dlQuiz) << "Cannot advance quiz: no active quiz";
        return false;
    }

    if (m_currentQuestionIndex + 1 < m_questions.size()) {
        ++m_currentQuestionIndex;
        m_lastError.clear();
        return true;
    }

    m_finished = true;
    m_resultCache = result();
    m_lastError.clear();
    qCInfo(dlQuiz) << "Finished quiz session with answered count" << (m_correctAnswerCount + m_wrongAnswerCount);
    return false;
}

QVariantMap DLQuizSession::progress() const
{
    QVariantMap progress;
    const int total = static_cast<int>(m_questions.size());
    const int answered = m_correctAnswerCount + m_wrongAnswerCount;
    progress.insert(QStringLiteral("index"), total > 0 ? m_currentQuestionIndex : 0);
    progress.insert(QStringLiteral("number"), total > 0 ? m_currentQuestionIndex + 1 : 0);
    progress.insert(QStringLiteral("total"), total);
    progress.insert(QStringLiteral("answered"), answered);
    progress.insert(QStringLiteral("correct"), m_correctAnswerCount);
    progress.insert(QStringLiteral("wrong"), m_wrongAnswerCount);
    progress.insert(QStringLiteral("finished"), m_finished);
    progress.insert(QStringLiteral("quizType"), m_activeQuizType);
    progress.insert(QStringLiteral("quizTitle"), m_activeQuizTitle);
    progress.insert(QStringLiteral("percent"), total > 0 ? qRound((answered * 100.0) / total) : 0);
    return progress;
}

QVariantMap DLQuizSession::result() const
{
    return m_resultCache.isEmpty() ? resultToMap(buildResult()) : m_resultCache;
}

QString DLQuizSession::lastError() const
{
    return m_lastError;
}

QVariantMap DLQuizSession::questionToMap(const DLQuizQuestion& question) const
{
    QVariantList options;
    for (const QString& option : question.options) {
        options.append(option);
    }

    QVariantMap map;
    map.insert(QStringLiteral("wordId"), question.wordId);
    map.insert(QStringLiteral("prompt"), question.prompt);
    map.insert(QStringLiteral("germanWord"), question.germanWord);
    map.insert(QStringLiteral("nativeTranslation"), question.nativeTranslation);
    map.insert(QStringLiteral("exampleDe"), question.exampleDe);
    map.insert(QStringLiteral("exampleNative"), question.exampleNative);
    map.insert(QStringLiteral("selectedAnswer"), question.selectedAnswer);
    map.insert(QStringLiteral("isAnswered"), question.isAnswered);
    map.insert(QStringLiteral("isCorrect"), question.isCorrect);
    map.insert(QStringLiteral("feedback"), question.feedback);
    map.insert(QStringLiteral("instruction"), question.instruction);
    map.insert(QStringLiteral("answer"), question.answer);
    map.insert(QStringLiteral("options"), options);
    return map;
}

QVariantMap DLQuizSession::resultToMap(const DLQuizResult& result) const
{
    QVariantMap map;
    map.insert(QStringLiteral("quizType"), result.quizType);
    map.insert(QStringLiteral("quizTitle"), result.quizTitle);
    map.insert(QStringLiteral("total"), result.total);
    map.insert(QStringLiteral("answered"), result.answered);
    map.insert(QStringLiteral("correct"), result.correct);
    map.insert(QStringLiteral("wrong"), result.wrong);
    map.insert(QStringLiteral("accuracy"), result.accuracy);
    map.insert(QStringLiteral("groupId"), result.groupId);
    return map;
}

DLQuizResult DLQuizSession::buildResult() const
{
    const int answered = m_correctAnswerCount + m_wrongAnswerCount;
    DLQuizResult result;
    result.quizType = m_activeQuizType;
    result.quizTitle = m_activeQuizTitle;
    result.total = static_cast<int>(m_questions.size());
    result.answered = answered;
    result.correct = m_correctAnswerCount;
    result.wrong = m_wrongAnswerCount;
    result.accuracy = answered > 0 ? qRound((m_correctAnswerCount * 100.0) / answered) : 0;
    result.groupId = m_groupId;
    return result;
}
