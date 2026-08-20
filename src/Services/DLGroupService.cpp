#include "DLGroupService.h"

#include "DLDatabaseManager.h"
#include "DLLogging.h"

DLGroupService::DLGroupService(DLDatabaseManager& database)
    : m_database(database)
{
}

QVariantList DLGroupService::availableGroups()
{
    m_lastError.clear();
    return m_database.fetchAllGroups();
}

QString DLGroupService::createGroup(const QString& name, const QString& colorHex)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_lastError = QStringLiteral("Group name is required.");
        qCWarning(dlService) << "Rejected group creation: empty name";
        return {};
    }

    const QString newId = m_database.insertGroup(trimmedName, normalizedColor(colorHex));
    m_lastError = newId.isEmpty() ? m_database.lastError() : QString();
    return newId;
}

bool DLGroupService::updateGroup(const QString& syncId, const QString& name, const QString& colorHex)
{
    if (syncId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Invalid group id.");
        qCWarning(dlService) << "Rejected group update: invalid id";
        return false;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_lastError = QStringLiteral("Group name is required.");
        qCWarning(dlService) << "Rejected group update: empty name";
        return false;
    }

    const bool success = m_database.updateGroup(syncId.trimmed(), trimmedName, normalizedColor(colorHex));
    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

bool DLGroupService::deleteGroup(const QString& syncId)
{
    if (syncId.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Invalid group id.");
        qCWarning(dlService) << "Rejected group delete: invalid id";
        return false;
    }

    const bool success = m_database.deleteGroup(syncId.trimmed());
    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

int DLGroupService::groupCount()
{
    m_lastError.clear();
    return m_database.getGroupCount();
}

QString DLGroupService::lastError() const
{
    return m_lastError;
}

QString DLGroupService::normalizedColor(const QString& colorHex) const
{
    return colorHex.trimmed().isEmpty() ? QStringLiteral("#337fe6") : colorHex.trimmed();
}
