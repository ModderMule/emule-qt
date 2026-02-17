/// @file AppContext.cpp
/// @brief Global application context definition.

#include "app/AppContext.h"
#include "server/ServerConnect.h"

namespace eMule {

AppContext theApp;

uint32 AppContext::getID() const
{
    if (serverConnect && serverConnect->isConnected())
        return serverConnect->clientID();
    return 0;
}

bool AppContext::isConnected() const
{
    return serverConnect && serverConnect->isConnected();
}

bool AppContext::isFirewalled() const
{
    return serverConnect && serverConnect->isConnected() && serverConnect->isLowID();
}

} // namespace eMule
