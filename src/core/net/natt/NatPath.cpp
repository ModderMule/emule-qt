#include "pch.h"
/// @file NatPath.cpp
/// @brief Shared helpers of the NAT rendezvous paths.

#include "net/natt/NatPath.h"

#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

namespace eMule::natt {

NatEnv& NatPath::env() const
{
    return m_hub.env();
}

bool NatPath::peerOnOurServer(const PeerInfo& peer) const
{
    const Server* server = env().currentServer();
    if (!server || peer.clientID == 0 || !isLowID(peer.clientID) || ourID() == 0)
        return false;
    if (server->port() != peer.serverPort)
        return false;
    // Sources from an IPv6 session carry no IPv4 server address (cf. ServerConnect::isLocalServer)
    return server->hasAddress(peer.serverAddress)
        || (peer.serverAddress.isNull() && server->ipAddress().isIPv6());
}

uint32 NatPath::ourID() const
{
    return env().currentServer() ? env().ourClientID() : 0;
}

void NatPath::log(const QString& text) const
{
    logDebug(QStringLiteral("NAT-T %1: %2").arg(QLatin1StringView(pathName(kind())), text));
}

bool NatPath::repeatDue(const Attempt& attempt, uint64 intervalMs, uint32 maxCount) const
{
    return attempt.requestCount < maxCount && env().now() - attempt.lastRequestMs >= intervalMs;
}

} // namespace eMule::natt
