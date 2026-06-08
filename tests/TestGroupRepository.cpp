#include <QtTest>

#include <QDir>
#include <QFile>
#include <QUuid>

#include "Managers/DLDatabaseManager.h"
#include "Models/DLWordGroup.h"
#include "Repositories/DLGroupRepository.h"

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

    const int groupId = repository.insertGroup(group);
    QVERIFY2(groupId > 0, qPrintable(DLDatabaseManager::instance().lastError()));
}

void TestGroupRepository::fetchAllGroupsReturnsInsertedGroups()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup a;
    a.name = QStringLiteral("Basics");
    DLWordGroup b;
    b.name = QStringLiteral("Travel");

    QVERIFY(repository.insertGroup(a) > 0);
    QVERIFY(repository.insertGroup(b) > 0);

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

    const int groupId = repository.insertGroup(group);
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
    const int groupId = repository.insertGroup(group);

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
    const int groupId = repository.insertGroup(group);

    QVERIFY2(repository.deleteGroup(groupId), qPrintable(DLDatabaseManager::instance().lastError()));
    QCOMPARE(repository.fetchGroupById(groupId).id, -1);
    QCOMPARE(repository.getGroupCount(), 0);
}

void TestGroupRepository::wordCountPerGroupWorks()
{
    DLGroupRepository repository(DLDatabaseManager::instance());
    DLWordGroup group;
    group.name = QStringLiteral("Basics");
    const int groupId = repository.insertGroup(group);

    QVERIFY(DLDatabaseManager::instance().insertWord(QStringLiteral("Haus"), QStringLiteral("das"),
                                                     QStringLiteral("Nomen"), QStringLiteral("house"),
                                                     QString(), QString(), groupId) > 0);
    QVERIFY(DLDatabaseManager::instance().insertWord(QStringLiteral("gehen"), QString(),
                                                     QStringLiteral("Verb"), QStringLiteral("go"),
                                                     QString(), QString(), groupId) > 0);

    QCOMPARE(repository.fetchGroupById(groupId).wordCount, 2);
}

QTEST_MAIN(TestGroupRepository)

#include "TestGroupRepository.moc"
