#include "pch.h"
/// @file DirectUdpNatPath.cpp
/// @brief Direct UDP path: OP_DIRECTCALLBACKREQ with the QUIC bit, then the hub's QUIC dial.

#include "net/natt/DirectUdpNatPath.h"

#include "net/Packet.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <cstring>

namespace eMule::natt {

namespace {

constexpr uint32 kRequestSize = 2 + 16 + 1;   // tcp port, user hash, connect options

} // namespace

bool DirectUdpNatPath::canServe(const PeerInfo& peer) const
{
    return peer.directUdp && peer.hashKnown && isLowID(peer.clientID)
        && peer.kadEndpoint.address().isIPv4() && peer.kadEndpoint.port() != 0
        && env().quicAvailable() && env().ourUdpPort() != 0;
}

bool DirectUdpNatPath::request(Attempt& attempt)
{
    if (!env().admit(attempt.peer.kadEndpoint.address()))
        return false;
    sendRequest(attempt);
    // Its UDP is open: the endpoint is known now, punch + caps + QUIC dial follow.
    hub().endpointKnown(attempt, attempt.peer.kadEndpoint);
    return true;
}

void DirectUdpNatPath::repeat(Attempt& attempt)
{
    // Until the QUIC dial: a lost request leaves the source without an expectation.
    if (attempt.role == Role::Requester && !attempt.quicDialed
        && repeatDue(attempt, kRepeatMs, kMaxRequests))
        sendRequest(attempt);
}

void DirectUdpNatPath::sendRequest(Attempt& attempt)
{
    auto packet = std::make_unique<Packet>(OP_DIRECTCALLBACKREQ, kRequestSize, OP_EMULEPROT);
    auto* out = reinterpret_cast<uint8*>(packet->pBuffer);
    pokeUInt16(out, env().ourTcpPort());
    const Hash16 ours = env().ourHash();
    std::memcpy(out + 2, ours.data(), 16);
    // Crypt bits as the stock request (MFC GetMyConnectOptions(true, false)), plus QUIC.
    out[18] = static_cast<uint8>((env().ourConnectOptions() & 0x07) | CONNECT_OPT_NATT_QUIC);
    attempt.lastRequestMs = env().now();
    ++attempt.requestCount;
    const bool crypt = attempt.peer.directUdpCrypt && (env().ourConnectOptions() & CONNECT_OPT_CRYPT_SUPPORTED);
    env().sendPeerUdp(std::move(packet), attempt.peer.kadEndpoint, crypt ? attempt.peer.hash.data() : nullptr);
    log(QStringLiteral("direct callback (QUIC) to %1").arg(attempt.peer.kadEndpoint.toString()));
}

} // namespace eMule::natt
