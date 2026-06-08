#include "DLDatabaseMaintenanceService.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include "DLDatabaseManager.h"
#include "DLGroupRepository.h"
#include "DLLogging.h"
#include "DLWordRepository.h"

DLDatabaseMaintenanceService::DLDatabaseMaintenanceService(DLDatabaseManager& database)
    : m_database(database)
{
}

QVariantMap DLDatabaseMaintenanceService::getDatabaseStats()
{
    DLWordRepository words(m_database);
    DLGroupRepository groups(m_database);

    QVariantMap stats;
    stats.insert(QStringLiteral("word_count"), words.getWordCount());
    stats.insert(QStringLiteral("group_count"), groups.getGroupCount());

    const QFileInfo info(m_database.databasePath());
    stats.insert(QStringLiteral("db_size_bytes"), info.exists() ? info.size() : -1);
    return stats;
}

bool DLDatabaseMaintenanceService::importDatabaseMerge(const QString& sourceDatabasePath)
{
    const QFileInfo sourceInfo(sourceDatabasePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        m_database.setLastError(QStringLiteral("Import file not found."));
        qCWarning(dlDb) << "Database import failed: source file not found";
        return false;
    }

    if (QDir::cleanPath(sourceInfo.absoluteFilePath()) == QDir::cleanPath(m_database.databasePath())) {
        m_database.setLastError(QStringLiteral("Choose a different database file to merge."));
        qCWarning(dlDb) << "Database import rejected: source matches current database";
        return false;
    }

    qCInfo(dlDb) << "Starting database merge import";
    if (!m_database.executeSql(QStringLiteral("ATTACH DATABASE :path AS importdb;"),
                               {{ QStringLiteral(":path"), sourceInfo.absoluteFilePath() }})) {
        qCWarning(dlDb) << "Database import failed while attaching source:" << m_database.lastError();
        return false;
    }

    const bool ok = m_database.transaction([&](QSqlDatabase& db, QString* error) {
        const qint64 now = DLDatabaseManager::currentUnixTime();
        const QStringList commands = {
            QStringLiteral(R"(
                INSERT OR IGNORE INTO groups (name, color_hex, created_at, updated_at)
                SELECT name, COALESCE(color_hex, '#3366CC'), created_at, COALESCE(updated_at, created_at)
                FROM importdb.groups;
            )"),
            QStringLiteral(R"(
                INSERT OR IGNORE INTO words
                    (german_word, normalized_german_word, article, part_of_speech,
                     native_translation, normalized_native_translation,
                     example_phrase_de, example_phrase_native, group_id,
                     notes, created_at, updated_at, deleted_at)
                SELECT iw.german_word,
                       COALESCE(NULLIF(iw.normalized_german_word, ''), LOWER(TRIM(iw.german_word))),
                       iw.article,
                       COALESCE(NULLIF(iw.part_of_speech, ''), 'Andere'),
                       iw.native_translation,
                       COALESCE(NULLIF(iw.normalized_native_translation, ''), LOWER(TRIM(iw.native_translation))),
                       iw.example_phrase_de,
                       iw.example_phrase_native,
                       (
                           SELECT g.id
                           FROM groups g
                           JOIN importdb.groups ig ON ig.name = g.name
                           WHERE ig.id = iw.group_id
                           LIMIT 1
                       ),
                       iw.notes,
                       iw.created_at,
                       COALESCE(iw.updated_at, iw.created_at),
                       iw.deleted_at
                FROM importdb.words iw;
            )"),
            QStringLiteral(R"(
                INSERT OR IGNORE INTO word_review_stats
                    (word_id, correct_answers, wrong_answers, last_reviewed_at,
                     ease_factor, interval_days, due_at, updated_at)
                SELECT tw.id,
                       COALESCE(irs.correct_answers, 0),
                       COALESCE(irs.wrong_answers, 0),
                       irs.last_reviewed_at,
                       COALESCE(irs.ease_factor, 2.5),
                       COALESCE(irs.interval_days, 0),
                       irs.due_at,
                       COALESCE(irs.updated_at, :now)
                FROM importdb.words iw
                JOIN words tw
                  ON tw.normalized_german_word = COALESCE(NULLIF(iw.normalized_german_word, ''), LOWER(TRIM(iw.german_word)))
                 AND tw.normalized_native_translation = COALESCE(NULLIF(iw.normalized_native_translation, ''), LOWER(TRIM(iw.native_translation)))
                LEFT JOIN importdb.word_review_stats irs ON irs.word_id = iw.id;
            )"),
            QStringLiteral(R"(
                INSERT OR IGNORE INTO noun_forms (word_id, plural_form)
                SELECT tw.id, inf.plural_form
                FROM importdb.words iw
                JOIN words tw
                  ON tw.normalized_german_word = COALESCE(NULLIF(iw.normalized_german_word, ''), LOWER(TRIM(iw.german_word)))
                 AND tw.normalized_native_translation = COALESCE(NULLIF(iw.normalized_native_translation, ''), LOWER(TRIM(iw.native_translation)))
                JOIN importdb.noun_forms inf ON inf.word_id = iw.id
                WHERE TRIM(COALESCE(inf.plural_form, '')) != '';
            )"),
            QStringLiteral(R"(
                INSERT OR IGNORE INTO verb_forms (word_id, praeteritum_form, partizip_ii_form)
                SELECT tw.id, ivf.praeteritum_form, ivf.partizip_ii_form
                FROM importdb.words iw
                JOIN words tw
                  ON tw.normalized_german_word = COALESCE(NULLIF(iw.normalized_german_word, ''), LOWER(TRIM(iw.german_word)))
                 AND tw.normalized_native_translation = COALESCE(NULLIF(iw.normalized_native_translation, ''), LOWER(TRIM(iw.native_translation)))
                JOIN importdb.verb_forms ivf ON ivf.word_id = iw.id
                WHERE TRIM(COALESCE(ivf.praeteritum_form, '')) != ''
                   OR TRIM(COALESCE(ivf.partizip_ii_form, '')) != '';
            )"),
            QStringLiteral(R"(
                INSERT OR IGNORE INTO adjective_forms
                    (word_id, positive_form, comparative_form, superlative_form)
                SELECT tw.id, iaf.positive_form, iaf.comparative_form, iaf.superlative_form
                FROM importdb.words iw
                JOIN words tw
                  ON tw.normalized_german_word = COALESCE(NULLIF(iw.normalized_german_word, ''), LOWER(TRIM(iw.german_word)))
                 AND tw.normalized_native_translation = COALESCE(NULLIF(iw.normalized_native_translation, ''), LOWER(TRIM(iw.native_translation)))
                JOIN importdb.adjective_forms iaf ON iaf.word_id = iw.id
                WHERE TRIM(COALESCE(iaf.positive_form, '')) != ''
                   OR TRIM(COALESCE(iaf.comparative_form, '')) != ''
                   OR TRIM(COALESCE(iaf.superlative_form, '')) != '';
            )")
        };

        for (const QString& sql : commands) {
            QSqlQuery query(db);
            query.prepare(sql);
            query.bindValue(QStringLiteral(":now"), now);
            if (!query.exec()) {
                if (error) {
                    *error = query.lastError().text();
                }
                return false;
            }
        }
        return true;
    });

    const bool detached = m_database.executeSql(QStringLiteral("DETACH DATABASE importdb;"));
    if (ok && detached) {
        qCInfo(dlDb) << "Database merge import completed";
    } else {
        qCWarning(dlDb) << "Database merge import failed:" << m_database.lastError();
    }
    return ok && detached;
}

bool DLDatabaseMaintenanceService::deleteAllData()
{
    qCInfo(dlDb) << "Deleting all database data";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral("DELETE FROM adjective_forms;"), {} },
        { QStringLiteral("DELETE FROM verb_forms;"), {} },
        { QStringLiteral("DELETE FROM noun_forms;"), {} },
        { QStringLiteral("DELETE FROM word_review_stats;"), {} },
        { QStringLiteral("DELETE FROM words;"), {} },
        { QStringLiteral("DELETE FROM groups;"), {} },
        { QStringLiteral("DELETE FROM sqlite_sequence WHERE name IN ('words', 'groups');"), {} }
    };

    return m_database.executeSqlBatch(commands);
}
