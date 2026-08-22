#include "DLWordService.h"

#include <algorithm>

#include "DLDatabaseManager.h"
#include "DLLogging.h"

DLWordService::DLWordService(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLWordService::createWord(const QVariantMap& wordData)
{
    const QString newId = m_database.insertWord(
        trimmedStringValue(wordData, QStringLiteral("german_word")),
        trimmedStringValue(wordData, QStringLiteral("article")),
        trimmedStringValue(wordData, QStringLiteral("part_of_speech")),
        trimmedStringValue(wordData, QStringLiteral("native_translation")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_de")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_native")),
        groupSyncIdFromWordData(wordData),
        trimmedStringValue(wordData, QStringLiteral("sync_id")),
        trimmedStringValue(wordData, QStringLiteral("plural_form")),
        trimmedStringValue(wordData, QStringLiteral("praeteritum_form")),
        trimmedStringValue(wordData, QStringLiteral("partizip_ii_form")),
        trimmedStringValue(wordData, QStringLiteral("positive_form")),
        trimmedStringValue(wordData, QStringLiteral("comparative_form")),
        trimmedStringValue(wordData, QStringLiteral("superlative_form")));

    m_lastError = newId.isEmpty() ? m_database.lastError() : QString();
    return newId;
}

bool DLWordService::updateWord(const QString& syncId, const QVariantMap& wordData)
{
    if (syncId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word update: invalid id";
        return false;
    }

    const bool success = m_database.updateWord(
        syncId.trimmed(),
        trimmedStringValue(wordData, QStringLiteral("german_word")),
        trimmedStringValue(wordData, QStringLiteral("article")),
        trimmedStringValue(wordData, QStringLiteral("part_of_speech")),
        trimmedStringValue(wordData, QStringLiteral("native_translation")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_de")),
        trimmedStringValue(wordData, QStringLiteral("example_phrase_native")),
        groupSyncIdFromWordData(wordData),
        trimmedStringValue(wordData, QStringLiteral("plural_form")),
        trimmedStringValue(wordData, QStringLiteral("praeteritum_form")),
        trimmedStringValue(wordData, QStringLiteral("partizip_ii_form")),
        trimmedStringValue(wordData, QStringLiteral("positive_form")),
        trimmedStringValue(wordData, QStringLiteral("comparative_form")),
        trimmedStringValue(wordData, QStringLiteral("superlative_form")));

    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

bool DLWordService::deleteWord(const QString& syncId)
{
    if (syncId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word delete: invalid id";
        return false;
    }

    const bool success = m_database.deleteWord(syncId.trimmed());
    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

QVariantMap DLWordService::wordById(const QString& syncId)
{
    if (syncId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Invalid word id.");
        qCWarning(dlService) << "Rejected word lookup: invalid id";
        return {};
    }

    const QVariantMap word = m_database.fetchWordById(syncId.trimmed());
    m_lastError = word.isEmpty() ? QStringLiteral("Word not found.") : QString();
    return word;
}

QVariantList DLWordService::loadWords(const QString& sortMode, const QString& groupSyncId)
{
    m_lastError.clear();
    return m_database.fetchAllWords(sortMode, groupSyncId);
}

QVariantList DLWordService::searchWords(const QString& query, const QString& sortMode, const QString& groupSyncId)
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return loadWords(sortMode, groupSyncId);
    }

    m_lastError.clear();
    return sortedWords(m_database.searchWords(trimmedQuery, groupSyncId), sortMode);
}

int DLWordService::wordCount(const QString& groupSyncId)
{
    m_lastError.clear();
    return m_database.getWordCount(groupSyncId);
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

QString DLWordService::groupSyncIdFromWordData(const QVariantMap& wordData) const
{
    const QVariant value = wordData.value(QStringLiteral("group_id"));
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    return value.toString().trimmed();
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
