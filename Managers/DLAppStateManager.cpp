#include "DLAppStateManager.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
