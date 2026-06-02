#include "DLAppStateManager.h"

DLAppStateManager::DLAppStateManager(QObject *parent)
    : QObject{parent}
{
    navigateTo(StartupLoadingPage);
}

DLAppStateManager::~DLAppStateManager() {
    DLDatabaseManager::instance().closeDatabase();
}

void DLAppStateManager::init(const QString& databasePath) {
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

void DLAppStateManager::goQuizSetupPage() {
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
        groupIdFromWordData(wordData)
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
        groupIdFromWordData(wordData)
        );

    if (!success) {
        setLastError(DLDatabaseManager::instance().lastError());
        return false;
    }

    setLastError(QString());
    emit wordsChanged();
    return true;
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
    return wordData.value(key).toString().trimmed();
}
