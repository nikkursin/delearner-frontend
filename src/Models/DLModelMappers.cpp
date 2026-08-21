#include "DLModelMappers.h"

namespace {
int nullableInt(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return -1;
    }

    bool ok = false;
    const int number = value.toInt(&ok);
    return ok ? number : -1;
}

QVariant nullableString(const QString& value)
{
    return value.isEmpty() ? QVariant() : QVariant(value);
}
}

DLWord DLModelMappers::wordFromMap(const QVariantMap& map)
{
    DLWord word;
    word.id = map.value(QStringLiteral("id"), -1).toInt();
    word.syncId = map.value(QStringLiteral("sync_id"), map.value(QStringLiteral("syncId"))).toString();
    word.germanWord = map.value(QStringLiteral("german_word")).toString();
    word.normalizedGermanWord = map.value(QStringLiteral("normalized_german_word")).toString();
    word.article = map.value(QStringLiteral("article")).toString();
    word.partOfSpeech = map.value(QStringLiteral("part_of_speech"), QStringLiteral("Andere")).toString();
    word.nativeTranslation = map.value(QStringLiteral("native_translation")).toString();
    word.normalizedNativeTranslation = map.value(QStringLiteral("normalized_native_translation")).toString();
    word.examplePhraseDe = map.value(QStringLiteral("example_phrase_de")).toString();
    word.examplePhraseNative = map.value(QStringLiteral("example_phrase_native")).toString();
    word.groupId = nullableInt(map.value(QStringLiteral("group_id")));
    word.groupSyncId = map.value(QStringLiteral("group_sync_id"), map.value(QStringLiteral("groupSyncId"))).toString();
    word.notes = map.value(QStringLiteral("notes")).toString();
    word.createdAt = map.value(QStringLiteral("created_at")).toLongLong();
    word.updatedAt = map.value(QStringLiteral("updated_at")).toLongLong();
    word.deletedAt = map.value(QStringLiteral("deleted_at"));

    word.reviewStats.wordId = word.id;
    word.reviewStats.wordSyncId = word.syncId;
    word.reviewStats.correctAnswers = map.value(QStringLiteral("correct_answers"), 0).toInt();
    word.reviewStats.wrongAnswers = map.value(QStringLiteral("wrong_answers"), 0).toInt();
    word.reviewStats.lastReviewedAt = map.value(QStringLiteral("last_reviewed_at"));
    word.reviewStats.easeFactor = map.value(QStringLiteral("ease_factor"), 2.5).toDouble();
    word.reviewStats.intervalDays = map.value(QStringLiteral("interval_days"), 0).toInt();
    word.reviewStats.dueAt = map.value(QStringLiteral("due_at"));
    word.reviewStats.updatedAt = map.value(QStringLiteral("stats_updated_at")).toLongLong();

    word.nounForms.pluralForm = map.value(QStringLiteral("plural_form")).toString();
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
    map.insert(QStringLiteral("id"), nullableString(word.syncId));
    map.insert(QStringLiteral("local_id"), word.id);
    map.insert(QStringLiteral("sync_id"), nullableString(word.syncId));
    map.insert(QStringLiteral("syncId"), nullableString(word.syncId));
    map.insert(QStringLiteral("german_word"), word.germanWord);
    map.insert(QStringLiteral("normalized_german_word"), word.normalizedGermanWord);
    map.insert(QStringLiteral("article"), nullableString(word.article));
    map.insert(QStringLiteral("part_of_speech"), word.partOfSpeech);
    map.insert(QStringLiteral("native_translation"), word.nativeTranslation);
    map.insert(QStringLiteral("normalized_native_translation"), word.normalizedNativeTranslation);
    map.insert(QStringLiteral("example_phrase_de"), nullableString(word.examplePhraseDe));
    map.insert(QStringLiteral("example_phrase_native"), nullableString(word.examplePhraseNative));
    map.insert(QStringLiteral("group_id"), nullableString(word.groupSyncId));
    map.insert(QStringLiteral("local_group_id"), word.groupId >= 0 ? QVariant(word.groupId) : QVariant());
    map.insert(QStringLiteral("group_sync_id"), nullableString(word.groupSyncId));
    map.insert(QStringLiteral("groupSyncId"), nullableString(word.groupSyncId));
    map.insert(QStringLiteral("notes"), nullableString(word.notes));
    map.insert(QStringLiteral("created_at"), word.createdAt);
    map.insert(QStringLiteral("updated_at"), word.updatedAt);
    map.insert(QStringLiteral("deleted_at"), word.deletedAt);

    map.insert(QStringLiteral("last_reviewed_at"), word.reviewStats.lastReviewedAt);
    map.insert(QStringLiteral("correct_answers"), word.reviewStats.correctAnswers);
    map.insert(QStringLiteral("wrong_answers"), word.reviewStats.wrongAnswers);
    map.insert(QStringLiteral("ease_factor"), word.reviewStats.easeFactor);
    map.insert(QStringLiteral("interval_days"), word.reviewStats.intervalDays);
    map.insert(QStringLiteral("due_at"), word.reviewStats.dueAt);

    map.insert(QStringLiteral("plural_form"), nullableString(word.nounForms.pluralForm));
    map.insert(QStringLiteral("pluralForm"), nullableString(word.nounForms.pluralForm));
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
    group.id = map.value(QStringLiteral("id"), -1).toInt();
    group.syncId = map.value(QStringLiteral("sync_id"), map.value(QStringLiteral("syncId"))).toString();
    group.name = map.value(QStringLiteral("name")).toString();
    group.colorHex = map.value(QStringLiteral("color_hex"), QStringLiteral("#3366CC")).toString();
    group.createdAt = map.value(QStringLiteral("created_at")).toLongLong();
    group.updatedAt = map.value(QStringLiteral("updated_at")).toLongLong();
    group.deletedAt = map.value(QStringLiteral("deleted_at"));
    group.wordCount = map.value(QStringLiteral("word_count"), 0).toInt();
    return group;
}

QVariantMap DLModelMappers::groupToMap(const DLWordGroup& group)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), nullableString(group.syncId));
    map.insert(QStringLiteral("local_id"), group.id);
    map.insert(QStringLiteral("sync_id"), nullableString(group.syncId));
    map.insert(QStringLiteral("syncId"), nullableString(group.syncId));
    map.insert(QStringLiteral("name"), group.name);
    map.insert(QStringLiteral("color_hex"), group.colorHex);
    map.insert(QStringLiteral("created_at"), group.createdAt);
    map.insert(QStringLiteral("updated_at"), group.updatedAt);
    map.insert(QStringLiteral("deleted_at"), group.deletedAt);
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
