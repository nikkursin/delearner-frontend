#include "DLSyncEventSerializer.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QTimeZone>
#include <QUuid>

namespace {
const QString kContractVersion = QStringLiteral("1.0");

const QSet<QString> kSyncableEntityTypes = {
    QStringLiteral("group"),
    QStringLiteral("word"),
    QStringLiteral("word_review_stats"),
    QStringLiteral("app_setting")
};

const QSet<QString> kOperations = {
    QStringLiteral("create"),
    QStringLiteral("update"),
    QStringLiteral("delete")
};

const QSet<QString> kForbiddenSyncFields = {
    QStringLiteral("quiz_session_id"),
    QStringLiteral("quiz_history"),
    QStringLiteral("raw_quiz_history"),
    QStringLiteral("question_history"),
    QStringLiteral("answers"),
    QStringLiteral("theme"),
    QStringLiteral("navigation_state"),
    QStringLiteral("transient_filters"),
    QStringLiteral("search_query"),
    QStringLiteral("layout_state"),
    QStringLiteral("window_state"),
    QStringLiteral("device_preferences")
};

bool isUuid(const QString& value)
{
    return !value.trimmed().isEmpty() && !QUuid(value.trimmed()).isNull();
}

void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

QVariant nullableString(const QString& value)
{
    return value.trimmed().isEmpty() ? QVariant() : QVariant(value.trimmed());
}

QVariant contractTimestamp(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        return text.isEmpty() ? QVariant() : QVariant(text);
    }

    bool ok = false;
    const qint64 seconds = value.toLongLong(&ok);
    if (!ok || seconds <= 0) {
        return QVariant();
    }

    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone(QTimeZone::UTC)).toString(Qt::ISODate);
}

QVariant normalizedContractValue(const QString& key, const QVariant& value)
{
    if (key == QStringLiteral("created_at")
        || key == QStringLiteral("updated_at")
        || key == QStringLiteral("last_reviewed_at")
        || key == QStringLiteral("due_at")
        || key == QStringLiteral("deletedAt")
        || key == QStringLiteral("updatedAt")) {
        return contractTimestamp(value);
    }

    if (!value.isValid()) {
        return QVariant();
    }
    return value;
}

QByteArray jsonForMap(const QVariantMap& map)
{
    return QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact);
}

bool containsForbiddenField(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (kForbiddenSyncFields.contains(it.key()) || containsForbiddenField(it.value())) {
                return true;
            }
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant& item : list) {
            if (containsForbiddenField(item)) {
                return true;
            }
        }
    }

    return false;
}

bool containsAllFields(const QVariantMap& map, const QStringList& fields, QString* error)
{
    for (const QString& field : fields) {
        if (!map.contains(field)) {
            setError(error, QStringLiteral("Missing required sync field: %1").arg(field));
            return false;
        }
    }
    return true;
}
}

QString DLSyncEventSerializer::contractVersion()
{
    return kContractVersion;
}

DLSyncEventEnvelope DLSyncEventSerializer::eventFromContractMap(const QVariantMap& map, const QString& contractVersion)
{
    DLSyncEventEnvelope event;
    event.contractVersion = contractVersion;
    event.eventId = map.value(QStringLiteral("eventId")).toString();
    event.deviceId = map.value(QStringLiteral("deviceId")).toString();
    event.entityType = map.value(QStringLiteral("entityType")).toString();
    event.entityId = map.value(QStringLiteral("entityId")).toString();
    event.operation = map.value(QStringLiteral("operation")).toString();
    event.updatedAt = map.value(QStringLiteral("updatedAt"));
    event.authenticatedUserId = map.value(QStringLiteral("context")).toMap().value(QStringLiteral("authenticatedUserId")).toString();
    event.payload = map.value(QStringLiteral("payload")).toMap();
    event.tombstone = map.value(QStringLiteral("tombstone")).toMap();
    return event;
}

QVariantMap DLSyncEventSerializer::contractMapFromEvent(const DLSyncEventEnvelope& event, QString* error)
{
    if (!validateEvent(event, error)) {
        return {};
    }

    QVariantMap context;
    context.insert(QStringLiteral("authenticatedUserId"), event.authenticatedUserId);

    QVariantMap map;
    map.insert(QStringLiteral("eventId"), event.eventId);
    map.insert(QStringLiteral("deviceId"), event.deviceId);
    map.insert(QStringLiteral("entityType"), event.entityType);
    map.insert(QStringLiteral("entityId"), event.entityId);
    map.insert(QStringLiteral("operation"), event.operation);
    map.insert(QStringLiteral("updatedAt"), normalizedContractValue(QStringLiteral("updatedAt"), event.updatedAt));
    map.insert(QStringLiteral("context"), context);

    if (event.operation == QStringLiteral("delete")) {
        map.insert(QStringLiteral("tombstone"), canonicalTombstoneForEntity(event.entityType, event.tombstone));
    } else {
        map.insert(QStringLiteral("payload"), canonicalPayloadForEntity(event.entityType, event.payload));
    }

    return map;
}

QByteArray DLSyncEventSerializer::serializeContractJson(const DLSyncEventEnvelope& event, QString* error)
{
    const QVariantMap map = contractMapFromEvent(event, error);
    return map.isEmpty() ? QByteArray() : jsonForMap(map);
}

DLSyncEventEnvelope DLSyncEventSerializer::parseContractJson(const QByteArray& json,
                                                            const QString& contractVersion,
                                                            QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid sync event JSON: %1").arg(parseError.errorString()));
        return {};
    }

    DLSyncEventEnvelope event = eventFromContractMap(document.object().toVariantMap(), contractVersion);
    if (!validateEvent(event, error)) {
        return {};
    }
    return event;
}

QByteArray DLSyncEventSerializer::serializeBodyJson(const DLSyncEventEnvelope& event, QString* error)
{
    if (!validateEvent(event, error)) {
        return {};
    }

    return event.operation == QStringLiteral("delete")
        ? jsonForMap(canonicalTombstoneForEntity(event.entityType, event.tombstone))
        : jsonForMap(canonicalPayloadForEntity(event.entityType, event.payload));
}

QVariantMap DLSyncEventSerializer::payloadForGroup(const DLWordGroup& group, const QString& ownerUserId)
{
    QVariantMap payload;
    payload.insert(QStringLiteral("id"), group.syncId);
    payload.insert(QStringLiteral("ownerUserId"), ownerUserId);
    payload.insert(QStringLiteral("name"), group.name);
    payload.insert(QStringLiteral("color_hex"), group.colorHex);
    payload.insert(QStringLiteral("created_at"), group.createdAt);
    payload.insert(QStringLiteral("updated_at"), group.updatedAt);
    return canonicalPayloadForEntity(QStringLiteral("group"), payload);
}

QVariantMap DLSyncEventSerializer::payloadForWord(const DLWord& word, const QString& ownerUserId)
{
    QVariantMap payload;
    payload.insert(QStringLiteral("id"), word.syncId);
    payload.insert(QStringLiteral("ownerUserId"), ownerUserId);
    payload.insert(QStringLiteral("german_word"), word.germanWord);
    payload.insert(QStringLiteral("normalized_german_word"), word.normalizedGermanWord);
    payload.insert(QStringLiteral("article"), nullableString(word.article));
    payload.insert(QStringLiteral("part_of_speech"), word.partOfSpeech);
    payload.insert(QStringLiteral("native_translation"), word.nativeTranslation);
    payload.insert(QStringLiteral("normalized_native_translation"), word.normalizedNativeTranslation);
    payload.insert(QStringLiteral("example_phrase_de"), nullableString(word.examplePhraseDe));
    payload.insert(QStringLiteral("example_phrase_native"), nullableString(word.examplePhraseNative));
    payload.insert(QStringLiteral("group_id"), nullableString(word.groupSyncId));
    payload.insert(QStringLiteral("notes"), nullableString(word.notes));
    payload.insert(QStringLiteral("plural_form"), nullableString(word.nounForms.pluralForm));
    payload.insert(QStringLiteral("praeteritum_form"), nullableString(word.verbForms.praeteritumForm));
    payload.insert(QStringLiteral("partizip_ii_form"), nullableString(word.verbForms.partizipIIForm));
    payload.insert(QStringLiteral("positive_form"), nullableString(word.adjectiveForms.positiveForm));
    payload.insert(QStringLiteral("comparative_form"), nullableString(word.adjectiveForms.comparativeForm));
    payload.insert(QStringLiteral("superlative_form"), nullableString(word.adjectiveForms.superlativeForm));
    payload.insert(QStringLiteral("created_at"), word.createdAt);
    payload.insert(QStringLiteral("updated_at"), word.updatedAt);
    return canonicalPayloadForEntity(QStringLiteral("word"), payload);
}

QVariantMap DLSyncEventSerializer::payloadForReviewStats(const DLWordReviewStats& stats,
                                                         const QString& ownerUserId,
                                                         const QVariant& createdAt)
{
    QVariantMap payload;
    payload.insert(QStringLiteral("word_id"), stats.wordSyncId);
    payload.insert(QStringLiteral("ownerUserId"), ownerUserId);
    payload.insert(QStringLiteral("correct_answers"), stats.correctAnswers);
    payload.insert(QStringLiteral("wrong_answers"), stats.wrongAnswers);
    payload.insert(QStringLiteral("last_reviewed_at"), stats.lastReviewedAt);
    payload.insert(QStringLiteral("ease_factor"), stats.easeFactor);
    payload.insert(QStringLiteral("interval_days"), stats.intervalDays);
    payload.insert(QStringLiteral("due_at"), stats.dueAt);
    payload.insert(QStringLiteral("created_at"), createdAt.isValid() ? createdAt : QVariant(stats.updatedAt));
    payload.insert(QStringLiteral("updated_at"), stats.updatedAt);
    return canonicalPayloadForEntity(QStringLiteral("word_review_stats"), payload);
}

QVariantMap DLSyncEventSerializer::payloadForAppSetting(const QVariantMap& setting, const QString& ownerUserId)
{
    QVariantMap payload = setting;
    payload.insert(QStringLiteral("ownerUserId"), ownerUserId);
    return canonicalPayloadForEntity(QStringLiteral("app_setting"), payload);
}

QVariantMap DLSyncEventSerializer::canonicalPayloadForEntity(const QString& entityType, const QVariantMap& source)
{
    const QStringList fields = entityType == QStringLiteral("delete")
        ? QStringList()
        : payloadFieldsForEntity(entityType);

    QVariantMap payload;
    for (const QString& field : fields) {
        if (source.contains(field)) {
            payload.insert(field, normalizedContractValue(field, source.value(field)));
        }
    }

    return payload;
}

QVariantMap DLSyncEventSerializer::tombstoneForEntity(const QString& entityType,
                                                      const QString& entityId,
                                                      const QString& ownerUserId,
                                                      const QVariant& deletedAt,
                                                      const QVariantMap& metadata)
{
    QVariantMap tombstone;
    tombstone.insert(QStringLiteral("entity_type"), entityType);
    tombstone.insert(QStringLiteral("entity_id"), entityId);
    tombstone.insert(QStringLiteral("ownerUserId"), ownerUserId);
    tombstone.insert(QStringLiteral("deletedAt"), deletedAt);
    tombstone.insert(QStringLiteral("updatedAt"), deletedAt);

    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        if (!kForbiddenSyncFields.contains(it.key())) {
            tombstone.insert(it.key(), it.value());
        }
    }

    return canonicalTombstoneForEntity(entityType, tombstone);
}

bool DLSyncEventSerializer::validateEvent(const DLSyncEventEnvelope& event, QString* error)
{
    if (event.contractVersion != kContractVersion) {
        setError(error, QStringLiteral("Unsupported sync contract version: %1").arg(event.contractVersion));
        return false;
    }
    if (!isUuid(event.eventId)) {
        setError(error, QStringLiteral("Sync event has invalid eventId."));
        return false;
    }
    if (!event.deviceId.trimmed().isEmpty() && !isUuid(event.deviceId)) {
        setError(error, QStringLiteral("Sync event has invalid deviceId."));
        return false;
    }
    if (!isUuid(event.entityId)) {
        setError(error, QStringLiteral("Sync event has invalid entityId."));
        return false;
    }
    if (!isUuid(event.authenticatedUserId)) {
        setError(error, QStringLiteral("Sync event has invalid authenticatedUserId."));
        return false;
    }
    if (!kSyncableEntityTypes.contains(event.entityType)) {
        setError(error, QStringLiteral("Unsupported sync entity type: %1").arg(event.entityType));
        return false;
    }
    if (!kOperations.contains(event.operation)) {
        setError(error, QStringLiteral("Unsupported sync operation: %1").arg(event.operation));
        return false;
    }
    if (!normalizedContractValue(QStringLiteral("updatedAt"), event.updatedAt).isValid()) {
        setError(error, QStringLiteral("Sync event is missing updatedAt."));
        return false;
    }

    if (event.operation == QStringLiteral("delete")) {
        const QVariantMap tombstone = canonicalTombstoneForEntity(event.entityType, event.tombstone);
        if (!event.payload.isEmpty()) {
            setError(error, QStringLiteral("Delete sync events must not include a full payload."));
            return false;
        }
        if (!containsAllFields(tombstone, requiredTombstoneFieldsForEntity(event.entityType), error)) {
            return false;
        }
        if (containsForbiddenField(tombstone)) {
            setError(error, QStringLiteral("Delete tombstone contains a local-only or quiz-history field."));
            return false;
        }
        return true;
    }

    const QVariantMap payload = canonicalPayloadForEntity(event.entityType, event.payload);
    if (!event.tombstone.isEmpty()) {
        setError(error, QStringLiteral("Create/update sync events must not include a tombstone."));
        return false;
    }
    if (!containsAllFields(payload, payloadFieldsForEntity(event.entityType), error)) {
        return false;
    }
    if (containsForbiddenField(payload)) {
        setError(error, QStringLiteral("Sync payload contains a local-only or quiz-history field."));
        return false;
    }
    return true;
}

QVariantMap DLSyncEventSerializer::canonicalTombstoneForEntity(const QString& entityType, const QVariantMap& source)
{
    QVariantMap tombstone;
    for (const QString& field : requiredTombstoneFieldsForEntity(entityType)) {
        if (source.contains(field)) {
            tombstone.insert(field, normalizedContractValue(field, source.value(field)));
        }
    }
    return tombstone;
}

QStringList DLSyncEventSerializer::payloadFieldsForEntity(const QString& entityType)
{
    if (entityType == QStringLiteral("group")) {
        return {
            QStringLiteral("id"),
            QStringLiteral("ownerUserId"),
            QStringLiteral("name"),
            QStringLiteral("color_hex"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at")
        };
    }
    if (entityType == QStringLiteral("word")) {
        return {
            QStringLiteral("id"),
            QStringLiteral("ownerUserId"),
            QStringLiteral("german_word"),
            QStringLiteral("normalized_german_word"),
            QStringLiteral("article"),
            QStringLiteral("part_of_speech"),
            QStringLiteral("native_translation"),
            QStringLiteral("normalized_native_translation"),
            QStringLiteral("example_phrase_de"),
            QStringLiteral("example_phrase_native"),
            QStringLiteral("group_id"),
            QStringLiteral("notes"),
            QStringLiteral("plural_form"),
            QStringLiteral("praeteritum_form"),
            QStringLiteral("partizip_ii_form"),
            QStringLiteral("positive_form"),
            QStringLiteral("comparative_form"),
            QStringLiteral("superlative_form"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at")
        };
    }
    if (entityType == QStringLiteral("word_review_stats")) {
        return {
            QStringLiteral("word_id"),
            QStringLiteral("ownerUserId"),
            QStringLiteral("correct_answers"),
            QStringLiteral("wrong_answers"),
            QStringLiteral("last_reviewed_at"),
            QStringLiteral("ease_factor"),
            QStringLiteral("interval_days"),
            QStringLiteral("due_at"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at")
        };
    }
    if (entityType == QStringLiteral("app_setting")) {
        return {
            QStringLiteral("id"),
            QStringLiteral("ownerUserId"),
            QStringLiteral("setting_key"),
            QStringLiteral("setting_value"),
            QStringLiteral("value_type"),
            QStringLiteral("scope"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at")
        };
    }

    return {};
}

QStringList DLSyncEventSerializer::requiredTombstoneFieldsForEntity(const QString& entityType)
{
    QStringList fields = {
        QStringLiteral("entity_type"),
        QStringLiteral("entity_id"),
        QStringLiteral("ownerUserId"),
        QStringLiteral("deletedAt"),
        QStringLiteral("updatedAt")
    };

    if (entityType == QStringLiteral("word_review_stats")) {
        fields.append(QStringLiteral("word_id"));
    } else if (entityType == QStringLiteral("app_setting")) {
        fields.append(QStringLiteral("setting_key"));
    }

    return fields;
}
