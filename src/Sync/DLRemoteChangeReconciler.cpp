#include "DLRemoteChangeReconciler.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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

bool DLRemoteChangeReconciler::applyPayloadEvent(QSqlDatabase& db, const DLSyncEventEnvelope& event, QString* error)
{
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
