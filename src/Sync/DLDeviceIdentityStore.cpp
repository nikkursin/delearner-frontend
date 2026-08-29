#include "DLDeviceIdentityStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <utility>

namespace {
QString normalizedUuid(const QString& value)
{
    const QUuid uuid(value.trimmed());
    return uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
}
}

DLDeviceIdentityStore::DLDeviceIdentityStore(QString storagePath)
    : m_storagePath(std::move(storagePath))
{
}

QString DLDeviceIdentityStore::storagePath() const
{
    return m_storagePath;
}

QString DLDeviceIdentityStore::lastError() const
{
    return m_lastError;
}

QString DLDeviceIdentityStore::deviceId()
{
    const QString existing = loadDeviceId();
    if (!existing.isEmpty()) {
        return existing;
    }
    if (!m_lastError.isEmpty()) {
        return {};
    }

    const QString generated = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return writeDeviceId(generated) ? generated : QString();
}

bool DLDeviceIdentityStore::clearAll()
{
    setLastError(QString());

    QFile file(m_storagePath);
    if (!file.exists()) {
        return true;
    }

    if (!file.remove()) {
        setLastError(QStringLiteral("Could not clear saved device identity."));
        return false;
    }

    return true;
}

QString DLDeviceIdentityStore::loadDeviceId() const
{
    setLastError(QString());

    QFile file(m_storagePath);
    if (!file.exists()) {
        return {};
    }

    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("Could not read saved device identity."));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Saved device identity is invalid."));
        return {};
    }

    const QString deviceId = document.object().value(QStringLiteral("deviceId")).toString().trimmed();
    const QString normalizedDeviceId = normalizedUuid(deviceId);
    if (normalizedDeviceId.isEmpty()) {
        setLastError(QStringLiteral("Saved device identity does not contain a valid UUID."));
        return {};
    }

    return normalizedDeviceId;
}

bool DLDeviceIdentityStore::writeDeviceId(const QString& deviceId)
{
    setLastError(QString());

    const QString normalizedDeviceId = normalizedUuid(deviceId);
    if (normalizedDeviceId.isEmpty()) {
        setLastError(QStringLiteral("Device identity requires a valid UUID."));
        return false;
    }

    const QFileInfo info(m_storagePath);
    const QDir parentDir = info.absoluteDir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        setLastError(QStringLiteral("Could not create device identity directory."));
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("deviceId"), normalizedDeviceId);

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("Could not write device identity."));
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setLastError(QStringLiteral("Could not commit device identity."));
        return false;
    }

    return true;
}

void DLDeviceIdentityStore::setLastError(const QString& error) const
{
    m_lastError = error;
}
