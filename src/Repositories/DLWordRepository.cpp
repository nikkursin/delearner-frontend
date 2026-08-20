#include "DLWordRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "DLDatabaseManager.h"
#include "DLLogging.h"
#include "../Models/DLModelMappers.h"

DLWordRepository::DLWordRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QString DLWordRepository::wordSelectColumns()
{
    return QStringLiteral(
        "w.id, w.sync_id, w.sync_id AS syncId, "
        "w.german_word, w.normalized_german_word, w.article, w.part_of_speech, "
        "w.native_translation, w.normalized_native_translation, "
        "w.example_phrase_de, w.example_phrase_native, w.group_id, w.group_sync_id, w.group_sync_id AS groupSyncId, w.notes, "
        "nf.plural_form, nf.plural_form AS pluralForm, "
        "vf.praeteritum_form, vf.praeteritum_form AS praeteritumForm, "
        "vf.partizip_ii_form, vf.partizip_ii_form AS partizipIIForm, "
        "af.positive_form, af.positive_form AS positiveForm, "
        "af.comparative_form, af.comparative_form AS comparativeForm, "
        "af.superlative_form, af.superlative_form AS superlativeForm, "
        "w.created_at, w.updated_at, w.deleted_at, "
        "rs.last_reviewed_at, COALESCE(rs.correct_answers, 0) AS correct_answers, "
        "COALESCE(rs.wrong_answers, 0) AS wrong_answers, "
        "COALESCE(rs.ease_factor, 2.5) AS ease_factor, "
        "COALESCE(rs.interval_days, 0) AS interval_days, rs.due_at, "
        "rs.updated_at AS stats_updated_at");
}

QString DLWordRepository::wordFromClause()
{
    return QStringLiteral(R"(
        words w
        LEFT JOIN word_review_stats rs ON rs.word_id = w.id
        LEFT JOIN noun_forms nf ON nf.word_id = w.id
        LEFT JOIN verb_forms vf ON vf.word_id = w.id
        LEFT JOIN adjective_forms af ON af.word_id = w.id
    )");
}

int DLWordRepository::insertWord(const DLWord& word)
{
    if (wordExists(word.germanWord, word.nativeTranslation)) {
        m_database.setLastError(QStringLiteral("Duplicate: word + translation pair already exists."));
        qCWarning(dlRepo) << "Rejected duplicate word insert";
        return -1;
    }

    int newId = -1;
    const bool ok = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTime();
        const QString syncId = word.syncId.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : word.syncId.trimmed();
        QString groupSyncId;
        if (word.groupId >= 0) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare(QStringLiteral("SELECT sync_id FROM groups WHERE id = :id;"));
            groupQuery.bindValue(QStringLiteral(":id"), word.groupId);
            if (!bindAndExec(groupQuery, error)) {
                return false;
            }
            if (groupQuery.next()) {
                groupSyncId = groupQuery.value(0).toString();
            }
        }
        QSqlQuery query(db);
        query.prepare(QStringLiteral(R"(
            INSERT INTO words
                (sync_id, german_word, normalized_german_word, article, part_of_speech,
                 native_translation, normalized_native_translation,
                 example_phrase_de, example_phrase_native, group_id, group_sync_id,
                 notes, created_at, updated_at, deleted_at)
            VALUES
                (:sync_id, :german_word, :normalized_german_word, :article, :part_of_speech,
                 :native_translation, :normalized_native_translation,
                 :example_phrase_de, :example_phrase_native, :group_id, :group_sync_id,
                 :notes, :created_at, :updated_at, NULL);
        )"));
        query.bindValue(QStringLiteral(":sync_id"), syncId);
        query.bindValue(QStringLiteral(":german_word"), word.germanWord);
        query.bindValue(QStringLiteral(":normalized_german_word"), DLDatabaseManager::normalizedText(word.germanWord));
        query.bindValue(QStringLiteral(":article"), word.article.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.article.trimmed()));
        query.bindValue(QStringLiteral(":part_of_speech"), word.partOfSpeech.trimmed().isEmpty() ? QStringLiteral("Andere") : word.partOfSpeech.trimmed());
        query.bindValue(QStringLiteral(":native_translation"), word.nativeTranslation);
        query.bindValue(QStringLiteral(":normalized_native_translation"), DLDatabaseManager::normalizedText(word.nativeTranslation));
        query.bindValue(QStringLiteral(":example_phrase_de"), word.examplePhraseDe.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseDe.trimmed()));
        query.bindValue(QStringLiteral(":example_phrase_native"), word.examplePhraseNative.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.examplePhraseNative.trimmed()));
        query.bindValue(QStringLiteral(":group_id"), word.groupId >= 0 ? QVariant(word.groupId) : DLDatabaseManager::nullVariant());
        query.bindValue(QStringLiteral(":group_sync_id"), groupSyncId.isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(groupSyncId));
        query.bindValue(QStringLiteral(":notes"), word.notes.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.notes.trimmed()));
        query.bindValue(QStringLiteral(":created_at"), now);
        query.bindValue(QStringLiteral(":updated_at"), now);

        if (!bindAndExec(query, error)) {
            return false;
        }

        newId = static_cast<int>(query.lastInsertId().toLongLong());

        QSqlQuery stats(db);
        stats.prepare(QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
                 ease_factor, interval_days, due_at, updated_at)
            VALUES (:word_id, :word_sync_id, 0, 0, NULL, 2.5, 0, NULL, :updated_at);
        )"));
        stats.bindValue(QStringLiteral(":word_id"), newId);
        stats.bindValue(QStringLiteral(":word_sync_id"), syncId);
        stats.bindValue(QStringLiteral(":updated_at"), now);
        if (!bindAndExec(stats, error)) {
            return false;
        }

        if (!saveForms(db, error, newId, word)) {
            return false;
        }

        DLWord phraseCandidate = word;
        phraseCandidate.id = newId;
        return createPhraseFromExample(db, error, phraseCandidate);
    });

    if (!ok) {
        qCWarning(dlRepo) << "Failed to insert word:" << m_database.lastError();
        return -1;
    }

    qCDebug(dlRepo) << "Inserted word with id" << newId;
    return newId;
}

bool DLWordRepository::updateWord(const DLWord& word)
{
    const DLWord current = fetchWordById(word.id);
    if (current.id < 0) {
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
                group_sync_id = :group_sync_id,
                notes = :notes,
                updated_at = :updated_at
            WHERE id = :id;
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
        query.bindValue(QStringLiteral(":group_id"), word.groupId >= 0 ? QVariant(word.groupId) : DLDatabaseManager::nullVariant());
        if (word.groupId >= 0) {
            QSqlQuery groupQuery(db);
            groupQuery.prepare(QStringLiteral("SELECT sync_id FROM groups WHERE id = :id;"));
            groupQuery.bindValue(QStringLiteral(":id"), word.groupId);
            if (!bindAndExec(groupQuery, error)) {
                return false;
            }
            query.bindValue(QStringLiteral(":group_sync_id"), groupQuery.next() ? groupQuery.value(0) : DLDatabaseManager::nullVariant());
        } else {
            query.bindValue(QStringLiteral(":group_sync_id"), DLDatabaseManager::nullVariant());
        }
        query.bindValue(QStringLiteral(":notes"), word.notes.trimmed().isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(word.notes.trimmed()));
        query.bindValue(QStringLiteral(":updated_at"), DLDatabaseManager::currentUnixTime());

        if (!bindAndExec(query, error) || !saveForms(db, error, word.id, word)) {
            return false;
        }

        const bool wasPhrase = current.partOfSpeech.compare(QStringLiteral("Phrase"), Qt::CaseInsensitive) == 0;
        return wasPhrase ? true : createPhraseFromExample(db, error, word);
    });
    if (!success) {
        qCWarning(dlRepo) << "Failed to update word" << word.id << ":" << m_database.lastError();
    }
    return success;
}

bool DLWordRepository::deleteWord(int id)
{
    const bool success = m_database.executeSql(
        QStringLiteral("DELETE FROM words WHERE id = :id;"),
        {{ QStringLiteral(":id"), id }});
    if (!success) {
        qCWarning(dlRepo) << "Failed to delete word" << id << ":" << m_database.lastError();
    }
    return success;
}

DLWord DLWordRepository::fetchWordById(int id)
{
    const QVariantMap row = m_database.selectOneRow(
        QStringLiteral("SELECT %1 FROM %2 WHERE w.id = :id AND w.deleted_at IS NULL;")
            .arg(wordSelectColumns(), wordFromClause()),
        {{ QStringLiteral(":id"), id }});

    if (row.isEmpty()) {
        return {};
    }

    return DLModelMappers::wordFromMap(row);
}

QList<DLWord> DLWordRepository::fetchAllWords(const QString& sortMode, int groupId)
{
    QString sql = QStringLiteral("SELECT %1 FROM %2 WHERE w.deleted_at IS NULL")
                      .arg(wordSelectColumns(), wordFromClause());
    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" AND w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY ") + sortClause(sortMode) + QStringLiteral(";");
    return fetchWords(sql, args);
}

QList<DLWord> DLWordRepository::searchWords(const QString& query, int groupId)
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
    if (groupId >= 0) {
        sql += QStringLiteral(" AND w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY w.german_word;");
    return fetchWords(sql, args);
}

bool DLWordRepository::wordExists(const QString& germanWord, const QString& nativeTranslation, int excludingId)
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

    if (excludingId >= 0) {
        sql += QStringLiteral(" AND id != :excluding_id");
        args.insert(QStringLiteral(":excluding_id"), excludingId);
    }

    return m_database.selectInt(sql + QStringLiteral(";"), args) > 0;
}

int DLWordRepository::getWordCount(int groupId)
{
    if (groupId >= 0) {
        return m_database.selectInt(
            QStringLiteral("SELECT COUNT(*) FROM words WHERE deleted_at IS NULL AND group_id = :group_id;"),
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

bool DLWordRepository::createPhraseFromExample(QSqlDatabase& db, QString* error, const DLWord& word)
{
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

    const qint64 now = DLDatabaseManager::currentUnixTime();
    const QString phraseSyncId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString groupSyncId;
    if (word.groupId >= 0) {
        QSqlQuery groupQuery(db);
        groupQuery.prepare(QStringLiteral("SELECT sync_id FROM groups WHERE id = :id;"));
        groupQuery.bindValue(QStringLiteral(":id"), word.groupId);
        if (!bindAndExec(groupQuery, error)) {
            return false;
        }
        if (groupQuery.next()) {
            groupSyncId = groupQuery.value(0).toString();
        }
    }

    QSqlQuery phrase(db);
    phrase.prepare(QStringLiteral(R"(
        INSERT INTO words
            (sync_id, german_word, normalized_german_word, article, part_of_speech,
             native_translation, normalized_native_translation,
             example_phrase_de, example_phrase_native, group_id, group_sync_id,
             notes, created_at, updated_at, deleted_at)
        VALUES
            (:sync_id, :german_word, :normalized_german_word, NULL, 'Phrase',
             :native_translation, :normalized_native_translation,
             NULL, NULL, :group_id, :group_sync_id, NULL, :created_at, :updated_at, NULL);
    )"));
    phrase.bindValue(QStringLiteral(":sync_id"), phraseSyncId);
    phrase.bindValue(QStringLiteral(":german_word"), phraseGerman);
    phrase.bindValue(QStringLiteral(":normalized_german_word"), DLDatabaseManager::normalizedText(phraseGerman));
    phrase.bindValue(QStringLiteral(":native_translation"), phraseNative);
    phrase.bindValue(QStringLiteral(":normalized_native_translation"), DLDatabaseManager::normalizedText(phraseNative));
    phrase.bindValue(QStringLiteral(":group_id"), word.groupId >= 0 ? QVariant(word.groupId) : DLDatabaseManager::nullVariant());
    phrase.bindValue(QStringLiteral(":group_sync_id"), groupSyncId.isEmpty() ? DLDatabaseManager::nullVariant() : QVariant(groupSyncId));
    phrase.bindValue(QStringLiteral(":created_at"), now);
    phrase.bindValue(QStringLiteral(":updated_at"), now);

    if (!bindAndExec(phrase, error)) {
        return false;
    }

    QSqlQuery stats(db);
    stats.prepare(QStringLiteral(R"(
        INSERT INTO word_review_stats
            (word_id, word_sync_id, correct_answers, wrong_answers, last_reviewed_at,
             ease_factor, interval_days, due_at, updated_at)
        VALUES (:word_id, :word_sync_id, 0, 0, NULL, 2.5, 0, NULL, :updated_at);
    )"));
    stats.bindValue(QStringLiteral(":word_id"), phrase.lastInsertId().toInt());
    stats.bindValue(QStringLiteral(":word_sync_id"), phraseSyncId);
    stats.bindValue(QStringLiteral(":updated_at"), now);
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
