#include "DLBootstrapStateRestorer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QUuid>
#include <QVariantList>

#include "Managers/DLDatabaseManager.h"
#include "Sync/DLRemoteChangeReconciler.h"
#include "Sync/DLSyncEventSerializer.h"

namespace {
void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

bool nonNegativeSequence(const QVariant& value, qint64* sequence, QString* error)
{
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (!ok || parsed < 0) {
        setError(error, QStringLiteral("Bootstrap response has an invalid serverSequence."));
        return false;
    }

    *sequence = parsed;
    return true;
}

bool stateList(const QVariantMap& state, const QString& key, QVariantList* list, QString* error)
{
    const QVariant value = state.value(key);
    if (!value.isValid() || value.isNull()) {
        list->clear();
        return true;
    }

    if (value.metaType().id() != QMetaType::QVariantList) {
        setError(error, QStringLiteral("Bootstrap state field is not a list: %1").arg(key));
        return false;
    }

    *list = value.toList();
    return true;
}

QString timestampForPayload(const QVariantMap& payload)
{
    const QString updatedAt = payload.value(QStringLiteral("updated_at")).toString().trimmed();
    return updatedAt.isEmpty() ? payload.value(QStringLiteral("created_at")).toString().trimmed() : updatedAt;
}

QString timestampForTombstone(const QVariantMap& tombstone)
{
    const QString updatedAt = tombstone.value(QStringLiteral("updatedAt")).toString().trimmed();
    return updatedAt.isEmpty() ? tombstone.value(QStringLiteral("deletedAt")).toString().trimmed() : updatedAt;
}

DLSyncEventEnvelope payloadEvent(const QString& entityType, const QVariantMap& payload, const QString& entityIdKey)
{
    DLSyncEventEnvelope event;
    event.contractVersion = DLSyncEventSerializer::contractVersion();
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.entityType = entityType;
    event.entityId = payload.value(entityIdKey).toString().trimmed();
    event.operation = QStringLiteral("update");
    event.updatedAt = timestampForPayload(payload);
    event.authenticatedUserId = payload.value(QStringLiteral("ownerUserId")).toString().trimmed();
    event.payload = DLSyncEventSerializer::canonicalPayloadForEntity(entityType, payload);
    return event;
}

DLSyncEventEnvelope tombstoneEvent(const QVariantMap& tombstone)
{
    DLSyncEventEnvelope event;
    event.contractVersion = DLSyncEventSerializer::contractVersion();
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.entityType = tombstone.value(QStringLiteral("entity_type")).toString().trimmed();
    event.entityId = tombstone.value(QStringLiteral("entity_id")).toString().trimmed();
    event.operation = QStringLiteral("delete");
    event.updatedAt = timestampForTombstone(tombstone);
    event.authenticatedUserId = tombstone.value(QStringLiteral("ownerUserId")).toString().trimmed();
    event.tombstone = DLSyncEventSerializer::tombstoneForEntity(
        event.entityType,
        event.entityId,
        event.authenticatedUserId,
        tombstone.value(QStringLiteral("deletedAt")),
        tombstone);
    return event;
}

bool appendPayloadEvents(const QVariantList& items,
                         const QString& entityType,
                         const QString& entityIdKey,
                         QList<DLSyncEventEnvelope>* events,
                         QString* error)
{
    for (const QVariant& item : items) {
        const QVariantMap payload = item.toMap();
        if (payload.isEmpty()) {
            setError(error, QStringLiteral("Bootstrap %1 entry is not an object.").arg(entityType));
            return false;
        }

        DLSyncEventEnvelope event = payloadEvent(entityType, payload, entityIdKey);
        QString validationError;
        if (!DLSyncEventSerializer::validateEvent(event, &validationError)) {
            setError(error, validationError);
            return false;
        }
        events->append(event);
    }

    return true;
}

bool appendTombstoneEvents(const QVariantList& items, QList<DLSyncEventEnvelope>* events, QString* error)
{
    for (const QVariant& item : items) {
        const QVariantMap tombstone = item.toMap();
        if (tombstone.isEmpty()) {
            setError(error, QStringLiteral("Bootstrap tombstone entry is not an object."));
            return false;
        }

        DLSyncEventEnvelope event = tombstoneEvent(tombstone);
        QString validationError;
        if (!DLSyncEventSerializer::validateEvent(event, &validationError)) {
            setError(error, validationError);
            return false;
        }
        events->append(event);
    }

    return true;
}
}

DLBootstrapStateRestorer::DLBootstrapStateRestorer(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLBootstrapStateRestorer::restoreFromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid bootstrap response JSON: %1").arg(parseError.errorString()));
        return false;
    }

    return restoreFromResponse(document.object().toVariantMap(), error);
}

bool DLBootstrapStateRestorer::restoreFromResponse(const QVariantMap& response, QString* error)
{
    qint64 serverSequence = 0;
    if (!nonNegativeSequence(response.value(QStringLiteral("serverSequence")), &serverSequence, error)) {
        return false;
    }

    if (!response.contains(QStringLiteral("state"))) {
        setError(error, QStringLiteral("Bootstrap response is missing state."));
        return false;
    }

    const QVariant stateValue = response.value(QStringLiteral("state"));
    if (stateValue.metaType().id() != QMetaType::QVariantMap) {
        setError(error, QStringLiteral("Bootstrap response state must be an object."));
        return false;
    }
    const QVariantMap state = stateValue.toMap();

    QVariantList groups;
    QVariantList words;
    QVariantList reviewStats;
    QVariantList tombstones;
    QVariantList learningSettings;
    if (!stateList(state, QStringLiteral("groups"), &groups, error)
        || !stateList(state, QStringLiteral("words"), &words, error)
        || !stateList(state, QStringLiteral("reviewStats"), &reviewStats, error)
        || !stateList(state, QStringLiteral("tombstones"), &tombstones, error)
        || !stateList(state, QStringLiteral("learningSettings"), &learningSettings, error)) {
        return false;
    }

    QList<DLSyncEventEnvelope> events;
    if (!appendPayloadEvents(groups, QStringLiteral("group"), QStringLiteral("id"), &events, error)
        || !appendPayloadEvents(words, QStringLiteral("word"), QStringLiteral("id"), &events, error)
        || !appendPayloadEvents(reviewStats, QStringLiteral("word_review_stats"), QStringLiteral("word_id"), &events, error)
        || !appendPayloadEvents(learningSettings, QStringLiteral("app_setting"), QStringLiteral("id"), &events, error)
        || !appendTombstoneEvents(tombstones, &events, error)) {
        return false;
    }

    DLRemoteChangeReconciler reconciler(m_database);
    return reconciler.replaceLocalStateWithRemoteEventsAndAdvanceCursor(events, serverSequence, error);
}
