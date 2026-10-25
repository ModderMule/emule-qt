#include "pch.h"
/// @file AppNatEnv.cpp
/// @brief NatEnv backed by the running core.

#include "net/natt/AppNatEnv.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "net/ClientUDPSocket.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "net/quic/QuicNatConnection.h"
#include "net/quic/QuicNatEndpoint.h"
#include "net/quic/QuicNatIo.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

namespace eMule::natt {

namespace {

uint8 cryptBits()
{
    return static_cast<uint8>((thePrefs.cryptLayerSupported() ? CONNECT_OPT_CRYPT_SUPPORTED : 0)
                              | (thePrefs.cryptLayerRequested() ? CONNECT_OPT_CRYPT_REQUESTED : 0)
                              | (thePrefs.cryptLayerRequired() ? CONNECT_OPT_CRYPT_REQUIRED : 0));
}

bool admitAddress(const Address& addr)
{
    if (!isGoodIP(addr))
        return false;
    if (theApp.ipFilter && theApp.ipFilter->isFiltered(addr, thePrefs.ipFilterLevel()))
        return false;
    return !(theApp.clientList && theApp.clientList->isBannedClient(addr));
}

/// Hash to obfuscate for, or zero when the peer cannot read obfuscated datagrams.
quic::Hash16 obfuscationHash(const Attempt& a)
{
    if (!a.peer.hashKnown || (a.peer.cryptKnown && !(a.peer.cryptOptions & CONNECT_OPT_CRYPT_SUPPORTED)))
        return {};
    return a.peer.hash;
}

} // namespace

/// The client UDP socket and prefs for the QUIC endpoint.
class AppQuicNatIo final : public quic::QuicNatIo {
public:
    [[nodiscard]] quic::Hash16 ourHash() const override { return thePrefs.userHash(); }
    [[nodiscard]] uint64 nowMs() const override { return getTickCount(); }
    void sendFrame(uint8 type, const uint8* payload, size_t len, const Endpoint& to,
                   const quic::Hash16* cryptHash) override
    {
        if (theApp.clientUDP)
            theApp.clientUDP->sendNattFrameNow(type, payload, len, to, cryptHash ? cryptHash->data() : nullptr);
    }
    [[nodiscard]] bool canObfuscate(const Endpoint& to) const override
    {
        // the client key mixes in our public IP of the destination's family
        const bool haveIP = to.address().isIPv6() ? !theApp.publicIPv6().isNull() : theApp.publicIP() != 0;
        return thePrefs.cryptLayerSupported() && haveIP;
    }
    [[nodiscard]] bool strictObfuscation() const override { return thePrefs.natTraversalStrictObfuscation(); }
    [[nodiscard]] bool admit(const Address& addr) const override { return admitAddress(addr); }
    [[nodiscard]] uint8 connectOptions() const override { return cryptBits(); }
    [[nodiscard]] Endpoint localEndpoint(const Endpoint& peer) const override
    {
        static const std::array<uint8, 16> any6{};
        return Endpoint(peer.address().isIPv6() ? Address::fromIPv6Bytes(any6.data()) : Address(),
                        theApp.listeningUdpPort());
    }
};

AppNatEnv::AppNatEnv() = default;

AppNatEnv::~AppNatEnv() = default;

uint64 AppNatEnv::now() const
{
    return getTickCount();
}

Hash16 AppNatEnv::ourHash() const
{
    return thePrefs.userHash();
}

uint32 AppNatEnv::ourClientID() const
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() ? sc->clientID() : 0;
}

uint16 AppNatEnv::ourUdpPort() const
{
    return theApp.clientUDP ? theApp.advertisedUdpPort() : uint16{0};
}

uint16 AppNatEnv::ourTcpPort() const
{
    return theApp.advertisedTcpPort();
}

uint8 AppNatEnv::ourConnectOptions() const
{
    return cryptBits();
}

uint32 AppNatEnv::ourPublicIPv4() const
{
    return theApp.publicIP();
}

Address AppNatEnv::ourPublicIPv6() const
{
    return theApp.hasConfidentPublicIPv6() ? theApp.publicIPv6() : Address();
}

bool AppNatEnv::firewalled() const
{
    return theApp.isFirewalled();
}

bool AppNatEnv::kadConnected() const
{
    auto* kad = kad::Kademlia::instance();
    return kad && kad->isConnected();
}

bool AppNatEnv::enabled() const
{
    return thePrefs.natTraversal();
}

const Server* AppNatEnv::currentServer() const
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() ? sc->currentServer() : nullptr;
}

Address AppNatEnv::serverSessionAddress() const
{
    return theApp.serverConnect ? theApp.serverConnect->sessionAddress() : Address();
}

std::vector<Endpoint> AppNatEnv::rendezvousServers(const PeerInfo& peer) const
{
    std::vector<Endpoint> out;
    if (!theApp.serverList)
        return out;
    const Server* cur = currentServer();
    const uint32 deadRetries = thePrefs.deadServerRetries();
    const bool serverKnown = peer.serverPort != 0 || !peer.serverAddress.isNull();
    for (const auto& s : theApp.serverList->servers()) {
        if (!s->supportsNatRendezvousUDP() || s->isDeadFor(deadRetries) || s->port() == 0)
            continue;
        if (cur && s->port() == cur->port() && cur->hasAddress(s->ipAddress()))
            continue;
        const Address v4 = s->ipv4Address();
        if (!v4.isIPv4() || !isGoodIP(v4))
            continue;
        // eNode-go takes PR_NAT on its main UDP port too, the one we know without a login.
        const Endpoint ep(v4, s->natPort() != 0 ? s->natPort() : static_cast<uint16>(s->port() + 4));
        if (!serverKnown) {
            out.push_back(ep);
            continue;
        }
        // An IPv6 session stamps no server address on its sources: match the port alone.
        if (peer.serverPort == s->port()
            && (peer.serverAddress.isNull() || s->hasAddress(peer.serverAddress)))
            return {ep};
    }
    return serverKnown ? std::vector<Endpoint>{} : out;
}

bool AppNatEnv::admit(const Address& addr) const
{
    return admitAddress(addr);
}

void AppNatEnv::sendPeerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, const uint8* cryptHash)
{
    if (!theApp.clientUDP)
        return;
    // The client key mixes in our public IP of the destination's family.
    const bool haveIP = to.address().isIPv6() ? !theApp.publicIPv6().isNull() : theApp.publicIP() != 0;
    const bool encrypt = cryptHash && thePrefs.cryptLayerSupported() && haveIP;
    if (!encrypt && packet->prot == OP_UDPRESERVEDPROT2 && thePrefs.natTraversalStrictObfuscation())
        return;   // strict: no plain 0xB2 frame
    theApp.clientUDP->sendPacket(std::move(packet), to, encrypt, cryptHash, false, 0);
}

void AppNatEnv::sendServerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, uint32 serverKey)
{
    if (theApp.clientUDP)
        theApp.clientUDP->sendServerPacket(std::move(packet), to,
                                           thePrefs.cryptLayerSupported() ? serverKey : 0u);
}

ServerUdpCrypt AppNatEnv::serverUdpCrypt() const
{
    const Server* cur = currentServer();
    if (!cur)
        return {};
    const Server* listed = theApp.serverList
        ? theApp.serverList->findByIPTcp(cur->ipAddress(), cur->port()) : nullptr;
    const Server& s = listed ? *listed : *cur;
    return {s.serverKeyUDP(), s.obfuscationPortUDP()};
}

void AppNatEnv::refreshServerKey()
{
    if (const Server* server = currentServer(); server && theApp.serverList)
        theApp.serverList->requestStatPing(*server);
}

void AppNatEnv::sendRawUdp(const QByteArray& data, const Endpoint& to)
{
    if (theApp.clientUDP)
        theApp.clientUDP->sendRawDatagram(data, to);
}

bool AppNatEnv::sendServerTcp(std::unique_ptr<Packet> packet)
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() && sc->sendPacket(std::move(packet));
}

UpDownClient* AppNatEnv::findClient(const Attempt& attempt)
{
    if (attempt.client)
        return attempt.client.data();
    if (attempt.peer.hashKnown && theApp.clientList)
        return theApp.clientList->findByUserHash(attempt.peer.hash.data());
    return nullptr;
}

void AppNatEnv::dialPeer(const Attempt& attempt, const Address& ipv6)
{
    if (!theApp.clientList)
        return;
    UpDownClient* client = findClient(attempt);
    if (!client) {
        if (attempt.role == Role::Requester || !attempt.peer.hashKnown)
            return;   // the source is gone
        // Target: the requester is new to us, as after a server callback (clientID 1 = LowID).
        client = new UpDownClient(attempt.peerTcpPort, 1, 0, 0, nullptr);
        client->setUserHash(attempt.peer.hash.data());
        theApp.clientList->addClient(client);
    }
    if (attempt.peerTcpPort != 0)
        client->setUserPort(attempt.peerTcpPort);
    // Crypt bits from the server (30-byte 0x61, 0x37): obfuscated from the first packet.
    if (attempt.peer.cryptKnown)
        client->setCryptOptions(attempt.peer.cryptOptions);
    client->setUserIPv6(ipv6);
    client->setOpenIPv6(true);
    client->setConnectAddress(ipv6);
    if (client->connectingState() == ConnectingState::NatRendezvous)
        client->setConnectingState(ConnectingState::None);
    // tryToConnect takes the direct branch for an IPv6 target.
    client->tryToConnect(true);
}

bool AppNatEnv::peerConnected(const Attempt& attempt) const
{
    const UpDownClient* client = findClient(attempt);
    return client && client->socket() && client->socket()->isConnected();
}

void AppNatEnv::attemptFailed(const Attempt& attempt)
{
    UpDownClient* client = findClient(attempt);
    if (!client || client->connectingState() != ConnectingState::NatRendezvous)
        return;
    // Kept as a source; PartFile::process() will not revive a failed rendezvous peer.
    client->setConnectingState(ConnectingState::None);
    if (client->downloadState() == DownloadState::Connecting)
        client->setDownloadState(DownloadState::LowToLowIP);
}

bool AppNatEnv::quicAvailable() const
{
    return enabled() && theApp.clientUDP && quic::QuicNatEndpoint::available();
}

void AppNatEnv::armQuic(const Attempt& attempt)
{
    auto* ep = quic();
    if (!ep || attempt.peerUdp.port() == 0)
        return;
    const quic::Hash16 crypt = obfuscationHash(attempt);
    ep->learnPeerHash(attempt.peerUdp, crypt);
    ep->expect(attempt.peerUdp, attempt.peer.hashKnown ? attempt.peer.hash : quic::Hash16{});
    ep->sendCaps(attempt.peerUdp, attempt.peer.hashKnown ? attempt.peer.hash : quic::Hash16{},
                 attempt.peer.fileHash, false);
}

void AppNatEnv::dialPeerQuic(const Attempt& attempt)
{
    auto* ep = quic();
    if (!ep)
        return;
    if (UpDownClient* client = findClient(attempt)) {
        if (attempt.peerTcpPort != 0)
            client->setUserPort(attempt.peerTcpPort);
        // a server source may have no hash yet: the link finds it by address
        m_quicClients[attempt.peerUdp.address()] = client;
    }
    ep->learnPeerHash(attempt.peerUdp, obfuscationHash(attempt));
    if (!ep->dial(attempt.peerUdp, attempt.peer.hashKnown ? attempt.peer.hash : quic::Hash16{}))
        logDebug(QStringLiteral("NAT-T QUIC: cannot dial %1").arg(attempt.peerUdp.toString()));
}

bool AppNatEnv::quicActive(const Attempt& attempt) const
{
    return m_quic && m_quic->hasConnection(attempt.peerUdp.address());
}

bool AppNatEnv::handleQuicFrame(uint8 type, const uint8* data, uint32 size, const Endpoint& from,
                                bool obfuscated)
{
    auto* ep = quic();
    return ep && ep->handleFrame(type, data, size, from, obfuscated);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

quic::QuicNatEndpoint* AppNatEnv::quic() const
{
    if (m_quic)
        return m_quic.get();
    if (!quicAvailable())
        return nullptr;
    m_quicIo = std::make_unique<AppQuicNatIo>();
    m_quic = std::make_unique<quic::QuicNatEndpoint>(*m_quicIo);
    m_quic->onEstablished = [this](std::shared_ptr<quic::QuicNatConnection> conn, bool outgoing) {
        const Endpoint peer = conn->peerEndpoint();
        logDebug(QStringLiteral("NAT-T: QUIC connected %1 (%2)")
                     .arg(peer.toString(), outgoing ? QStringLiteral("out") : QStringLiteral("in")));
        if (!outgoing) {
            // like an accepted TCP connection: the hello finds the known client by hash
            if (theApp.listenSocket)
                theApp.listenSocket->adoptTransport(std::move(conn));
            else
                conn->close();
            return;
        }
        UpDownClient* client = nullptr;
        if (const auto it = m_quicClients.find(peer.address()); it != m_quicClients.end()) {
            client = it->second.data();
            m_quicClients.erase(it);
        }
        if (!client && theApp.clientList)
            client = theApp.clientList->findByUserHash(conn->peerHash().data());
        if (!client && theApp.clientList) {
            // we dialled a requester we did not know (target fallback): as after a callback
            client = new UpDownClient(0, 1, 0, 0, nullptr);
            client->setUserHash(conn->peerHash().data());
            theApp.clientList->addClient(client);
        }
        if (!client) {
            conn->close();
            return;
        }
        if (client->connectingState() == ConnectingState::NatRendezvous)
            client->setConnectingState(ConnectingState::None);
        if (!client->connectOverTransport(conn))
            logDebug(QStringLiteral("NAT-T: QUIC link to %1 not needed, already connected").arg(peer.toString()));
    };
    m_quic->onFailed = [](const Endpoint& peer, const quic::Hash16&, const QString& reason) {
        logDebug(QStringLiteral("NAT-T: QUIC to %1 failed: %2").arg(peer.toString(), reason));
    };
    m_quic->onCaps = [](const Endpoint& peer, const quic::Caps& caps, bool ack) {
        logDebug(QStringLiteral("NAT-T: caps%1 from %2 options 0x%3")
                     .arg(ack ? QStringLiteral(" ack") : QString(), peer.toString())
                     .arg(caps.options, 2, 16, QLatin1Char('0')));
    };
    return m_quic.get();
}

} // namespace eMule::natt
