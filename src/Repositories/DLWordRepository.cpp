#include "DLWordRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Models/DLModelMappers.h"

namespace {
int localWordId(QSqlDatabase& db, const QString& syncId, QString* error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM words WHERE sync_id = :sync_id AND deleted_at IS NULL;"));
    query.bindValue(QStringLiteral(":sync_id"), syncId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return -1;
    }
    return query.next() ? query.value(0).toInt() : -1;
}

QVariant localGroupId(QSqlDatabase& db, const QString& groupSyncId, QString* error)
{
    if (groupSyncId.trimmed().isEmpty()) {
        return DLDatabaseManager::nullVariant();
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM groups WHERE sync_id = :sync_id AND deleted_at IS NULL;"));
    query.bindValue(QStringLiteral(":sync_id"), groupSyncId.trimmed());
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return DLDatabaseManager::nullVariant();
    }
    return query.next() ? QVariant(query.value(0).toInt()) : DLDatabaseManager::nullVariant();
}
}

DLWordRepository::DLWordRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLWordRepository::wordSelectColumns()
{
    return QStringLiteral(
        "w.sync_id AS id, w.id AS local_id, w.sync_id, w.sync_id AS syncId, "
        "w.german_word, w.normalized_german_word, w.article, w.part_of_speech, "
        "w.native_translation, w.normalized_native_translation, "
        "w.example_phrase_de, w.example_phrase_native, "
        "g.sync_id AS group_id, w.group_id AS local_group_id, w.notes, "
        "COALESCE(w.plural_form, nf.plural_form) AS plural_form, "
        "COALESCE(w.plural_form, nf.plural_form) AS pluralForm, "
        "vf.praeteritum_form, vf.praeteritum_form AS praeteritumForm, "
        "vf.partizip_ii_form, vf.partizip_ii_form AS partizipIIForm, "
        "af.positive_form, af.positive_form AS positiveForm, "
        "af.comparative_form, af.comparative_form AS comparativeForm, "
        "af.superlative_form, af.superlative_form AS superlativeForm, "
        "w.created_at, w.updated_at, w.deleted_at, w.server_updated_at, "
        "w.server_version, w.device_id, w.dirty, "
        "rs.sync_id AS stats_id, "
        "rs.last_reviewed_at, COALESCE(rs.correct_answers, 0) AS correct_answers, "
        "COALESCE(rs.wrong_answers, 0) AS wrong_answers, "
        "COALESCE(rs.ease_factor, 2.5) AS ease_factor, "
        "COALESCE(rs.interval_days, 0) AS interval_days, rs.due_at, "
        "rs.created_at AS stats_created_at, rs.updated_at AS stats_updated_at, "
        "rs.deleted_at AS stats_deleted_at, rs.server_updated_at AS stats_server_updated_at, "
        "rs.server_version AS stats_server_version, rs.device_id AS stats_device_id, "
        "rs.dirty AS stats_dirty");
}

QString DLWordRepository::wordFromClause()
{
    return QStringLiteral(R"(
        words w
        LEFT JOIN word_review_stats rs ON rs.word_id = w.id
        LEFT JOIN groups g ON g.id = w.group_id
        LEFT JOIN noun_forms nf ON nf.word_id = w.id
        LEFT JOIN verb_forms vf ON vf.word_id = w.id
        LEFT JOIN adjective_forms af ON af.word_id = w.id
    )");
}

QString DLWordRepository::insertWord(const DLWord& word)
{
    if (wordExists(word.germanWord, word.nativeTranslation)) {
        m_database.setLastError(QStringLiteral("Duplicate: word + translation pair already exists."));
        qCWarning(dlRepo) << "Rejected duplicate word insert";
        return {};
    }

    int newId = -1;
    const QString newSyncId = word.id.trimmed().isEmpty() ? DLDatabaseManager::generateUuid() : word.id.trimmed();
    const bool ok = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTimeMs();
        const QString deviceId = DLDatabaseManager::currentDeviceId();
        const QVariant resolvedGroupId = localGroupId(db, word.groupId, error);
        if (error && !error->isEmpty()) {
            return false;
        }
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO words
                (sync_id, german_word, normalized_german_word, article, part_of_speech,
                 native_translation, normalized_native_translation,
                 example_phrase_de, example_phrase_native, group_id,
                 plural_form, notes, created_at, updated_at, deleted_at,
                 server_updated_at, server_version, device_id, dirty)
            VALUES
                (:sync_id, :german_word, :normalized_german_word, :article, :part_of_speech,
                 :native_translation, :normalized_native_translation,
                 :example_phrase_de, :example_phrase_native, :group_id,
                 :plural_form, :notes, :created_at, :updated_at, NULL,
                 NULL, 0, :device_id, 1);
        )"));
        query.bindValue(QStringLiteral(":sync_id"), newSyncId);
        query.bindValue(QStringLiteral(":german_word"), word.germanWord);
        query.bindValue(QStringLiteral(":normalized_german_word"), DLDatabaseManager::normalizedText(word.germanWord));
        query.bindValue(QStringLiteral(":article"), word.article.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.article.trimmed()));
        query.bindValue(QStringLiteral(":part_of_speech"), word.partOfSpeech.trimmed().isEmpty() ? QStringLiteral("Andere") : word.partOfSpeech.trimmed());
        query.bindValue(QStringLiteral(":native_translation"), word.nativeTranslation);
        query.bindValue(QStringLiteral(":normalized_native_translation"), DLDatabaseManager::normalizedText(word.nativeTranslation));
        query.bindValue(QStringLiteral(":example_phrase_de"), word.examplePhraseDe.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseDe.trimmed()));
        query.bindValue(QStringLiteral(":example_phrase_native"), word.examplePhraseNative.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseNative.trimmed()));
        query.bindValue(QStringLiteral(":group_id"), resolvedGroupId);
        const QString pluralForm = word.pluralForm.isEmpty() ? word.nounForms.pluralForm : word.pluralForm;
        query.bindValue(QStringLiteral(":plural_form"), pluralForm.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(pluralForm.trimmed()));
        query.bindValue(QStringLiteral(":notes"), word.notes.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.notes.trimmed()));
        query.bindValue(QStringLiteral(":created_at"), now);
        query.bindValue(QStringLiteral(":updated_at"), now);
        query.bindValue(QStringLiteral(":device_id"), deviceId);

        if (!bindAndExec(query, error)) {
            return false;
        }

        newId = static_cast<int>(query.lastInsertId().toLongLong());

        QSqlQuery stats(db);
        stats.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, created_at, updated_at,
                 deleted_at, server_updated_at, server_version, device_id, dirty)
            VALUES
                (:word_id, :sync_id, 0, 0, NULL, 2.5, 0, NULL,
                 :updated_at, :updated_at, NULL, NULL, 0, :device_id, 1);
        )"));
        stats.bindValue(QStringLiteral(":word_id"), newId);
        stats.bindValue(QStringLiteral(":sync_id"), DLDatabaseManager::generateUuid());
        stats.bindValue(QStringLiteral(":updated_at"), now);
        stats.bindValue(QStringLiteral(":device_id"), deviceId);
        if (!bindAndExec(stats, error)) {
            return false;
        }

        if (!saveForms(db, error, newId, word)) {
            return false;
        }

        DLWord phraseCandidate = word;
        phraseCandidate.id = newSyncId;
        return createPhraseFromExample(db, error, phraseCandidate, newId);
    });

    if (!ok) {
        qCWarning(dlRepo) << "Failed to insert word:" << m_database.lastError();
        return {};
    }

    qCDebug(dlRepo) << "Inserted word with id" << newSyncId;
    return newSyncId;
}

bool DLWordRepository::updateWord(const DLWord& word)
{
    const DLWord current = fetchWordById(word.id);
    if (current.id.isEmpty()) {
        m_database.setLastError(QStringLiteral("Word not found."));
        qCWarning(dlRepo) << "Cannot update missing word" << word.id;
        return false;
    }

    if (wordExists(word.germanWord, word.nativeTranslation, word.id)) {
        m_database.setLastError(QStringLiteral("Duplicate: word + translation pair already exists."));
        qCWarning(dlRepo) << "Rejected duplicate word update for id" << word.id;
        return false;
    }

    const bool success = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const int localId = localWordId(db, word.id, error);
        if ((error && !error->isEmpty()) || localId < 0) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Word not found.");
            }
            return false;
        }

        const QVariant resolvedGroupId = localGroupId(db, word.groupId, error);
        if (error && !error->isEmpty()) {
            return false;
        }

        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            UPDATE words SET
                german_word = :german_word,
                normalized_german_word = :normalized_german_word,
                article = :article,
                part_of_speech = :part_of_speech,
                native_translation = :native_translation,
                normalized_native_translation = :normalized_native_translation,
                example_phrase_de = :example_phrase_de,
                example_phrase_native = :example_phrase_native,
                group_id = :group_id,
                plural_form = :plural_form,
                notes = :notes,
                updated_at = :updated_at,
                device_id = :device_id,
                dirty = 1
            WHERE sync_id = :id;
        )"));
        query.bindValue(QStringLiteral(":id"), word.id);
        query.bindValue(QStringLiteral(":german_word"), word.germanWord);
        query.bindValue(QStringLiteral(":normalized_german_word"), DLDatabaseManager::normalizedText(word.germanWord));
        query.bindValue(QStringLiteral(":article"), word.article.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.article.trimmed()));
        query.bindValue(QStringLiteral(":part_of_speech"), word.partOfSpeech.trimmed().isEmpty() ? QStringLiteral("Andere") : word.partOfSpeech.trimmed());
        query.bindValue(QStringLiteral(":native_translation"), word.nativeTranslation);
        query.bindValue(QStringLiteral(":normalized_native_translation"), DLDatabaseManager::normalizedText(word.nativeTranslation));
        query.bindValue(QStringLiteral(":example_phrase_de"), word.examplePhraseDe.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseDe.trimmed()));
        query.bindValue(QStringLiteral(":example_phrase_native"), word.examplePhraseNative.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseNative.trimmed()));
        query.bindValue(QStringLiteral(":group_id"), resolvedGroupId);
        const QString pluralForm = word.pluralForm.isEmpty() ? word.nounForms.pluralForm : word.pluralForm;
        query.bindValue(QStringLiteral(":plural_form"), pluralForm.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(pluralForm.trimmed()));
        query.bindValue(QStringLiteral(":notes"), word.notes.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.notes.trimmed()));
        query.bindValue(QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTimeMs());
        query.bindValue(QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId());

        if (!bindAndExec(query, error) || !saveForms(db, error, localId, word)) {
            return false;
        }

        const bool wasPhrase = current.partOfSpeech.compare(QStringLiteral("Phrase"), Qt::CaseInsensitive) == 0;
        return wasPhrase ? true : createPhraseFromExample(db, error, word, localId);
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update word" << word.id << ":" << m_database.lastError();
    }
    return success;
}

bool DLWordRepository::deleteWord(const QString& id)
{
    const bool success = m_database.executeSql(
        QStringLiteral(R"(
            UPDATE words
            SET deleted_at = COALESCE(deleted_at, :deleted_at),
                updated_at = :deleted_at,
                device_id = :device_id,
                dirty = 1
            WHERE sync_id = :id
              AND deleted_at IS NULL;
        )"),
        {
            { QStringLiteral(":id"), id },
            { QStringLiteral(":deleted_at"), DLDatabaseManager::currentUnixTimeMs() },
            { QStringLiteral(":device_id"), DLDatabaseManager::currentDeviceId() }
        });
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete word" << id << ":" << m_database.lastError();
    }
    return success;
}

DLWord DLWordRepository::fetchWordById(const QString& id)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral("SELECT %1 FROM %2 WHERE w.sync_id = :id AND w.deleted_at IS NULL;")
            .arg(wordSelectColumns(), wordFromClause()),
        {{ QStringLiteral(":id"), id }});

    if (row.isEmpty()) {
        return {};
    }

    return DLModelMappers::wordFromMap(row);
}

QList<DLWord> DLWordRepository::fetchAllWords(const QString& sortMode, const QString& groupId)
{
    QString sql = QStringLiteral("SELECT %1 FROM %2 WHERE w.deleted_at IS NULL")
                      .arg(wordSelectColumns(), wordFromClause());
    QVariantMap args;

    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY ") + sortClause(sortMode) + QStringLiteral(";");
    return fetchWords(sql, args);
}

QList<DLWord> DLWordRepository::searchWords(const QString& query, const QString& groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE w.deleted_at IS NULL
          AND (w.german_word LIKE :pattern
               OR w.native_translation LIKE :pattern
               OR nf.plural_form LIKE :pattern
               OR vf.praeteritum_form LIKE :pattern
               OR vf.partizip_ii_form LIKE :pattern
               OR af.positive_form LIKE :pattern
               OR af.comparative_form LIKE :pattern
               OR af.superlative_form LIKE :pattern)
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args = {{ QStringLiteral(":pattern"), QStringLiteral("%") + query + QStringLiteral("%") }};
    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY w.german_word;");
    return fetchWords(sql, args);
}

bool DLWordRepository::wordExists(const QString& germanWord, const QString& nativeTranslation, const QString& excludingId)
{
    QString sql = QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words
        WHERE normalized_german_word = :german_word
          AND normalized_native_translation = :native_translation
          AND deleted_at IS NULL
    )");

    QVariantMap args = {
        { QStringLiteral(":german_word"), DLDatabaseManager::normalizedText(germanWord) },
        { QStringLiteral(":native_translation"), DLDatabaseManager::normalizedText(nativeTranslation) }
    };

    if (!excludingId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND sync_id != :excluding_id");
        args.insert(QStringLiteral(":excluding_id"), excludingId);
    }

    return m_database.selectInt(sql + QStringLiteral(";"), args) > 0;
}

int DLWordRepository::getWordCount(const QString& groupId)
{
    if (!groupId.trimmed().isEmpty()) {
        return m_database.selectInt(
            QStringLiteral(R"(
                SELECT COUNT(*)
                FROM words w
                JOIN groups g ON g.id = w.group_id
                WHERE w.deleted_at IS NULL
                  AND g.sync_id = :group_id;
            )"),
            {{ QStringLiteral(":group_id"), groupId }});
    }

    return m_database.selectInt(QStringLiteral("SELECT COUNT(*) FROM words WHERE deleted_at IS NULL;"));
}

QString DLWordRepository::sortClause(const QString& sortMode) const
{
    if (sortMode == QStringLiteral("oldest")) {
        return QStringLiteral("w.created_at ASC");
    }
    if (sortMode == QStringLiteral("az")) {
        return QStringLiteral("w.german_word ASC");
    }
    if (sortMode == QStringLiteral("za")) {
        return QStringLiteral("w.german_word DESC");
    }
    return QStringLiteral("w.created_at DESC");
}

QList<DLWord> DLWordRepository::fetchWords(const QString& sql, const QVariantMap& args)
{
    const QVariantList rows = m_database.selectRows(sql, args);
    QList<DLWord> words;
    for (const QVariant& row : rows) {
        words.append(DLModelMappers::wordFromMap(row.toMap()));
    }
    return words;
}

bool DLWordRepository::saveForms(QSqlDatabase& db, QString* error, int wordId, const DLWord& word)
{
    QSqlQuery deleteNoun(db);
    deleteNoun.prepare(QStringLiteral("DELETE FROM noun_forms WHERE word_id = :word_id;"));
    deleteNoun.bindValue(QStringLiteral(":word_id"), wordId);
    if (!bindAndExec(deleteNoun, error)) {
        return false;
    }

    if (!word.nounForms.pluralForm.trimmed().isEmpty()) {
        QSqlQuery noun(db);
        noun.prepare(QStringLiteral("INSERT INTO noun_forms (word_id, plural_form) VALUES (:word_id, :plural_form);"));
        noun.bindValue(QStringLiteral(":word_id"), wordId);
        noun.bindValue(QStringLiteral(":plural_form"), word.nounForms.pluralForm.trimmed());
        if (!bindAndExec(noun, error)) {
            return false;
        }
    }

    QSqlQuery deleteVerb(db);
    deleteVerb.prepare(QStringLiteral("DELETE FROM verb_forms WHERE word_id = :word_id;"));
    deleteVerb.bindValue(QStringLiteral(":word_id"), wordId);
    if (!bindAndExec(deleteVerb, error)) {
        return false;
    }

    const bool hasVerbForms = !word.verbForms.praeteritumForm.trimmed().isEmpty()
        || !word.verbForms.partizipIIForm.trimmed().isEmpty();
    if (hasVerbForms) {
        QSqlQuery verb(db);
        verb.prepare(QStringLiteral(R"(
            INSERT INTO verb_forms (word_id, praeteritum_form, partizip_ii_form)
            VALUES (:word_id, :praeteritum_form, :partizip_ii_form);
        )"));
        verb.bindValue(QStringLiteral(":word_id"), wordId);
        verb.bindValue(QStringLiteral(":praeteritum_form"), word.verbForms.praeteritumForm.trimmed().isEmpty()
                       ? DLDatabaseManager::nullVariant()
                       : QVariant(word.verbForms.praeteritumForm.trimmed()));
        verb.bindValue(QStringLiteral(":partizip_ii_form"), word.verbForms.partizipIIForm.trimmed().isEmpty()
                       ? DLDatabaseManager::nullVariant()
                       : QVariant(word.verbForms.partizipIIForm.trimmed()));
        if (!bindAndExec(verb, error)) {
            return false;
        }
    }

    QSqlQuery deleteAdjective(db);
    deleteAdjective.prepare(QStringLiteral("DELETE FROM adjective_forms WHERE word_id = :word_id;"));
    deleteAdjective.bindValue(QStringLiteral(":word_id"), wordId);
    if (!bindAndExec(deleteAdjective, error)) {
        return false;
    }

    const bool hasAdjectiveForms = !word.adjectiveForms.positiveForm.trimmed().isEmpty()
        || !word.adjectiveForms.comparativeForm.trimmed().isEmpty()
        || !word.adjectiveForms.superlativeForm.trimmed().isEmpty();
    if (hasAdjectiveForms) {
        QSqlQuery adjective(db);
        adjective.prepare(QStringLiteral(R"(
            INSERT INTO adjective_forms
                (word_id, positive_form, comparative_form, superlative_form)
            VALUES
                (:word_id, :positive_form, :comparative_form, :superlative_form);
        )"));
        adjective.bindValue(QStringLiteral(":word_id"), wordId);
        adjective.bindValue(QStringLiteral(":positive_form"), word.adjectiveForms.positiveForm.trimmed().isEmpty()
                            ? DLDatabaseManager::nullVariant()
                            : QVariant(word.adjectiveForms.positiveForm.trimmed()));
        adjective.bindValue(QStringLiteral(":comparative_form"), word.adjectiveForms.comparativeForm.trimmed().isEmpty()
                            ? DLDatabaseManager::nullVariant()
                            : QVariant(word.adjectiveForms.comparativeForm.trimmed()));
        adjective.bindValue(QStringLiteral(":superlative_form"), word.adjectiveForms.superlativeForm.trimmed().isEmpty()
                            ? DLDatabaseManager::nullVariant()
                            : QVariant(word.adjectiveForms.superlativeForm.trimmed()));
        return bindAndExec(adjective, error);
    }

    return true;
}

bool DLWordRepository::createPhraseFromExample(QSqlDatabase& db, QString* error, const DLWord& word, int localWordId)
{
    Q_UNUSED(localWordId);

    if (word.partOfSpeech.compare(QStringLiteral("Phrase"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString phraseGerman = word.examplePhraseDe.trimmed();
    const QString phraseNative = word.examplePhraseNative.trimmed();
    if (phraseGerman.isEmpty() || phraseNative.isEmpty()) {
        return true;
    }

    QSqlQuery exists(db);
    exists.prepare(QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words
        WHERE normalized_german_word = :german_word
          AND normalized_native_translation = :native_translation
          AND deleted_at IS NULL;
    )"));
    exists.bindValue(QStringLiteral(":german_word"), DLDatabaseManager::normalizedText(phraseGerman));
    exists.bindValue(QStringLiteral(":native_translation"), DLDatabaseManager::normalizedText(phraseNative));
    if (!bindAndExec(exists, error) || (exists.next() && exists.value(0).toInt() > 0)) {
        return exists.isActive();
    }

    const qint64 now = DLDatabaseManager::currentUnixTimeMs();
    const QString phraseId = DLDatabaseManager::generateUuid();
    const QString deviceId = DLDatabaseManager::currentDeviceId();
    const QVariant resolvedGroupId = localGroupId(db, word.groupId, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    QSqlQuery phrase(db);
    phrase.prepare(QStringLiteral(R"(
        INSERT INTO words
            (sync_id, german_word, normalized_german_word, article, part_of_speech,
             native_translation, normalized_native_translation,
             example_phrase_de, example_phrase_native, group_id,
             plural_form, notes, created_at, updated_at, deleted_at,
             server_updated_at, server_version, device_id, dirty)
        VALUES
            (:sync_id, :german_word, :normalized_german_word, NULL, 'Phrase',
             :native_translation, :normalized_native_translation,
             NULL, NULL, :group_id, NULL, NULL, :created_at, :updated_at, NULL,
             NULL, 0, :device_id, 1);
    )"));
    phrase.bindValue(QStringLiteral(":sync_id"), phraseId);
    phrase.bindValue(QStringLiteral(":german_word"), phraseGerman);
    phrase.bindValue(QStringLiteral(":normalized_german_word"), DLDatabaseManager::normalizedText(phraseGerman));
    phrase.bindValue(QStringLiteral(":native_translation"), phraseNative);
    phrase.bindValue(QStringLiteral(":normalized_native_translation"), DLDatabaseManager::normalizedText(phraseNative));
    phrase.bindValue(QStringLiteral(":group_id"), resolvedGroupId);
    phrase.bindValue(QStringLiteral(":created_at"), now);
    phrase.bindValue(QStringLiteral(":updated_at"), now);
    phrase.bindValue(QStringLiteral(":device_id"), deviceId);

    if (!bindAndExec(phrase, error)) {
        return false;
    }

    QSqlQuery stats(db);
    stats.prepare(QStringLiteral(R"(
        INSERT INTO word_review_stats
            (word_id, sync_id, correct_answers, wrong_answers, last_reviewed_at,
             ease_factor, interval_days, due_at, created_at, updated_at,
             deleted_at, server_updated_at, server_version, device_id, dirty)
        VALUES
            (:word_id, :sync_id, 0, 0, NULL, 2.5, 0, NULL,
             :updated_at, :updated_at, NULL, NULL, 0, :device_id, 1);
    )"));
    stats.bindValue(QStringLiteral(":word_id"), phrase.lastInsertId().toInt());
    stats.bindValue(QStringLiteral(":sync_id"), DLDatabaseManager::generateUuid());
    stats.bindValue(QStringLiteral(":updated_at"), now);
    stats.bindValue(QStringLiteral(":device_id"), deviceId);
    return bindAndExec(stats, error);
}

bool DLWordRepository::bindAndExec(QSqlQuery& query, QString* error)
{
    if (query.exec()) {
        return true;
    }

    if (error) {
        *error = query.lastError().text();
    }
    qCWarning(dlRepo) << "Failed repository query:" << query.lastError().text();
    return false;
}
