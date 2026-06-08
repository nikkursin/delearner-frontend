#include "DLDatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

static const QStringList wordMetadataColumns()
{
    return {
        QStringLiteral("plural_form"),
        QStringLiteral("praeteritum_form"),
        QStringLiteral("partizip_ii_form"),
        QStringLiteral("positive_form"),
        QStringLiteral("comparative_form"),
        QStringLiteral("superlative_form")
    };
}

static QString normalizedText(const QString& value)
{
    return value.trimmed().toLower();
}

static QString nullableColumnSql(const QString& tableAlias,
                                 const QString& columnName,
                                 bool columnExists,
                                 const QString& fallback = QStringLiteral("NULL"))
{
    if (tableAlias.isEmpty()) {
        return columnExists ? columnName : fallback;
    }

    return columnExists
        ? tableAlias + QStringLiteral(".") + columnName
        : fallback;
}

static QString wordSelectColumns()
{
    return QStringLiteral(
        "w.id, NULL AS sync_id, NULL AS syncId, "
        "w.german_word, w.normalized_german_word, w.article, w.part_of_speech, "
        "w.native_translation, w.normalized_native_translation, "
        "w.example_phrase_de, w.example_phrase_native, w.group_id, w.notes, "
        "nf.plural_form, nf.plural_form AS pluralForm, "
        "vf_praeteritum.form_value AS praeteritum_form, vf_praeteritum.form_value AS praeteritumForm, "
        "vf_partizip.form_value AS partizip_ii_form, vf_partizip.form_value AS partizipIIForm, "
        "af.positive_form, af.positive_form AS positiveForm, "
        "af.comparative_form, af.comparative_form AS comparativeForm, "
        "af.superlative_form, af.superlative_form AS superlativeForm, "
        "w.created_at, w.updated_at, w.deleted_at, "
        "rs.last_reviewed_at, COALESCE(rs.correct_answers, 0) AS correct_answers, "
        "COALESCE(rs.wrong_answers, 0) AS wrong_answers, rs.ease_factor, rs.interval_days, rs.due_at"
        );
}

static QString wordFromClause()
{
    return QStringLiteral(R"(
        words w
        LEFT JOIN word_review_stats rs ON rs.word_id = w.id
        LEFT JOIN noun_forms nf ON nf.word_id = w.id
        LEFT JOIN adjective_forms af ON af.word_id = w.id
        LEFT JOIN verb_forms vf_praeteritum
               ON vf_praeteritum.word_id = w.id
              AND vf_praeteritum.form_key = 'praeteritum_form'
              AND vf_praeteritum.deleted_at IS NULL
        LEFT JOIN verb_forms vf_partizip
               ON vf_partizip.word_id = w.id
              AND vf_partizip.form_key = 'partizip_ii_form'
              AND vf_partizip.deleted_at IS NULL
    )");
}

static qint64 nowUnix()
{
    return QDateTime::currentSecsSinceEpoch();
}

DLDatabaseManager::DLDatabaseManager()
    : m_connectionName(QStringLiteral("delearner_main_connection"))
{
}

DLDatabaseManager::~DLDatabaseManager()
{
    closeDatabase();
}

DLDatabaseManager& DLDatabaseManager::instance()
{
    static DLDatabaseManager instance;
    return instance;
}

QString DLDatabaseManager::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

QVariant DLDatabaseManager::nullVariant() const
{
    return QVariant();
}

bool DLDatabaseManager::openDatabase(const QString& databasePath)
{
    {
        QMutexLocker locker(&m_mutex);

        if (QSqlDatabase::contains(m_connectionName)) {
            m_db = QSqlDatabase::database(m_connectionName);

            if (m_db.isOpen()) {
                return true;
            }
        } else {
            m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        }

        m_db.setDatabaseName(databasePath);

        if (!m_db.open()) {
            m_lastError = m_db.lastError().text();
            return false;
        }

        if (!executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
            return false;
        }
    }

    if (!createTablesIfNeeded()) {
        return false;
    }

    if (!migrateDatabaseIfNeeded()) {
        return false;
    }

    if (!createIndexesIfNeeded()) {
        return false;
    }

    return true;
}

void DLDatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        m_db.close();
    }

    m_db = QSqlDatabase();

    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DLDatabaseManager::executeSql(const QString& sql,
                                   const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);
    return executeSqlNoLock(sql, args);
}

bool DLDatabaseManager::executeSqlNoLock(const QString& sql,
                                         const QVariantMap& args)
{
    QSqlQuery q(m_db);
    q.prepare(sql);

    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        q.bindValue(it.key(), it.value());
    }

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }

    return true;
}

bool DLDatabaseManager::executeSqlBatch(const QList<DLSqlCommand>& commands)
{
    QMutexLocker locker(&m_mutex);
    return executeSqlBatchNoLock(commands);
}

bool DLDatabaseManager::executeSqlBatchNoLock(const QList<DLSqlCommand>& commands)
{
    for (const DLSqlCommand& command : commands) {
        if (!executeSqlNoLock(command.sql, command.args)) {
            return false;
        }
    }

    return true;
}

QVariantList DLDatabaseManager::selectRows(const QString& sql,
                                           const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);
    return selectRowsNoLock(sql, args);
}

QVariantList DLDatabaseManager::selectRowsNoLock(const QString& sql,
                                                 const QVariantMap& args)
{
    QSqlQuery q(m_db);
    q.prepare(sql);

    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        q.bindValue(it.key(), it.value());
    }

    QVariantList result;

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }

    const QSqlRecord record = q.record();

    while (q.next()) {
        QVariantMap row;

        for (int i = 0; i < record.count(); ++i) {
            row[record.fieldName(i)] = q.value(i);
        }

        result.append(row);
    }

    return result;
}

QVariantMap DLDatabaseManager::selectOneRow(const QString& sql,
                                            const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);
    return selectOneRowNoLock(sql, args);
}

QVariantMap DLDatabaseManager::selectOneRowNoLock(const QString& sql,
                                                  const QVariantMap& args)
{
    const QVariantList rows = selectRowsNoLock(sql, args);

    if (rows.isEmpty()) {
        return {};
    }

    return rows.first().toMap();
}

int DLDatabaseManager::selectInt(const QString& sql,
                                 const QVariantMap& args,
                                 int fallback)
{
    QMutexLocker locker(&m_mutex);
    return selectIntNoLock(sql, args, fallback);
}

int DLDatabaseManager::selectIntNoLock(const QString& sql,
                                       const QVariantMap& args,
                                       int fallback)
{
    QSqlQuery q(m_db);
    q.prepare(sql);

    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        q.bindValue(it.key(), it.value());
    }

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return fallback;
    }

    if (!q.next()) {
        return fallback;
    }

    return q.value(0).toInt();
}

bool DLDatabaseManager::createTablesIfNeeded()
{
    const QList<DLSqlCommand> commands = {
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS groups (
                    id         INTEGER PRIMARY KEY AUTOINCREMENT,
                    name       TEXT    NOT NULL,
                    color_hex  TEXT    DEFAULT '#3366CC',
                    created_at REAL    NOT NULL
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS words (
                    id                            INTEGER PRIMARY KEY AUTOINCREMENT,
                    german_word                   TEXT    NOT NULL,
                    normalized_german_word        TEXT    NOT NULL,
                    article                       TEXT,
                    part_of_speech                TEXT    DEFAULT 'Andere',
                    native_translation            TEXT    NOT NULL,
                    normalized_native_translation TEXT    NOT NULL,
                    example_phrase_de             TEXT,
                    example_phrase_native         TEXT,
                    group_id                      INTEGER,
                    notes                         TEXT,
                    created_at                    REAL    NOT NULL,
                    updated_at                    REAL    NOT NULL,
                    deleted_at                    REAL,
                    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE SET NULL
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS word_review_stats (
                    word_id          INTEGER PRIMARY KEY,
                    correct_answers  INTEGER NOT NULL DEFAULT 0,
                    wrong_answers    INTEGER NOT NULL DEFAULT 0,
                    last_reviewed_at REAL,
                    ease_factor      REAL    NOT NULL DEFAULT 2.5,
                    interval_days    INTEGER NOT NULL DEFAULT 0,
                    due_at           REAL,
                    updated_at       REAL    NOT NULL,
                    FOREIGN KEY (word_id) REFERENCES words(id) ON DELETE CASCADE
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS verb_forms (
                    id         INTEGER PRIMARY KEY AUTOINCREMENT,
                    word_id    INTEGER NOT NULL,
                    form_key   TEXT    NOT NULL,
                    form_value TEXT    NOT NULL,
                    source     TEXT,
                    created_at REAL    NOT NULL,
                    updated_at REAL    NOT NULL,
                    deleted_at REAL,
                    FOREIGN KEY (word_id) REFERENCES words(id) ON DELETE CASCADE
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS adjective_forms (
                    word_id          INTEGER PRIMARY KEY,
                    positive_form    TEXT,
                    comparative_form TEXT,
                    superlative_form TEXT,
                    FOREIGN KEY (word_id) REFERENCES words(id) ON DELETE CASCADE
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS noun_forms (
                    word_id     INTEGER PRIMARY KEY,
                    plural_form TEXT,
                    FOREIGN KEY (word_id) REFERENCES words(id) ON DELETE CASCADE
                );
            )"),
            {}
        }
    };

    return executeSqlBatch(commands);
}

bool DLDatabaseManager::migrateDatabaseIfNeeded()
{
    QMutexLocker locker(&m_mutex);
    return migrateLegacyWordsNoLock();
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    const QList<DLSqlCommand> commands = {
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_german_word ON words(german_word);"),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_normalized_german_word ON words(normalized_german_word);"),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_group_id ON words(group_id);"),
            {}
        },
        {
            QStringLiteral(
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_word_unique "
                "ON words(normalized_german_word, normalized_native_translation) "
                "WHERE deleted_at IS NULL;"
                ),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_due_at ON word_review_stats(due_at);"),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_verb_forms_word_id ON verb_forms(word_id);"),
            {}
        },
        {
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_verb_forms_unique_active ON verb_forms(word_id, form_key) WHERE deleted_at IS NULL;"),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_part_of_speech ON words(part_of_speech);"),
            {}
        }
    };

    return executeSqlBatch(commands);
}

int DLDatabaseManager::insertGroup(const QString& name,
                                   const QString& colorHex)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);

    q.prepare(QStringLiteral(
        "INSERT INTO groups (name, color_hex, created_at) "
        "VALUES (:name, :color, :created_at);"
        ));

    q.bindValue(QStringLiteral(":name"), name);
    q.bindValue(QStringLiteral(":color"), colorHex);
    q.bindValue(QStringLiteral(":created_at"), static_cast<double>(nowUnix()));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }

    return static_cast<int>(q.lastInsertId().toLongLong());
}

bool DLDatabaseManager::updateGroup(int id,
                                    const QString& name,
                                    const QString& colorHex)
{
    return executeSql(
        QStringLiteral(
            "UPDATE groups "
            "SET name = :name, color_hex = :color "
            "WHERE id = :id;"
            ),
        {
            { QStringLiteral(":id"), id },
            { QStringLiteral(":name"), name },
            { QStringLiteral(":color"), colorHex }
        }
        );
}

bool DLDatabaseManager::deleteGroup(int id)
{
    return executeSql(
        QStringLiteral("DELETE FROM groups WHERE id = :id;"),
        {
            { QStringLiteral(":id"), id }
        }
        );
}

QVariantList DLDatabaseManager::fetchAllGroups()
{
    return selectRows(QStringLiteral(R"(
        SELECT g.id, g.name, g.color_hex, g.created_at,
               (SELECT COUNT(*) FROM words WHERE group_id = g.id) AS word_count
        FROM groups g
        ORDER BY g.name;
    )"));
}

QVariantMap DLDatabaseManager::fetchGroupById(int id)
{
    return selectOneRow(
        QStringLiteral(R"(
            SELECT g.id, g.name, g.color_hex, g.created_at,
                   (SELECT COUNT(*) FROM words WHERE group_id = g.id) AS word_count
            FROM groups g
            WHERE g.id = :id;
        )"),
        {
            { QStringLiteral(":id"), id }
        }
        );
}

bool DLDatabaseManager::wordExists(const QString& germanWord,
                                   const QString& nativeTranslation,
                                   int excludingId)
{
    QMutexLocker locker(&m_mutex);
    return wordExistsNoLock(germanWord, nativeTranslation, excludingId);
}

bool DLDatabaseManager::wordExistsNoLock(const QString& germanWord,
                                         const QString& nativeTranslation,
                                         int excludingId)
{
    QString sql;

    QVariantMap args = {
        { QStringLiteral(":german_word"), normalizedText(germanWord) },
        { QStringLiteral(":native_translation"), normalizedText(nativeTranslation) }
    };

    if (excludingId >= 0) {
        sql = QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE normalized_german_word = :german_word "
            "AND normalized_native_translation = :native_translation "
            "AND id != :excluding_id;"
            );

        args.insert(QStringLiteral(":excluding_id"), excludingId);
    } else {
        sql = QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE normalized_german_word = :german_word "
            "AND normalized_native_translation = :native_translation;"
            );
    }

    return selectIntNoLock(sql, args) > 0;
}

bool DLDatabaseManager::columnExistsNoLock(const QString& tableName,
                                           const QString& columnName,
                                           const QString& schemaName)
{
    QSqlQuery q(m_db);
    const QString pragma = schemaName.isEmpty()
        ? QStringLiteral("PRAGMA table_info(%1);").arg(tableName)
        : QStringLiteral("PRAGMA %1.table_info(%2);").arg(schemaName, tableName);

    if (!q.exec(pragma)) {
        m_lastError = q.lastError().text();
        return false;
    }

    while (q.next()) {
        if (q.value(1).toString() == columnName) {
            return true;
        }
    }

    return false;
}

bool DLDatabaseManager::tableExistsNoLock(const QString& tableName,
                                          const QString& schemaName)
{
    QSqlQuery q(m_db);
    const QString sql = schemaName.isEmpty()
        ? QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = :name;")
        : QStringLiteral("SELECT COUNT(*) FROM %1.sqlite_master WHERE type = 'table' AND name = :name;").arg(schemaName);

    q.prepare(sql);
    q.bindValue(QStringLiteral(":name"), tableName);

    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return false;
    }

    return q.value(0).toInt() > 0;
}

bool DLDatabaseManager::addColumnIfMissingNoLock(const QString& tableName,
                                                 const QString& columnName,
                                                 const QString& definition)
{
    if (columnExistsNoLock(tableName, columnName)) {
        return true;
    }

    return executeSqlNoLock(
        QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(tableName, definition)
        );
}

bool DLDatabaseManager::migrateLegacyWordsNoLock()
{
    const bool hasCoreSchema = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_german_word"))
        && columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_native_translation"))
        && columnExistsNoLock(QStringLiteral("words"), QStringLiteral("updated_at"))
        && columnExistsNoLock(QStringLiteral("words"), QStringLiteral("deleted_at"))
        && !columnExistsNoLock(QStringLiteral("words"), QStringLiteral("plural_form"))
        && !columnExistsNoLock(QStringLiteral("words"), QStringLiteral("correct_answers"))
        && !columnExistsNoLock(QStringLiteral("words"), QStringLiteral("sync_id"));

    if (hasCoreSchema) {
        return true;
    }

    const bool hasPluralForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("plural_form"));
    const bool hasPraeteritumForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("praeteritum_form"));
    const bool hasPartizipIIForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("partizip_ii_form"));
    const bool hasPositiveForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("positive_form"));
    const bool hasComparativeForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("comparative_form"));
    const bool hasSuperlativeForm = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("superlative_form"));
    const bool hasLastReviewedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("last_reviewed_at"));
    const bool hasCorrectAnswers = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("correct_answers"));
    const bool hasWrongAnswers = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("wrong_answers"));
    const bool hasNotes = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("notes"));
    const bool hasUpdatedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("updated_at"));
    const bool hasDeletedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("deleted_at"));
    const bool hasNormalizedGerman = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_german_word"));
    const bool hasNormalizedNative = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_native_translation"));

    if (!executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = OFF;"))) {
        return false;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = ON;"));
        return false;
    }

    const QList<DLSqlCommand> copyCommands = {
        {
            QStringLiteral(R"(
                INSERT OR REPLACE INTO word_review_stats
                    (word_id, correct_answers, wrong_answers, last_reviewed_at, ease_factor, interval_days, due_at, updated_at)
                SELECT id,
                       COALESCE(%1, 0),
                       COALESCE(%2, 0),
                       %3,
                       2.5,
                       0,
                       NULL,
                       COALESCE(%4, created_at)
                FROM words;
            )").arg(nullableColumnSql(QString(), QStringLiteral("correct_answers"), hasCorrectAnswers),
                   nullableColumnSql(QString(), QStringLiteral("wrong_answers"), hasWrongAnswers),
                   nullableColumnSql(QString(), QStringLiteral("last_reviewed_at"), hasLastReviewedAt),
                   nullableColumnSql(QString(), QStringLiteral("updated_at"), hasUpdatedAt)),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT OR REPLACE INTO noun_forms (word_id, plural_form)
                SELECT id, %2
                FROM words
                WHERE %1 AND TRIM(COALESCE(%2, '')) != '';
            )").arg(hasPluralForm ? QStringLiteral("1") : QStringLiteral("0"),
                   nullableColumnSql(QString(), QStringLiteral("plural_form"), hasPluralForm)),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT OR REPLACE INTO adjective_forms
                    (word_id, positive_form, comparative_form, superlative_form)
                SELECT id, %1, %2, %3
                FROM words
                WHERE TRIM(COALESCE(%1, '')) != ''
                   OR TRIM(COALESCE(%2, '')) != ''
                   OR TRIM(COALESCE(%3, '')) != '';
            )").arg(nullableColumnSql(QString(), QStringLiteral("positive_form"), hasPositiveForm),
                   nullableColumnSql(QString(), QStringLiteral("comparative_form"), hasComparativeForm),
                   nullableColumnSql(QString(), QStringLiteral("superlative_form"), hasSuperlativeForm)),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT OR IGNORE INTO verb_forms
                    (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                SELECT id, 'praeteritum_form', %3, 'user', created_at, COALESCE(%1, created_at), NULL
                FROM words
                WHERE %2 AND TRIM(COALESCE(%3, '')) != '';
            )").arg(nullableColumnSql(QString(), QStringLiteral("updated_at"), hasUpdatedAt),
                   hasPraeteritumForm ? QStringLiteral("1") : QStringLiteral("0"),
                   nullableColumnSql(QString(), QStringLiteral("praeteritum_form"), hasPraeteritumForm)),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT OR IGNORE INTO verb_forms
                    (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                SELECT id, 'partizip_ii_form', %3, 'user', created_at, COALESCE(%1, created_at), NULL
                FROM words
                WHERE %2 AND TRIM(COALESCE(%3, '')) != '';
            )").arg(nullableColumnSql(QString(), QStringLiteral("updated_at"), hasUpdatedAt),
                   hasPartizipIIForm ? QStringLiteral("1") : QStringLiteral("0"),
                   nullableColumnSql(QString(), QStringLiteral("partizip_ii_form"), hasPartizipIIForm)),
            {}
        },
        {
            QStringLiteral(R"(
                CREATE TABLE words_new (
                    id                            INTEGER PRIMARY KEY AUTOINCREMENT,
                    german_word                   TEXT    NOT NULL,
                    normalized_german_word        TEXT    NOT NULL,
                    article                       TEXT,
                    part_of_speech                TEXT    DEFAULT 'Andere',
                    native_translation            TEXT    NOT NULL,
                    normalized_native_translation TEXT    NOT NULL,
                    example_phrase_de             TEXT,
                    example_phrase_native         TEXT,
                    group_id                      INTEGER,
                    notes                         TEXT,
                    created_at                    REAL    NOT NULL,
                    updated_at                    REAL    NOT NULL,
                    deleted_at                    REAL,
                    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE SET NULL
                );
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT INTO words_new
                    (id, german_word, normalized_german_word, article, part_of_speech,
                     native_translation, normalized_native_translation, example_phrase_de,
                     example_phrase_native, group_id, notes, created_at, updated_at, deleted_at)
                SELECT id,
                       german_word,
                       COALESCE(NULLIF(%1, ''), LOWER(TRIM(german_word))),
                       article,
                       COALESCE(NULLIF(part_of_speech, ''), 'Andere'),
                       native_translation,
                       COALESCE(NULLIF(%2, ''), LOWER(TRIM(native_translation))),
                       example_phrase_de,
                       example_phrase_native,
                       group_id,
                       %3,
                       created_at,
                       COALESCE(%4, created_at),
                       %5
                FROM words;
            )").arg(nullableColumnSql(QString(), QStringLiteral("normalized_german_word"), hasNormalizedGerman),
                   nullableColumnSql(QString(), QStringLiteral("normalized_native_translation"), hasNormalizedNative),
                   nullableColumnSql(QString(), QStringLiteral("notes"), hasNotes),
                   nullableColumnSql(QString(), QStringLiteral("updated_at"), hasUpdatedAt),
                   nullableColumnSql(QString(), QStringLiteral("deleted_at"), hasDeletedAt)),
            {}
        },
        {
            QStringLiteral("DROP TABLE words;"),
            {}
        },
        {
            QStringLiteral("ALTER TABLE words_new RENAME TO words;"),
            {}
        }
    };

    if (!executeSqlBatchNoLock(copyCommands)) {
        m_db.rollback();
        executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = ON;"));
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = ON;"));
        return false;
    }

    return executeSqlNoLock(QStringLiteral("PRAGMA foreign_keys = ON;"));
}

bool DLDatabaseManager::upsertWordReviewStatsNoLock(int wordId,
                                                    int correctAnswers,
                                                    int wrongAnswers,
                                                    const QVariant& lastReviewedAt)
{
    return executeSqlNoLock(
        QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at, ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:word_id, :correct_answers, :wrong_answers, :last_reviewed_at, 2.5, 0, NULL, :updated_at)
            ON CONFLICT(word_id) DO UPDATE SET
                correct_answers  = excluded.correct_answers,
                wrong_answers    = excluded.wrong_answers,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at       = excluded.updated_at;
        )"),
        {
            { QStringLiteral(":word_id"), wordId },
            { QStringLiteral(":correct_answers"), correctAnswers },
            { QStringLiteral(":wrong_answers"), wrongAnswers },
            { QStringLiteral(":last_reviewed_at"), lastReviewedAt },
            { QStringLiteral(":updated_at"), static_cast<double>(nowUnix()) }
        }
        );
}

bool DLDatabaseManager::saveWordFormsNoLock(int wordId,
                                            const QString& pluralForm,
                                            const QString& praeteritumForm,
                                            const QString& partizipIIForm,
                                            const QString& positiveForm,
                                            const QString& comparativeForm,
                                            const QString& superlativeForm)
{
    const double now = static_cast<double>(nowUnix());

    if (pluralForm.trimmed().isEmpty()) {
        if (!executeSqlNoLock(QStringLiteral("DELETE FROM noun_forms WHERE word_id = :word_id;"),
                              {{ QStringLiteral(":word_id"), wordId }})) {
            return false;
        }
    } else if (!executeSqlNoLock(
                   QStringLiteral(R"(
                       INSERT INTO noun_forms (word_id, plural_form)
                       VALUES (:word_id, :plural_form)
                       ON CONFLICT(word_id) DO UPDATE SET plural_form = excluded.plural_form;
                   )"),
                   {
                       { QStringLiteral(":word_id"), wordId },
                       { QStringLiteral(":plural_form"), pluralForm.trimmed() }
                   })) {
        return false;
    }

    const bool hasAdjectiveForms = !positiveForm.trimmed().isEmpty()
        || !comparativeForm.trimmed().isEmpty()
        || !superlativeForm.trimmed().isEmpty();

    if (!hasAdjectiveForms) {
        if (!executeSqlNoLock(QStringLiteral("DELETE FROM adjective_forms WHERE word_id = :word_id;"),
                              {{ QStringLiteral(":word_id"), wordId }})) {
            return false;
        }
    } else if (!executeSqlNoLock(
                   QStringLiteral(R"(
                       INSERT INTO adjective_forms
                           (word_id, positive_form, comparative_form, superlative_form)
                       VALUES
                           (:word_id, :positive_form, :comparative_form, :superlative_form)
                       ON CONFLICT(word_id) DO UPDATE SET
                           positive_form    = excluded.positive_form,
                           comparative_form = excluded.comparative_form,
                           superlative_form = excluded.superlative_form;
                   )"),
                   {
                       { QStringLiteral(":word_id"), wordId },
                       { QStringLiteral(":positive_form"), positiveForm.trimmed().isEmpty() ? nullVariant() : QVariant(positiveForm.trimmed()) },
                       { QStringLiteral(":comparative_form"), comparativeForm.trimmed().isEmpty() ? nullVariant() : QVariant(comparativeForm.trimmed()) },
                       { QStringLiteral(":superlative_form"), superlativeForm.trimmed().isEmpty() ? nullVariant() : QVariant(superlativeForm.trimmed()) }
                   })) {
        return false;
    }

    const QVariantMap verbForms = {
        { QStringLiteral("praeteritum_form"), praeteritumForm.trimmed() },
        { QStringLiteral("partizip_ii_form"), partizipIIForm.trimmed() }
    };

    for (auto it = verbForms.constBegin(); it != verbForms.constEnd(); ++it) {
        if (!executeSqlNoLock(
                QStringLiteral("DELETE FROM verb_forms WHERE word_id = :word_id AND form_key = :form_key;"),
                {
                    { QStringLiteral(":word_id"), wordId },
                    { QStringLiteral(":form_key"), it.key() }
                })) {
            return false;
        }

        if (it.value().toString().isEmpty()) {
            continue;
        }

        if (!executeSqlNoLock(
                       QStringLiteral(R"(
                           INSERT INTO verb_forms
                               (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                           VALUES
                               (:word_id, :form_key, :form_value, 'user', :created_at, :updated_at, NULL);
                       )"),
                       {
                           { QStringLiteral(":word_id"), wordId },
                           { QStringLiteral(":form_key"), it.key() },
                           { QStringLiteral(":form_value"), it.value() },
                           { QStringLiteral(":created_at"), now },
                           { QStringLiteral(":updated_at"), now }
                       })) {
            return false;
        }
    }

    return true;
}

bool DLDatabaseManager::createPhraseFromExampleNoLock(const QString& parentPartOfSpeech,
                                                      const QString& examplePhraseDe,
                                                      const QString& examplePhraseNative,
                                                      int groupId)
{
    if (parentPartOfSpeech.compare(QStringLiteral("Phrase"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString phraseGerman = examplePhraseDe.trimmed();
    const QString phraseNative = examplePhraseNative.trimmed();
    if (phraseGerman.isEmpty() || phraseNative.isEmpty()) {
        return true;
    }

    if (wordExistsNoLock(phraseGerman, phraseNative)) {
        return true;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO words
            (german_word, normalized_german_word, article, part_of_speech, native_translation,
             normalized_native_translation,
             example_phrase_de, example_phrase_native, group_id,
             notes, created_at, updated_at, deleted_at)
        VALUES
            (:german_word, :normalized_german_word, NULL, 'Phrase', :native_translation,
             :normalized_native_translation,
             NULL, NULL, :group_id,
             NULL, :created_at, :updated_at, NULL);
    )"));

    q.bindValue(QStringLiteral(":german_word"), phraseGerman);
    q.bindValue(QStringLiteral(":normalized_german_word"), normalizedText(phraseGerman));
    q.bindValue(QStringLiteral(":native_translation"), phraseNative);
    q.bindValue(QStringLiteral(":normalized_native_translation"), normalizedText(phraseNative));
    q.bindValue(QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant());
    const double now = static_cast<double>(nowUnix());
    q.bindValue(QStringLiteral(":created_at"), now);
    q.bindValue(QStringLiteral(":updated_at"), now);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }

    return upsertWordReviewStatsNoLock(static_cast<int>(q.lastInsertId().toLongLong()));
}

int DLDatabaseManager::insertWord(const QString& germanWord,
                                  const QString& article,
                                  const QString& partOfSpeech,
                                  const QString& nativeTranslation,
                                  const QString& examplePhraseDe,
                                  const QString& examplePhraseNative,
                                  int groupId,
                                  const QString& syncId,
                                  const QString& pluralForm,
                                  const QString& praeteritumForm,
                                  const QString& partizipIIForm,
                                  const QString& positiveForm,
                                  const QString& comparativeForm,
                                  const QString& superlativeForm)
{
    Q_UNUSED(syncId);

    QMutexLocker locker(&m_mutex);

    if (wordExistsNoLock(germanWord, nativeTranslation)) {
        m_lastError = QStringLiteral("Duplicate: word + translation pair already exists.");
        return -1;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return -1;
    }

    QSqlQuery q(m_db);

    q.prepare(QStringLiteral(R"(
        INSERT INTO words
            (german_word, normalized_german_word, article, part_of_speech, native_translation,
             normalized_native_translation,
             example_phrase_de, example_phrase_native, group_id,
             notes, created_at, updated_at, deleted_at)
        VALUES
            (:german_word, :normalized_german_word, :article, :part_of_speech, :native_translation,
             :normalized_native_translation,
             :example_phrase_de, :example_phrase_native, :group_id,
             NULL, :created_at, :updated_at, NULL);
    )"));

    q.bindValue(QStringLiteral(":german_word"), germanWord);
    q.bindValue(QStringLiteral(":normalized_german_word"), normalizedText(germanWord));
    q.bindValue(QStringLiteral(":article"), article.isEmpty() ? nullVariant() : QVariant(article));
    q.bindValue(QStringLiteral(":part_of_speech"), partOfSpeech.isEmpty() ? QStringLiteral("Andere") : partOfSpeech);
    q.bindValue(QStringLiteral(":native_translation"), nativeTranslation);
    q.bindValue(QStringLiteral(":normalized_native_translation"), normalizedText(nativeTranslation));
    q.bindValue(QStringLiteral(":example_phrase_de"), examplePhraseDe.isEmpty() ? nullVariant() : QVariant(examplePhraseDe));
    q.bindValue(QStringLiteral(":example_phrase_native"), examplePhraseNative.isEmpty() ? nullVariant() : QVariant(examplePhraseNative));
    q.bindValue(QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant());
    const double now = static_cast<double>(nowUnix());
    q.bindValue(QStringLiteral(":created_at"), now);
    q.bindValue(QStringLiteral(":updated_at"), now);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        m_db.rollback();
        return -1;
    }

    const int newId = static_cast<int>(q.lastInsertId().toLongLong());

    if (!upsertWordReviewStatsNoLock(newId)
        || !saveWordFormsNoLock(newId,
                                pluralForm,
                                praeteritumForm,
                                partizipIIForm,
                                positiveForm,
                                comparativeForm,
                                superlativeForm)) {
        m_db.rollback();
        return -1;
    }

    if (!createPhraseFromExampleNoLock(partOfSpeech, examplePhraseDe, examplePhraseNative, groupId)) {
        m_db.rollback();
        return -1;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return -1;
    }

    return newId;
}

bool DLDatabaseManager::updateWord(int id,
                                   const QString& germanWord,
                                   const QString& article,
                                   const QString& partOfSpeech,
                                   const QString& nativeTranslation,
                                   const QString& examplePhraseDe,
                                   const QString& examplePhraseNative,
                                   int groupId,
                                   const QString& syncId,
                                   const QString& pluralForm,
                                   const QString& praeteritumForm,
                                   const QString& partizipIIForm,
                                   const QString& positiveForm,
                                   const QString& comparativeForm,
                                   const QString& superlativeForm)
{
    Q_UNUSED(syncId);

    QMutexLocker locker(&m_mutex);

    const QVariantMap currentWord = selectOneRowNoLock(
        QStringLiteral("SELECT part_of_speech FROM words WHERE id = :id;"),
        {
            { QStringLiteral(":id"), id }
        }
        );
    if (currentWord.isEmpty()) {
        m_lastError = QStringLiteral("Word not found.");
        return false;
    }

    const QString currentPartOfSpeech = currentWord.value(QStringLiteral("part_of_speech")).toString();
    const QString phraseCreationPartOfSpeech = currentPartOfSpeech.compare(QStringLiteral("Phrase"), Qt::CaseInsensitive) == 0
        ? currentPartOfSpeech
        : partOfSpeech;

    if (wordExistsNoLock(germanWord, nativeTranslation, id)) {
        m_lastError = QStringLiteral("Duplicate: word + translation pair already exists.");
        return false;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    const bool updateSuccess = executeSqlNoLock(
        QStringLiteral(R"(
            UPDATE words SET
                german_word                   = :german_word,
                normalized_german_word        = :normalized_german_word,
                article                       = :article,
                part_of_speech                = :part_of_speech,
                native_translation            = :native_translation,
                normalized_native_translation = :normalized_native_translation,
                example_phrase_de             = :example_phrase_de,
                example_phrase_native         = :example_phrase_native,
                group_id                      = :group_id,
                updated_at                    = :updated_at
            WHERE id = :id;
        )"),
        {
            { QStringLiteral(":id"), id },
            { QStringLiteral(":german_word"), germanWord },
            { QStringLiteral(":normalized_german_word"), normalizedText(germanWord) },
            { QStringLiteral(":article"), article.isEmpty() ? nullVariant() : QVariant(article) },
            { QStringLiteral(":part_of_speech"), partOfSpeech.isEmpty() ? QStringLiteral("Andere") : QVariant(partOfSpeech) },
            { QStringLiteral(":native_translation"), nativeTranslation },
            { QStringLiteral(":normalized_native_translation"), normalizedText(nativeTranslation) },
            { QStringLiteral(":example_phrase_de"), examplePhraseDe.isEmpty() ? nullVariant() : QVariant(examplePhraseDe) },
            { QStringLiteral(":example_phrase_native"), examplePhraseNative.isEmpty() ? nullVariant() : QVariant(examplePhraseNative) },
            { QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant() },
            { QStringLiteral(":updated_at"), static_cast<double>(nowUnix()) }
        }
        );

    if (!updateSuccess) {
        m_db.rollback();
        return false;
    }

    if (!saveWordFormsNoLock(id,
                             pluralForm,
                             praeteritumForm,
                             partizipIIForm,
                             positiveForm,
                             comparativeForm,
                             superlativeForm)) {
        m_db.rollback();
        return false;
    }

    if (!createPhraseFromExampleNoLock(phraseCreationPartOfSpeech, examplePhraseDe, examplePhraseNative, groupId)) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

bool DLDatabaseManager::deleteWord(int id)
{
    return executeSql(
        QStringLiteral("DELETE FROM words WHERE id = :id;"),
        {
            { QStringLiteral(":id"), id }
        }
        );
}

QVariantMap DLDatabaseManager::fetchWordById(int id)
{
    return selectOneRow(
        QStringLiteral("SELECT %1 FROM %2 WHERE w.id = :id;").arg(wordSelectColumns(), wordFromClause()),
        {
            { QStringLiteral(":id"), id }
        }
        );
}

QString DLDatabaseManager::sortClause(const QString& sortMode) const
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

QVariantList DLDatabaseManager::fetchAllWords(const QString& sortMode,
                                              int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" WHERE w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY ") + sortClause(sortMode) + QStringLiteral(";");

    return selectRows(sql, args);
}

QVariantList DLDatabaseManager::searchWords(const QString& query,
                                            int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE (w.german_word LIKE :pattern
               OR w.native_translation LIKE :pattern
               OR nf.plural_form LIKE :pattern
               OR vf_praeteritum.form_value LIKE :pattern
               OR vf_partizip.form_value LIKE :pattern
               OR af.positive_form LIKE :pattern
               OR af.comparative_form LIKE :pattern
               OR af.superlative_form LIKE :pattern)
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args = {
        { QStringLiteral(":pattern"), QStringLiteral("%") + query + QStringLiteral("%") }
    };

    if (groupId >= 0) {
        sql += QStringLiteral(" AND w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY w.german_word;");

    return selectRows(sql, args);
}

QVariantList DLDatabaseManager::fetchWordsByGroup(int groupId)
{
    return fetchAllWords(QStringLiteral("newest"), groupId);
}

QVariantList DLDatabaseManager::fetchRandomWords(int limit,
                                                 int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args = {
        { QStringLiteral(":limit"), limit }
    };

    if (groupId >= 0) {
        sql += QStringLiteral(" WHERE w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");

    return selectRows(sql, args);
}

QVariantList DLDatabaseManager::fetchTranslationQuizWords(int limit,
                                                          int groupId,
                                                          const QString& partOfSpeech)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE TRIM(COALESCE(w.native_translation, '')) != ''
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args = {
        { QStringLiteral(":limit"), limit }
    };

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND w.part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (groupId >= 0) {
        sql += QStringLiteral(" AND w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");

    return selectRows(sql, args);
}

QVariantList DLDatabaseManager::fetchNouns(int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE w.article IN ('der', 'die', 'das')
    )").arg(wordSelectColumns(), wordFromClause());

    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" AND w.group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY w.created_at DESC;");

    return selectRows(sql, args);
}

int DLDatabaseManager::getWordCount(int groupId)
{
    if (groupId >= 0) {
        return selectInt(
            QStringLiteral("SELECT COUNT(*) FROM words WHERE group_id = :group_id;"),
            {
                { QStringLiteral(":group_id"), groupId }
            }
            );
    }

    return selectInt(QStringLiteral("SELECT COUNT(*) FROM words;"));
}

int DLDatabaseManager::getTranslationQuizWordCount(int groupId,
                                                   const QString& partOfSpeech)
{
    QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM words "
        "WHERE TRIM(COALESCE(native_translation, '')) != ''"
        );

    QVariantMap args;

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (groupId >= 0) {
        sql += QStringLiteral(" AND group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(";");

    return selectInt(sql, args);
}

int DLDatabaseManager::getGroupCount()
{
    return selectInt(QStringLiteral("SELECT COUNT(*) FROM groups;"));
}

int DLDatabaseManager::getNounCount(int groupId)
{
    QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM words "
        "WHERE article IN ('der', 'die', 'das')"
        );

    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" AND group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(";");

    return selectInt(sql, args);
}

bool DLDatabaseManager::incrementCorrectAnswer(int wordId)
{
    return executeSql(
        QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at, ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:id, 1, 0, :last_reviewed_at, 2.5, 0, NULL, :last_reviewed_at)
            ON CONFLICT(word_id) DO UPDATE SET
                correct_answers  = word_review_stats.correct_answers + 1,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at       = excluded.updated_at;
        )"),
        {
            { QStringLiteral(":id"), wordId },
            { QStringLiteral(":last_reviewed_at"), static_cast<double>(nowUnix()) }
        }
        );
}

bool DLDatabaseManager::incrementWrongAnswer(int wordId)
{
    return executeSql(
        QStringLiteral(R"(
            INSERT INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at, ease_factor, interval_days, due_at, updated_at)
            VALUES
                (:id, 0, 1, :last_reviewed_at, 2.5, 0, NULL, :last_reviewed_at)
            ON CONFLICT(word_id) DO UPDATE SET
                wrong_answers    = word_review_stats.wrong_answers + 1,
                last_reviewed_at = excluded.last_reviewed_at,
                updated_at       = excluded.updated_at;
        )"),
        {
            { QStringLiteral(":id"), wordId },
            { QStringLiteral(":last_reviewed_at"), static_cast<double>(nowUnix()) }
        }
        );
}

QVariantMap DLDatabaseManager::getDatabaseStats()
{
    QVariantMap stats;

    stats[QStringLiteral("word_count")] = getWordCount();
    stats[QStringLiteral("group_count")] = getGroupCount();

    QMutexLocker locker(&m_mutex);

    const QFileInfo fileInfo(m_db.databaseName());
    stats[QStringLiteral("db_size_bytes")] = fileInfo.exists() ? fileInfo.size() : -1;

    return stats;
}

bool DLDatabaseManager::importDatabaseMerge(const QString& sourceDatabasePath)
{
    QMutexLocker locker(&m_mutex);

    const QFileInfo sourceInfo(sourceDatabasePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        m_lastError = QStringLiteral("Import file not found.");
        return false;
    }

    if (QDir::cleanPath(sourceInfo.absoluteFilePath()) == QDir::cleanPath(m_db.databaseName())) {
        m_lastError = QStringLiteral("Choose a different database file to merge.");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("ATTACH DATABASE :path AS importdb;"));
    query.bindValue(QStringLiteral(":path"), sourceInfo.absoluteFilePath());

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    auto detachImportDatabase = [this]() {
        QSqlQuery detachQuery(m_db);
        if (!detachQuery.exec(QStringLiteral("DETACH DATABASE importdb;"))) {
            m_lastError = detachQuery.lastError().text();
            return false;
        }

        return true;
    };

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        detachImportDatabase();
        return false;
    }

    const bool sourceHasNormalizedGerman = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_german_word"), QStringLiteral("importdb"));
    const bool sourceHasNormalizedNative = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("normalized_native_translation"), QStringLiteral("importdb"));
    const bool sourceHasNotes = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("notes"), QStringLiteral("importdb"));
    const bool sourceHasUpdatedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("updated_at"), QStringLiteral("importdb"));
    const bool sourceHasDeletedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("deleted_at"), QStringLiteral("importdb"));
    const bool sourceHasReviewStats = tableExistsNoLock(QStringLiteral("word_review_stats"), QStringLiteral("importdb"));
    const bool sourceHasNounForms = tableExistsNoLock(QStringLiteral("noun_forms"), QStringLiteral("importdb"));
    const bool sourceHasVerbForms = tableExistsNoLock(QStringLiteral("verb_forms"), QStringLiteral("importdb"));
    const bool sourceHasAdjectiveForms = tableExistsNoLock(QStringLiteral("adjective_forms"), QStringLiteral("importdb"));
    const bool sourceHasLastReviewedAt = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("last_reviewed_at"), QStringLiteral("importdb"));
    const bool sourceHasCorrectAnswers = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("correct_answers"), QStringLiteral("importdb"));
    const bool sourceHasWrongAnswers = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("wrong_answers"), QStringLiteral("importdb"));
    QVariantMap sourceHasMetadata;
    for (const QString& columnName : wordMetadataColumns()) {
        sourceHasMetadata.insert(columnName, columnExistsNoLock(QStringLiteral("words"), columnName, QStringLiteral("importdb")));
    }

    const QString sourceGermanKey = QStringLiteral("COALESCE(NULLIF(%1, ''), LOWER(TRIM(iw.german_word)))")
        .arg(nullableColumnSql(QStringLiteral("iw"), QStringLiteral("normalized_german_word"), sourceHasNormalizedGerman));
    const QString sourceNativeKey = QStringLiteral("COALESCE(NULLIF(%1, ''), LOWER(TRIM(iw.native_translation)))")
        .arg(nullableColumnSql(QStringLiteral("iw"), QStringLiteral("normalized_native_translation"), sourceHasNormalizedNative));
    const QString targetWordJoin = QStringLiteral(
        "JOIN words tw ON tw.normalized_german_word = %1 "
        "AND tw.normalized_native_translation = %2"
        ).arg(sourceGermanKey, sourceNativeKey);

    QList<DLSqlCommand> commands = {
        {
            QStringLiteral(R"(
                INSERT OR IGNORE INTO groups (name, color_hex, created_at)
                SELECT name, color_hex, created_at
                FROM importdb.groups;
            )"),
            {}
        },
        {
            QStringLiteral(R"(
                INSERT OR IGNORE INTO words
                    (german_word, normalized_german_word, article, part_of_speech,
                     native_translation, normalized_native_translation,
                     example_phrase_de, example_phrase_native, group_id,
                     notes, created_at, updated_at, deleted_at)
                SELECT iw.german_word,
                       %1,
                       iw.article,
                       COALESCE(NULLIF(iw.part_of_speech, ''), 'Andere'),
                       iw.native_translation,
                       %2,
                       iw.example_phrase_de,
                       iw.example_phrase_native,
                       (
                           SELECT g.id
                           FROM groups g
                           JOIN importdb.groups ig ON ig.name = g.name
                           WHERE ig.id = iw.group_id
                           LIMIT 1
                       ),
                       %3,
                       iw.created_at,
                       COALESCE(%4, iw.created_at),
                       %5
                FROM importdb.words iw;
            )").arg(sourceGermanKey,
                   sourceNativeKey,
                   nullableColumnSql(QStringLiteral("iw"), QStringLiteral("notes"), sourceHasNotes),
                   nullableColumnSql(QStringLiteral("iw"), QStringLiteral("updated_at"), sourceHasUpdatedAt),
                   nullableColumnSql(QStringLiteral("iw"), QStringLiteral("deleted_at"), sourceHasDeletedAt)),
            {}
        }
    };

    commands.append({
        QStringLiteral(R"(
            INSERT OR IGNORE INTO word_review_stats
                (word_id, correct_answers, wrong_answers, last_reviewed_at, ease_factor, interval_days, due_at, updated_at)
            SELECT tw.id,
                   COALESCE(%1, 0),
                   COALESCE(%2, 0),
                   %3,
                   COALESCE(%4, 2.5),
                   COALESCE(%5, 0),
                   %6,
                   COALESCE(%7, iw.created_at)
            FROM importdb.words iw
            %8
            %9;
        )").arg(sourceHasReviewStats
                   ? QStringLiteral("irs.correct_answers")
                   : nullableColumnSql(QStringLiteral("iw"), QStringLiteral("correct_answers"), sourceHasCorrectAnswers),
               sourceHasReviewStats
                   ? QStringLiteral("irs.wrong_answers")
                   : nullableColumnSql(QStringLiteral("iw"), QStringLiteral("wrong_answers"), sourceHasWrongAnswers),
               sourceHasReviewStats
                   ? QStringLiteral("irs.last_reviewed_at")
                   : nullableColumnSql(QStringLiteral("iw"), QStringLiteral("last_reviewed_at"), sourceHasLastReviewedAt),
               sourceHasReviewStats ? QStringLiteral("irs.ease_factor") : QStringLiteral("NULL"),
               sourceHasReviewStats ? QStringLiteral("irs.interval_days") : QStringLiteral("NULL"),
               sourceHasReviewStats ? QStringLiteral("irs.due_at") : QStringLiteral("NULL"),
               sourceHasReviewStats
                   ? QStringLiteral("irs.updated_at")
                   : nullableColumnSql(QStringLiteral("iw"), QStringLiteral("updated_at"), sourceHasUpdatedAt),
               targetWordJoin,
               sourceHasReviewStats
                   ? QStringLiteral("LEFT JOIN importdb.word_review_stats irs ON irs.word_id = iw.id")
                   : QString()),
        {}
    });

    if (sourceHasNounForms) {
        commands.append({
            QStringLiteral(R"(
                INSERT OR IGNORE INTO noun_forms (word_id, plural_form)
                SELECT tw.id, inf.plural_form
                FROM importdb.words iw
                %1
                JOIN importdb.noun_forms inf ON inf.word_id = iw.id
                WHERE TRIM(COALESCE(inf.plural_form, '')) != '';
            )").arg(targetWordJoin),
            {}
        });
    } else if (sourceHasMetadata.value(QStringLiteral("plural_form")).toBool()) {
        commands.append({
            QStringLiteral(R"(
                INSERT OR IGNORE INTO noun_forms (word_id, plural_form)
                SELECT tw.id, iw.plural_form
                FROM importdb.words iw
                %1
                WHERE TRIM(COALESCE(iw.plural_form, '')) != '';
            )").arg(targetWordJoin),
            {}
        });
    }

    if (sourceHasAdjectiveForms) {
        commands.append({
            QStringLiteral(R"(
                INSERT OR IGNORE INTO adjective_forms
                    (word_id, positive_form, comparative_form, superlative_form)
                SELECT tw.id, iaf.positive_form, iaf.comparative_form, iaf.superlative_form
                FROM importdb.words iw
                %1
                JOIN importdb.adjective_forms iaf ON iaf.word_id = iw.id
                WHERE TRIM(COALESCE(iaf.positive_form, '')) != ''
                   OR TRIM(COALESCE(iaf.comparative_form, '')) != ''
                   OR TRIM(COALESCE(iaf.superlative_form, '')) != '';
            )").arg(targetWordJoin),
            {}
        });
    } else if (sourceHasMetadata.value(QStringLiteral("positive_form")).toBool()
               || sourceHasMetadata.value(QStringLiteral("comparative_form")).toBool()
               || sourceHasMetadata.value(QStringLiteral("superlative_form")).toBool()) {
        commands.append({
            QStringLiteral(R"(
                INSERT OR IGNORE INTO adjective_forms
                    (word_id, positive_form, comparative_form, superlative_form)
                SELECT tw.id, %1, %2, %3
                FROM importdb.words iw
                %4
                WHERE TRIM(COALESCE(%1, '')) != ''
                   OR TRIM(COALESCE(%2, '')) != ''
                   OR TRIM(COALESCE(%3, '')) != '';
            )").arg(nullableColumnSql(QStringLiteral("iw"), QStringLiteral("positive_form"), sourceHasMetadata.value(QStringLiteral("positive_form")).toBool()),
                   nullableColumnSql(QStringLiteral("iw"), QStringLiteral("comparative_form"), sourceHasMetadata.value(QStringLiteral("comparative_form")).toBool()),
                   nullableColumnSql(QStringLiteral("iw"), QStringLiteral("superlative_form"), sourceHasMetadata.value(QStringLiteral("superlative_form")).toBool()),
                   targetWordJoin),
            {}
        });
    }

    if (sourceHasVerbForms) {
        commands.append({
            QStringLiteral(R"(
                INSERT OR IGNORE INTO verb_forms
                    (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                SELECT tw.id, ivf.form_key, ivf.form_value, ivf.source,
                       ivf.created_at, ivf.updated_at, ivf.deleted_at
                FROM importdb.words iw
                %1
                JOIN importdb.verb_forms ivf ON ivf.word_id = iw.id
                WHERE ivf.deleted_at IS NULL
                  AND TRIM(COALESCE(ivf.form_value, '')) != '';
            )").arg(targetWordJoin),
            {}
        });
    } else {
        if (sourceHasMetadata.value(QStringLiteral("praeteritum_form")).toBool()) {
            commands.append({
                QStringLiteral(R"(
                    INSERT OR IGNORE INTO verb_forms
                        (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                    SELECT tw.id, 'praeteritum_form', iw.praeteritum_form, 'user',
                           iw.created_at, COALESCE(%1, iw.created_at), NULL
                    FROM importdb.words iw
                    %2
                    WHERE TRIM(COALESCE(iw.praeteritum_form, '')) != '';
                )").arg(nullableColumnSql(QStringLiteral("iw"), QStringLiteral("updated_at"), sourceHasUpdatedAt),
                       targetWordJoin),
                {}
            });
        }

        if (sourceHasMetadata.value(QStringLiteral("partizip_ii_form")).toBool()) {
            commands.append({
                QStringLiteral(R"(
                    INSERT OR IGNORE INTO verb_forms
                        (word_id, form_key, form_value, source, created_at, updated_at, deleted_at)
                    SELECT tw.id, 'partizip_ii_form', iw.partizip_ii_form, 'user',
                           iw.created_at, COALESCE(%1, iw.created_at), NULL
                    FROM importdb.words iw
                    %2
                    WHERE TRIM(COALESCE(iw.partizip_ii_form, '')) != '';
                )").arg(nullableColumnSql(QStringLiteral("iw"), QStringLiteral("updated_at"), sourceHasUpdatedAt),
                       targetWordJoin),
                {}
            });
        }
    }

    if (!executeSqlBatchNoLock(commands)) {
        m_db.rollback();
        detachImportDatabase();
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        detachImportDatabase();
        return false;
    }

    return detachImportDatabase();
}

bool DLDatabaseManager::deleteAllData()
{
    const QList<DLSqlCommand> commands = {
        {
            QStringLiteral("DELETE FROM verb_forms;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM adjective_forms;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM noun_forms;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM word_review_stats;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM words;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM groups;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM sqlite_sequence WHERE name IN ('words', 'groups', 'verb_forms');"),
            {}
        }
    };

    return executeSqlBatch(commands);
}
