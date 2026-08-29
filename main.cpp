#include <QApplication>
#include <FelgoApplication>

#include <QQmlApplicationEngine>

#include <QScopedPointer>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QNetworkInformation>
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

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, appStateManager.data(), [appStateManager = appStateManager.data()](Qt::ApplicationState state) {
        appStateManager->setApplicationActive(state == Qt::ApplicationActive);
    });
    appStateManager->setApplicationActive(app.applicationState() == Qt::ApplicationActive);

    if (QNetworkInformation::loadDefaultBackend()) {
        QNetworkInformation* networkInformation = QNetworkInformation::instance();
        const auto applyReachability = [appStateManager = appStateManager.data()](QNetworkInformation::Reachability reachability) {
            appStateManager->setNetworkAvailable(reachability != QNetworkInformation::Reachability::Disconnected);
        };
        applyReachability(networkInformation->reachability());
        QObject::connect(networkInformation, &QNetworkInformation::reachabilityChanged, appStateManager.data(), applyReachability);
    }

    appStateManager->init(dbPath);

    return app.exec();
}
