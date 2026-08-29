#include "DLAuthSession.h"

bool DLAuthSession::hasSessionCredentials() const
{
    return priorSuccessfulAuthentication
        && !userId.trimmed().isEmpty()
        && !sessionToken.trimmed().isEmpty();
}

bool DLAuthSession::allowsOfflineFreeCore() const
{
    return priorSuccessfulAuthentication && !userId.trimmed().isEmpty();
}
