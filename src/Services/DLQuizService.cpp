#include "DLQuizService.h"

#include <algorithm>

#include <QRandomGenerator>
#include <QSet>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "DLQuizRepository.h"

DLQuizService::DLQuizService(DLDatabaseManager& database)
    : m_database(database)
    , m_session(database)
{
}

QVariantList DLQuizService::availableQuizModes()
{
    QVariantList modes;

    const int translationCount = availableQuestionCount(TranslationQuiz, QString());
    QVariantMap translationMode;
    translationMode.insert(QStringLiteral("type"), quizTypeToString(TranslationQuiz));
    translationMode.insert(QStringLiteral("title"), quizTypeTitle(TranslationQuiz));
    translationMode.insert(QStringLiteral("description"), QStringLiteral("Pick the native translation for each German word."));
    translationMode.insert(QStringLiteral("availableCount"), translationCount);
    translationMode.insert(QStringLiteral("minimumCount"), 2);
    translationMode.insert(QStringLiteral("available"), translationCount >= 2);
    translationMode.insert(QStringLiteral("unavailableReason"), translationCount >= 2 ? QString() : QStringLiteral("Add at least 2 words with translations to start."));
    modes.append(translationMode);

    const int articleCount = availableQuestionCount(ArticleQuiz, QString());
    QVariantMap articleMode;
    articleMode.insert(QStringLiteral("type"), quizTypeToString(ArticleQuiz));
    articleMode.insert(QStringLiteral("title"), quizTypeTitle(ArticleQuiz));
    articleMode.insert(QStringLiteral("description"), QStringLiteral("Choose der, die, or das for saved German nouns."));
    articleMode.insert(QStringLiteral("availableCount"), articleCount);
    articleMode.insert(QStringLiteral("minimumCount"), 1);
    articleMode.insert(QStringLiteral("available"), articleCount >= 1);
    articleMode.insert(QStringLiteral("unavailableReason"), articleCount >= 1 ? QString() : QStringLiteral("Add at least 1 noun with an article to start."));
    modes.append(articleMode);

    m_lastError.clear();
    return modes;
}

int DLQuizService::availableQuizQuestionCount(const QString& type, const QString& groupSyncId, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    const int count = availableQuestionCount(quizType, groupSyncId, partOfSpeech);
    m_lastError = quizType == UnknownQuiz ? QStringLiteral("Choose a quiz type.") : QString();
    return count;
}

bool DLQuizService::canStartQuiz(const QString& type, const QString& groupSyncId, int questionCount, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    if (quizType == UnknownQuiz) {
        m_lastError = QStringLiteral("Choose a quiz type.");
        qCWarning(dlService) << "Rejected quiz start: unknown quiz type";
        return false;
    }

    if (questionCount <= 0) {
        m_lastError = QStringLiteral("Choose at least 1 question.");
        qCWarning(dlService) << "Rejected quiz start: invalid question count";
        return false;
    }

    const int availableCount = availableQuestionCount(quizType, groupSyncId, partOfSpeech);
    const int minimumCount = quizType == TranslationQuiz ? 2 : 1;
    if (availableCount < minimumCount) {
        m_lastError = quizType == TranslationQuiz
            ? QStringLiteral("Not enough words with translations for this quiz.")
            : QStringLiteral("Not enough nouns with articles for this quiz.");
        qCWarning(dlService) << "Rejected quiz start: insufficient available questions";
        return false;
    }

    if (questionCount > availableCount) {
        m_lastError = QStringLiteral("This scope only has %1 available %2.")
            .arg(availableCount)
            .arg(availableCount == 1 ? QStringLiteral("question") : QStringLiteral("questions"));
        qCWarning(dlService) << "Rejected quiz start: requested more questions than available";
        return false;
    }

    m_lastError.clear();
    return true;
}

bool DLQuizService::startQuiz(const QString& type, const QString& groupSyncId, int questionCount, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    if (!canStartQuiz(type, groupSyncId, questionCount, partOfSpeech)) {
        return false;
    }

    DLQuizRepository quizRepository(m_database);
    const int availableCount = availableQuestionCount(quizType, groupSyncId, partOfSpeech);
    const QList<DLWord> pool = quizType == ArticleQuiz
        ? quizRepository.fetchNouns(groupSyncId)
        : quizRepository.fetchTranslationQuizWords(availableCount, groupSyncId, partOfSpeech);

    QList<DLWord> questionWords = pool;
    if (quizType == ArticleQuiz) {
        std::shuffle(questionWords.begin(), questionWords.end(), *QRandomGenerator::global());
    }

    const QList<DLQuizQuestion> questions = buildQuestions(
        quizType,
        questionWords,
        pool,
        std::min(questionCount, static_cast<int>(questionWords.size())));

    if (questions.isEmpty()) {
        m_lastError = QStringLiteral("Unable to build quiz questions.");
        qCWarning(dlQuiz) << "Failed to build quiz questions";
        return false;
    }

    m_selectedQuizType = quizType;
    m_session.start(quizTypeToString(quizType), quizTypeTitle(quizType), groupSyncId, questions);
    qCInfo(dlQuiz) << "Started quiz with question count" << questions.size();
    m_lastError.clear();
    return true;
}

void DLQuizService::resetQuiz()
{
    m_session.reset();
    qCDebug(dlQuiz) << "Reset quiz";
    m_lastError.clear();
}

bool DLQuizService::selectQuizType(const QString& type)
{
    const QuizType quizType = quizTypeFromString(type);
    if (quizType == UnknownQuiz) {
        return false;
    }

    m_selectedQuizType = quizType;
    m_lastError.clear();
    return true;
}

QString DLQuizService::selectedQuizType() const
{
    return quizTypeToString(m_selectedQuizType);
}

QString DLQuizService::activeQuizType() const
{
    return m_session.activeQuizType();
}

DLQuizSession& DLQuizService::session()
{
    return m_session;
}

const DLQuizSession& DLQuizService::session() const
{
    return m_session;
}

QString DLQuizService::lastError() const
{
    return m_lastError.isEmpty() ? m_session.lastError() : m_lastError;
}

DLQuizService::QuizType DLQuizService::quizTypeFromString(const QString& type) const
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("translation") || normalized == QStringLiteral("translation_quiz")) {
        return TranslationQuiz;
    }
    if (normalized == QStringLiteral("article") || normalized == QStringLiteral("article_quiz")) {
        return ArticleQuiz;
    }
    return UnknownQuiz;
}

QString DLQuizService::quizTypeToString(QuizType type) const
{
    switch (type) {
    case TranslationQuiz:
        return QStringLiteral("translation");
    case ArticleQuiz:
        return QStringLiteral("article");
    default:
        return QString();
    }
}

QString DLQuizService::quizTypeTitle(QuizType type) const
{
    switch (type) {
    case TranslationQuiz:
        return QStringLiteral("Translation Quiz");
    case ArticleQuiz:
        return QStringLiteral("Article Quiz");
    default:
        return QStringLiteral("Quiz");
    }
}

int DLQuizService::availableQuestionCount(QuizType type, const QString& groupSyncId, const QString& partOfSpeech) const
{
    DLQuizRepository quizRepository(m_database);
    if (type == TranslationQuiz) {
        return quizRepository.getTranslationQuizWordCount(groupSyncId, partOfSpeech);
    }
    if (type == ArticleQuiz) {
        return quizRepository.getNounCount(groupSyncId);
    }
    return 0;
}

QList<DLQuizQuestion> DLQuizService::buildQuestions(QuizType type,
                                                    const QList<DLWord>& questionWords,
                                                    const QList<DLWord>& pool,
                                                    int limit) const
{
    QList<DLQuizQuestion> questions;
    for (int i = 0; i < limit; ++i) {
        const DLQuizQuestion question = buildQuestion(type, questionWords.at(i), pool);
        if (!question.wordId.isEmpty()) {
            questions.append(question);
        }
    }
    return questions;
}

DLQuizQuestion DLQuizService::buildQuestion(QuizType type, const DLWord& word, const QList<DLWord>& pool) const
{
    const QString germanWord = word.germanWord.trimmed();
    if (word.id <= 0 || word.syncId.trimmed().isEmpty() || germanWord.isEmpty()) {
        return {};
    }

    DLQuizQuestion question;
    question.wordId = word.syncId;
    question.localWordId = word.id;
    question.quizType = quizTypeToString(type);
    question.prompt = germanWord;
    question.germanWord = germanWord;
    question.nativeTranslation = word.nativeTranslation;
    question.exampleDe = word.examplePhraseDe;
    question.exampleNative = word.examplePhraseNative;

    if (type == ArticleQuiz) {
        const QString article = word.article.trimmed();
        if (article != QStringLiteral("der") && article != QStringLiteral("die") && article != QStringLiteral("das")) {
            return {};
        }

        question.instruction = QStringLiteral("Choose the correct article.");
        question.answer = article;
        question.options = { QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("das") };
        return question;
    }

    if (type == TranslationQuiz) {
        const QString translation = word.nativeTranslation.trimmed();
        if (translation.isEmpty()) {
            return {};
        }

        question.instruction = QStringLiteral("Choose the native translation.");
        question.answer = translation;
        question.options = answerOptionsForTranslation(word, pool);
        return question;
    }

    return {};
}

QStringList DLQuizService::answerOptionsForTranslation(const DLWord& word, QList<DLWord> pool) const
{
    QStringList options;
    QSet<QString> seen;

    const QString correctAnswer = word.nativeTranslation.trimmed();
    if (!correctAnswer.isEmpty()) {
        options.append(correctAnswer);
        seen.insert(correctAnswer.toLower());
    }

    std::shuffle(pool.begin(), pool.end(), *QRandomGenerator::global());
    for (const DLWord& poolWord : pool) {
        if (options.size() >= 4) {
            break;
        }

        const QString option = poolWord.nativeTranslation.trimmed();
        const QString key = option.toLower();
        if (!option.isEmpty() && !seen.contains(key)) {
            options.append(option);
            seen.insert(key);
        }
    }

    std::shuffle(options.begin(), options.end(), *QRandomGenerator::global());
    return options;
}
