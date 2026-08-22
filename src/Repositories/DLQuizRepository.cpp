#include "DLQuizRepository.h"

#include "DLDatabaseManager.h"
#include "DLWordRepository.h"
#include "../Models/DLModelMappers.h"

DLQuizRepository::DLQuizRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QList<DLWord> DLQuizRepository::fetchRandomWords(int limit, const QString& groupSyncId)
{
    QString sql = QStringLiteral("SELECT %1 FROM %2 WHERE w.deleted_at IS NULL")
                      .arg(DLWordRepository::wordSelectColumns(), DLWordRepository::wordFromClause());
    QVariantMap args = {{ QStringLiteral(":limit"), limit }};

    if (!groupSyncId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND w.group_sync_id = :group_sync_id");
        args.insert(QStringLiteral(":group_sync_id"), groupSyncId.trimmed());
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");
    return fetchWords(sql, args);
}

QList<DLWord> DLQuizRepository::fetchTranslationQuizWords(int limit, const QString& groupSyncId, const QString& partOfSpeech)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE w.deleted_at IS NULL
          AND TRIM(COALESCE(w.native_translation, '')) != ''
    )").arg(DLWordRepository::wordSelectColumns(), DLWordRepository::wordFromClause());

    QVariantMap args = {{ QStringLiteral(":limit"), limit }};

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND w.part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (!groupSyncId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND w.group_sync_id = :group_sync_id");
        args.insert(QStringLiteral(":group_sync_id"), groupSyncId.trimmed());
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");
    return fetchWords(sql, args);
}

QList<DLWord> DLQuizRepository::fetchNouns(const QString& groupSyncId)
{
    QString sql = QStringLiteral(R"(
        SELECT %1
        FROM %2
        WHERE w.deleted_at IS NULL
          AND LOWER(TRIM(COALESCE(w.article, ''))) IN ('der', 'die', 'das')
          AND (
              LOWER(TRIM(COALESCE(w.part_of_speech, ''))) IN ('nomen', 'substantiv', 'noun')
              OR LOWER(TRIM(COALESCE(w.article, ''))) IN ('der', 'die', 'das')
          )
    )").arg(DLWordRepository::wordSelectColumns(), DLWordRepository::wordFromClause());

    QVariantMap args;
    if (!groupSyncId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND w.group_sync_id = :group_sync_id");
        args.insert(QStringLiteral(":group_sync_id"), groupSyncId.trimmed());
    }

    sql += QStringLiteral(" ORDER BY w.created_at DESC;");
    return fetchWords(sql, args);
}

int DLQuizRepository::getNounCount(const QString& groupSyncId)
{
    QString sql = QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words
        WHERE deleted_at IS NULL
          AND LOWER(TRIM(COALESCE(article, ''))) IN ('der', 'die', 'das')
          AND (
              LOWER(TRIM(COALESCE(part_of_speech, ''))) IN ('nomen', 'substantiv', 'noun')
              OR LOWER(TRIM(COALESCE(article, ''))) IN ('der', 'die', 'das')
          )
    )");
    QVariantMap args;

    if (!groupSyncId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND group_sync_id = :group_sync_id");
        args.insert(QStringLiteral(":group_sync_id"), groupSyncId.trimmed());
    }

    return m_database.selectInt(sql + QStringLiteral(";"), args);
}

int DLQuizRepository::getTranslationQuizWordCount(const QString& groupSyncId, const QString& partOfSpeech)
{
    QString sql = QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words
        WHERE deleted_at IS NULL
          AND TRIM(COALESCE(native_translation, '')) != ''
    )");
    QVariantMap args;

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (!groupSyncId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND group_sync_id = :group_sync_id");
        args.insert(QStringLiteral(":group_sync_id"), groupSyncId.trimmed());
    }

    return m_database.selectInt(sql + QStringLiteral(";"), args);
}

QList<DLWord> DLQuizRepository::fetchWords(const QString& sql, const QVariantMap& args)
{
    const QVariantList rows = m_database.selectRows(sql, args);
    QList<DLWord> words;
    for (const QVariant& row : rows) {
        words.append(DLModelMappers::wordFromMap(row.toMap()));
    }
    return words;
}
