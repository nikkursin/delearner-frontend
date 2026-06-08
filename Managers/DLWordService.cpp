#include "DLWordService.h"

#include <algorithm>

#include "DLDatabaseManager.h"
#include "DLLogging.h"

DLWordService::DLWordService(DLDatabaseManager& database)
    : m_database(database)
{
}

int DLWordService::createWord(const QVariantMap& wordData)
{
    const int newId = m_database.insertWord(
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
        trimmedStringValue(wordData, QStringLiteral("superlative_form")));

    m_lastError = newId < 0 ? m_database.lastError() : QString();
    return newId;
}

bool DLWordService::updateWord(int id, const QVariantMap& wordData)
{
    if (id <= 0) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word update: invalid id";
        return false;
    }

    const bool success = m_database.updateWord(
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
        trimmedStringValue(wordData, QStringLiteral("superlative_form")));

    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

bool DLWordService::deleteWord(int id)
{
    if (id <= 0) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word delete: invalid id";
        return false;
    }

    const bool success = m_database.deleteWord(id);
    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

QVariantMap DLWordService::wordById(int id)
{
    if (id <= 0) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word lookup: invalid id";
        return {};
    }

    const QVariantMap word = m_database.fetchWordById(id);
    m_lastError = word.isEmpty() ? QStringLiteral("Word not found.") : QString();
    return word;
}

QVariantList DLWordService::loadWords(const QString& sortMode, int groupId)
{
    m_lastError.clear();
    return m_database.fetchAllWords(sortMode, groupId);
}

QVariantList DLWordService::searchWords(const QString& query, const QString& sortMode, int groupId)
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return loadWords(sortMode, groupId);
    }

    m_lastError.clear();
    return sortedWords(m_database.searchWords(trimmedQuery, groupId), sortMode);
}

int DLWordService::wordCount(int groupId)
{
    m_lastError.clear();
    return m_database.getWordCount(groupId);
}

QString DLWordService::lastError() const
{
    return m_lastError;
}

QString DLWordService::trimmedStringValue(const QVariantMap& wordData, const QString& key) const
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

int DLWordService::groupIdFromWordData(const QVariantMap& wordData) const
{
    const QVariant value = wordData.value(QStringLiteral("group_id"));
    if (!value.isValid() || value.isNull()) {
        return -1;
    }

    bool ok = false;
    const int groupId = value.toInt(&ok);
    return ok && groupId >= 0 ? groupId : -1;
}

QVariantList DLWordService::sortedWords(const QVariantList& words, const QString& sortMode) const
{
    QVariantList sorted = words;
    std::sort(sorted.begin(), sorted.end(), [sortMode](const QVariant& left, const QVariant& right) {
        const QVariantMap leftWord = left.toMap();
        const QVariantMap rightWord = right.toMap();

        if (sortMode == QStringLiteral("oldest")) {
            return leftWord.value(QStringLiteral("created_at")).toDouble() < rightWord.value(QStringLiteral("created_at")).toDouble();
        }
        if (sortMode == QStringLiteral("az")) {
            return QString::localeAwareCompare(leftWord.value(QStringLiteral("german_word")).toString(),
                                               rightWord.value(QStringLiteral("german_word")).toString()) < 0;
        }
        if (sortMode == QStringLiteral("za")) {
            return QString::localeAwareCompare(leftWord.value(QStringLiteral("german_word")).toString(),
                                               rightWord.value(QStringLiteral("german_word")).toString()) > 0;
        }

        return leftWord.value(QStringLiteral("created_at")).toDouble() > rightWord.value(QStringLiteral("created_at")).toDouble();
    });
    return sorted;
}
