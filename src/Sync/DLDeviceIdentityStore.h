#ifndef DLDEVICEIDENTITYSTORE_H
#define DLDEVICEIDENTITYSTORE_H

#include <QString>

class DLDeviceIdentityStore
{
public:
    explicit DLDeviceIdentityStore(QString storagePath);

    QString storagePath() const;
    QString lastError() const;

    QString deviceId();
    bool clearAll();

private:
    QString loadDeviceId() const;
    bool writeDeviceId(const QString& deviceId);
    void setLastError(const QString& error) const;

    QString m_storagePath;
    mutable QString m_lastError;
};

#endif // DLDEVICEIDENTITYSTORE_H
