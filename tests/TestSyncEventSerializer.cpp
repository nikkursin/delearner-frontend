#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QUuid>

#include "Sync/DLSyncEventSerializer.h"

class TestSyncEventSerializer : public QObject
{
    Q_OBJECT

private slots:
    void sharedFixturesRoundTripThroughSerializer();
    void payloadFilteringExcludesLocalAndQuizHistoryFields();
    void deleteEventsRejectFullPayloads();

private:
    QString fixtureDir() const;
    QList<QVariantMap> loadFixtureEvents(const QString& fileName, QString* contractVersion) const;
};

QString TestSyncEventSerializer::fixtureDir() const
{
#ifdef DELEARNER_ROOT_DIR
    const QString configured = QDir(QStringLiteral(DELEARNER_ROOT_DIR)).filePath(QStringLiteral("contracts/fixtures/v1"));
    if (QDir(configured).exists()) {
        return configured;
    }
#endif

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("contracts/fixtures/v1"));
        if (QDir(candidate).exists()) {
            return candidate;
        }
        dir.cdUp();
    }

    return {};
}

QList<QVariantMap> TestSyncEventSerializer::loadFixtureEvents(const QString& fileName, QString* contractVersion) const
{
    const QString path = QDir(fixtureDir()).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open fixture:" << path;
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Could not parse fixture:" << path << error.errorString();
        return {};
    }

    const QJsonObject root = document.object();
    if (contractVersion) {
        *contractVersion = root.value(QStringLiteral("contractVersion")).toString();
    }

    QList<QVariantMap> fixtures;
    const QJsonArray array = root.value(QStringLiteral("fixtures")).toArray();
    for (const QJsonValue& value : array) {
        fixtures.append(value.toObject().toVariantMap());
    }
    return fixtures;
}

void TestSyncEventSerializer::sharedFixturesRoundTripThroughSerializer()
{
    QVERIFY2(!fixtureDir().isEmpty(), "Root contract fixture directory was not found from the test binary path.");

    const QStringList files = {
        QStringLiteral("word-and-group-events.json"),
        QStringLiteral("review-settings-and-tombstone-events.json")
    };

    for (const QString& fileName : files) {
        QString contractVersion;
        const QList<QVariantMap> fixtures = loadFixtureEvents(fileName, &contractVersion);
        QVERIFY2(!fixtures.isEmpty(), qPrintable(QStringLiteral("No fixtures loaded from %1").arg(fileName)));
        QCOMPARE(contractVersion, DLSyncEventSerializer::contractVersion());

        for (const QVariantMap& fixture : fixtures) {
            const DLSyncEventEnvelope event = DLSyncEventSerializer::eventFromContractMap(fixture, contractVersion);

            QString error;
            const QByteArray serialized = DLSyncEventSerializer::serializeContractJson(event, &error);
            QVERIFY2(!serialized.isEmpty(), qPrintable(error));

            QJsonParseError parseError;
            const QJsonDocument actual = QJsonDocument::fromJson(serialized, &parseError);
            QCOMPARE(parseError.error, QJsonParseError::NoError);
            QVariantMap expectedMap = fixture;
            expectedMap.remove(QStringLiteral("name"));
            const QJsonDocument expected(QJsonObject::fromVariantMap(expectedMap));
            QCOMPARE(actual, expected);

            const DLSyncEventEnvelope parsed = DLSyncEventSerializer::parseContractJson(serialized, contractVersion, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(parsed.eventId, event.eventId);
            QCOMPARE(parsed.deviceId, event.deviceId);
            QCOMPARE(parsed.entityType, event.entityType);
            QCOMPARE(parsed.entityId, event.entityId);
            QCOMPARE(parsed.operation, event.operation);

            const QByteArray body = DLSyncEventSerializer::serializeBodyJson(event, &error);
            QVERIFY2(!body.isEmpty(), qPrintable(error));
            const QJsonDocument actualBody = QJsonDocument::fromJson(body, &parseError);
            QCOMPARE(parseError.error, QJsonParseError::NoError);
            const QVariantMap expectedBodyMap = event.operation == QStringLiteral("delete")
                ? fixture.value(QStringLiteral("tombstone")).toMap()
                : fixture.value(QStringLiteral("payload")).toMap();
            QCOMPARE(actualBody, QJsonDocument(QJsonObject::fromVariantMap(expectedBodyMap)));
        }
    }
}

void TestSyncEventSerializer::payloadFilteringExcludesLocalAndQuizHistoryFields()
{
    QVariantMap source;
    source.insert(QStringLiteral("id"), QStringLiteral("471e206c-9c4f-4212-ae90-e90a2ee04ce4"));
    source.insert(QStringLiteral("ownerUserId"), QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b"));
    source.insert(QStringLiteral("german_word"), QStringLiteral("die Fahrkarte"));
    source.insert(QStringLiteral("normalized_german_word"), QStringLiteral("fahrkarte"));
    source.insert(QStringLiteral("article"), QStringLiteral("die"));
    source.insert(QStringLiteral("part_of_speech"), QStringLiteral("Nomen"));
    source.insert(QStringLiteral("native_translation"), QStringLiteral("ticket"));
    source.insert(QStringLiteral("normalized_native_translation"), QStringLiteral("ticket"));
    source.insert(QStringLiteral("example_phrase_de"), QStringLiteral("Ich kaufe eine Fahrkarte."));
    source.insert(QStringLiteral("example_phrase_native"), QStringLiteral("I am buying a ticket."));
    source.insert(QStringLiteral("group_id"), QStringLiteral("0b2cfd59-d17f-4772-9de0-7db26005f507"));
    source.insert(QStringLiteral("notes"), QStringLiteral("Common travel vocabulary."));
    source.insert(QStringLiteral("plural_form"), QStringLiteral("die Fahrkarten"));
    source.insert(QStringLiteral("praeteritum_form"), QVariant());
    source.insert(QStringLiteral("partizip_ii_form"), QVariant());
    source.insert(QStringLiteral("positive_form"), QVariant());
    source.insert(QStringLiteral("comparative_form"), QVariant());
    source.insert(QStringLiteral("superlative_form"), QVariant());
    source.insert(QStringLiteral("created_at"), QStringLiteral("2026-01-15T08:35:00Z"));
    source.insert(QStringLiteral("updated_at"), QStringLiteral("2026-01-15T08:35:00Z"));

    source.insert(QStringLiteral("local_id"), 42);
    source.insert(QStringLiteral("syncId"), source.value(QStringLiteral("id")));
    source.insert(QStringLiteral("word_count"), 3);
    source.insert(QStringLiteral("correct_answers"), 7);
    source.insert(QStringLiteral("quiz_history"), QVariantList{QStringLiteral("raw answer")});
    source.insert(QStringLiteral("theme"), QStringLiteral("dark"));
    source.insert(QStringLiteral("device_preferences"), QVariantMap{{ QStringLiteral("fontScale"), 1.2 }});

    const QVariantMap payload = DLSyncEventSerializer::canonicalPayloadForEntity(QStringLiteral("word"), source);

    QVERIFY(payload.contains(QStringLiteral("id")));
    QVERIFY(payload.contains(QStringLiteral("plural_form")));
    QVERIFY(!payload.contains(QStringLiteral("local_id")));
    QVERIFY(!payload.contains(QStringLiteral("syncId")));
    QVERIFY(!payload.contains(QStringLiteral("word_count")));
    QVERIFY(!payload.contains(QStringLiteral("correct_answers")));
    QVERIFY(!payload.contains(QStringLiteral("quiz_history")));
    QVERIFY(!payload.contains(QStringLiteral("theme")));
    QVERIFY(!payload.contains(QStringLiteral("device_preferences")));
}

void TestSyncEventSerializer::deleteEventsRejectFullPayloads()
{
    DLSyncEventEnvelope event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.entityType = QStringLiteral("word");
    event.entityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.operation = QStringLiteral("delete");
    event.updatedAt = QStringLiteral("2026-01-25T13:10:00Z");
    event.authenticatedUserId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.payload.insert(QStringLiteral("german_word"), QStringLiteral("die Fahrkarte"));
    event.tombstone = DLSyncEventSerializer::tombstoneForEntity(
        event.entityType,
        event.entityId,
        event.authenticatedUserId,
        event.updatedAt);

    QString error;
    QVERIFY(!DLSyncEventSerializer::validateEvent(event, &error));
    QVERIFY(error.contains(QStringLiteral("full payload")));
}

QTEST_MAIN(TestSyncEventSerializer)

#include "TestSyncEventSerializer.moc"
