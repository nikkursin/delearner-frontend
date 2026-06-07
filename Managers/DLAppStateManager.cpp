#include "DLAppStateManager.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>
#include <QUrl>

DLAppStateManager::DLAppStateManager(QObject *parent)
    : QObject{parent}
{
    navigateTo(StartupLoadingPage);
}

DLAppStateManager::~DLAppStateManager() {
    DLDatabaseManager::instance().closeDatabase();
}

void DLAppStateManager::init(const QString& databasePath) {
    m_databasePath = databasePath;

    if (!DLDatabaseManager::instance().openDatabase(databasePath)) {
        setLastError(DLDatabaseManager::instance().lastError());
    } else {
        setLastError(QString());
        navigateTo(AddEditWordPage);
    }
}

DLAppStateManager::DLScreen DLAppStateManager::currentScreen() const {
    return m_currentScreen;
}

QString DLAppStateManager::lastError() const
{
    return m_lastError;
}

int DLAppStateManager::selectedWordId() const
{
    return m_selectedWordId;
}

void DLAppStateManager::goStartupLoadingPage() {
    navigateTo(StartupLoadingPage);
}

void DLAppStateManager::goWordsPage() {
    navigateTo(WordsPage);
}

void DLAppStateManager::goWordDetails() {
    navigateTo(WordDetails);
}

void DLAppStateManager::goAddEditWordPage() {
    navigateTo(AddEditWordPage);
}

void DLAppStateManager::goGroupsPage() {
    navigateTo(GroupsPage);
}

void DLAppStateManager::goGroupEditPage() {
    navigateTo(GroupEditPage);
}

void DLAppStateManager::goQuizHomePage() {
    navigateTo(QuizHomePage);
}

void DLAppStateManager::goQuizSetupPage(const QString& quizType) {
    const QuizType parsedType = quizTypeFromString(quizType);
    if (parsedType != UnknownQuiz) {
        m_selectedQuizType = parsedType;
        emit quizStateChanged();
    }
    navigateTo(QuizSetupPage);
}

void DLAppStateManager::goTranslationQuizSessionPage() {
    navigateTo(TranslationQuizSessionPage);
}

void DLAppStateManager::goArticleQuizSessionPage() {
    navigateTo(ArticleQuizSessionPage);
}

void DLAppStateManager::goQuizResults() {
    navigateTo(QuizResults);
}

void DLAppStateManager::goSettingsPage() {
    navigateTo(SettingsPage);
}

QVariantList DLAppStateManager::availableGroups()
{
    const QVariantList groups = DLDatabaseManager::instance().fetchAllGroups();
    setLastError(QString());
    return groups;
}

int DLAppStateManager::createGroup(const QString& name, const QString& colorHex)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setLastError(QStringLiteral("Group name is required."));
        return -1;
    }

    const QString trimmedColor = colorHex.trimmed().isEmpty()
        ? QStringLiteral("#337fe6")
        : colorHex.trimmed();

    const int newId = DLDatabaseManager::instance().insertGroup(trimmedName, trimmedColor);
    if (newId < 0) {
        setLastError(DLDatabaseManager::instance().lastError());
        return -1;
    }

    setLastError(QString());
    emit groupsChanged();
    emit wordsChanged();
    return newId;
}

bool DLAppStateManager::updateGroup(int id, const QString& name, const QString& colorHex)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid group id."));
        return false;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setLastError(QStringLiteral("Group name is required."));
        return false;
    }

    const QString trimmedColor = colorHex.trimmed().isEmpty()
        ? QStringLiteral("#337fe6")
        : colorHex.trimmed();

    const bool success = DLDatabaseManager::instance().updateGroup(id, trimmedName, trimmedColor);
    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setLastError(QString());
    emit groupsChanged();
    emit wordsChanged();
    return true;
}

bool DLAppStateManager::deleteGroup(int id)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid group id."));
        return false;
    }

    const bool success = DLDatabaseManager::instance().deleteGroup(id);
    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setLastError(QString());
    emit groupsChanged();
    emit wordsChanged();
    return true;
}

QVariantList DLAppStateManager::loadWords(const QString& sortMode, int groupId)
{
    const QVariantList words = DLDatabaseManager::instance().fetchAllWords(sortMode, groupId);
    setLastError(QString());
    return words;
}

QVariantList DLAppStateManager::searchWords(const QString& query, const QString& sortMode, int groupId)
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return loadWords(sortMode, groupId);
    }

    const QVariantList words = DLDatabaseManager::instance().searchWords(trimmedQuery, groupId);
    setLastError(QString());
    return sortedWords(words, sortMode);
}

int DLAppStateManager::wordCount(int groupId)
{
    const int count = DLDatabaseManager::instance().getWordCount(groupId);
    setLastError(QString());
    return count;
}

int DLAppStateManager::groupCount()
{
    const int count = DLDatabaseManager::instance().getGroupCount();
    setLastError(QString());
    return count;
}

qint64 DLAppStateManager::databaseSize()
{
    const QVariantMap stats = databaseStats();
    return stats.value(QStringLiteral("db_size_bytes"), -1).toLongLong();
}

QVariantMap DLAppStateManager::databaseStats()
{
    const QVariantMap stats = DLDatabaseManager::instance().getDatabaseStats();
    setLastError(QString());
    return stats;
}

QVariantMap DLAppStateManager::wordById(int id)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid word id."));
        return {};
    }

    const QVariantMap word = DLDatabaseManager::instance().fetchWordById(id);
    if (word.isEmpty()) {
        setLastError(QStringLiteral("Word not found."));
    } else {
        setLastError(QString());
    }

    return word;
}

int DLAppStateManager::createWord(const QVariantMap& wordData)
{
    const int newId = DLDatabaseManager::instance().insertWord(
        trimmedStringValue(wordData, QStringLiteral("german_word")),
        trimmedStringValue(wordData, QStringLiteral("article")),
        trimmedStringValue(wordData, QStringLiteral("part_of_speech")),
        trimmedStringValue(wordData, QStringLiteral("native_translation")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_de")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_native")),
        groupIdFromWordData(wordData),
        trimmedStringValue(wordData, QStringLiteral("sync_id")),
        trimmedStringValue(wordData, QStringLiteral("plural_form")),
        trimmedStringValue(wordData, QStringLiteral("praeteritum_form")),
        trimmedStringValue(wordData, QStringLiteral("partizip_ii_form")),
        trimmedStringValue(wordData, QStringLiteral("positive_form")),
        trimmedStringValue(wordData, QStringLiteral("comparative_form")),
        trimmedStringValue(wordData, QStringLiteral("superlative_form"))
        );

    if (newId < 0) {
        setLastError(DLDatabaseManager::instance().lastError());
        return -1;
    }

    setLastError(QString());
    emit wordsChanged();
    return newId;
}

bool DLAppStateManager::updateWord(int id, const QVariantMap& wordData)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid word id."));
        return false;
    }

    const bool success = DLDatabaseManager::instance().updateWord(
        id,
        trimmedStringValue(wordData, QStringLiteral("german_word")),
        trimmedStringValue(wordData, QStringLiteral("article")),
        trimmedStringValue(wordData, QStringLiteral("part_of_speech")),
        trimmedStringValue(wordData, QStringLiteral("native_translation")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_de")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_native")),
        groupIdFromWordData(wordData),
        trimmedStringValue(wordData, QStringLiteral("sync_id")),
        trimmedStringValue(wordData, QStringLiteral("plural_form")),
        trimmedStringValue(wordData, QStringLiteral("praeteritum_form")),
        trimmedStringValue(wordData, QStringLiteral("partizip_ii_form")),
        trimmedStringValue(wordData, QStringLiteral("positive_form")),
        trimmedStringValue(wordData, QStringLiteral("comparative_form")),
        trimmedStringValue(wordData, QStringLiteral("superlative_form"))
        );

    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setLastError(QString());
    emit wordsChanged();
    return true;
}

bool DLAppStateManager::deleteWord(int id)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid word id."));
        return false;
    }

    const bool success = DLDatabaseManager::instance().deleteWord(id);
    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    if (m_selectedWordId == id) {
        setSelectedWordId(-1);
    }

    setLastError(QString());
    emit wordsChanged();
    return true;
}

bool DLAppStateManager::exportDatabase(const QString& targetPath)
{
    const QString destinationPath = localPathFromUrlOrPath(targetPath);
    if (destinationPath.isEmpty()) {
        setLastError(QStringLiteral("Choose a destination file."));
        return false;
    }

    QFileInfo destinationInfo(destinationPath);
    QDir destinationDir = destinationInfo.absoluteDir();
    if (!destinationDir.exists() && !destinationDir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create export folder."));
        return false;
    }

    if (destinationInfo.exists() && !QFile::remove(destinationInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to replace the selected export file."));
        return false;
    }

    if (!QFile::copy(m_databasePath, destinationInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to export database."));
        return false;
    }

    setLastError(QString());
    return true;
}

bool DLAppStateManager::importDatabaseReplace(const QString& sourcePath)
{
    const QString importPath = localPathFromUrlOrPath(sourcePath);
    if (importPath.isEmpty()) {
        setLastError(QStringLiteral("Choose a database file to import."));
        return false;
    }

    const QFileInfo importInfo(importPath);
    if (!importInfo.exists() || !importInfo.isFile()) {
        setLastError(QStringLiteral("Import file not found."));
        return false;
    }

    if (QDir::cleanPath(importInfo.absoluteFilePath()) == QDir::cleanPath(m_databasePath)) {
        setLastError(QStringLiteral("Choose a different database file to import."));
        return false;
    }

    DLDatabaseManager::instance().closeDatabase();

    bool success = true;
    const QFileInfo currentInfo(m_databasePath);
    if (currentInfo.exists() && !QFile::remove(currentInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to replace the current database."));
        success = false;
    } else if (!QFile::copy(importInfo.absoluteFilePath(), currentInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to copy imported database."));
        success = false;
    }

    if (!DLDatabaseManager::instance().openDatabase(m_databasePath)) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    if (!success) {
        return false;
    }

    setSelectedWordId(-1);
    setLastError(QString());
    emit wordsChanged();
    return true;
}

bool DLAppStateManager::importDatabaseMerge(const QString& sourcePath)
{
    const QString importPath = localPathFromUrlOrPath(sourcePath);
    if (importPath.isEmpty()) {
        setLastError(QStringLiteral("Choose a database file to merge."));
        return false;
    }

    const bool success = DLDatabaseManager::instance().importDatabaseMerge(importPath);
    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setLastError(QString());
    emit wordsChanged();
    return true;
}

bool DLAppStateManager::deleteAllData()
{
    const bool success = DLDatabaseManager::instance().deleteAllData();
    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setSelectedWordId(-1);
    setLastError(QString());
    emit wordsChanged();
    return true;
}

void DLAppStateManager::openWordDetails(int id)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid word id."));
        return;
    }

    setSelectedWordId(id);
    setLastError(QString());
    navigateTo(WordDetails);
}

void DLAppStateManager::openEditWord(int id)
{
    if (id <= 0) {
        setLastError(QStringLiteral("Invalid word id."));
        return;
    }

    setSelectedWordId(id);
    setLastError(QString());
    navigateTo(AddEditWordPage);
}

QVariantList DLAppStateManager::availableQuizModes()
{
    QVariantList modes;

    const int translationCount = availableQuestionCount(TranslationQuiz, -1);
    QVariantMap translationMode;
    translationMode.insert(QStringLiteral("type"), quizTypeToString(TranslationQuiz));
    translationMode.insert(QStringLiteral("title"), quizTypeTitle(TranslationQuiz));
    translationMode.insert(QStringLiteral("description"), QStringLiteral("Pick the native translation for each German word."));
    translationMode.insert(QStringLiteral("availableCount"), translationCount);
    translationMode.insert(QStringLiteral("minimumCount"), 2);
    translationMode.insert(QStringLiteral("available"), translationCount >= 2);
    translationMode.insert(QStringLiteral("unavailableReason"), translationCount >= 2 ? QString() : QStringLiteral("Add at least 2 words with translations to start."));
    modes.append(translationMode);

    const int articleCount = availableQuestionCount(ArticleQuiz, -1);
    QVariantMap articleMode;
    articleMode.insert(QStringLiteral("type"), quizTypeToString(ArticleQuiz));
    articleMode.insert(QStringLiteral("title"), quizTypeTitle(ArticleQuiz));
    articleMode.insert(QStringLiteral("description"), QStringLiteral("Choose der, die, or das for saved German nouns."));
    articleMode.insert(QStringLiteral("availableCount"), articleCount);
    articleMode.insert(QStringLiteral("minimumCount"), 1);
    articleMode.insert(QStringLiteral("available"), articleCount >= 1);
    articleMode.insert(QStringLiteral("unavailableReason"), articleCount >= 1 ? QString() : QStringLiteral("Add at least 1 noun with an article to start."));
    modes.append(articleMode);

    setLastError(QString());
    return modes;
}

QString DLAppStateManager::selectedQuizType() const
{
    return quizTypeToString(m_selectedQuizType);
}

int DLAppStateManager::availableQuizQuestionCount(const QString& type, int groupId, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    const int count = availableQuestionCount(quizType, groupId, partOfSpeech);
    if (quizType == UnknownQuiz) {
        setLastError(QStringLiteral("Choose a quiz type."));
    } else {
        setLastError(QString());
    }
    return count;
}

bool DLAppStateManager::canStartQuiz(const QString& type, int groupId, int questionCount, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    if (quizType == UnknownQuiz) {
        setLastError(QStringLiteral("Choose a quiz type."));
        return false;
    }

    if (questionCount <= 0) {
        setLastError(QStringLiteral("Choose at least 1 question."));
        return false;
    }

    const int availableCount = availableQuestionCount(quizType, groupId, partOfSpeech);
    const int minimumCount = quizType == TranslationQuiz ? 2 : 1;
    if (availableCount < minimumCount) {
        setLastError(quizType == TranslationQuiz
                         ? QStringLiteral("Not enough words with translations for this quiz.")
                         : QStringLiteral("Not enough nouns with articles for this quiz."));
        return false;
    }

    if (questionCount > availableCount) {
        setLastError(QStringLiteral("This scope only has %1 available %2.")
                         .arg(availableCount)
                         .arg(availableCount == 1 ? QStringLiteral("question") : QStringLiteral("questions")));
        return false;
    }

    setLastError(QString());
    return true;
}

bool DLAppStateManager::startQuiz(const QString& type, int groupId, int questionCount, const QString& partOfSpeech)
{
    const QuizType quizType = quizTypeFromString(type);
    if (!canStartQuiz(type, groupId, questionCount, partOfSpeech)) {
        return false;
    }

    const int availableCount = availableQuestionCount(quizType, groupId, partOfSpeech);
    const QVariantList pool = quizType == ArticleQuiz
        ? DLDatabaseManager::instance().fetchNouns(groupId)
        : DLDatabaseManager::instance().fetchTranslationQuizWords(availableCount, groupId, partOfSpeech);

    QVariantList questionRows = pool;
    if (quizType == ArticleQuiz) {
        std::shuffle(questionRows.begin(), questionRows.end(), *QRandomGenerator::global());
    }

    m_quizQuestions.clear();
    const int limit = std::min(questionCount, static_cast<int>(questionRows.size()));
    for (int i = 0; i < limit; ++i) {
        const QVariantMap question = buildQuestion(quizType, questionRows.at(i).toMap(), pool);
        if (!question.isEmpty()) {
            m_quizQuestions.append(question);
        }
    }

    if (m_quizQuestions.isEmpty()) {
        setLastError(QStringLiteral("Unable to build quiz questions."));
        return false;
    }

    m_activeQuizType = quizType;
    m_selectedQuizType = quizType;
    m_quizGroupId = groupId;
    m_currentQuestionIndex = 0;
    m_correctAnswerCount = 0;
    m_wrongAnswerCount = 0;
    m_quizFinished = false;
    m_quizResultCache.clear();

    setLastError(QString());
    emit quizStateChanged();
    navigateTo(quizType == ArticleQuiz ? ArticleQuizSessionPage : TranslationQuizSessionPage);
    return true;
}

QVariantMap DLAppStateManager::currentQuizQuestion() const
{
    if (m_quizQuestions.isEmpty()
        || m_currentQuestionIndex < 0
        || m_currentQuestionIndex >= m_quizQuestions.size()) {
        return {};
    }

    QVariantMap question = m_quizQuestions.at(m_currentQuestionIndex);
    question.insert(QStringLiteral("index"), m_currentQuestionIndex);
    question.insert(QStringLiteral("number"), m_currentQuestionIndex + 1);
    question.insert(QStringLiteral("total"), m_quizQuestions.size());
    question.insert(QStringLiteral("quizType"), quizTypeToString(m_activeQuizType));
    question.insert(QStringLiteral("quizTitle"), quizTypeTitle(m_activeQuizType));
    return question;
}

QVariantMap DLAppStateManager::submitQuizAnswer(const QString& answer)
{
    if (m_quizQuestions.isEmpty()
        || m_currentQuestionIndex < 0
        || m_currentQuestionIndex >= m_quizQuestions.size()) {
        setLastError(QStringLiteral("No active quiz question."));
        return {};
    }

    QVariantMap question = m_quizQuestions[m_currentQuestionIndex];
    if (question.value(QStringLiteral("isAnswered")).toBool()) {
        return currentQuizQuestion();
    }

    const QString selectedAnswer = answer.trimmed();
    const QString correctAnswer = question.value(QStringLiteral("answer")).toString();
    const bool isCorrect = QString::compare(selectedAnswer, correctAnswer, Qt::CaseInsensitive) == 0;
    const int wordId = question.value(QStringLiteral("wordId")).toInt();

    question.insert(QStringLiteral("selectedAnswer"), selectedAnswer);
    question.insert(QStringLiteral("isAnswered"), true);
    question.insert(QStringLiteral("isCorrect"), isCorrect);
    question.insert(QStringLiteral("feedback"), isCorrect
                        ? QStringLiteral("Correct")
                        : QStringLiteral("Correct answer: %1").arg(correctAnswer));
    m_quizQuestions[m_currentQuestionIndex] = question;

    if (isCorrect) {
        ++m_correctAnswerCount;
        if (!DLDatabaseManager::instance().incrementCorrectAnswer(wordId)) {
            setLastError(DLDatabaseManager::instance().lastError());
        } else {
            setLastError(QString());
        }
    } else {
        ++m_wrongAnswerCount;
        if (!DLDatabaseManager::instance().incrementWrongAnswer(wordId)) {
            setLastError(DLDatabaseManager::instance().lastError());
        } else {
            setLastError(QString());
        }
    }

    emit wordsChanged();
    emit quizStateChanged();
    return currentQuizQuestion();
}

bool DLAppStateManager::nextQuizQuestion()
{
    if (m_quizQuestions.isEmpty()) {
        setLastError(QStringLiteral("No active quiz."));
        return false;
    }

    if (m_currentQuestionIndex + 1 < m_quizQuestions.size()) {
        ++m_currentQuestionIndex;
        setLastError(QString());
        emit quizStateChanged();
        return true;
    }

    m_quizFinished = true;
    updateQuizResultCache();
    setLastError(QString());
    emit quizStateChanged();
    navigateTo(QuizResults);
    return false;
}

QVariantMap DLAppStateManager::quizProgress() const
{
    QVariantMap progress;
    const int total = static_cast<int>(m_quizQuestions.size());
    const int answered = m_correctAnswerCount + m_wrongAnswerCount;
    progress.insert(QStringLiteral("index"), total > 0 ? m_currentQuestionIndex : 0);
    progress.insert(QStringLiteral("number"), total > 0 ? m_currentQuestionIndex + 1 : 0);
    progress.insert(QStringLiteral("total"), total);
    progress.insert(QStringLiteral("answered"), answered);
    progress.insert(QStringLiteral("correct"), m_correctAnswerCount);
    progress.insert(QStringLiteral("wrong"), m_wrongAnswerCount);
    progress.insert(QStringLiteral("finished"), m_quizFinished);
    progress.insert(QStringLiteral("quizType"), quizTypeToString(m_activeQuizType));
    progress.insert(QStringLiteral("quizTitle"), quizTypeTitle(m_activeQuizType));
    progress.insert(QStringLiteral("percent"), total > 0 ? qRound((answered * 100.0) / total) : 0);
    return progress;
}

QVariantMap DLAppStateManager::quizResult() const
{
    if (!m_quizResultCache.isEmpty()) {
        return m_quizResultCache;
    }

    QVariantMap result;
    const int total = static_cast<int>(m_quizQuestions.size());
    const int answered = m_correctAnswerCount + m_wrongAnswerCount;
    result.insert(QStringLiteral("quizType"), quizTypeToString(m_activeQuizType));
    result.insert(QStringLiteral("quizTitle"), quizTypeTitle(m_activeQuizType));
    result.insert(QStringLiteral("total"), total);
    result.insert(QStringLiteral("answered"), answered);
    result.insert(QStringLiteral("correct"), m_correctAnswerCount);
    result.insert(QStringLiteral("wrong"), m_wrongAnswerCount);
    result.insert(QStringLiteral("accuracy"), answered > 0 ? qRound((m_correctAnswerCount * 100.0) / answered) : 0);
    result.insert(QStringLiteral("groupId"), m_quizGroupId);
    return result;
}

void DLAppStateManager::resetQuiz()
{
    m_activeQuizType = UnknownQuiz;
    m_quizGroupId = -1;
    m_currentQuestionIndex = 0;
    m_correctAnswerCount = 0;
    m_wrongAnswerCount = 0;
    m_quizFinished = false;
    m_quizQuestions.clear();
    m_quizResultCache.clear();
    setLastError(QString());
    emit quizStateChanged();
}

void DLAppStateManager::navigateTo(const DLScreen &screen)
{
    if (m_currentScreen == screen)
        return;

    m_currentScreen = screen;
    emit currentScreenChanged();
}

void DLAppStateManager::setLastError(const QString& error)
{
    if (m_lastError == error)
        return;

    m_lastError = error;
    emit lastErrorChanged();
}

void DLAppStateManager::setSelectedWordId(int id)
{
    if (m_selectedWordId == id)
        return;

    m_selectedWordId = id;
    emit selectedWordIdChanged();
}

DLAppStateManager::QuizType DLAppStateManager::quizTypeFromString(const QString& type) const
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

QString DLAppStateManager::quizTypeToString(QuizType type) const
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

QString DLAppStateManager::quizTypeTitle(QuizType type) const
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

int DLAppStateManager::availableQuestionCount(QuizType type, int groupId, const QString& partOfSpeech) const
{
    if (type == TranslationQuiz) {
        return DLDatabaseManager::instance().getTranslationQuizWordCount(groupId, partOfSpeech);
    }

    if (type == ArticleQuiz) {
        return DLDatabaseManager::instance().getNounCount(groupId);
    }

    return 0;
}

QVariantList DLAppStateManager::answerOptionsForTranslation(const QVariantMap& word, const QVariantList& pool) const
{
    QVariantList options;
    QSet<QString> seen;

    const QString correctAnswer = word.value(QStringLiteral("native_translation")).toString().trimmed();
    if (!correctAnswer.isEmpty()) {
        options.append(correctAnswer);
        seen.insert(correctAnswer.toLower());
    }

    QVariantList shuffledPool = pool;
    std::shuffle(shuffledPool.begin(), shuffledPool.end(), *QRandomGenerator::global());

    for (const QVariant& item : shuffledPool) {
        if (options.size() >= 4) {
            break;
        }

        const QString option = item.toMap().value(QStringLiteral("native_translation")).toString().trimmed();
        const QString key = option.toLower();
        if (!option.isEmpty() && !seen.contains(key)) {
            options.append(option);
            seen.insert(key);
        }
    }

    std::shuffle(options.begin(), options.end(), *QRandomGenerator::global());
    return options;
}

QVariantMap DLAppStateManager::buildQuestion(QuizType type, const QVariantMap& word, const QVariantList& pool) const
{
    const int wordId = word.value(QStringLiteral("id")).toInt();
    const QString germanWord = word.value(QStringLiteral("german_word")).toString().trimmed();
    if (wordId <= 0 || germanWord.isEmpty()) {
        return {};
    }

    QVariantMap question;
    question.insert(QStringLiteral("wordId"), wordId);
    question.insert(QStringLiteral("prompt"), germanWord);
    question.insert(QStringLiteral("germanWord"), germanWord);
    question.insert(QStringLiteral("nativeTranslation"), word.value(QStringLiteral("native_translation")).toString());
    question.insert(QStringLiteral("exampleDe"), word.value(QStringLiteral("example_phrase_de")).toString());
    question.insert(QStringLiteral("exampleNative"), word.value(QStringLiteral("example_phrase_native")).toString());
    question.insert(QStringLiteral("selectedAnswer"), QString());
    question.insert(QStringLiteral("isAnswered"), false);
    question.insert(QStringLiteral("isCorrect"), false);
    question.insert(QStringLiteral("feedback"), QString());

    if (type == ArticleQuiz) {
        const QString article = word.value(QStringLiteral("article")).toString().trimmed();
        if (article != QStringLiteral("der") && article != QStringLiteral("die") && article != QStringLiteral("das")) {
            return {};
        }

        question.insert(QStringLiteral("instruction"), QStringLiteral("Choose the correct article."));
        question.insert(QStringLiteral("answer"), article);
        question.insert(QStringLiteral("options"), QVariantList({ QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("das") }));
    } else if (type == TranslationQuiz) {
        const QString translation = word.value(QStringLiteral("native_translation")).toString().trimmed();
        if (translation.isEmpty()) {
            return {};
        }

        question.insert(QStringLiteral("instruction"), QStringLiteral("Choose the native translation."));
        question.insert(QStringLiteral("answer"), translation);
        question.insert(QStringLiteral("options"), answerOptionsForTranslation(word, pool));
    }

    return question;
}

void DLAppStateManager::updateQuizResultCache()
{
    m_quizResultCache = quizResult();
}

QVariantList DLAppStateManager::sortedWords(const QVariantList& words, const QString& sortMode) const
{
    QVariantList sorted = words;

    std::sort(sorted.begin(), sorted.end(), [sortMode](const QVariant& left, const QVariant& right) {
        const QVariantMap leftWord = left.toMap();
        const QVariantMap rightWord = right.toMap();

        if (sortMode == QStringLiteral("oldest")) {
            return leftWord.value(QStringLiteral("created_at")).toDouble() < rightWord.value(QStringLiteral("created_at")).toDouble();
        }

        if (sortMode == QStringLiteral("az")) {
            return QString::localeAwareCompare(
                       leftWord.value(QStringLiteral("german_word")).toString(),
                       rightWord.value(QStringLiteral("german_word")).toString()) < 0;
        }

        if (sortMode == QStringLiteral("za")) {
            return QString::localeAwareCompare(
                       leftWord.value(QStringLiteral("german_word")).toString(),
                       rightWord.value(QStringLiteral("german_word")).toString()) > 0;
        }

        return leftWord.value(QStringLiteral("created_at")).toDouble() > rightWord.value(QStringLiteral("created_at")).toDouble();
    });

    return sorted;
}

int DLAppStateManager::groupIdFromWordData(const QVariantMap& wordData) const
{
    const QVariant value = wordData.value(QStringLiteral("group_id"));
    if (!value.isValid() || value.isNull())
        return -1;

    bool ok = false;
    const int groupId = value.toInt(&ok);
    if (!ok || groupId < 0)
        return -1;

    return groupId;
}

QString DLAppStateManager::trimmedStringValue(const QVariantMap& wordData, const QString& key) const
{
    static const QVariantMap camelCaseAliases = {
        { QStringLiteral("sync_id"), QStringLiteral("syncId") },
        { QStringLiteral("plural_form"), QStringLiteral("pluralForm") },
        { QStringLiteral("praeteritum_form"), QStringLiteral("praeteritumForm") },
        { QStringLiteral("partizip_ii_form"), QStringLiteral("partizipIIForm") },
        { QStringLiteral("positive_form"), QStringLiteral("positiveForm") },
        { QStringLiteral("comparative_form"), QStringLiteral("comparativeForm") },
        { QStringLiteral("superlative_form"), QStringLiteral("superlativeForm") }
    };

    if (!wordData.contains(key) && camelCaseAliases.contains(key)) {
        return wordData.value(camelCaseAliases.value(key).toString()).toString().trimmed();
    }

    return wordData.value(key).toString().trimmed();
}

QString DLAppStateManager::localPathFromUrlOrPath(const QString& value) const
{
    const QString trimmedValue = value.trimmed();
    if (trimmedValue.isEmpty()) {
        return QString();
    }

    const QUrl url(trimmedValue);
    if (url.isValid() && url.isLocalFile()) {
        return url.toLocalFile();
    }

    return trimmedValue;
}
