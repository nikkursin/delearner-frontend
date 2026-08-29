#include "DLAppStateManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "Auth/DLClientAuthService.h"
#include "DLDatabaseManager.h"
#include "DLDatabaseExportService.h"
#include "DLGroupService.h"
#include "DLLogging.h"
#include "DLQuizService.h"
#include "Sync/DLSyncCoordinator.h"
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

    const QString authSessionPath = QStringLiteral("%1.auth.json").arg(databasePath);
    m_authService = std::make_unique<DLClientAuthService>(authSessionPath);
    connect(m_authService.get(), &DLClientAuthService::authSucceeded, this, [this]() {
        setAuthBusy(false);
        setAuthState(QStringLiteral("authenticated_session"));
        if (!openFreeCore()) {
            return;
        }
        setLastError(QString());
        navigateTo(AddEditWordPage);
        requestActiveSync();
    });
    connect(m_authService.get(), &DLClientAuthService::authFailed, this, [this](const QString& message) {
        setAuthBusy(false);
        setLastError(message);
        setAuthState(QStringLiteral("authentication_required"));
        navigateTo(AuthPage);
    });

    if (!m_authService->hasPriorSuccessfulAuthentication()) {
        setAuthState(QStringLiteral("authentication_required"));
        navigateTo(AuthPage);
        return;
    }

    setAuthState(DLClientAuthService::stateName(m_authService->startupState(m_networkAvailable)));
    if (!openFreeCore()) {
        return;
    }

    setLastError(QString());
    navigateTo(AddEditWordPage);
    requestActiveSync();
}

bool DLAppStateManager::openFreeCore()
{
    if (m_authService && m_authService->hasRegisteredDevice()) {
        DLDatabaseManager::instance().setSyncContext(m_authService->userId(), m_authService->deviceId());
    } else {
        DLDatabaseManager::instance().clearSyncContext();
    }

    if (!DLDatabaseManager::instance().openDatabase(m_databasePath)) {
        setLastError(DLDatabaseManager::instance().lastError());
        qCCritical(dlApp) << "App initialization failed:" << m_lastError;
        return false;
    }

    m_syncCoordinator.reset();
    if (m_authService && m_authService->hasRegisteredDevice()) {
        m_syncCoordinator = std::make_unique<DLSyncCoordinator>(
            DLDatabaseManager::instance(),
            DLClientAuthService::defaultApiBaseUrl(),
            this);
        connect(m_syncCoordinator.get(), &DLSyncCoordinator::syncFinished, this, [this](bool success) {
            handleSyncFinished(success);
        });
    }

    return true;
}

DLAppStateManager::DLScreen DLAppStateManager::currentScreen() const
{
    return m_currentScreen;
}

QString DLAppStateManager::lastError() const
{
    return m_lastError;
}

QString DLAppStateManager::selectedWordId() const
{
    return m_selectedWordId;
}

QString DLAppStateManager::authState() const
{
    return m_authState;
}

bool DLAppStateManager::authBusy() const
{
    return m_authBusy;
}

void DLAppStateManager::goStartupLoadingPage() { navigateTo(StartupLoadingPage); }
void DLAppStateManager::goWordsPage() { navigateTo(WordsPage); }
void DLAppStateManager::goWordDetails() { navigateTo(WordDetails); }

void DLAppStateManager::goAddEditWordPage()
{
    setSelectedWordId(QString());
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

void DLAppStateManager::signIn(const QString& email, const QString& password)
{
    if (!m_authService || m_authBusy) {
        return;
    }

    setLastError(QString());
    setAuthBusy(true);
    m_authService->signIn(email, password);
}

void DLAppStateManager::registerAccount(const QString& email, const QString& password)
{
    if (!m_authService || m_authBusy) {
        return;
    }

    setLastError(QString());
    setAuthBusy(true);
    m_authService->registerAccount(email, password);
}

bool DLAppStateManager::requestManualSync()
{
    const bool requested = requestActiveSync();
    if (!requested) {
        setLastError(QStringLiteral("Sync is unavailable while offline, inactive or unauthenticated."));
    }
    return requested;
}

void DLAppStateManager::setApplicationActive(bool active)
{
    if (m_applicationActive == active) {
        return;
    }

    m_applicationActive = active;
    if (m_applicationActive) {
        requestActiveSync();
    }
}

void DLAppStateManager::setNetworkAvailable(bool available)
{
    if (m_networkAvailable == available) {
        return;
    }

    m_networkAvailable = available;
    if (m_networkAvailable) {
        requestActiveSync();
    }
}

QVariantList DLAppStateManager::availableGroups()
{
    const QVariantList groups = m_groupService->availableGroups();
    setLastError(m_groupService->lastError());
    return groups;
}

QString DLAppStateManager::createGroup(const QString& name, const QString& colorHex)
{
    const QString newId = m_groupService->createGroup(name, colorHex);
    setLastError(m_groupService->lastError());
    if (!newId.isEmpty()) {
        emit groupsChanged();
        emit wordsChanged();
        requestActiveSync();
    }
    return newId;
}

bool DLAppStateManager::updateGroup(const QString& syncId, const QString& name, const QString& colorHex)
{
    const bool success = m_groupService->updateGroup(syncId, name, colorHex);
    setLastError(m_groupService->lastError());
    if (success) {
        emit groupsChanged();
        emit wordsChanged();
        requestActiveSync();
    }
    return success;
}

bool DLAppStateManager::deleteGroup(const QString& syncId)
{
    const bool success = m_groupService->deleteGroup(syncId);
    setLastError(m_groupService->lastError());
    if (success) {
        emit groupsChanged();
        emit wordsChanged();
        requestActiveSync();
    }
    return success;
}

QVariantList DLAppStateManager::loadWords(const QString& sortMode, const QString& groupSyncId)
{
    const QVariantList words = m_wordService->loadWords(sortMode, groupSyncId);
    setLastError(m_wordService->lastError());
    return words;
}

QVariantList DLAppStateManager::searchWords(const QString& query, const QString& sortMode, const QString& groupSyncId)
{
    const QVariantList words = m_wordService->searchWords(query, sortMode, groupSyncId);
    setLastError(m_wordService->lastError());
    return words;
}

int DLAppStateManager::wordCount(const QString& groupSyncId)
{
    const int count = m_wordService->wordCount(groupSyncId);
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

QVariantMap DLAppStateManager::wordById(const QString& syncId)
{
    const QVariantMap word = m_wordService->wordById(syncId);
    setLastError(m_wordService->lastError());
    return word;
}

QString DLAppStateManager::createWord(const QVariantMap& wordData)
{
    const QString newId = m_wordService->createWord(wordData);
    setLastError(m_wordService->lastError());
    if (!newId.isEmpty()) {
        emit wordsChanged();
        requestActiveSync();
    }
    return newId;
}

bool DLAppStateManager::updateWord(const QString& syncId, const QVariantMap& wordData)
{
    const bool success = m_wordService->updateWord(syncId, wordData);
    setLastError(m_wordService->lastError());
    if (success) {
        emit wordsChanged();
        requestActiveSync();
    }
    return success;
}

bool DLAppStateManager::deleteWord(const QString& syncId)
{
    const bool success = m_wordService->deleteWord(syncId);
    setLastError(m_wordService->lastError());
    if (success) {
        if (m_selectedWordId == syncId) {
            setSelectedWordId(QString());
        }
        emit wordsChanged();
        requestActiveSync();
    }
    return success;
}

bool DLAppStateManager::exportVocabularyDatabase()
{
    QString exportedPath;
    QString error;
    const bool success = DLDatabaseExportService().exportVocabularyDatabase(m_databasePath, &exportedPath, &error);
    setLastError(success ? QString() : error);
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

    QString error;
    const bool success = DLDatabaseExportService().exportDatabaseToPath(m_databasePath, destinationPath, &error);
    setLastError(success ? QString() : error);
    return success;
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

    setSelectedWordId(QString());
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
        setSelectedWordId(QString());
        m_quizService->resetQuiz();
        emit wordsChanged();
        emit groupsChanged();
        emit quizStateChanged();
    }
    return success;
}

void DLAppStateManager::openWordDetails(const QString& syncId)
{
    if (syncId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Invalid word id."));
        return;
    }

    setSelectedWordId(syncId.trimmed());
    setLastError(QString());
    navigateTo(WordDetails);
}

void DLAppStateManager::openEditWord(const QString& syncId)
{
    if (syncId.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Invalid word id."));
        return;
    }

    setSelectedWordId(syncId.trimmed());
    setLastError(QString());
    navigateTo(AddEditWordPage);
}

QVariantList DLAppStateManager::availableQuizModes()
{
    const QVariantList modes = m_quizService->availableQuizModes();
    setLastError(m_quizService->lastError());
    return modes;
}

int DLAppStateManager::availableQuizQuestionCount(const QString& type, const QString& groupSyncId, const QString& partOfSpeech)
{
    const int count = m_quizService->availableQuizQuestionCount(type, groupSyncId, partOfSpeech);
    setLastError(m_quizService->lastError());
    return count;
}

QString DLAppStateManager::selectedQuizType() const
{
    return m_quizService->selectedQuizType();
}

bool DLAppStateManager::canStartQuiz(const QString& type, const QString& groupSyncId, int questionCount, const QString& partOfSpeech)
{
    const bool success = m_quizService->canStartQuiz(type, groupSyncId, questionCount, partOfSpeech);
    setLastError(m_quizService->lastError());
    return success;
}

bool DLAppStateManager::startQuiz(const QString& type, const QString& groupSyncId, int questionCount, const QString& partOfSpeech)
{
    const bool success = m_quizService->startQuiz(type, groupSyncId, questionCount, partOfSpeech);
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
        requestActiveSync();
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

void DLAppStateManager::setSelectedWordId(const QString& syncId)
{
    if (m_selectedWordId == syncId) {
        return;
    }

    m_selectedWordId = syncId;
    emit selectedWordIdChanged();
}

void DLAppStateManager::setAuthState(const QString& authState)
{
    if (m_authState == authState) {
        return;
    }

    m_authState = authState;
    emit authStateChanged();
}

void DLAppStateManager::setAuthBusy(bool authBusy)
{
    if (m_authBusy == authBusy) {
        return;
    }

    m_authBusy = authBusy;
    emit authBusyChanged();
}

bool DLAppStateManager::requestActiveSync()
{
    if (!m_applicationActive || !m_networkAvailable || !m_authService || !m_syncCoordinator) {
        return false;
    }

    const std::optional<DLAuthSession> session = m_authService->currentSession();
    if (!session.has_value() || !session->hasSessionCredentials() || !session->hasRegisteredDevice()) {
        return false;
    }

    if (m_syncCoordinator->syncInProgress()) {
        m_syncRequestedDuringCycle = true;
        return true;
    }

    m_syncRequestedDuringCycle = false;
    if (!m_syncCoordinator->startSync(session.value())) {
        setLastError(m_syncCoordinator->lastError());
        return false;
    }
    return true;
}

void DLAppStateManager::handleSyncFinished(bool success)
{
    if (!success || !m_syncRequestedDuringCycle) {
        m_syncRequestedDuringCycle = false;
        return;
    }

    m_syncRequestedDuringCycle = false;
    requestActiveSync();
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
