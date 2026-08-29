#ifndef DLTESTSUPPORT_H
#define DLTESTSUPPORT_H

#include "Managers/DLDatabaseManager.h"

namespace DLTestSupport {
inline QString testUserId()
{
    return QStringLiteral("df5cb428-b235-4b56-9a21-137f1169015b");
}

inline QString testDeviceId()
{
    return QStringLiteral("d783fc93-d487-4d99-b566-886a018403a1");
}

inline void installSyncContext()
{
    DLDatabaseManager::instance().setSyncContext(testUserId(), testDeviceId());
}
}

#endif // DLTESTSUPPORT_H
