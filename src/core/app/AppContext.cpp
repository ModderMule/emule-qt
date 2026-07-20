#include "pch.h"
/// @file AppContext.cpp
/// @brief Global application context definition.

#include "app/AppContext.h"
#include "kademlia/Kademlia.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

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
    // MFC: CemuleApp::IsConnected() — Emule.cpp:1122
    // Connected to *any* network: ED2K server or Kad.
    if (serverConnect && serverConnect->isConnected())
        return true;

    auto* kadInst = kad::Kademlia::instance();
    return kadInst && kadInst->isConnected();
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

uint32 AppContext::publicIP(bool ignoreKadIP) const
{
    // MFC: CemuleApp::GetPublicIP() — Emule.cpp:1542, with the priority inverted:
    // Kad outranks the ED2K value here. KadPrefs::setIPAddress() only commits an
    // address two independent nodes agreed on, while a server's claim is one
    // unverified assertion from a single host.
    if (!ignoreKadIP) {
        // Kad holds IPs with the first octet in the MSB; ED2K wants it in the LSB.
        // Same direction as the ipFilter calls in KadRoutingZone.cpp.
        auto* kadInst = kad::Kademlia::instance();
        if (kadInst && kadInst->isConnected() && kadInst->getIPAddress() != 0)
            return htonl(kadInst->getIPAddress());
    }
    return m_publicIP;
}

void AppContext::setPublicIP(uint32 ip)
{
    // MFC: CemuleApp::SetPublicIP() — Emule.cpp:1548.
    if (ip != 0) {
        // MFC logs this same disagreement (Emule.cpp:1557). Under our Kad-first
        // priority it is more than a curiosity: the server issues its UDP key for
        // the IP *it* sees us on, so if these two ever diverge, the key is stamped
        // and validated against Kad's view and can never match. This log is how
        // that shows up.
        auto* kadInst = kad::Kademlia::instance();
        if (kadInst && kadInst->isConnected() && kadInst->getIPAddress() != 0) {
            const uint32 kadIP = htonl(kadInst->getIPAddress());
            if (kadIP != ip)
                logDebug(QStringLiteral("Public IP reported by Kad (%1) differs from the "
                                        "ED2K-reported one (%2); Kad's takes precedence")
                             .arg(ipstr(kadIP), ipstr(ip)));
        }
    }

    // Server UDP keys are bound to the IP they were issued for, so a real change
    // invalidates every one of them and those servers must be re-pinged.
    // Compares the stored value, not publicIP(): against the Kad-backed getter
    // this check would never fire while Kad is supplying the effective IP.
    const uint32 oldIP = m_publicIP;
    m_publicIP = ip;
    if (ip != 0 && ip != oldIP && serverList)
        serverList->checkForExpiredUDPKeys(publicIP());
}

void AppContext::onEffectivePublicIPChanged(uint32 newIP)
{
    // Kad outranks m_publicIP, so a change to *its* address changes our effective
    // IP and kills every server UDP key — with no call to setPublicIP() to notice.
    // Without this hook the Kad-first priority silently reintroduces stale keys.
    if (newIP != 0 && serverList)
        serverList->checkForExpiredUDPKeys(publicIP());
}

} // namespace eMule
