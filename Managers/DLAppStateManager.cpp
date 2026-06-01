#include "DLAppStateManager.h"

DLAppStateManager::DLAppStateManager(QObject *parent)
    : QObject{parent}
{
}

DLAppStateManager::~DLAppStateManager() {
    m_databaseManager->closeDatabase();
}

void DLAppStateManager::init(const QString& databasePath) {
    DLDatabaseManager::instance().openDatabase(databasePath);
}

DLAppStateManager::DLScreen DLAppStateManager::currentScreen() const {
    return m_currentScreen;
}

Q_INVOKABLE void DLAppStateManager::goStartupLoadingPage() {

}

Q_INVOKABLE void DLAppStateManager::goWordsPage() {

}

Q_INVOKABLE void DLAppStateManager::goWordDetails() {

}

Q_INVOKABLE void DLAppStateManager::goAddEditWordPage() {

}

Q_INVOKABLE void DLAppStateManager::goGroupsPage() {

}

Q_INVOKABLE void DLAppStateManager::goGroupEditPage() {

}

Q_INVOKABLE void DLAppStateManager::goQuizHomePage() {

}

Q_INVOKABLE void DLAppStateManager::goQuizSetupPage() {

}

Q_INVOKABLE void DLAppStateManager::goTranslationQuizSessionPage() {

}

Q_INVOKABLE void DLAppStateManager::goArticleQuizSessionPage() {

}

Q_INVOKABLE void DLAppStateManager::goQuizResults() {

}

Q_INVOKABLE void DLAppStateManager::goSettingsPage() {

}
