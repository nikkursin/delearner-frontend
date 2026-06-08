#include "DLDatabaseManager.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QMutexLocker>

#include "DLDatabaseMaintenanceService.h"
#include "DLGroupRepository.h"
#include "DLLogging.h"
#include "DLQuizRepository.h"
#include "DLReviewStatsRepository.h"
#include "DLWordRepository.h"
#include "../Models/DLModelMappers.h"

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

bool DLDatabaseManager::openDatabase(const QString& databasePath)
{
    {
        QMutexLocker locker(&m_mutex);

        qCInfo(dlDb) << "Opening database at" << databasePath;

        if (QSqlDatabase::contains(m_connectionName)) {
            m_db = QSqlDatabase::database(m_connectionName);
        } else {
            m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        }

        if (m_db.isOpen() && m_db.databaseName() == databasePath) {
            return true;
        }

        if (m_db.isOpen()) {
            m_db.close();
        }

        m_db.setDatabaseName(databasePath);
        if (!m_db.open()) {
            m_lastError = m_db.lastError().text();
            qCCritical(dlDb) << "Failed to open database:" << m_lastError;
            return false;
        }

        QSqlQuery pragma(m_db);
        if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
            m_lastError = pragma.lastError().text();
            qCCritical(dlDb) << "Failed to enable SQLite foreign keys:" << m_lastError;
            return false;
        }
    }

    return createTablesIfNeeded() && createIndexesIfNeeded();
}

void DLDatabaseManager::closeDatabase()
{
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        qCInfo(dlDb) << "Closing database";
        m_db.close();
    }

    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DLDatabaseManager::createTablesIfNeeded()
{
    qCDebug(dlDb) << "Creating database tables if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS groups (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                color_hex TEXT DEFAULT '#3366CC',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS words (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                german_word TEXT NOT NULL,
                normalized_german_word TEXT NOT NULL,
                article TEXT,
                part_of_speech TEXT NOT NULL DEFAULT 'Andere',
                native_translation TEXT NOT NULL,
                normalized_native_translation TEXT NOT NULL,
                example_phrase_de TEXT,
                example_phrase_native TEXT,
                group_id INTEGER,
                notes TEXT,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                deleted_at INTEGER,
                FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE SET NULL
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS word_review_stats (
                word_id INTEGER PRIMARY KEY,
                correct_answers INTEGER NOT NULL DEFAULT 0,
                wrong_answers INTEGER NOT NULL DEFAULT 0,
                last_reviewed_at INTEGER,
                ease_factor REAL DEFAULT 2.5,
                interval_days INTEGER DEFAULT 0,
                due_at INTEGER,
                updated_at INTEGER NOT NULL,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS noun_forms (
                word_id INTEGER PRIMARY KEY,
                plural_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS verb_forms (
                word_id INTEGER PRIMARY KEY,
                praeteritum_form TEXT,
                partizip_ii_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} },
        { QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS adjective_forms (
                word_id INTEGER PRIMARY KEY,
                positive_form TEXT,
                comparative_form TEXT,
                superlative_form TEXT,
                FOREIGN KEY(word_id) REFERENCES words(id) ON DELETE CASCADE
            );
        )"), {} }
    };

    return executeSqlBatch(commands);
}

bool DLDatabaseManager::createIndexesIfNeeded()
{
    qCDebug(dlDb) << "Creating database indexes if needed";
    const QList<DLSqlCommand> commands = {
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_group_id ON words(group_id);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_normalized_german ON words(normalized_german_word);"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_part_of_speech ON words(part_of_speech);"), {} },
        { QStringLiteral(R"(
            CREATE UNIQUE INDEX IF NOT EXISTS idx_words_unique_active_translation
            ON words(normalized_german_word, normalized_native_translation)
            WHERE deleted_at IS NULL;
        )"), {} },
        { QStringLiteral("CREATE INDEX IF NOT EXISTS idx_word_review_stats_due_at ON word_review_stats(due_at);"), {} }
    };

    return executeSqlBatch(commands);
}

QString DLDatabaseManager::databasePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_db.databaseName();
}

QString DLDatabaseManager::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void DLDatabaseManager::setLastError(const QString& error)
{
    QMutexLocker locker(&m_mutex);
    m_lastError = error;
}

bool DLDatabaseManager::executeSql(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        query.bindValue(it.key(), it.value());
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qCWarning(dlDb) << "Failed to execute SQL:" << m_lastError;
        return false;
    }

    return true;
}

bool DLDatabaseManager::executeSqlBatch(const QList<DLSqlCommand>& commands)
{
    return transaction([&](QSqlDatabase& db, QString* error) {
        for (const DLSqlCommand& command : commands) {
            QSqlQuery query(db);
            query.prepare(command.sql);
            for (auto it = command.args.constBegin(); it != command.args.constEnd(); ++it) {
                query.bindValue(it.key(), it.value());
            }

            if (!query.exec()) {
                if (error) {
                    *error = query.lastError().text();
                }
                qCWarning(dlDb) << "Failed to execute SQL batch command:" << query.lastError().text();
                return false;
            }
        }
        return true;
    });
}

int DLDatabaseManager::executeInsert(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        query.bindValue(it.key(), it.value());
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qCWarning(dlDb) << "Failed to execute insert:" << m_lastError;
        return -1;
    }

    return static_cast<int>(query.lastInsertId().toLongLong());
}

QVariantList DLDatabaseManager::selectRows(const QString& sql, const QVariantMap& args)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        query.bindValue(it.key(), it.value());
    }

    QVariantList rows;
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qCWarning(dlDb) << "Failed to select rows:" << m_lastError;
        return rows;
    }

    const QSqlRecord record = query.record();
    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < record.count(); ++i) {
            row.insert(record.fieldName(i), query.value(i));
        }
        rows.append(row);
    }

    return rows;
}

QVariantMap DLDatabaseManager::selectOneRow(const QString& sql, const QVariantMap& args)
{
    const QVariantList rows = selectRows(sql, args);
    return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}

int DLDatabaseManager::selectInt(const QString& sql, const QVariantMap& args, int fallback)
{
    QMutexLocker locker(&m_mutex);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        query.bindValue(it.key(), it.value());
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qCWarning(dlDb) << "Failed to select integer:" << m_lastError;
        return fallback;
    }

    return query.next() ? query.value(0).toInt() : fallback;
}

bool DLDatabaseManager::transaction(const std::function<bool(QSqlDatabase&, QString*)>& callback)
{
    QMutexLocker locker(&m_mutex);

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        qCWarning(dlDb) << "Failed to start database transaction:" << m_lastError;
        return false;
    }

    QString callbackError;
    if (!callback(m_db, &callbackError)) {
        if (!callbackError.isEmpty()) {
            m_lastError = callbackError;
        }
        qCWarning(dlDb) << "Rolling back database transaction:" << m_lastError;
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        qCWarning(dlDb) << "Failed to commit database transaction:" << m_lastError;
        m_db.rollback();
        return false;
    }

    return true;
}

qint64 DLDatabaseManager::currentUnixTime()
{
    return QDateTime::currentSecsSinceEpoch();
}

QString DLDatabaseManager::normalizedText(const QString& value)
{
    return value.trimmed().toLower();
}

QVariant DLDatabaseManager::nullVariant()
{
    return QVariant();
}

int DLDatabaseManager::insertGroup(const QString& name, const QString& colorHex)
{
    DLWordGroup group;
    group.name = name;
    group.colorHex = colorHex;
    return DLGroupRepository(*this).insertGroup(group);
}

bool DLDatabaseManager::updateGroup(int id, const QString& name, const QString& colorHex)
{
    DLWordGroup group;
    group.id = id;
    group.name = name;
    group.colorHex = colorHex;
    return DLGroupRepository(*this).updateGroup(group);
}

bool DLDatabaseManager::deleteGroup(int id)
{
    return DLGroupRepository(*this).deleteGroup(id);
}

QVariantList DLDatabaseManager::fetchAllGroups()
{
    return DLModelMappers::groupsToList(DLGroupRepository(*this).fetchAllGroups());
}

QVariantMap DLDatabaseManager::fetchGroupById(int id)
{
    const DLWordGroup group = DLGroupRepository(*this).fetchGroupById(id);
    return group.id < 0 ? QVariantMap() : DLModelMappers::groupToMap(group);
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

    DLWord word;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupId = groupId;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).insertWord(word);
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

    DLWord word;
    word.id = id;
    word.germanWord = germanWord;
    word.article = article;
    word.partOfSpeech = partOfSpeech;
    word.nativeTranslation = nativeTranslation;
    word.examplePhraseDe = examplePhraseDe;
    word.examplePhraseNative = examplePhraseNative;
    word.groupId = groupId;
    word.nounForms.pluralForm = pluralForm;
    word.verbForms.praeteritumForm = praeteritumForm;
    word.verbForms.partizipIIForm = partizipIIForm;
    word.adjectiveForms.positiveForm = positiveForm;
    word.adjectiveForms.comparativeForm = comparativeForm;
    word.adjectiveForms.superlativeForm = superlativeForm;
    return DLWordRepository(*this).updateWord(word);
}

bool DLDatabaseManager::deleteWord(int id)
{
    return DLWordRepository(*this).deleteWord(id);
}

QVariantMap DLDatabaseManager::fetchWordById(int id)
{
    const DLWord word = DLWordRepository(*this).fetchWordById(id);
    return word.id < 0 ? QVariantMap() : DLModelMappers::wordToMap(word);
}

bool DLDatabaseManager::wordExists(const QString& germanWord, const QString& nativeTranslation, int excludingId)
{
    return DLWordRepository(*this).wordExists(germanWord, nativeTranslation, excludingId);
}

QVariantList DLDatabaseManager::fetchAllWords(const QString& sortMode, int groupId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).fetchAllWords(sortMode, groupId));
}

QVariantList DLDatabaseManager::searchWords(const QString& query, int groupId)
{
    return DLModelMappers::wordsToList(DLWordRepository(*this).searchWords(query, groupId));
}

QVariantList DLDatabaseManager::fetchWordsByGroup(int groupId)
{
    return fetchAllWords(QStringLiteral("newest"), groupId);
}

QVariantList DLDatabaseManager::fetchRandomWords(int limit, int groupId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchRandomWords(limit, groupId));
}

QVariantList DLDatabaseManager::fetchTranslationQuizWords(int limit, int groupId, const QString& partOfSpeech)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchTranslationQuizWords(limit, groupId, partOfSpeech));
}

QVariantList DLDatabaseManager::fetchNouns(int groupId)
{
    return DLModelMappers::wordsToList(DLQuizRepository(*this).fetchNouns(groupId));
}

int DLDatabaseManager::getWordCount(int groupId)
{
    return DLWordRepository(*this).getWordCount(groupId);
}

int DLDatabaseManager::getTranslationQuizWordCount(int groupId, const QString& partOfSpeech)
{
    return DLQuizRepository(*this).getTranslationQuizWordCount(groupId, partOfSpeech);
}

int DLDatabaseManager::getGroupCount()
{
    return DLGroupRepository(*this).getGroupCount();
}

int DLDatabaseManager::getNounCount(int groupId)
{
    return DLQuizRepository(*this).getNounCount(groupId);
}

bool DLDatabaseManager::incrementCorrectAnswer(int wordId)
{
    return DLReviewStatsRepository(*this).incrementCorrectAnswer(wordId);
}

bool DLDatabaseManager::incrementWrongAnswer(int wordId)
{
    return DLReviewStatsRepository(*this).incrementWrongAnswer(wordId);
}

QVariantMap DLDatabaseManager::getDatabaseStats()
{
    return DLDatabaseMaintenanceService(*this).getDatabaseStats();
}

bool DLDatabaseManager::importDatabaseMerge(const QString& sourceDatabasePath)
{
    return DLDatabaseMaintenanceService(*this).importDatabaseMerge(sourceDatabasePath);
}

bool DLDatabaseManager::deleteAllData()
{
    return DLDatabaseMaintenanceService(*this).deleteAllData();
}
