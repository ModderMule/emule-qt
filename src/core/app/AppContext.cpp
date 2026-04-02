#include "pch.h"
/// @file AppContext.cpp
/// @brief Global application context definition.

#include "app/AppContext.h"
#include "kademlia/Kademlia.h"
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
    // MFC: CemuleApp::IsFirewalled() — Emule.cpp:1176
    // Not firewalled if we have an ed2k High ID
    if (serverConnect && serverConnect->isConnected() && !serverConnect->isLowID())
        return false;

    // Not firewalled if Kad says we're open
    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && kadInst->isConnected() && !kadInst->isFirewalled())
        return false;

    return true;
}

} // namespace eMule
