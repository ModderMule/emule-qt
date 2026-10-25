#include "pch.h"
/// @file ForeignCallbackPath.cpp
/// @brief Foreign callback: 0xA6 with our ID 0 to the source's server, 0xA7 back.

#include "net/natt/ForeignCallbackPath.h"

#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

namespace eMule::natt {

bool ForeignCallbackPath::foreignPeer(const PeerInfo& peer) const
{
    // Callback-capable targets get an odd LowID (see LugdunumNatPath::canServe).
    if (peer.clientID == 0 || !isLowID(peer.clientID) || (peer.clientID & 1) == 0)
        return false;
    if (!peer.serverAddress.isIPv4() || peer.serverPort == 0 || peerOnOurServer(peer))
        return false;
    const Server* cur = env().currentServer();
    return !(cur && cur->port() == peer.serverPort && cur->hasAddress(peer.serverAddress));
}

bool ForeignCallbackPath::canServe(const PeerInfo& peer) const
{
    // The server takes only an obfuscated 0xA6: its key is what proves our address.
    if (env().ourUdpPort() == 0 || !(env().ourConnectOptions() & CONNECT_OPT_CRYPT_SUPPORTED)
        || !foreignPeer(peer))
        return false;
    // Probing here is what "a source of an active download" means: sources come in
    // through keepLowIdSource / tryToConnect, both of which ask canServe.
    const ForeignServer fs = env().foreignServer(peer.serverAddress, peer.serverPort, true);
    return fs.state != ForeignServer::State::No;
}

std::optional<LugdunumNatPath::RequestRoute> ForeignCallbackPath::requestRoute(const Attempt& attempt)
{
    const ForeignServer fs = env().foreignServer(attempt.peer.serverAddress, attempt.peer.serverPort, true);
    if (fs.state != ForeignServer::State::Ready)
        return std::nullopt;   // status ping still out
    if (fs.crypt.key == 0 || fs.crypt.port == 0) {
        refreshKey(attempt);
        return std::nullopt;
    }
    return RequestRoute{Endpoint(fs.ipv4, fs.crypt.port), fs.crypt.key, 0};
}

void ForeignCallbackPath::refreshKey(const Attempt& attempt)
{
    env().refreshForeignServerKey(attempt.peer.serverAddress, attempt.peer.serverPort);
}

void ForeignCallbackPath::repeat(Attempt& attempt)
{
    if (attempt.role != Role::Requester || attempt.state != Attempt::State::Requested)
        return;
    const ForeignServer fs = env().foreignServer(attempt.peer.serverAddress, attempt.peer.serverPort, false);
    if (fs.state == ForeignServer::State::No) {
        hub().failOrNext(attempt, QStringLiteral("server %1:%2 has no foreign callback")
                                      .arg(attempt.peer.serverAddress.toString()).arg(attempt.peer.serverPort));
        return;
    }
    if (repeatDue(attempt, kRepeatMs, kMaxForeignRequests))
        sendRequest(attempt);
}

std::optional<uint32> ForeignCallbackPath::noticeKey(const Endpoint& from) const
{
    // Only from a server one of our foreign requests went to, read with its key.
    const Attempt* a = hub().findAttempt([&](const Attempt& x) {
        return x.path == kind() && x.role == Role::Requester && x.peer.serverAddress == from.address();
    });
    if (!a)
        return std::nullopt;
    return env().foreignServer(a->peer.serverAddress, a->peer.serverPort, false).crypt.key;
}

void ForeignCallbackPath::onNotice(const Endpoint& from, const uint8* data, uint32 size)
{
    const auto n = parseNotice(env(), data, size);
    if (!n) {
        log(QStringLiteral("short 0xA7 (%1 bytes) from %2").arg(size).arg(from.toString()));
        return;
    }
    log(QStringLiteral("0xA7 from %1: id %2 at %3 flags 0x%4")
            .arg(from.toString()).arg(n->peer.clientID).arg(n->udp.toString())
            .arg(n->flags, 2, 16, QLatin1Char('0')));
    Attempt* mine = hub().findAttempt([&](const Attempt& a) {
        return a.path == kind() && a.role == Role::Requester && a.peer.clientID == n->peer.clientID
            && a.peer.serverAddress == from.address();
    });
    if (!mine) {
        log(QStringLiteral("0xA7 for no request of ours dropped"));
        return;
    }
    onAnswer(*mine, *n);
}

} // namespace eMule::natt
