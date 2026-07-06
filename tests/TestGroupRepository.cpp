#include <QtTest>

#include <QDir>
#include <QFile>
#include <QUuid>

#include "Managers/DLDatabaseManager.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"
#include "Repositories/DLOutboundSyncQueueRepository.h"

class TestGroupRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void insertGroupReturnsValidId();
    void fetchAllGroupsReturnsInsertedGroups();
    void fetchGroupByIdReturnsCorrectGroup();
    void updateGroupChangesNameAndColor();
    void deleteGroupRemovesGroup();
    void localCrudSetsDirtyAndUsesSoftDelete();
    void outboundQueueTracksGroupCrud();
    void outboundQueueCanBeSuppressed();
    void wordCountPerGroupWorks();

private:
    QString m_dbPath;
};

void TestGroupRepository::init()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("de_vocab_test_%1.sqlite")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile::remove(m_dbPath);
    DLDatabaseManager::instance().closeDatabase();
    QVERIFY2(DLDatabaseManager::instance().openDatabase(m_dbPath),
             qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestGroupRepository::cleanup()
{
    DLDatabaseManager::instance().closeDatabase();
    QFile::remove(m_dbPath);
}

void TestGroupRepository::insertGroupReturnsValidId()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    group.colorHex = QStringLiteral("#123456");

    const QString groupId = repository.insertGroup(group);
    QVERIFY2(!groupId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(QUuid::fromString(groupId).toString(QUuid::WithoutBraces), groupId);
}

void TestGroupRepository::fetchAllGroupsReturnsInsertedGroups()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup a;
    a.name = QStringLiteral("Basics");
    DLWordGroup b;
    b.name = QStringLiteral("Travel");

    QVERIFY(!repository.insertGroup(a).isEmpty());
    QVERIFY(!repository.insertGroup(b).isEmpty());

    const QList<DLWordGroup> groups = repository.fetchAllGroups();
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.at(0).name, QStringLiteral("Basics"));
    QCOMPARE(groups.at(1).name, QStringLiteral("Travel"));
}

void TestGroupRepository::fetchGroupByIdReturnsCorrectGroup()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    group.colorHex = QStringLiteral("#445566");

    const QString groupId = repository.insertGroup(group);
    const DLWordGroup fetched = repository.fetchGroupById(groupId);

    QCOMPARE(fetched.id, groupId);
    QCOMPARE(fetched.name, group.name);
    QCOMPARE(fetched.colorHex, group.colorHex);
}

void TestGroupRepository::updateGroupChangesNameAndColor()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = repository.insertGroup(group);

    DLWordGroup update;
    update.id = groupId;
    update.name = QStringLiteral("Grammar");
    update.colorHex = QStringLiteral("#AABBCC");

    QVERIFY2(repository.updateGroup(update), qPrintable(DLDatabaseManager::instance().lastError()));
    const DLWordGroup fetched = repository.fetchGroupById(groupId);
    QCOMPARE(fetched.name, update.name);
    QCOMPARE(fetched.colorHex, update.colorHex);
}

void TestGroupRepository::deleteGroupRemovesGroup()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = repository.insertGroup(group);

    QVERIFY2(repository.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY(repository.fetchGroupById(groupId).id.isEmpty());
    QCOMPARE(repository.getGroupCount(), 0);
}

void TestGroupRepository::localCrudSetsDirtyAndUsesSoftDelete()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");

    const QString groupId = repository.insertGroup(group);
    QVERIFY2(!groupId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));

    QVariantMap row = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT dirty, deleted_at FROM groups WHERE sync_id = :id;"),
        {{ QStringLiteral(":id"), groupId }});
    QCOMPARE(row.value(QStringLiteral("dirty")).toInt(), 1);
    QVERIFY(row.value(QStringLiteral("deleted_at")).isNull());

    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral("UPDATE groups SET dirty = 0 WHERE sync_id = :id;"),
                 {{ QStringLiteral(":id"), groupId }}),
             qPrintable(DLDatabaseManager::instance().lastError()));

    DLWordGroup update;
    update.id = groupId;
    update.name = QStringLiteral("Grammar");
    update.colorHex = QStringLiteral("#445566");
    QVERIFY2(repository.updateGroup(update), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(DLDatabaseManager::instance().selectInt(
                 QStringLiteral("SELECT dirty FROM groups WHERE sync_id = :id;"),
                 {{ QStringLiteral(":id"), groupId }}),
             1);

    QVERIFY2(DLDatabaseManager::instance().executeSql(
                 QStringLiteral("UPDATE groups SET dirty = 0 WHERE sync_id = :id;"),
                 {{ QStringLiteral(":id"), groupId }}),
             qPrintable(DLDatabaseManager::instance().lastError()));

    QVERIFY2(repository.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));
    row = DLDatabaseManager::instance().selectOneRow(
        QStringLiteral("SELECT dirty, deleted_at FROM groups WHERE sync_id = :id;"),
        {{ QStringLiteral(":id"), groupId }});
    QCOMPARE(row.value(QStringLiteral("dirty")).toInt(), 1);
    QVERIFY(!row.value(QStringLiteral("deleted_at")).isNull());
    QVERIFY(repository.fetchGroupById(groupId).id.isEmpty());
    QCOMPARE(repository.fetchAllGroups().size(), 0);
}

void TestGroupRepository::outboundQueueTracksGroupCrud()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    group.colorHex = QStringLiteral("#112233");

    const QString groupId = repository.insertGroup(group);
    QVERIFY2(!groupId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));

    DLWordGroup update;
    update.id = groupId;
    update.name = QStringLiteral("Grammar");
    update.colorHex = QStringLiteral("#445566");
    QVERIFY2(repository.updateGroup(update), qPrintable(DLDatabaseManager::instance().lastError()));
    QVERIFY2(repository.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));

    const QVariantList rows = DLDatabaseManager::instance().selectRows(QStringLiteral(R"(
        SELECT entity_type, entity_id, operation, payload_json, retry_count, last_error, pushed_at
        FROM outbound_sync_queue
        ORDER BY rowid;
    )"));
    QCOMPARE(rows.size(), 3);

    const QStringList operations = {
        QStringLiteral("create"),
        QStringLiteral("update"),
        QStringLiteral("delete")
    };
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        QCOMPARE(row.value(QStringLiteral("entity_type")).toString(), QStringLiteral("groups"));
        QCOMPARE(row.value(QStringLiteral("entity_id")).toString(), groupId);
        QCOMPARE(row.value(QStringLiteral("operation")).toString(), operations.at(i));
        QCOMPARE(row.value(QStringLiteral("retry_count")).toInt(), 0);
        QVERIFY(row.value(QStringLiteral("last_error")).isNull());
        QVERIFY(row.value(QStringLiteral("pushed_at")).isNull());
    }
    QVERIFY(rows.at(0).toMap().value(QStringLiteral("payload_json")).toString().contains(QStringLiteral("Basics")));
    QVERIFY(rows.at(1).toMap().value(QStringLiteral("payload_json")).toString().contains(QStringLiteral("Grammar")));
    QVERIFY(rows.at(2).toMap().value(QStringLiteral("payload_json")).isNull());
}

void TestGroupRepository::outboundQueueCanBeSuppressed()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Server Group");

    {
        DLOutboundSyncQueueScope suppressQueue(false);
        const QString groupId = repository.insertGroup(group);
        QVERIFY2(!groupId.isEmpty(), qPrintable(DLDatabaseManager::instance().lastError()));
    }

    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM groups WHERE deleted_at IS NULL;")), 1);
    QCOMPARE(DLDatabaseManager::instance().selectInt(QStringLiteral("SELECT COUNT(*) FROM outbound_sync_queue;")), 0);
}

void TestGroupRepository::wordCountPerGroupWorks()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const QString groupId = repository.insertGroup(group);

    QVERIFY(!DLDatabaseManager::instance().insertWord(QStringLiteral("Haus"), QStringLiteral("das"),
                                                      QStringLiteral("Nomen"), QStringLiteral("house"),
                                                      QString(), QString(), groupId).isEmpty());
    QVERIFY(!DLDatabaseManager::instance().insertWord(QStringLiteral("gehen"), QString(),
                                                      QStringLiteral("Verb"), QStringLiteral("go"),
                                                      QString(), QString(), groupId).isEmpty());

    QCOMPARE(repository.fetchGroupById(groupId).wordCount, 2);
}

QTEST_MAIN(TestGroupRepository)

#include "TestGroupRepository.moc"
