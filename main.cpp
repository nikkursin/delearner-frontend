#include <QApplication>
#include <FelgoApplication>

#include <QQmlApplicationEngine>

#include <QScopedPointer>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include "Managers/DLAppStateManager.h"

// Uncomment this line to add Felgo Hot Reload and use hot reloading with your custom C++ code
//#include <FelgoHotReload>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    FelgoApplication felgo;

    QQmlApplicationEngine engine;
    felgo.initialize(&engine);

    QScopedPointer<DLAppStateManager> appStateManager(new DLAppStateManager());
    const QString dbPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/delearner.sqlite";

    engine.rootContext()->setContextProperty(
        "appStateManager",
        appStateManager.data()
        );

    felgo.setLicenseKey(PRODUCT_LICENSE_KEY);

    felgo.setMainQmlFileName(QStringLiteral("qml/Main.qml"));

    engine.load(QUrl(felgo.mainQmlFileName()));

    // if (engine.rootObjects().isEmpty()) {
    //     return -1;
    // }

    // DLAppStateManager* appStateManagerPtr = appStateManager.data();
    // QTimer::singleShot(0, appStateManagerPtr, [appStateManagerPtr, dbPath]() {
    //     QDir().mkpath(QFileInfo(dbPath).absolutePath());
    //     appStateManagerPtr->init(dbPath);
    // });

    appStateManager->init(dbPath);

    return app.exec();
}
