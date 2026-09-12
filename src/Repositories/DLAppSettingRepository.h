#ifndef DLAPPSETTINGREPOSITORY_H
#define DLAPPSETTINGREPOSITORY_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class DLDatabaseManager;

class DLAppSettingRepository
{
public:
    explicit DLAppSettingRepository(DLDatabaseManager& database);

    bool setSetting(const QString& settingKey,
                    const QString& settingValue,
                    const QString& valueType = QStringLiteral("string"));
    bool deleteSetting(const QString& settingKey);
    QVariantMap fetchSetting(const QString& settingKey);
    QVariantList fetchAllSettings();

private:
    DLDatabaseManager& m_database;
};

#endif // DLAPPSETTINGREPOSITORY_H
