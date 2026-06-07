#include "DLDatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QUuid>

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

static QString createSyncId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

static QString sqliteUuidExpression()
{
    return QStringLiteral(
        "lower(hex(randomblob(4))) || '-' || "
        "lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))), 2) || '-' || "
        "substr('89ab', abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))), 2) || '-' || "
        "lower(hex(randomblob(6)))"
        );
}

static QString nullableTextValueSql(const QString& tableAlias,
                                    const QString& columnName,
                                    bool columnExists)
{
    return columnExists
        ? tableAlias + QStringLiteral(".") + columnName
        : QStringLiteral("NULL");
}

static QString wordSelectColumns()
{
    return QStringLiteral(
        "id, sync_id, sync_id AS syncId, german_word, article, part_of_speech, native_translation, "
        "example_phrase_de, example_phrase_native, group_id, plural_form, plural_form AS pluralForm, "
        "praeteritum_form, praeteritum_form AS praeteritumForm, "
        "partizip_ii_form, partizip_ii_form AS partizipIIForm, "
        "positive_form, positive_form AS positiveForm, comparative_form, comparative_form AS comparativeForm, "
        "superlative_form, superlative_form AS superlativeForm, "
        "created_at, last_reviewed_at, correct_answers, wrong_answers"
        );
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
                    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
                    sync_id               TEXT    NOT NULL UNIQUE,
                    german_word           TEXT    NOT NULL,
                    article               TEXT,
                    part_of_speech        TEXT    DEFAULT 'Andere',
                    native_translation    TEXT    NOT NULL,
                    example_phrase_de     TEXT,
                    example_phrase_native TEXT,
                    group_id              INTEGER,
                    plural_form           TEXT,
                    praeteritum_form      TEXT,
                    partizip_ii_form      TEXT,
                    positive_form         TEXT,
                    comparative_form      TEXT,
                    superlative_form      TEXT,
                    created_at            REAL    NOT NULL,
                    last_reviewed_at      REAL,
                    correct_answers       INTEGER DEFAULT 0,
                    wrong_answers         INTEGER DEFAULT 0,
                    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE SET NULL
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

    if (!addColumnIfMissingNoLock(QStringLiteral("words"),
                                  QStringLiteral("sync_id"),
                                  QStringLiteral("sync_id TEXT"))) {
        return false;
    }

    for (const QString& columnName : wordMetadataColumns()) {
        if (!addColumnIfMissingNoLock(QStringLiteral("words"),
                                      columnName,
                                      columnName + QStringLiteral(" TEXT"))) {
            return false;
        }
    }

    return backfillMissingSyncIdsNoLock();
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    const QList<DLSqlCommand> commands = {
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_german_word ON words(german_word);"),
            {}
        },
        {
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_group_id ON words(group_id);"),
            {}
        },
        {
            QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_words_sync_id ON words(sync_id);"),
            {}
        },
        {
            QStringLiteral(
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_word_unique "
                "ON words(german_word, native_translation);"
                ),
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
        { QStringLiteral(":german_word"), germanWord },
        { QStringLiteral(":native_translation"), nativeTranslation }
    };

    if (excludingId >= 0) {
        sql = QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE LOWER(german_word) = LOWER(:german_word) "
            "AND LOWER(native_translation) = LOWER(:native_translation) "
            "AND id != :excluding_id;"
            );

        args.insert(QStringLiteral(":excluding_id"), excludingId);
    } else {
        sql = QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE LOWER(german_word) = LOWER(:german_word) "
            "AND LOWER(native_translation) = LOWER(:native_translation);"
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

bool DLDatabaseManager::backfillMissingSyncIdsNoLock(const QString& qualifiedTableName)
{
    QSqlQuery selectQuery(m_db);
    if (!selectQuery.exec(QStringLiteral("SELECT id FROM %1 WHERE sync_id IS NULL OR sync_id = '';").arg(qualifiedTableName))) {
        m_lastError = selectQuery.lastError().text();
        return false;
    }

    QVariantList ids;
    while (selectQuery.next()) {
        ids.append(selectQuery.value(0));
    }

    for (const QVariant& id : ids) {
        QSqlQuery updateQuery(m_db);
        updateQuery.prepare(QStringLiteral("UPDATE %1 SET sync_id = :sync_id WHERE id = :id;").arg(qualifiedTableName));
        updateQuery.bindValue(QStringLiteral(":sync_id"), createSyncId());
        updateQuery.bindValue(QStringLiteral(":id"), id);

        if (!updateQuery.exec()) {
            m_lastError = updateQuery.lastError().text();
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
            (sync_id, german_word, article, part_of_speech, native_translation,
             example_phrase_de, example_phrase_native, group_id,
             created_at, correct_answers, wrong_answers)
        VALUES
            (:sync_id, :german_word, NULL, 'Phrase', :native_translation,
             NULL, NULL, :group_id,
             :created_at, 0, 0);
    )"));

    q.bindValue(QStringLiteral(":sync_id"), createSyncId());
    q.bindValue(QStringLiteral(":german_word"), phraseGerman);
    q.bindValue(QStringLiteral(":native_translation"), phraseNative);
    q.bindValue(QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant());
    q.bindValue(QStringLiteral(":created_at"), static_cast<double>(nowUnix()));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }

    return true;
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
            (sync_id, german_word, article, part_of_speech, native_translation,
             example_phrase_de, example_phrase_native, group_id,
             plural_form, praeteritum_form, partizip_ii_form,
             positive_form, comparative_form, superlative_form,
             created_at, correct_answers, wrong_answers)
        VALUES
            (:sync_id, :german_word, :article, :part_of_speech, :native_translation,
             :example_phrase_de, :example_phrase_native, :group_id,
             :plural_form, :praeteritum_form, :partizip_ii_form,
             :positive_form, :comparative_form, :superlative_form,
             :created_at, 0, 0);
    )"));

    q.bindValue(QStringLiteral(":sync_id"), syncId.isEmpty() ? createSyncId() : syncId);
    q.bindValue(QStringLiteral(":german_word"), germanWord);
    q.bindValue(QStringLiteral(":article"), article.isEmpty() ? nullVariant() : QVariant(article));
    q.bindValue(QStringLiteral(":part_of_speech"), partOfSpeech.isEmpty() ? QStringLiteral("Andere") : partOfSpeech);
    q.bindValue(QStringLiteral(":native_translation"), nativeTranslation);
    q.bindValue(QStringLiteral(":example_phrase_de"), examplePhraseDe.isEmpty() ? nullVariant() : QVariant(examplePhraseDe));
    q.bindValue(QStringLiteral(":example_phrase_native"), examplePhraseNative.isEmpty() ? nullVariant() : QVariant(examplePhraseNative));
    q.bindValue(QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant());
    q.bindValue(QStringLiteral(":plural_form"), pluralForm.isEmpty() ? nullVariant() : QVariant(pluralForm));
    q.bindValue(QStringLiteral(":praeteritum_form"), praeteritumForm.isEmpty() ? nullVariant() : QVariant(praeteritumForm));
    q.bindValue(QStringLiteral(":partizip_ii_form"), partizipIIForm.isEmpty() ? nullVariant() : QVariant(partizipIIForm));
    q.bindValue(QStringLiteral(":positive_form"), positiveForm.isEmpty() ? nullVariant() : QVariant(positiveForm));
    q.bindValue(QStringLiteral(":comparative_form"), comparativeForm.isEmpty() ? nullVariant() : QVariant(comparativeForm));
    q.bindValue(QStringLiteral(":superlative_form"), superlativeForm.isEmpty() ? nullVariant() : QVariant(superlativeForm));
    q.bindValue(QStringLiteral(":created_at"), static_cast<double>(nowUnix()));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        m_db.rollback();
        return -1;
    }

    const int newId = static_cast<int>(q.lastInsertId().toLongLong());

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
                german_word           = :german_word,
                article               = :article,
                part_of_speech        = :part_of_speech,
                native_translation    = :native_translation,
                example_phrase_de     = :example_phrase_de,
                example_phrase_native = :example_phrase_native,
                group_id              = :group_id,
                sync_id               = COALESCE(NULLIF(:sync_id, ''), sync_id),
                plural_form           = :plural_form,
                praeteritum_form      = :praeteritum_form,
                partizip_ii_form      = :partizip_ii_form,
                positive_form         = :positive_form,
                comparative_form      = :comparative_form,
                superlative_form      = :superlative_form
            WHERE id = :id;
        )"),
        {
            { QStringLiteral(":id"), id },
            { QStringLiteral(":german_word"), germanWord },
            { QStringLiteral(":article"), article.isEmpty() ? nullVariant() : QVariant(article) },
            { QStringLiteral(":part_of_speech"), partOfSpeech.isEmpty() ? QStringLiteral("Andere") : QVariant(partOfSpeech) },
            { QStringLiteral(":native_translation"), nativeTranslation },
            { QStringLiteral(":example_phrase_de"), examplePhraseDe.isEmpty() ? nullVariant() : QVariant(examplePhraseDe) },
            { QStringLiteral(":example_phrase_native"), examplePhraseNative.isEmpty() ? nullVariant() : QVariant(examplePhraseNative) },
            { QStringLiteral(":group_id"), groupId >= 0 ? QVariant(groupId) : nullVariant() },
            { QStringLiteral(":sync_id"), syncId },
            { QStringLiteral(":plural_form"), pluralForm.isEmpty() ? nullVariant() : QVariant(pluralForm) },
            { QStringLiteral(":praeteritum_form"), praeteritumForm.isEmpty() ? nullVariant() : QVariant(praeteritumForm) },
            { QStringLiteral(":partizip_ii_form"), partizipIIForm.isEmpty() ? nullVariant() : QVariant(partizipIIForm) },
            { QStringLiteral(":positive_form"), positiveForm.isEmpty() ? nullVariant() : QVariant(positiveForm) },
            { QStringLiteral(":comparative_form"), comparativeForm.isEmpty() ? nullVariant() : QVariant(comparativeForm) },
            { QStringLiteral(":superlative_form"), superlativeForm.isEmpty() ? nullVariant() : QVariant(superlativeForm) }
        }
        );

    if (!updateSuccess) {
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
        QStringLiteral("SELECT %1 FROM words WHERE id = :id;").arg(wordSelectColumns()),
        {
            { QStringLiteral(":id"), id }
        }
        );
}

QString DLDatabaseManager::sortClause(const QString& sortMode) const
{
    if (sortMode == QStringLiteral("oldest")) {
        return QStringLiteral("created_at ASC");
    }

    if (sortMode == QStringLiteral("az")) {
        return QStringLiteral("german_word ASC");
    }

    if (sortMode == QStringLiteral("za")) {
        return QStringLiteral("german_word DESC");
    }

    return QStringLiteral("created_at DESC");
}

QVariantList DLDatabaseManager::fetchAllWords(const QString& sortMode,
                                              int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM words
    )").arg(wordSelectColumns());

    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" WHERE group_id = :group_id");
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
        FROM words
        WHERE (german_word LIKE :pattern
               OR native_translation LIKE :pattern
               OR plural_form LIKE :pattern
               OR praeteritum_form LIKE :pattern
               OR partizip_ii_form LIKE :pattern
               OR positive_form LIKE :pattern
               OR comparative_form LIKE :pattern
               OR superlative_form LIKE :pattern)
    )").arg(wordSelectColumns());

    QVariantMap args = {
        { QStringLiteral(":pattern"), QStringLiteral("%") + query + QStringLiteral("%") }
    };

    if (groupId >= 0) {
        sql += QStringLiteral(" AND group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY german_word;");

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
        FROM words
    )").arg(wordSelectColumns());

    QVariantMap args = {
        { QStringLiteral(":limit"), limit }
    };

    if (groupId >= 0) {
        sql += QStringLiteral(" WHERE group_id = :group_id");
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
        FROM words
        WHERE TRIM(COALESCE(native_translation, '')) != ''
    )").arg(wordSelectColumns());

    QVariantMap args = {
        { QStringLiteral(":limit"), limit }
    };

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (groupId >= 0) {
        sql += QStringLiteral(" AND group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");

    return selectRows(sql, args);
}

QVariantList DLDatabaseManager::fetchNouns(int groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM words
        WHERE article IN ('der', 'die', 'das')
    )").arg(wordSelectColumns());

    QVariantMap args;

    if (groupId >= 0) {
        sql += QStringLiteral(" AND group_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY created_at DESC;");

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
            UPDATE words
            SET correct_answers = correct_answers + 1,
                last_reviewed_at = :last_reviewed_at
            WHERE id = :id;
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
            UPDATE words
            SET wrong_answers = wrong_answers + 1,
                last_reviewed_at = :last_reviewed_at
            WHERE id = :id;
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

    const bool sourceHasSyncId = columnExistsNoLock(QStringLiteral("words"), QStringLiteral("sync_id"), QStringLiteral("importdb"));
    QVariantMap sourceHasMetadata;
    for (const QString& columnName : wordMetadataColumns()) {
        sourceHasMetadata.insert(columnName, columnExistsNoLock(QStringLiteral("words"), columnName, QStringLiteral("importdb")));
    }

    const QString syncIdExpression = sourceHasSyncId
        ? QStringLiteral("COALESCE(NULLIF(iw.sync_id, ''), ") + sqliteUuidExpression() + QStringLiteral(")")
        : sqliteUuidExpression();
    const QString pluralFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("plural_form"), sourceHasMetadata.value(QStringLiteral("plural_form")).toBool());
    const QString praeteritumFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("praeteritum_form"), sourceHasMetadata.value(QStringLiteral("praeteritum_form")).toBool());
    const QString partizipIIFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("partizip_ii_form"), sourceHasMetadata.value(QStringLiteral("partizip_ii_form")).toBool());
    const QString positiveFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("positive_form"), sourceHasMetadata.value(QStringLiteral("positive_form")).toBool());
    const QString comparativeFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("comparative_form"), sourceHasMetadata.value(QStringLiteral("comparative_form")).toBool());
    const QString superlativeFormExpression = nullableTextValueSql(QStringLiteral("iw"), QStringLiteral("superlative_form"), sourceHasMetadata.value(QStringLiteral("superlative_form")).toBool());

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
                    (sync_id, german_word, article, part_of_speech, native_translation,
                     example_phrase_de, example_phrase_native, group_id,
                     plural_form, praeteritum_form, partizip_ii_form,
                     positive_form, comparative_form, superlative_form,
                     created_at, last_reviewed_at, correct_answers, wrong_answers)
                SELECT %1,
                       iw.german_word,
                       iw.article,
                       iw.part_of_speech,
                       iw.native_translation,
                       iw.example_phrase_de,
                       iw.example_phrase_native,
                       (
                           SELECT g.id
                           FROM groups g
                           JOIN importdb.groups ig ON ig.name = g.name
                           WHERE ig.id = iw.group_id
                           LIMIT 1
                       ),
                       %2,
                       %3,
                       %4,
                       %5,
                       %6,
                       %7,
                       iw.created_at,
                       iw.last_reviewed_at,
                       iw.correct_answers,
                       iw.wrong_answers
                FROM importdb.words iw;
            )").arg(syncIdExpression,
                   pluralFormExpression,
                   praeteritumFormExpression,
                   partizipIIFormExpression,
                   positiveFormExpression,
                   comparativeFormExpression,
                   superlativeFormExpression),
            {}
        }
    };

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
            QStringLiteral("DELETE FROM words;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM groups;"),
            {}
        },
        {
            QStringLiteral("DELETE FROM sqlite_sequence WHERE name IN ('words', 'groups');"),
            {}
        }
    };

    return executeSqlBatch(commands);
}
