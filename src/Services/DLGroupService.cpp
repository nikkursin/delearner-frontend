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

int DLGroupService::createGroup(const QString& name, const QString& colorHex)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        m_lastError = QStringLiteral("Group name is required.");
        qCWarning(dlService) << "Rejected group creation: empty name";
        return -1;
    }

    const int newId = m_database.insertGroup(trimmedName, normalizedColor(colorHex));
    m_lastError = newId < 0 ? m_database.lastError() : QString();
    return newId;
}

bool DLGroupService::updateGroup(int id, const QString& name, const QString& colorHex)
{
    if (id <= 0) {
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

    const bool success = m_database.updateGroup(id, trimmedName, normalizedColor(colorHex));
    m_lastError = success ? QString() : m_database.lastError();
    return success;
}

bool DLGroupService::deleteGroup(int id)
{
    if (id <= 0) {
        m_lastError = QStringLiteral("Invalid group id.");
        qCWarning(dlService) << "Rejected group delete: invalid id";
        return false;
    }

    const bool success = m_database.deleteGroup(id);
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
