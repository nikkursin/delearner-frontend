#include "DLQuizRepository.h"

#include "DLDatabaseManager.h"
#include "DLWordRepository.h"
#include "../Models/DLModelMappers.h"

DLQuizRepository::DLQuizRepository(DLDatabaseManager& database)
    : m_database(database)
{
}

QList<DLWord> DLQuizRepository::fetchRandomWords(int limit, const QString& groupId)
{
    QString sql = QStringLiteral("SELECT %1 FROM %2 WHERE w.deleted_at IS NULL")
                      .arg(DLWordRepository::wordSelectColumns(), DLWordRepository::wordFromClause());
    QVariantMap args = {{ QStringLiteral(":limit"), limit }};

    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");
    return fetchWords(sql, args);
}

QList<DLWord> DLQuizRepository::fetchTranslationQuizWords(int limit, const QString& groupId, const QString& partOfSpeech)
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

    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY RANDOM() LIMIT :limit;");
    return fetchWords(sql, args);
}

QList<DLWord> DLQuizRepository::fetchNouns(const QString& groupId)
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
    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    sql += QStringLiteral(" ORDER BY w.created_at DESC;");
    return fetchWords(sql, args);
}

int DLQuizRepository::getNounCount(const QString& groupId)
{
    QString sql = QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words w
        LEFT JOIN groups g ON g.id = w.group_id
        WHERE w.deleted_at IS NULL
          AND LOWER(TRIM(COALESCE(w.article, ''))) IN ('der', 'die', 'das')
          AND (
              LOWER(TRIM(COALESCE(w.part_of_speech, ''))) IN ('nomen', 'substantiv', 'noun')
              OR LOWER(TRIM(COALESCE(w.article, ''))) IN ('der', 'die', 'das')
          )
    )");
    QVariantMap args;

    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
    }

    return m_database.selectInt(sql + QStringLiteral(";"), args);
}

int DLQuizRepository::getTranslationQuizWordCount(const QString& groupId, const QString& partOfSpeech)
{
    QString sql = QStringLiteral(R"(
        SELECT COUNT(*)
        FROM words w
        LEFT JOIN groups g ON g.id = w.group_id
        WHERE w.deleted_at IS NULL
          AND TRIM(COALESCE(w.native_translation, '')) != ''
    )");
    QVariantMap args;

    const QString trimmedPartOfSpeech = partOfSpeech.trimmed();
    if (!trimmedPartOfSpeech.isEmpty() && trimmedPartOfSpeech != QStringLiteral("All")) {
        sql += QStringLiteral(" AND w.part_of_speech = :part_of_speech");
        args.insert(QStringLiteral(":part_of_speech"), trimmedPartOfSpeech);
    }

    if (!groupId.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.sync_id = :group_id");
        args.insert(QStringLiteral(":group_id"), groupId);
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
