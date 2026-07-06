#include "DLModelMappers.h"

namespace {
QVariant nullableString(const QString& value)
{
    return value.isEmpty() ? QVariant() : QVariant(value);
}
}

DLWord DLModelMappers::wordFromMap(const QVariantMap& map)
{
    DLWord word;
    word.id = map.value(QStringLiteral("id")).toString();
    word.germanWord = map.value(QStringLiteral("german_word")).toString();
    word.normalizedGermanWord = map.value(QStringLiteral("normalized_german_word")).toString();
    word.article = map.value(QStringLiteral("article")).toString();
    word.partOfSpeech = map.value(QStringLiteral("part_of_speech"), QStringLiteral("Andere")).toString();
    word.nativeTranslation = map.value(QStringLiteral("native_translation")).toString();
    word.normalizedNativeTranslation = map.value(QStringLiteral("normalized_native_translation")).toString();
    word.examplePhraseDe = map.value(QStringLiteral("example_phrase_de")).toString();
    word.examplePhraseNative = map.value(QStringLiteral("example_phrase_native")).toString();
    word.groupId = map.value(QStringLiteral("group_id")).toString();
    word.notes = map.value(QStringLiteral("notes")).toString();
    word.createdAt = map.value(QStringLiteral("created_at")).toLongLong();
    word.updatedAt = map.value(QStringLiteral("updated_at")).toLongLong();
    word.deletedAt = map.value(QStringLiteral("deleted_at"));
    word.serverUpdatedAt = map.value(QStringLiteral("server_updated_at"));
    word.serverVersion = map.value(QStringLiteral("server_version"), 0).toInt();
    word.deviceId = map.value(QStringLiteral("device_id")).toString();
    word.dirty = map.value(QStringLiteral("dirty"), true).toBool();

    word.reviewStats.wordId = word.id;
    word.reviewStats.id = map.value(QStringLiteral("stats_id")).toString();
    word.reviewStats.correctAnswers = map.value(QStringLiteral("correct_answers"), 0).toInt();
    word.reviewStats.wrongAnswers = map.value(QStringLiteral("wrong_answers"), 0).toInt();
    word.reviewStats.lastReviewedAt = map.value(QStringLiteral("last_reviewed_at"));
    word.reviewStats.easeFactor = map.value(QStringLiteral("ease_factor"), 2.5).toDouble();
    word.reviewStats.intervalDays = map.value(QStringLiteral("interval_days"), 0).toInt();
    word.reviewStats.dueAt = map.value(QStringLiteral("due_at"));
    word.reviewStats.createdAt = map.value(QStringLiteral("stats_created_at")).toLongLong();
    word.reviewStats.updatedAt = map.value(QStringLiteral("stats_updated_at")).toLongLong();
    word.reviewStats.deletedAt = map.value(QStringLiteral("stats_deleted_at"));
    word.reviewStats.serverUpdatedAt = map.value(QStringLiteral("stats_server_updated_at"));
    word.reviewStats.serverVersion = map.value(QStringLiteral("stats_server_version"), 0).toInt();
    word.reviewStats.deviceId = map.value(QStringLiteral("stats_device_id")).toString();
    word.reviewStats.dirty = map.value(QStringLiteral("stats_dirty"), true).toBool();

    word.pluralForm = map.value(QStringLiteral("plural_form")).toString();
    word.nounForms.pluralForm = word.pluralForm;
    word.verbForms.praeteritumForm = map.value(QStringLiteral("praeteritum_form")).toString();
    word.verbForms.partizipIIForm = map.value(QStringLiteral("partizip_ii_form")).toString();
    word.adjectiveForms.positiveForm = map.value(QStringLiteral("positive_form")).toString();
    word.adjectiveForms.comparativeForm = map.value(QStringLiteral("comparative_form")).toString();
    word.adjectiveForms.superlativeForm = map.value(QStringLiteral("superlative_form")).toString();

    return word;
}

QVariantMap DLModelMappers::wordToMap(const DLWord& word)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), word.id);
    map.insert(QStringLiteral("sync_id"), nullableString(word.id));
    map.insert(QStringLiteral("syncId"), nullableString(word.id));
    map.insert(QStringLiteral("german_word"), word.germanWord);
    map.insert(QStringLiteral("normalized_german_word"), word.normalizedGermanWord);
    map.insert(QStringLiteral("article"), nullableString(word.article));
    map.insert(QStringLiteral("part_of_speech"), word.partOfSpeech);
    map.insert(QStringLiteral("native_translation"), word.nativeTranslation);
    map.insert(QStringLiteral("normalized_native_translation"), word.normalizedNativeTranslation);
    map.insert(QStringLiteral("example_phrase_de"), nullableString(word.examplePhraseDe));
    map.insert(QStringLiteral("example_phrase_native"), nullableString(word.examplePhraseNative));
    map.insert(QStringLiteral("group_id"), nullableString(word.groupId));
    map.insert(QStringLiteral("notes"), nullableString(word.notes));
    map.insert(QStringLiteral("created_at"), word.createdAt);
    map.insert(QStringLiteral("updated_at"), word.updatedAt);
    map.insert(QStringLiteral("deleted_at"), word.deletedAt);
    map.insert(QStringLiteral("server_updated_at"), word.serverUpdatedAt);
    map.insert(QStringLiteral("server_version"), word.serverVersion);
    map.insert(QStringLiteral("device_id"), nullableString(word.deviceId));
    map.insert(QStringLiteral("dirty"), word.dirty);

    map.insert(QStringLiteral("last_reviewed_at"), word.reviewStats.lastReviewedAt);
    map.insert(QStringLiteral("correct_answers"), word.reviewStats.correctAnswers);
    map.insert(QStringLiteral("wrong_answers"), word.reviewStats.wrongAnswers);
    map.insert(QStringLiteral("ease_factor"), word.reviewStats.easeFactor);
    map.insert(QStringLiteral("interval_days"), word.reviewStats.intervalDays);
    map.insert(QStringLiteral("due_at"), word.reviewStats.dueAt);
    map.insert(QStringLiteral("stats_id"), nullableString(word.reviewStats.id));
    map.insert(QStringLiteral("stats_created_at"), word.reviewStats.createdAt);
    map.insert(QStringLiteral("stats_updated_at"), word.reviewStats.updatedAt);
    map.insert(QStringLiteral("stats_deleted_at"), word.reviewStats.deletedAt);
    map.insert(QStringLiteral("stats_server_updated_at"), word.reviewStats.serverUpdatedAt);
    map.insert(QStringLiteral("stats_server_version"), word.reviewStats.serverVersion);
    map.insert(QStringLiteral("stats_device_id"), nullableString(word.reviewStats.deviceId));
    map.insert(QStringLiteral("stats_dirty"), word.reviewStats.dirty);

    const QString pluralForm = word.pluralForm.isEmpty() ? word.nounForms.pluralForm : word.pluralForm;
    map.insert(QStringLiteral("plural_form"), nullableString(pluralForm));
    map.insert(QStringLiteral("pluralForm"), nullableString(pluralForm));
    map.insert(QStringLiteral("praeteritum_form"), nullableString(word.verbForms.praeteritumForm));
    map.insert(QStringLiteral("praeteritumForm"), nullableString(word.verbForms.praeteritumForm));
    map.insert(QStringLiteral("partizip_ii_form"), nullableString(word.verbForms.partizipIIForm));
    map.insert(QStringLiteral("partizipIIForm"), nullableString(word.verbForms.partizipIIForm));
    map.insert(QStringLiteral("positive_form"), nullableString(word.adjectiveForms.positiveForm));
    map.insert(QStringLiteral("positiveForm"), nullableString(word.adjectiveForms.positiveForm));
    map.insert(QStringLiteral("comparative_form"), nullableString(word.adjectiveForms.comparativeForm));
    map.insert(QStringLiteral("comparativeForm"), nullableString(word.adjectiveForms.comparativeForm));
    map.insert(QStringLiteral("superlative_form"), nullableString(word.adjectiveForms.superlativeForm));
    map.insert(QStringLiteral("superlativeForm"), nullableString(word.adjectiveForms.superlativeForm));
    return map;
}

QVariantList DLModelMappers::wordsToList(const QList<DLWord>& words)
{
    QVariantList result;
    for (const DLWord& word : words) {
        result.append(wordToMap(word));
    }
    return result;
}

DLWordGroup DLModelMappers::groupFromMap(const QVariantMap& map)
{
    DLWordGroup group;
    group.id = map.value(QStringLiteral("id")).toString();
    group.name = map.value(QStringLiteral("name")).toString();
    group.colorHex = map.value(QStringLiteral("color_hex"), QStringLiteral("#3366CC")).toString();
    group.createdAt = map.value(QStringLiteral("created_at")).toLongLong();
    group.updatedAt = map.value(QStringLiteral("updated_at")).toLongLong();
    group.deletedAt = map.value(QStringLiteral("deleted_at"));
    group.serverUpdatedAt = map.value(QStringLiteral("server_updated_at"));
    group.serverVersion = map.value(QStringLiteral("server_version"), 0).toInt();
    group.deviceId = map.value(QStringLiteral("device_id")).toString();
    group.dirty = map.value(QStringLiteral("dirty"), true).toBool();
    group.wordCount = map.value(QStringLiteral("word_count"), 0).toInt();
    return group;
}

QVariantMap DLModelMappers::groupToMap(const DLWordGroup& group)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), group.id);
    map.insert(QStringLiteral("name"), group.name);
    map.insert(QStringLiteral("color_hex"), group.colorHex);
    map.insert(QStringLiteral("created_at"), group.createdAt);
    map.insert(QStringLiteral("updated_at"), group.updatedAt);
    map.insert(QStringLiteral("deleted_at"), group.deletedAt);
    map.insert(QStringLiteral("server_updated_at"), group.serverUpdatedAt);
    map.insert(QStringLiteral("server_version"), group.serverVersion);
    map.insert(QStringLiteral("device_id"), nullableString(group.deviceId));
    map.insert(QStringLiteral("dirty"), group.dirty);
    map.insert(QStringLiteral("word_count"), group.wordCount);
    return map;
}

QVariantList DLModelMappers::groupsToList(const QList<DLWordGroup>& groups)
{
    QVariantList result;
    for (const DLWordGroup& group : groups) {
        result.append(groupToMap(group));
    }
    return result;
}
