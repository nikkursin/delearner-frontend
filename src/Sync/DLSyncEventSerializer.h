#ifndef DLSYNCEVENTSERIALIZER_H
#define DLSYNCEVENTSERIALIZER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "Models/DLWord.h"
#include "Models/DLWordGroup.h"
#include "Models/DLWordReviewStats.h"

struct DLSyncEventEnvelope
{
    QString contractVersion = QStringLiteral("1.0");
    QString eventId;
    QString deviceId;
    QString entityType;
    QString entityId;
    QString operation;
    QVariant updatedAt;
    QString authenticatedUserId;
    QVariantMap payload;
    QVariantMap tombstone;
};

class DLSyncEventSerializer
{
public:
    static QString contractVersion();

    static DLSyncEventEnvelope eventFromContractMap(const QVariantMap& map,
                                                    const QString& contractVersion = DLSyncEventSerializer::contractVersion());
    static QVariantMap contractMapFromEvent(const DLSyncEventEnvelope& event, QString* error = nullptr);

    static QByteArray serializeContractJson(const DLSyncEventEnvelope& event, QString* error = nullptr);
    static DLSyncEventEnvelope parseContractJson(const QByteArray& json,
                                                 const QString& contractVersion = DLSyncEventSerializer::contractVersion(),
                                                 QString* error = nullptr);

    static QByteArray serializeBodyJson(const DLSyncEventEnvelope& event, QString* error = nullptr);

    static QVariantMap payloadForGroup(const DLWordGroup& group, const QString& ownerUserId);
    static QVariantMap payloadForWord(const DLWord& word, const QString& ownerUserId);
    static QVariantMap payloadForReviewStats(const DLWordReviewStats& stats,
                                             const QString& ownerUserId,
                                             const QVariant& createdAt = QVariant());
    static QVariantMap payloadForAppSetting(const QVariantMap& setting, const QString& ownerUserId);

    static QVariantMap canonicalPayloadForEntity(const QString& entityType, const QVariantMap& source);
    static QVariantMap tombstoneForEntity(const QString& entityType,
                                          const QString& entityId,
                                          const QString& ownerUserId,
                                          const QVariant& deletedAt,
                                          const QVariantMap& metadata = QVariantMap());

    static bool validateEvent(const DLSyncEventEnvelope& event, QString* error = nullptr);

private:
    static QStringList payloadFieldsForEntity(const QString& entityType);
    static QStringList requiredTombstoneFieldsForEntity(const QString& entityType);
    static QVariantMap canonicalTombstoneForEntity(const QString& entityType, const QVariantMap& source);
};

#endif // DLSYNCEVENTSERIALIZER_H
