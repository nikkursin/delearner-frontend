#include "DLRemoteChangeReconciler.h"

#include <QDateTime>
#include <QMetaType>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "Managers/DLDatabaseManager.h"

namespace {
void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

bool execQuery(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    setError(error, query.lastError().text());
    return false;
}

QString requiredString(const QVariantMap& map, const QString& key)
{
    return map.value(key).toString().trimmed();
}

QVariant nullableString(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QVariant() : QVariant(text);
}

bool timestampSeconds(const QVariant& value, const QString& fieldName, qint64* seconds, QString* error)
{
    if (!value.isValid() || value.isNull()) {
        setError(error, QStringLiteral("Remote sync event is missing timestamp field: %1").arg(fieldName));
        return false;
    }

    bool numberOk = false;
    const qint64 number = value.toLongLong(&numberOk);
    if (numberOk && number > 0) {
        *seconds = number;
        return true;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        setError(error, QStringLiteral("Remote sync event is missing timestamp field: %1").arg(fieldName));
        return false;
    }

    QDateTime timestamp = QDateTime::fromString(text, Qt::ISODate);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(text, Qt::ISODateWithMs);
    }
    if (!timestamp.isValid()) {
        setError(error, QStringLiteral("Remote sync event has invalid timestamp field: %1").arg(fieldName));
        return false;
    }

    *seconds = timestamp.toUTC().toSecsSinceEpoch();
    if (*seconds <= 0) {
        setError(error, QStringLiteral("Remote sync event has invalid timestamp field: %1").arg(fieldName));
        return false;
    }
    return true;
}

QVariant optionalTimestampSeconds(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    qint64 seconds = 0;
    QString ignoredError;
    return timestampSeconds(value, QString(), &seconds, &ignoredError) ? QVariant(seconds) : QVariant();
}

bool payloadIdentityMatchesEvent(const DLSyncEventEnvelope& event, const QString& payloadIdKey, QString* error)
{
    const QString payloadId = requiredString(event.payload, payloadIdKey);
    if (payloadId != event.entityId.trimmed()) {
        setError(error, QStringLiteral("Remote sync payload identity does not match event identity."));
        return false;
    }
    return true;
}

bool tombstoneIdentityMatchesEvent(const DLSyncEventEnvelope& event, QString* error)
{
    const QString tombstoneEntityType = requiredString(event.tombstone, QStringLiteral("entity_type"));
    const QString tombstoneEntityId = requiredString(event.tombstone, QStringLiteral("entity_id"));
    if (tombstoneEntityType != event.entityType.trimmed() || tombstoneEntityId != event.entityId.trimmed()) {
        setError(error, QStringLiteral("Remote sync tombstone identity does not match event identity."));
        return false;
    }
    return true;
}

bool nonNegativeSequence(const QVariant& value, const QString& fieldName, qint64* sequence, QString* error)
{
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    if (!ok || parsed < 0) {
        setError(error, QStringLiteral("Remote download response has an invalid sequence field: %1").arg(fieldName));
        return false;
    }

    *sequence = parsed;
    return true;
}

bool requiredMap(const QVariant& value, const QString& fieldName, QVariantMap* map, QString* error)
{
    if (value.metaType().id() != QMetaType::QVariantMap) {
        setError(error, QStringLiteral("Remote download event field is not an object: %1").arg(fieldName));
        return false;
    }

    *map = value.toMap();
    return true;
}

QVariant payloadUpdatedAt(const QVariantMap& payload)
{
    const QVariant updatedAt = payload.value(QStringLiteral("updated_at"));
    return updatedAt.isValid() && !updatedAt.toString().trimmed().isEmpty()
        ? updatedAt
        : payload.value(QStringLiteral("created_at"));
}

QVariant tombstoneUpdatedAt(const QVariantMap& tombstone)
{
    const QVariant updatedAt = tombstone.value(QStringLiteral("updatedAt"));
    return updatedAt.isValid() && !updatedAt.toString().trimmed().isEmpty()
        ? updatedAt
        : tombstone.value(QStringLiteral("deletedAt"));
}

bool eventFromDownloadMap(const QVariantMap& map, DLSyncEventEnvelope* event, QString* error)
{
    DLSyncEventEnvelope parsed;
    parsed.contractVersion = DLSyncEventSerializer::contractVersion();
    parsed.eventId = requiredString(map, QStringLiteral("eventId"));
    parsed.deviceId = requiredString(map, QStringLiteral("deviceId"));
    parsed.entityType = requiredString(map, QStringLiteral("entityType"));
    parsed.entityId = requiredString(map, QStringLiteral("entityId"));
    parsed.operation = requiredString(map, QStringLiteral("operation"));

    if (parsed.operation == QStringLiteral("delete")) {
        if (!requiredMap(map.value(QStringLiteral("tombstone")), QStringLiteral("tombstone"), &parsed.tombstone, error)) {
            return false;
        }
        parsed.updatedAt = tombstoneUpdatedAt(parsed.tombstone);
        parsed.authenticatedUserId = requiredString(parsed.tombstone, QStringLiteral("ownerUserId"));
    } else {
        if (!requiredMap(map.value(QStringLiteral("payload")), QStringLiteral("payload"), &parsed.payload, error)) {
            return false;
        }
        parsed.updatedAt = payloadUpdatedAt(parsed.payload);
        parsed.authenticatedUserId = requiredString(parsed.payload, QStringLiteral("ownerUserId"));
    }

    QString validationError;
    if (!DLSyncEventSerializer::validateEvent(parsed, &validationError)) {
        setError(error, validationError);
        return false;
    }

    *event = parsed;
    return true;
}

int localGroupId(QSqlDatabase& db, const QString& groupSyncId, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM groups WHERE sync_id = :sync_id AND deleted_at IS NULL;"));
    query.bindValue(QStringLiteral(":sync_id"), groupSyncId);
    if (!execQuery(query, error)) {
        return -1;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("Referenced remote group does not exist locally: %1").arg(groupSyncId));
        return -1;
    }
    return query.value(0).toInt();
}

int localWordId(QSqlDatabase& db, const QString& wordSyncId, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM words WHERE sync_id = :sync_id AND deleted_at IS NULL;"));
    query.bindValue(QStringLiteral(":sync_id"), wordSyncId);
    if (!execQuery(query, error)) {
        return -1;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("Referenced remote word does not exist locally: %1").arg(wordSyncId));
        return -1;
    }
    return query.value(0).toInt();
}

int storedWordId(QSqlDatabase& db, const QString& wordSyncId, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM words WHERE sync_id = :sync_id;"));
    query.bindValue(QStringLiteral(":sync_id"), wordSyncId);
    if (!execQuery(query, error)) {
        return -1;
    }
    if (!query.next()) {
        setError(error, QStringLiteral("Remote word was not stored locally: %1").arg(wordSyncId));
        return -1;
    }
    return query.value(0).toInt();
}

bool saveWordForms(QSqlDatabase& db, int wordId, const QVariantMap& payload, QString* error)
{
    QSqlQuery deleteNoun(db);
    deleteNoun.prepare(QStringLiteral("DELETE FROM noun_forms WHERE word_id = :word_id;"));
    deleteNoun.bindValue(QStringLiteral(":word_id"), wordId);
    if (!execQuery(deleteNoun, error)) {
        return false;
    }

    const QString plural = payload.value(QStringLiteral("plural_form")).toString().trimmed();
    if (!plural.isEmpty()) {
        QSqlQuery noun(db);
        noun.prepare(QStringLiteral("INSERT INTO noun_forms (word_id, plural_form) VALUES (:word_id, :plural_form);"));
        noun.bindValue(QStringLiteral(":word_id"), wordId);
        noun.bindValue(QStringLiteral(":plural_form"), plural);
        if (!execQuery(noun, error)) {
            return false;
        }
    }

    QSqlQuery deleteVerb(db);
    deleteVerb.prepare(QStringLiteral("DELETE FROM verb_forms WHERE word_id = :word_id;"));
    deleteVerb.bindValue(QStringLiteral(":word_id"), wordId);
    if (!execQuery(deleteVerb, error)) {
        return false;
    }

    const QString praeteritum = payload.value(QStringLiteral("praeteritum_form")).toString().trimmed();
    const QString partizip = payload.value(QStringLiteral("partizip_ii_form")).toString().trimmed();
    if (!praeteritum.isEmpty() || !partizip.isEmpty()) {
        QSqlQuery verb(db);
        verb.prepare(QStringLiteral(R"(
            INSERT INTO verb_forms (word_id, praeteritum_form, partizip_ii_form)
            VALUES (:word_id, :praeteritum_form, :partizip_ii_form);
        )"));
        verb.bindValue(QStringLiteral(":word_id"), wordId);
        verb.bindValue(QStringLiteral(":praeteritum_form"), praeteritum.isEmpty() ? QVariant() : QVariant(praeteritum));
        verb.bindValue(QStringLiteral(":partizip_ii_form"), partizip.isEmpty() ? QVariant() : QVariant(partizip));
        if (!execQuery(verb, error)) {
            return false;
        }
    }

    QSqlQuery deleteAdjective(db);
    deleteAdjective.prepare(QStringLiteral("DELETE FROM adjective_forms WHERE word_id = :word_id;"));
    deleteAdjective.bindValue(QStringLiteral(":word_id"), wordId);
    if (!execQuery(deleteAdjective, error)) {
        return false;
    }

    const QString positive = payload.value(QStringLiteral("positive_form")).toString().trimmed();
    const QString comparative = payload.value(QStringLiteral("comparative_form")).toString().trimmed();
    const QString superlative = payload.value(QStringLiteral("superlative_form")).toString().trimmed();
    if (!positive.isEmpty() || !comparative.isEmpty() || !superlative.isEmpty()) {
        QSqlQuery adjective(db);
        adjective.prepare(QStringLiteral(R"(
            INSERT INTO adjective_forms (word_id, positive_form, comparative_form, superlative_form)
            VALUES (:word_id, :positive_form, :comparative_form, :superlative_form);
        )"));
        adjective.bindValue(QStringLiteral(":word_id"), wordId);
        adjective.bindValue(QStringLiteral(":positive_form"), positive.isEmpty() ? QVariant() : QVariant(positive));
        adjective.bindValue(QStringLiteral(":comparative_form"), comparative.isEmpty() ? QVariant() : QVariant(comparative));
        adjective.bindValue(QStringLiteral(":superlative_form"), superlative.isEmpty() ? QVariant() : QVariant(superlative));
        if (!execQuery(adjective, error)) {
            return false;
        }
    }

    return true;
}
}

DLRemoteChangeReconciler::DLRemoteChangeReconciler(DLDatabaseManager& database)
    : m_database(database)
{
}

bool DLRemoteChangeReconciler::applyRemoteEvents(const QList<DLSyncEventEnvelope>& events, QString* error)
{
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* transactionError) {
        for (const DLSyncEventEnvelope& event : events) {
            if (!applyRemoteEventInTransaction(db, event, transactionError)) {
                return false;
            }
        }
        return true;
    });
    if (!success) {
        setError(error, m_database.lastError());
    }
    return success;
}

bool DLRemoteChangeReconciler::applyRemoteEvent(const DLSyncEventEnvelope& event, QString* error)
{
    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* transactionError) {
        return applyRemoteEventInTransaction(db, event, transactionError);
    });
    if (!success) {
        setError(error, m_database.lastError());
    }
    return success;
}

bool DLRemoteChangeReconciler::applyRemoteEventsAndAdvanceCursor(const QList<DLSyncEventEnvelope>& events,
                                                                 qint64 consumedSequence,
                                                                 QString* error)
{
    if (consumedSequence < 0) {
        setError(error, QStringLiteral("Remote cursor must be non-negative."));
        return false;
    }

    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* transactionError) {
        for (const DLSyncEventEnvelope& event : events) {
            if (!applyRemoteEventInTransaction(db, event, transactionError)) {
                return false;
            }
        }
        return recordRemoteCursor(db, consumedSequence, transactionError);
    });
    if (!success) {
        setError(error, m_database.lastError());
    }
    return success;
}

bool DLRemoteChangeReconciler::applyDownloadedEventsAndAdvanceCursor(const QVariantMap& downloadResponse,
                                                                     QString* error)
{
    if (downloadResponse.value(QStringLiteral("rebootstrapRequired")).toBool()
        || downloadResponse.value(QStringLiteral("cursorGapDetected")).toBool()) {
        setError(error, QStringLiteral("Remote cursor is stale; re-bootstrap is required."));
        return false;
    }

    qint64 nextSequence = 0;
    if (!nonNegativeSequence(downloadResponse.value(QStringLiteral("nextSequence")),
                             QStringLiteral("nextSequence"),
                             &nextSequence,
                             error)) {
        return false;
    }

    const QVariant eventsValue = downloadResponse.value(QStringLiteral("events"));
    if (eventsValue.metaType().id() != QMetaType::QVariantList) {
        setError(error, QStringLiteral("Remote download response is missing events."));
        return false;
    }

    QList<DLSyncEventEnvelope> events;
    const QVariantList eventItems = eventsValue.toList();
    for (const QVariant& eventItem : eventItems) {
        if (eventItem.metaType().id() != QMetaType::QVariantMap) {
            setError(error, QStringLiteral("Remote download event is not an object."));
            return false;
        }

        qint64 serverSequence = 0;
        const QVariantMap eventMap = eventItem.toMap();
        if (!nonNegativeSequence(eventMap.value(QStringLiteral("serverSequence")),
                                 QStringLiteral("serverSequence"),
                                 &serverSequence,
                                 error)) {
            return false;
        }
        if (serverSequence > nextSequence) {
            setError(error, QStringLiteral("Remote download event sequence is beyond nextSequence."));
            return false;
        }

        DLSyncEventEnvelope event;
        if (!eventFromDownloadMap(eventMap, &event, error)) {
            return false;
        }
        events.append(event);
    }

    return applyRemoteEventsAndAdvanceCursor(events, nextSequence, error);
}

bool DLRemoteChangeReconciler::replaceLocalStateWithRemoteEventsAndAdvanceCursor(const QList<DLSyncEventEnvelope>& events,
                                                                                 qint64 consumedSequence,
                                                                                 QString* error)
{
    if (consumedSequence < 0) {
        setError(error, QStringLiteral("Remote cursor must be non-negative."));
        return false;
    }

    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* transactionError) {
        QList<DLSyncEventEnvelope> pendingOutboxEvents;
        if (!loadPendingOutboxEvents(db, &pendingOutboxEvents, transactionError)
            || !clearLocalSyncableState(db, transactionError)) {
            return false;
        }

        for (const DLSyncEventEnvelope& event : events) {
            if (!applyRemoteEventInTransaction(db, event, transactionError)) {
                return false;
            }
        }

        for (DLSyncEventEnvelope event : pendingOutboxEvents) {
            if (event.entityType == QStringLiteral("app_setting")) {
                QVariantMap& body = event.operation == QStringLiteral("delete") ? event.tombstone : event.payload;
                QSqlQuery identity(db);
                identity.prepare(QStringLiteral("SELECT sync_id FROM account_learning_settings WHERE setting_key = ?"));
                identity.addBindValue(body.value(QStringLiteral("setting_key")));
                if (!execQuery(identity, transactionError)) {
                    return false;
                }
                if (identity.next()) {
                    event.entityId = identity.value(0).toString();
                    body.insert(event.operation == QStringLiteral("delete") ? QStringLiteral("entity_id") : QStringLiteral("id"), event.entityId);
                }
            }
            if (!applyRemoteEventInTransaction(db, event, transactionError)) {
                return false;
            }
        }

        return recordRemoteCursor(db, consumedSequence, transactionError);
    });
    if (!success) {
        setError(error, m_database.lastError());
    }
    return success;
}

bool DLRemoteChangeReconciler::applyRemoteEventInTransaction(QSqlDatabase& db,
                                                            const DLSyncEventEnvelope& event,
                                                            QString* error)
{
    QString validationError;
    if (!DLSyncEventSerializer::validateEvent(event, &validationError)) {
        setError(error, validationError);
        return false;
    }

    if (event.operation == QStringLiteral("delete")) {
        return applyTombstoneEvent(db, event, error);
    }
    return applyPayloadEvent(db, event, error);
}

bool DLRemoteChangeReconciler::recordRemoteCursor(QSqlDatabase& db, qint64 consumedSequence, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO sync_pull_cursor (id, consumed_sequence, updated_at)
        VALUES (1, :consumed_sequence, :updated_at)
        ON CONFLICT(id) DO UPDATE SET
            consumed_sequence = MAX(sync_pull_cursor.consumed_sequence, excluded.consumed_sequence),
            updated_at = excluded.updated_at;
    )"));
    query.bindValue(QStringLiteral(":consumed_sequence"), consumedSequence);
    query.bindValue(QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTime());
    return execQuery(query, error);
}

bool DLRemoteChangeReconciler::loadPendingOutboxEvents(QSqlDatabase& db,
                                                       QList<DLSyncEventEnvelope>* events,
                                                       QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        SELECT contract_version, envelope_json
        FROM sync_outbox_events
        ORDER BY created_at ASC, rowid ASC;
    )"));
    if (!execQuery(query, error)) {
        return false;
    }

    events->clear();
    while (query.next()) {
        const QString contractVersion = query.value(0).toString().trimmed();
        const QByteArray envelopeJson = query.value(1).toString().toUtf8();
        QString parseError;
        const DLSyncEventEnvelope event = DLSyncEventSerializer::parseContractJson(
            envelopeJson,
            contractVersion.isEmpty() ? DLSyncEventSerializer::contractVersion() : contractVersion,
            &parseError);
        if (!parseError.isEmpty()) {
            setError(error, QStringLiteral("Pending sync outbox event cannot be replayed during bootstrap restore: %1").arg(parseError));
            return false;
        }
        events->append(event);
    }
    return true;
}

bool DLRemoteChangeReconciler::clearLocalSyncableState(QSqlDatabase& db, QString* error)
{
    const QStringList statements = {
        QStringLiteral("DELETE FROM adjective_forms;"),
        QStringLiteral("DELETE FROM verb_forms;"),
        QStringLiteral("DELETE FROM noun_forms;"),
        QStringLiteral("DELETE FROM word_review_stats;"),
        QStringLiteral("DELETE FROM words;"),
        QStringLiteral("DELETE FROM groups;"),
        QStringLiteral("DELETE FROM account_learning_settings;"),
        QStringLiteral("DELETE FROM sync_acknowledged_event_diagnostics;"),
        QStringLiteral("DELETE FROM sync_pull_cursor;")
    };

    for (const QString& statement : statements) {
        QSqlQuery query(db);
        if (!query.exec(statement)) {
            setError(error, query.lastError().text());
            return false;
        }
    }

    return true;
}

bool DLRemoteChangeReconciler::applyPayloadEvent(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    if (event.entityType == QStringLiteral("app_setting")) {
        return upsertLearningSetting(db, event, error);
    }

    if (event.entityType == QStringLiteral("group")) {
        return upsertGroup(db, event, error);
    }
    if (event.entityType == QStringLiteral("word")) {
        return upsertWord(db, event, error);
    }
    if (event.entityType == QStringLiteral("word_review_stats")) {
        return upsertReviewStats(db, event, error);
    }

    setError(error, QStringLiteral("Remote sync entity is not stored in the current local schema: %1").arg(event.entityType));
    return false;
}

bool DLRemoteChangeReconciler::applyTombstoneEvent(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    if (event.entityType == QStringLiteral("app_setting")) {
        return upsertLearningSetting(db, event, error);
    }

    if (!tombstoneIdentityMatchesEvent(event, error)) {
        return false;
    }

    if (event.entityType == QStringLiteral("group")) {
        return tombstoneGroup(db, event, error);
    }
    if (event.entityType == QStringLiteral("word")) {
        return tombstoneWord(db, event, error);
    }
    if (event.entityType == QStringLiteral("word_review_stats")) {
        return deleteReviewStats(db, event, error);
    }

    setError(error, QStringLiteral("Remote sync entity is not stored in the current local schema: %1").arg(event.entityType));
    return false;
}

bool DLRemoteChangeReconciler::upsertGroup(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    if (!payloadIdentityMatchesEvent(event, QStringLiteral("id"), error)) {
        return false;
    }

    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    if (!timestampSeconds(event.payload.value(QStringLiteral("created_at")), QStringLiteral("created_at"), &createdAt, error)
        || !timestampSeconds(event.payload.value(QStringLiteral("updated_at")), QStringLiteral("updated_at"), &updatedAt, error)) {
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO groups (sync_id, name, color_hex, created_at, updated_at, deleted_at)
        VALUES (:sync_id, :name, :color_hex, :created_at, :updated_at, NULL)
        ON CONFLICT(sync_id) DO UPDATE SET
            name = excluded.name,
            color_hex = excluded.color_hex,
            created_at = excluded.created_at,
            updated_at = excluded.updated_at,
            deleted_at = NULL;
    )"));
    query.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    query.bindValue(QStringLiteral(":name"), event.payload.value(QStringLiteral("name")).toString());
    query.bindValue(QStringLiteral(":color_hex"), event.payload.value(QStringLiteral("color_hex")).toString().trimmed().isEmpty()
                    ? QStringLiteral("#3366CC")
                    : event.payload.value(QStringLiteral("color_hex")).toString().trimmed());
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    query.bindValue(QStringLiteral(":updated_at"), updatedAt);
    return execQuery(query, error);
}

bool DLRemoteChangeReconciler::upsertWord(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    if (!payloadIdentityMatchesEvent(event, QStringLiteral("id"), error)) {
        return false;
    }

    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    if (!timestampSeconds(event.payload.value(QStringLiteral("created_at")), QStringLiteral("created_at"), &createdAt, error)
        || !timestampSeconds(event.payload.value(QStringLiteral("updated_at")), QStringLiteral("updated_at"), &updatedAt, error)) {
        return false;
    }

    const QString groupSyncId = event.payload.value(QStringLiteral("group_id")).toString().trimmed();
    const int groupId = groupSyncId.isEmpty() ? -1 : localGroupId(db, groupSyncId, error);
    if (!groupSyncId.isEmpty() && groupId < 0) {
        return false;
    }

    const QString germanWord = event.payload.value(QStringLiteral("german_word")).toString();
    const QString nativeTranslation = event.payload.value(QStringLiteral("native_translation")).toString();
    const QString normalizedGerman = event.payload.value(QStringLiteral("normalized_german_word")).toString().trimmed().isEmpty()
        ? DLDatabaseManager::normalizedText(germanWord)
        : event.payload.value(QStringLiteral("normalized_german_word")).toString().trimmed();
    const QString normalizedNative = event.payload.value(QStringLiteral("normalized_native_translation")).toString().trimmed().isEmpty()
        ? DLDatabaseManager::normalizedText(nativeTranslation)
        : event.payload.value(QStringLiteral("normalized_native_translation")).toString().trimmed();
    const QString partOfSpeech = event.payload.value(QStringLiteral("part_of_speech")).toString().trimmed().isEmpty()
        ? QStringLiteral("Andere")
        : event.payload.value(QStringLiteral("part_of_speech")).toString().trimmed();

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO words
            (sync_id, german_word, normalized_german_word, article, part_of_speech,
             native_translation, normalized_native_translation, example_phrase_de,
             example_phrase_native, group_id, group_sync_id, notes, created_at,
             updated_at, deleted_at)
        VALUES
            (:sync_id, :german_word, :normalized_german_word, :article, :part_of_speech,
             :native_translation, :normalized_native_translation, :example_phrase_de,
             :example_phrase_native, :group_id, :group_sync_id, :notes, :created_at,
             :updated_at, NULL)
        ON CONFLICT(sync_id) DO UPDATE SET
            german_word = excluded.german_word,
            normalized_german_word = excluded.normalized_german_word,
            article = excluded.article,
            part_of_speech = excluded.part_of_speech,
            native_translation = excluded.native_translation,
            normalized_native_translation = excluded.normalized_native_translation,
            example_phrase_de = excluded.example_phrase_de,
            example_phrase_native = excluded.example_phrase_native,
            group_id = excluded.group_id,
            group_sync_id = excluded.group_sync_id,
            notes = excluded.notes,
            created_at = excluded.created_at,
            updated_at = excluded.updated_at,
            deleted_at = NULL;
    )"));
    query.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    query.bindValue(QStringLiteral(":german_word"), germanWord);
    query.bindValue(QStringLiteral(":normalized_german_word"), normalizedGerman);
    query.bindValue(QStringLiteral(":article"), nullableString(event.payload.value(QStringLiteral("article"))));
    query.bindValue(QStringLiteral(":part_of_speech"), partOfSpeech);
    query.bindValue(QStringLiteral(":native_translation"), nativeTranslation);
    query.bindValue(QStringLiteral(":normalized_native_translation"), normalizedNative);
    query.bindValue(QStringLiteral(":example_phrase_de"), nullableString(event.payload.value(QStringLiteral("example_phrase_de"))));
    query.bindValue(QStringLiteral(":example_phrase_native"), nullableString(event.payload.value(QStringLiteral("example_phrase_native"))));
    query.bindValue(QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : QVariant());
    query.bindValue(QStringLiteral(":group_sync_id"), groupSyncId.isEmpty() ? QVariant() : QVariant(groupSyncId));
    query.bindValue(QStringLiteral(":notes"), nullableString(event.payload.value(QStringLiteral("notes"))));
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    query.bindValue(QStringLiteral(":updated_at"), updatedAt);
    if (!execQuery(query, error)) {
        return false;
    }

    const int wordId = storedWordId(db, event.entityId.trimmed(), error);
    if (wordId < 0 || !saveWordForms(db, wordId, event.payload, error)) {
        return false;
    }

    QSqlQuery stats(db);
    stats.prepare(QStringLiteral(R"(
        INSERT OR IGNORE INTO word_review_stats
            (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
             ease_factor, interval_days, due_at, updated_at)
        VALUES (:word_id, :word_sync_id, 0, 0, NULL, 2.5, 0, NULL, :updated_at);
    )"));
    stats.bindValue(QStringLiteral(":word_id"), wordId);
    stats.bindValue(QStringLiteral(":word_sync_id"), event.entityId.trimmed());
    stats.bindValue(QStringLiteral(":updated_at"), updatedAt);
    return execQuery(stats, error);
}

bool DLRemoteChangeReconciler::upsertReviewStats(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    if (!payloadIdentityMatchesEvent(event, QStringLiteral("word_id"), error)) {
        return false;
    }

    qint64 updatedAt = 0;
    if (!timestampSeconds(event.payload.value(QStringLiteral("updated_at")), QStringLiteral("updated_at"), &updatedAt, error)) {
        return false;
    }

    const int wordId = localWordId(db, event.entityId.trimmed(), error);
    if (wordId < 0) {
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO word_review_stats
            (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
             ease_factor, interval_days, due_at, updated_at)
        VALUES
            (:word_id, :word_sync_id, :correct_answers, :wrong_answers, :last_reviewed_at,
             :ease_factor, :interval_days, :due_at, :updated_at)
        ON CONFLICT(word_sync_id) DO UPDATE SET
            word_id = excluded.word_id,
            correct_answers = excluded.correct_answers,
            wrong_answers = excluded.wrong_answers,
            last_reviewed_at = excluded.last_reviewed_at,
            ease_factor = excluded.ease_factor,
            interval_days = excluded.interval_days,
            due_at = excluded.due_at,
            updated_at = excluded.updated_at;
    )"));
    query.bindValue(QStringLiteral(":word_id"), wordId);
    query.bindValue(QStringLiteral(":word_sync_id"), event.entityId.trimmed());
    query.bindValue(QStringLiteral(":correct_answers"), event.payload.value(QStringLiteral("correct_answers")).toInt());
    query.bindValue(QStringLiteral(":wrong_answers"), event.payload.value(QStringLiteral("wrong_answers")).toInt());
    query.bindValue(QStringLiteral(":last_reviewed_at"), optionalTimestampSeconds(event.payload.value(QStringLiteral("last_reviewed_at"))));
    query.bindValue(QStringLiteral(":ease_factor"), event.payload.value(QStringLiteral("ease_factor")).toDouble());
    query.bindValue(QStringLiteral(":interval_days"), event.payload.value(QStringLiteral("interval_days")).toInt());
    query.bindValue(QStringLiteral(":due_at"), optionalTimestampSeconds(event.payload.value(QStringLiteral("due_at"))));
    query.bindValue(QStringLiteral(":updated_at"), updatedAt);
    return execQuery(query, error);
}

bool DLRemoteChangeReconciler::tombstoneGroup(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    qint64 deletedAt = 0;
    if (!timestampSeconds(event.tombstone.value(QStringLiteral("deletedAt")), QStringLiteral("deletedAt"), &deletedAt, error)) {
        return false;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral(R"(
        UPDATE groups
        SET deleted_at = :deleted_at,
            updated_at = :updated_at
        WHERE sync_id = :sync_id;
    )"));
    update.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    update.bindValue(QStringLiteral(":deleted_at"), deletedAt);
    update.bindValue(QStringLiteral(":updated_at"), deletedAt);
    if (!execQuery(update, error)) {
        return false;
    }

    if (update.numRowsAffected() == 0) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral(R"(
            INSERT INTO groups (sync_id, name, color_hex, created_at, updated_at, deleted_at)
            VALUES (:sync_id, '', '#3366CC', :created_at, :updated_at, :deleted_at);
        )"));
        insert.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
        insert.bindValue(QStringLiteral(":created_at"), deletedAt);
        insert.bindValue(QStringLiteral(":updated_at"), deletedAt);
        insert.bindValue(QStringLiteral(":deleted_at"), deletedAt);
        if (!execQuery(insert, error)) {
            return false;
        }
    }

    QSqlQuery unlinkWords(db);
    unlinkWords.prepare(QStringLiteral(R"(
        UPDATE words
        SET group_id = NULL,
            group_sync_id = NULL,
            updated_at = :updated_at
        WHERE group_sync_id = :sync_id
          AND deleted_at IS NULL;
    )"));
    unlinkWords.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    unlinkWords.bindValue(QStringLiteral(":updated_at"), deletedAt);
    return execQuery(unlinkWords, error);
}

bool DLRemoteChangeReconciler::tombstoneWord(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    qint64 deletedAt = 0;
    if (!timestampSeconds(event.tombstone.value(QStringLiteral("deletedAt")), QStringLiteral("deletedAt"), &deletedAt, error)) {
        return false;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral(R"(
        UPDATE words
        SET deleted_at = :deleted_at,
            updated_at = :updated_at
        WHERE sync_id = :sync_id;
    )"));
    update.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    update.bindValue(QStringLiteral(":deleted_at"), deletedAt);
    update.bindValue(QStringLiteral(":updated_at"), deletedAt);
    if (!execQuery(update, error)) {
        return false;
    }

    if (update.numRowsAffected() > 0) {
        return true;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(R"(
        INSERT INTO words
            (sync_id, german_word, normalized_german_word, article, part_of_speech,
             native_translation, normalized_native_translation, example_phrase_de,
             example_phrase_native, group_id, group_sync_id, notes, created_at,
             updated_at, deleted_at)
        VALUES
            (:sync_id, '', '', NULL, 'Andere', '', '', NULL, NULL, NULL, NULL, NULL,
             :created_at, :updated_at, :deleted_at);
    )"));
    insert.bindValue(QStringLiteral(":sync_id"), event.entityId.trimmed());
    insert.bindValue(QStringLiteral(":created_at"), deletedAt);
    insert.bindValue(QStringLiteral(":updated_at"), deletedAt);
    insert.bindValue(QStringLiteral(":deleted_at"), deletedAt);
    return execQuery(insert, error);
}

bool DLRemoteChangeReconciler::deleteReviewStats(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM word_review_stats WHERE word_sync_id = :word_sync_id;"));
    query.bindValue(QStringLiteral(":word_sync_id"), event.entityId.trimmed());
    return execQuery(query, error);
}

bool DLRemoteChangeReconciler::upsertLearningSetting(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
    const bool deleted = event.operation == QStringLiteral("delete");
    if (deleted ? !tombstoneIdentityMatchesEvent(event, error)
                : !payloadIdentityMatchesEvent(event, QStringLiteral("id"), error)) {
        return false;
    }
    const QVariantMap body = deleted ? event.tombstone : event.payload;
    const QString key = body.value(QStringLiteral("setting_key")).toString();
    if (!key.startsWith(QStringLiteral("learning."))
        || (!deleted && body.value(QStringLiteral("scope")).toString() != QStringLiteral("account_learning"))) {
        setError(error, QStringLiteral("Only account learning settings may synchronize."));
        return false;
    }
    qint64 updatedAt = 0;
    qint64 createdAt = 0;
    qint64 deletedAt = 0;
    if (!timestampSeconds(body.value(deleted ? QStringLiteral("updatedAt") : QStringLiteral("updated_at")),
                          QStringLiteral("updatedAt"), &updatedAt, error)
        || (!deleted && !timestampSeconds(body.value(QStringLiteral("created_at")), QStringLiteral("created_at"), &createdAt, error))
        || (deleted && !timestampSeconds(body.value(QStringLiteral("deletedAt")), QStringLiteral("deletedAt"), &deletedAt, error))) {
        return false;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"(
        INSERT INTO account_learning_settings
            (sync_id, setting_key, setting_value, value_type, created_at, updated_at, deleted_at)
        VALUES (:id, :key, :value, :type, :created, :updated, :deleted)
        ON CONFLICT(setting_key) DO UPDATE SET
            sync_id = excluded.sync_id, setting_value = excluded.setting_value,
            value_type = excluded.value_type, created_at = excluded.created_at,
            updated_at = excluded.updated_at, deleted_at = excluded.deleted_at;
    )"));
    query.bindValue(QStringLiteral(":id"), event.entityId);
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), deleted ? QVariant() : body.value(QStringLiteral("setting_value")));
    query.bindValue(QStringLiteral(":type"), deleted ? QStringLiteral("string") : body.value(QStringLiteral("value_type")).toString());
    query.bindValue(QStringLiteral(":created"), deleted ? updatedAt : createdAt);
    query.bindValue(QStringLiteral(":updated"), updatedAt);
    query.bindValue(QStringLiteral(":deleted"), deleted ? QVariant(deletedAt) : QVariant());
    return execQuery(query, error);
}
