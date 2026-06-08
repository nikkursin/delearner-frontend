#include "DLAppStateManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "DLDatabaseManager.h"
#include "DLGroupService.h"
#include "DLLogging.h"
#include "DLQuizService.h"
#include "DLWordService.h"

DLAppStateManager::DLAppStateManager(QObject *parent)
    : QObject{parent}
    , m_wordService(std::make_unique<DLWordService>(DLDatabaseManager::instance()))
    , m_groupService(std::make_unique<DLGroupService>(DLDatabaseManager::instance()))
    , m_quizService(std::make_unique<DLQuizService>(DLDatabaseManager::instance()))
{
    navigateTo(StartupLoadingPage);
}

DLAppStateManager::~DLAppStateManager()
{
    DLDatabaseManager::instance().closeDatabase();
}

void DLAppStateManager::init(const QString& databasePath)
{
    m_databasePath = databasePath;
    qCInfo(dlApp) << "Initializing app state";

    if (!DLDatabaseManager::instance().openDatabase(databasePath)) {
        setLastError(DLDatabaseManager::instance().lastError());
        qCCritical(dlApp) << "App initialization failed:" << m_lastError;
        return;
    }

    setLastError(QString());
    navigateTo(AddEditWordPage);
}

DLAppStateManager::DLScreen DLAppStateManager::currentScreen() const
{
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

void DLAppStateManager::goStartupLoadingPage() { navigateTo(StartupLoadingPage); }
void DLAppStateManager::goWordsPage() { navigateTo(WordsPage); }
void DLAppStateManager::goWordDetails() { navigateTo(WordDetails); }

void DLAppStateManager::goAddEditWordPage()
{
    setSelectedWordId(-1);
    navigateTo(AddEditWordPage);
}

void DLAppStateManager::goGroupsPage() { navigateTo(GroupsPage); }
void DLAppStateManager::goGroupEditPage() { navigateTo(GroupEditPage); }
void DLAppStateManager::goQuizHomePage() { navigateTo(QuizHomePage); }

void DLAppStateManager::goQuizSetupPage(const QString& quizType)
{
    if (m_quizService->selectQuizType(quizType)) {
        emit quizStateChanged();
    }
    navigateTo(QuizSetupPage);
}

void DLAppStateManager::goTranslationQuizSessionPage() { navigateTo(TranslationQuizSessionPage); }
void DLAppStateManager::goArticleQuizSessionPage() { navigateTo(ArticleQuizSessionPage); }
void DLAppStateManager::goQuizResults() { navigateTo(QuizResults); }
void DLAppStateManager::goSettingsPage() { navigateTo(SettingsPage); }

QVariantList DLAppStateManager::availableGroups()
{
    const QVariantList groups = m_groupService->availableGroups();
    setLastError(m_groupService->lastError());
    return groups;
}

int DLAppStateManager::createGroup(const QString& name, const QString& colorHex)
{
    const int newId = m_groupService->createGroup(name, colorHex);
    setLastError(m_groupService->lastError());
    if (newId >= 0) {
        emit groupsChanged();
        emit wordsChanged();
    }
    return newId;
}

bool DLAppStateManager::updateGroup(int id, const QString& name, const QString& colorHex)
{
    const bool success = m_groupService->updateGroup(id, name, colorHex);
    setLastError(m_groupService->lastError());
    if (success) {
        emit groupsChanged();
        emit wordsChanged();
    }
    return success;
}

bool DLAppStateManager::deleteGroup(int id)
{
    const bool success = m_groupService->deleteGroup(id);
    setLastError(m_groupService->lastError());
    if (success) {
        emit groupsChanged();
        emit wordsChanged();
    }
    return success;
}

QVariantList DLAppStateManager::loadWords(const QString& sortMode, int groupId)
{
    const QVariantList words = m_wordService->loadWords(sortMode, groupId);
    setLastError(m_wordService->lastError());
    return words;
}

QVariantList DLAppStateManager::searchWords(const QString& query, const QString& sortMode, int groupId)
{
    const QVariantList words = m_wordService->searchWords(query, sortMode, groupId);
    setLastError(m_wordService->lastError());
    return words;
}

int DLAppStateManager::wordCount(int groupId)
{
    const int count = m_wordService->wordCount(groupId);
    setLastError(m_wordService->lastError());
    return count;
}

int DLAppStateManager::groupCount()
{
    const int count = m_groupService->groupCount();
    setLastError(m_groupService->lastError());
    return count;
}

qint64 DLAppStateManager::databaseSize()
{
    return databaseStats().value(QStringLiteral("db_size_bytes"), -1).toLongLong();
}

QVariantMap DLAppStateManager::databaseStats()
{
    const QVariantMap stats = DLDatabaseManager::instance().getDatabaseStats();
    setLastError(QString());
    return stats;
}

QVariantMap DLAppStateManager::wordById(int id)
{
    const QVariantMap word = m_wordService->wordById(id);
    setLastError(m_wordService->lastError());
    return word;
}

int DLAppStateManager::createWord(const QVariantMap& wordData)
{
    const int newId = m_wordService->createWord(wordData);
    setLastError(m_wordService->lastError());
    if (newId >= 0) {
        emit wordsChanged();
    }
    return newId;
}

bool DLAppStateManager::updateWord(int id, const QVariantMap& wordData)
{
    const bool success = m_wordService->updateWord(id, wordData);
    setLastError(m_wordService->lastError());
    if (success) {
        emit wordsChanged();
    }
    return success;
}

bool DLAppStateManager::deleteWord(int id)
{
    const bool success = m_wordService->deleteWord(id);
    setLastError(m_wordService->lastError());
    if (success) {
        if (m_selectedWordId == id) {
            setSelectedWordId(-1);
        }
        emit wordsChanged();
    }
    return success;
}

bool DLAppStateManager::exportDatabase(const QString& targetPath)
{
    const QString destinationPath = localPathFromUrlOrPath(targetPath);
    if (destinationPath.isEmpty()) {
        setLastError(QStringLiteral("Choose a destination file."));
        qCWarning(dlApp) << "Database export rejected: empty destination";
        return false;
    }

    QFileInfo destinationInfo(destinationPath);
    QDir destinationDir = destinationInfo.absoluteDir();
    if (!destinationDir.exists() && !destinationDir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create export folder."));
        qCWarning(dlApp) << "Database export failed: unable to create folder";
        return false;
    }

    if (destinationInfo.exists() && !QFile::remove(destinationInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to replace the selected export file."));
        qCWarning(dlApp) << "Database export failed: unable to replace destination";
        return false;
    }

    qCInfo(dlApp) << "Exporting database";
    if (!QFile::copy(m_databasePath, destinationInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("Unable to export database."));
        qCWarning(dlApp) << "Database export failed while copying";
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
        qCWarning(dlApp) << "Database replace import rejected: empty source";
        return false;
    }

    const QFileInfo importInfo(importPath);
    if (!importInfo.exists() || !importInfo.isFile()) {
        setLastError(QStringLiteral("Import file not found."));
        qCWarning(dlApp) << "Database replace import failed: source file not found";
        return false;
    }

    if (QDir::cleanPath(importInfo.absoluteFilePath()) == QDir::cleanPath(m_databasePath)) {
        setLastError(QStringLiteral("Choose a different database file to import."));
        qCWarning(dlApp) << "Database replace import rejected: source matches current database";
        return false;
    }

    qCInfo(dlApp) << "Replacing database from import";
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
    emit groupsChanged();
    emit quizStateChanged();
    return true;
}

bool DLAppStateManager::importDatabaseMerge(const QString& sourcePath)
{
    const QString importPath = localPathFromUrlOrPath(sourcePath);
    if (importPath.isEmpty()) {
        setLastError(QStringLiteral("Choose a database file to merge."));
        qCWarning(dlApp) << "Database merge import rejected: empty source";
        return false;
    }

    const bool success = DLDatabaseManager::instance().importDatabaseMerge(importPath);
    setLastError(success ? QString() : DLDatabaseManager::instance().lastError());
    if (success) {
        emit wordsChanged();
        emit groupsChanged();
    }
    return success;
}

bool DLAppStateManager::deleteAllData()
{
    const bool success = DLDatabaseManager::instance().deleteAllData();
    setLastError(success ? QString() : DLDatabaseManager::instance().lastError());
    if (success) {
        setSelectedWordId(-1);
        m_quizService->resetQuiz();
        emit wordsChanged();
        emit groupsChanged();
        emit quizStateChanged();
    }
    return success;
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
    const QVariantList modes = m_quizService->availableQuizModes();
    setLastError(m_quizService->lastError());
    return modes;
}

int DLAppStateManager::availableQuizQuestionCount(const QString& type, int groupId, const QString& partOfSpeech)
{
    const int count = m_quizService->availableQuizQuestionCount(type, groupId, partOfSpeech);
    setLastError(m_quizService->lastError());
    return count;
}

QString DLAppStateManager::selectedQuizType() const
{
    return m_quizService->selectedQuizType();
}

bool DLAppStateManager::canStartQuiz(const QString& type, int groupId, int questionCount, const QString& partOfSpeech)
{
    const bool success = m_quizService->canStartQuiz(type, groupId, questionCount, partOfSpeech);
    setLastError(m_quizService->lastError());
    return success;
}

bool DLAppStateManager::startQuiz(const QString& type, int groupId, int questionCount, const QString& partOfSpeech)
{
    const bool success = m_quizService->startQuiz(type, groupId, questionCount, partOfSpeech);
    setLastError(m_quizService->lastError());
    if (success) {
        emit quizStateChanged();
        navigateTo(m_quizService->activeQuizType() == QStringLiteral("article")
                       ? ArticleQuizSessionPage
                       : TranslationQuizSessionPage);
    }
    return success;
}

QVariantMap DLAppStateManager::currentQuizQuestion() const
{
    return m_quizService->session().currentQuestion();
}

QVariantMap DLAppStateManager::submitQuizAnswer(const QString& answer)
{
    const QVariantMap question = m_quizService->session().submitAnswer(answer);
    setLastError(m_quizService->lastError());
    if (!question.isEmpty()) {
        emit wordsChanged();
        emit quizStateChanged();
    }
    return question;
}

bool DLAppStateManager::nextQuizQuestion()
{
    const bool advanced = m_quizService->session().nextQuestion();
    setLastError(m_quizService->lastError());
    emit quizStateChanged();
    if (!advanced && m_quizService->session().isFinished()) {
        navigateTo(QuizResults);
    }
    return advanced;
}

QVariantMap DLAppStateManager::quizProgress() const
{
    return m_quizService->session().progress();
}

QVariantMap DLAppStateManager::quizResult() const
{
    return m_quizService->session().result();
}

void DLAppStateManager::resetQuiz()
{
    m_quizService->resetQuiz();
    setLastError(m_quizService->lastError());
    emit quizStateChanged();
}

void DLAppStateManager::navigateTo(const DLScreen &screen)
{
    if (m_currentScreen == screen) {
        return;
    }

    m_currentScreen = screen;
    emit currentScreenChanged();
}

void DLAppStateManager::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }

    m_lastError = error;
    emit lastErrorChanged();
}

void DLAppStateManager::setSelectedWordId(int id)
{
    if (m_selectedWordId == id) {
        return;
    }

    m_selectedWordId = id;
    emit selectedWordIdChanged();
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
