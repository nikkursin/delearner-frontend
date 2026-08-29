#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "Auth/DLAuthSessionStore.h"
#include "Managers/DLAppStateManager.h"

class TestAppStateAuthBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void freshInstallRoutesToAuthPageWithoutOpeningDatabase();
    void priorSuccessfulAuthenticationOpensFreeCore();
};

void TestAppStateAuthBootstrap::freshInstallRoutesToAuthPageWithoutOpeningDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAppStateManager manager;
    manager.init(databasePath);

    QCOMPARE(manager.currentScreen(), DLAppStateManager::AuthPage);
    QCOMPARE(manager.authState(), QStringLiteral("authentication_required"));
    QVERIFY(!QFile::exists(databasePath));
}

void TestAppStateAuthBootstrap::priorSuccessfulAuthenticationOpensFreeCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("delearner.sqlite"));

    DLAuthSession session;
    session.userId = QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
    session.email = QStringLiteral("learner@example.com");
    session.sessionToken = QStringLiteral("cached-token");

    DLAuthSessionStore store(QStringLiteral("%1.auth.json").arg(databasePath));
    QVERIFY2(store.saveSuccessfulSession(session), qPrintable(store.lastError()));

    DLAppStateManager manager;
    manager.init(databasePath);

    QCOMPARE(manager.currentScreen(), DLAppStateManager::AddEditWordPage);
    QCOMPARE(manager.authState(), QStringLiteral("authenticated_session"));
    QVERIFY(QFile::exists(databasePath));
}

QTEST_MAIN(TestAppStateAuthBootstrap)
#include "TestAppStateAuthBootstrap.moc"
