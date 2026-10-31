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

ForeignServer AppNatEnv::foreignServer(const Address& addr, uint16 tcpPort, bool probe)
{
    ForeignServer out;
    out.ipv4 = addr;
    out.tcpPort = tcpPort;
    auto* list = theApp.serverList;
    if (!list || !addr.isIPv4() || tcpPort == 0)
        return out;
    if (const Server* s = list->findByIPTcp(addr, tcpPort)) {
        out.listed = true;
        if (s->supportsForeignCallbackUDP() && !s->isDeadFor(thePrefs.deadServerRetries())) {
            out.state = ForeignServer::State::Ready;
            out.crypt = {s->serverKeyUDP(), s->obfuscationPortUDP()};
        }
        return out;
    }
    if (!thePrefs.natDialUnlistedServers())
        return out;
    UnlistedServers& unlisted = list->unlisted();
    const uint64 now = getTickCount();
    switch (unlisted.state(addr, tcpPort, now)) {
    case UnlistedServers::State::Pending:
        out.state = ForeignServer::State::Pending;
        return out;
    case UnlistedServers::State::NoAnswer:
        return out;
    case UnlistedServers::State::Answered:
        if (const Server* s = unlisted.find(addr, tcpPort); s && s->supportsForeignCallbackUDP()) {
            out.state = ForeignServer::State::Ready;
            out.crypt = {s->serverKeyUDP(), s->obfuscationPortUDP()};
        }
        return out;
    case UnlistedServers::State::Unknown:
        break;
    }
    // A peer names any address as its server: same filters as for a listed one, never
    // our own address, and the caps in UnlistedServers.
    if (!probe || !ServerList::isGoodServerIP(addr)
        || ServerList::isFilteredServerIP(addr, u"NAT callback server", addr.toString())
        || (theApp.ipFilter && theApp.ipFilter->isFiltered(addr, thePrefs.ipFilterLevel())))
        return out;
    if (const uint32 own = theApp.publicIP(); own != 0 && addr == Address::fromNetworkOrder(own))
        return out;
    if (list->pingUnlisted(addr, tcpPort, now))
        out.state = ForeignServer::State::Pending;
    return out;
}

void AppNatEnv::refreshForeignServerKey(const Address& addr, uint16 tcpPort)
{
    // Listed only: an unlisted server gets its single ping and nothing more.
    if (auto* list = theApp.serverList) {
        if (const Server* s = list->findByIPTcp(addr, tcpPort))
            list->requestStatPing(*s);
    }
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
    const qint64 loginAt = theApp.serverConnect ? theApp.serverConnect->loginAtMs() : 0;
    return {s.sessionKeyUDP(loginAt), s.obfuscationPortUDP()};
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
    if (!ep)
        return;
    const quic::Hash16 crypt = obfuscationHash(attempt);
    const quic::Hash16 hash = attempt.peer.hashKnown ? attempt.peer.hash : quic::Hash16{};
    // the peer may dial from either family the server gave us
    const Endpoint second = attempt.peerUdp6 != attempt.peerUdp ? attempt.peerUdp6 : Endpoint();
    for (const Endpoint& to : {attempt.peerUdp, second}) {
        if (to.port() == 0)
            continue;
        ep->learnPeerHash(to, crypt);
        ep->expect(to, hash);
        ep->sendCaps(to, hash, attempt.peer.fileHash, false);
    }
}

void AppNatEnv::dialPeerQuic(const Attempt& attempt)
{
    auto* ep = quic();
    if (!ep)
        return;
    const Endpoint to = attempt.quicEndpoint();
    if (UpDownClient* client = findClient(attempt)) {
        if (attempt.peerTcpPort != 0)
            client->setUserPort(attempt.peerTcpPort);
        // a server source may have no hash yet: the link finds it by address
        m_quicClients[to.address()] = client;
    }
    ep->learnPeerHash(to, obfuscationHash(attempt));
    if (!ep->dial(to, attempt.peer.hashKnown ? attempt.peer.hash : quic::Hash16{}))
        logDebug(QStringLiteral("NAT-T QUIC: cannot dial %1").arg(to.toString()));
}

bool AppNatEnv::quicActive(const Attempt& attempt) const
{
    return m_quic && m_quic->hasConnection(attempt.quicEndpoint().address());
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
