#include "DLDatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <QVariant>
#include <QMutexLocker>

DLDatabaseManager* DLDatabaseManager::m_instance = nullptr;
std::once_flag DLDatabaseManager::m_initFlag;
QMutex DLDatabaseManager::m_mutex;

static qint64 nowUnix()
{
    return QDateTime::currentSecsSinceEpoch();
}

DLDatabaseManager::DLDatabaseManager()
    : m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

DLDatabaseManager::~DLDatabaseManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }

    m_db = QSqlDatabase();

    if (!m_connectionName.isEmpty()
        && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }}

void DLDatabaseManager::init() {
    QMutexLocker locker(&m_mutex);

    std::call_once(m_initFlag, [&]() {
        m_instance = new DLDatabaseManager();
    });
}

DLDatabaseManager& DLDatabaseManager::instance() {
    QMutexLocker locker(&m_mutex);

    if(!m_instance) {
        throw std::runtime_error("m_instance of DLDatabaseManager doesn`t exist. Exiting");
    }

    return *m_instance;
}

bool DLDatabaseManager::openDatabase(const QString &databasePath)
{
    QMutexLocker locker(&m_mutex);

    if (QSqlDatabase::contains(m_connectionName)) {
        m_db = QSqlDatabase::database(m_connectionName);
        if (m_db.isOpen())
            return true;
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    }

    m_db.setDatabaseName(databasePath);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    // Enable foreign-key enforcement (must be per-connection in SQLite).
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    return createTablesIfNeeded() && createIndexesIfNeeded();
}

void DLDatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        m_db.close();
    }

    m_db = QSqlDatabase();

    if (!m_connectionName.isEmpty()
        && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DLDatabaseManager::createTablesIfNeeded()
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);

    const QString createGroups = QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS groups (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            name       TEXT    NOT NULL,
            color_hex  TEXT    DEFAULT '#3366CC',
            created_at REAL    NOT NULL
        );
    )");

    const QString createWords = QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS words (
            id                    INTEGER PRIMARY KEY AUTOINCREMENT,
            german_word           TEXT    NOT NULL,
            article               TEXT,
            part_of_speech        TEXT    DEFAULT 'Andere',
            native_translation    TEXT    NOT NULL,
            example_phrase_de     TEXT,
            example_phrase_native TEXT,
            group_id              INTEGER,
            created_at            REAL    NOT NULL,
            last_reviewed_at      REAL,
            correct_answers       INTEGER DEFAULT 0,
            wrong_answers         INTEGER DEFAULT 0,
            FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE SET NULL
        );
    )");

    if (!q.exec(createGroups)) {
        m_lastError = q.lastError().text();
        return false;
    }

    if (!q.exec(createWords)) {
        m_lastError = q.lastError().text();
        return false;
    }

    return true;
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);

    const QStringList statements = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_german_word ON words(german_word);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_group_id    ON words(group_id);"),
        // Unique index prevents exact duplicate (german_word, native_translation) pairs.
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_word_unique "
                       "ON words(german_word, native_translation);")
    };

    for (const QString &sql : statements) {
        if (!q.exec(sql)) {
            m_lastError = q.lastError().text();
            return false;
        }
    }

    return true;
}

QString DLDatabaseManager::lastError() const
{
    return m_lastError;
}

int DLDatabaseManager::insertGroup(const QString &name, const QString &colorHex)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO groups (name, color_hex, created_at) VALUES (:name, :color, :ts);"));
    q.bindValue(QStringLiteral(":name"),  name);
    q.bindValue(QStringLiteral(":color"), colorHex);
    q.bindValue(QStringLiteral(":ts"),    static_cast<double>(nowUnix()));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return static_cast<int>(q.lastInsertId().toLongLong());
}

bool DLDatabaseManager::updateGroup(int id, const QString &name, const QString &colorHex)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE groups SET name = :name, color_hex = :color WHERE id = :id;"));
    q.bindValue(QStringLiteral(":name"),  name);
    q.bindValue(QStringLiteral(":color"), colorHex);
    q.bindValue(QStringLiteral(":id"),    id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DLDatabaseManager::deleteGroup(int id)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM groups WHERE id = :id;"));
    q.bindValue(QStringLiteral(":id"), id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVariantList DLDatabaseManager::fetchAllGroups()
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);

    // word_count subquery mirrors the Swift implementation.
    const QString sql = QStringLiteral(R"(
        SELECT g.id, g.name, g.color_hex, g.created_at,
               (SELECT COUNT(*) FROM words WHERE group_id = g.id) AS word_count
        FROM groups g
        ORDER BY g.name;
    )");

    QVariantList result;
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        return result;
    }

    while (q.next()) {
        QVariantMap row;
        row[QStringLiteral("id")]         = q.value(0);
        row[QStringLiteral("name")]       = q.value(1);
        row[QStringLiteral("color_hex")]  = q.value(2);
        row[QStringLiteral("created_at")] = q.value(3);
        row[QStringLiteral("word_count")] = q.value(4);
        result.append(row);
    }
    return result;
}

QVariantMap DLDatabaseManager::fetchGroupById(int id)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT g.id, g.name, g.color_hex, g.created_at,
               (SELECT COUNT(*) FROM words WHERE group_id = g.id) AS word_count
        FROM groups g
        WHERE g.id = :id;
    )"));
    q.bindValue(QStringLiteral(":id"), id);

    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return {};
    }

    QVariantMap row;
    row[QStringLiteral("id")]         = q.value(0);
    row[QStringLiteral("name")]       = q.value(1);
    row[QStringLiteral("color_hex")]  = q.value(2);
    row[QStringLiteral("created_at")] = q.value(3);
    row[QStringLiteral("word_count")] = q.value(4);
    return row;
}

bool DLDatabaseManager::wordExists(const QString &germanWord,
                                   const QString &nativeTranslation,
                                   int            excludingId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);

    if (excludingId >= 0) {
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE LOWER(german_word) = LOWER(:gw) "
            "  AND LOWER(native_translation) = LOWER(:nt) "
            "  AND id != :excl;"));
        q.bindValue(QStringLiteral(":excl"), excludingId);
    } else {
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM words "
            "WHERE LOWER(german_word) = LOWER(:gw) "
            "  AND LOWER(native_translation) = LOWER(:nt);"));
    }

    q.bindValue(QStringLiteral(":gw"), germanWord);
    q.bindValue(QStringLiteral(":nt"), nativeTranslation);

    if (!q.exec() || !q.next())
        return false;

    return q.value(0).toInt() > 0;
}

int DLDatabaseManager::insertWord(const QString &germanWord,
                                  const QString &article,
                                  const QString &partOfSpeech,
                                  const QString &nativeTranslation,
                                  const QString &examplePhraseDe,
                                  const QString &examplePhraseNative,
                                  int            groupId)
{
    QMutexLocker locker(&m_mutex);

    if (wordExists(germanWord, nativeTranslation)) {
        m_lastError = QStringLiteral("Duplicate: word + translation pair already exists.");
        return -1;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO words
            (german_word, article, part_of_speech, native_translation,
             example_phrase_de, example_phrase_native, group_id,
             created_at, correct_answers, wrong_answers)
        VALUES
            (:gw, :art, :pos, :nt, :epde, :epnat, :gid, :ts, 0, 0);
    )"));

    q.bindValue(QStringLiteral(":gw"),    germanWord);
    q.bindValue(QStringLiteral(":art"),   article.isEmpty()   ? QVariant(QMetaType(QMetaType::QString)) : article);
    q.bindValue(QStringLiteral(":pos"),   partOfSpeech.isEmpty() ? QStringLiteral("Andere") : partOfSpeech);
    q.bindValue(QStringLiteral(":nt"),    nativeTranslation);
    q.bindValue(QStringLiteral(":epde"),  examplePhraseDe.isEmpty()    ? QVariant(QMetaType(QMetaType::QString)) : examplePhraseDe);
    q.bindValue(QStringLiteral(":epnat"), examplePhraseNative.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : examplePhraseNative);
    q.bindValue(QStringLiteral(":gid"),   groupId >= 0 ? QVariant(groupId) : QVariant(QMetaType(QMetaType::Int)));
    q.bindValue(QStringLiteral(":ts"),    static_cast<double>(nowUnix()));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return static_cast<int>(q.lastInsertId().toLongLong());
}

bool DLDatabaseManager::updateWord(int            id,
                                   const QString &germanWord,
                                   const QString &article,
                                   const QString &partOfSpeech,
                                   const QString &nativeTranslation,
                                   const QString &examplePhraseDe,
                                   const QString &examplePhraseNative,
                                   int            groupId)
{
    QMutexLocker locker(&m_mutex);

    if (wordExists(germanWord, nativeTranslation, id)) {
        m_lastError = QStringLiteral("Duplicate: word + translation pair already exists.");
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        UPDATE words SET
            german_word           = :gw,
            article               = :art,
            part_of_speech        = :pos,
            native_translation    = :nt,
            example_phrase_de     = :epde,
            example_phrase_native = :epnat,
            group_id              = :gid
        WHERE id = :id;
    )"));

    q.bindValue(QStringLiteral(":gw"),    germanWord);
    q.bindValue(QStringLiteral(":art"),   article.isEmpty()   ? QVariant(QMetaType(QMetaType::QString)) : article);
    q.bindValue(QStringLiteral(":pos"),   partOfSpeech.isEmpty() ? QStringLiteral("Andere") : partOfSpeech);
    q.bindValue(QStringLiteral(":nt"),    nativeTranslation);
    q.bindValue(QStringLiteral(":epde"),  examplePhraseDe.isEmpty()    ? QVariant(QMetaType(QMetaType::QString)) : examplePhraseDe);
    q.bindValue(QStringLiteral(":epnat"), examplePhraseNative.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : examplePhraseNative);
    q.bindValue(QStringLiteral(":gid"),   groupId >= 0 ? QVariant(groupId) : QVariant(QMetaType(QMetaType::Int)));
    q.bindValue(QStringLiteral(":id"),    id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DLDatabaseManager::deleteWord(int id)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM words WHERE id = :id;"));
    q.bindValue(QStringLiteral(":id"), id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVariantMap DLDatabaseManager::fetchWordById(int id)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, german_word, article, part_of_speech, native_translation, "
        "       example_phrase_de, example_phrase_native, group_id, "
        "       created_at, last_reviewed_at, correct_answers, wrong_answers "
        "FROM words WHERE id = :id;"));
    q.bindValue(QStringLiteral(":id"), id);

    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return {};
    }

    QVariantMap row;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i)
        row[rec.fieldName(i)] = q.value(i);
    return row;
}

QString DLDatabaseManager::sortClause(const QString &sortMode) const
{
    QMutexLocker locker(&m_mutex);

    if (sortMode == QStringLiteral("oldest")) return QStringLiteral("created_at ASC");
    if (sortMode == QStringLiteral("az"))     return QStringLiteral("german_word ASC");
    if (sortMode == QStringLiteral("za"))     return QStringLiteral("german_word DESC");
    return QStringLiteral("created_at DESC"); // default: newest
}

QVariantList DLDatabaseManager::fetchAllWords(const QString &sortMode, int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, german_word, article, part_of_speech, native_translation, "
        "       example_phrase_de, example_phrase_native, group_id, "
        "       created_at, last_reviewed_at, correct_answers, wrong_answers "
        "FROM words");

    if (groupId >= 0) {
        sql += QStringLiteral(" WHERE group_id = :gid");
    }
    sql += QStringLiteral(" ORDER BY ") + sortClause(sortMode) + QStringLiteral(";");

    q.prepare(sql);
    if (groupId >= 0)
        q.bindValue(QStringLiteral(":gid"), groupId);

    QVariantList result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }

    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

QVariantList DLDatabaseManager::searchWords(const QString &query, int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, german_word, article, part_of_speech, native_translation, "
        "       example_phrase_de, example_phrase_native, group_id, "
        "       created_at, last_reviewed_at, correct_answers, wrong_answers "
        "FROM words "
        "WHERE (german_word LIKE :pat OR native_translation LIKE :pat)");

    if (groupId >= 0)
        sql += QStringLiteral(" AND group_id = :gid");

    sql += QStringLiteral(" ORDER BY german_word;");

    q.prepare(sql);
    q.bindValue(QStringLiteral(":pat"), QStringLiteral("%") + query + QStringLiteral("%"));
    if (groupId >= 0)
        q.bindValue(QStringLiteral(":gid"), groupId);

    QVariantList result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }

    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

QVariantList DLDatabaseManager::fetchWordsByGroup(int groupId)
{
    QMutexLocker locker(&m_mutex);

    return fetchAllWords(QStringLiteral("newest"), groupId);
}

QVariantList DLDatabaseManager::fetchRandomWords(int limit, int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, german_word, article, part_of_speech, native_translation, "
        "       example_phrase_de, example_phrase_native, group_id, "
        "       created_at, last_reviewed_at, correct_answers, wrong_answers "
        "FROM words");

    if (groupId >= 0)
        sql += QStringLiteral(" WHERE group_id = :gid");

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :lim;");

    q.prepare(sql);
    if (groupId >= 0)
        q.bindValue(QStringLiteral(":gid"), groupId);
    q.bindValue(QStringLiteral(":lim"), limit);

    QVariantList result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }

    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

QVariantList DLDatabaseManager::fetchNouns(int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id, german_word, article, part_of_speech, native_translation, "
        "       example_phrase_de, example_phrase_native, group_id, "
        "       created_at, last_reviewed_at, correct_answers, wrong_answers "
        "FROM words "
        "WHERE (article IN ('der','die','das') OR part_of_speech = 'Substantiv')");

    if (groupId >= 0)
        sql += QStringLiteral(" AND group_id = :gid");

    sql += QStringLiteral(" ORDER BY created_at DESC;");

    q.prepare(sql);
    if (groupId >= 0)
        q.bindValue(QStringLiteral(":gid"), groupId);

    QVariantList result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }

    const QSqlRecord rec = q.record();
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

int DLDatabaseManager::getWordCount(int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    if (groupId >= 0) {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM words WHERE group_id = :gid;"));
        q.bindValue(QStringLiteral(":gid"), groupId);
    } else {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM words;"));
    }

    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

int DLDatabaseManager::getGroupCount()
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM groups;"));
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

int DLDatabaseManager::getNounCount(int groupId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM words "
        "WHERE (article IN ('der','die','das') OR part_of_speech = 'Substantiv')");

    if (groupId >= 0)
        sql += QStringLiteral(" AND group_id = :gid");
    sql += QStringLiteral(";");

    q.prepare(sql);
    if (groupId >= 0)
        q.bindValue(QStringLiteral(":gid"), groupId);

    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

bool DLDatabaseManager::incrementCorrectAnswer(int wordId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE words "
        "SET correct_answers = correct_answers + 1, last_reviewed_at = :ts "
        "WHERE id = :id;"));
    q.bindValue(QStringLiteral(":ts"), static_cast<double>(nowUnix()));
    q.bindValue(QStringLiteral(":id"), wordId);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DLDatabaseManager::incrementWrongAnswer(int wordId)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE words "
        "SET wrong_answers = wrong_answers + 1, last_reviewed_at = :ts "
        "WHERE id = :id;"));
    q.bindValue(QStringLiteral(":ts"), static_cast<double>(nowUnix()));
    q.bindValue(QStringLiteral(":id"), wordId);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVariantMap DLDatabaseManager::getDatabaseStats()
{
    QMutexLocker locker(&m_mutex);

    QVariantMap stats;
    stats[QStringLiteral("word_count")]  = getWordCount();
    stats[QStringLiteral("group_count")] = getGroupCount();

    // File size in bytes; -1 if unavailable.
    const QString path = m_db.databaseName();
    const QFileInfo fi(path);
    stats[QStringLiteral("db_size_bytes")] = fi.exists() ? fi.size() : -1;

    return stats;
}
