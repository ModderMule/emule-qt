#pragma once
/// @file NatTestEnv.h
/// @brief Fake NatEnv for the NAT rendezvous tests: records every send, fixed clock.

#include "net/Packet.h"
#include "net/natt/NatEnv.h"
#include "net/natt/NatRendezvous.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QByteArray>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace eMule::natt::test {

struct SentUdp {
    enum class Kind { Peer, Server, Raw } kind = Kind::Peer;
    uint8 prot = 0;
    uint8 opcode = 0;
    QByteArray payload;     ///< after prot+opcode (Raw: the whole datagram)
    Endpoint to;
    bool encrypted = false; ///< Peer: crypt hash given
    Hash16 cryptHash{};
    uint32 serverKey = 0;
};

struct SentTcp {
    uint8 opcode = 0;
    QByteArray payload;
};

class FakeNatEnv final : public NatEnv {
public:
    // -- knobs --
    uint64 clock = 1'000'000;
    Hash16 hash{};
    uint32 clientID = 1001;           // a LowID
    uint16 udpPort = 4672;
    uint16 tcpPort = 4662;
    uint32 publicIPv4 = 0;
    uint32 serverSeenIP = 0;          // serverSeenIPv4()
    uint32 udpSeenIP = 0;             // udpSeenIPv4()
    Address publicIPv6 = Address::fromString(QStringLiteral("2a00:1450:4001:800::1"));
    bool isFirewalled = true;
    bool isKadConnected = false;
    bool isEnabled = true;
    int keyRefreshes = 0;             // refreshServerKey() calls
    uint8 connectOptions = CONNECT_OPT_CRYPT_SUPPORTED;
    std::optional<Server> server;
    Address sessionAddress;
    qint64 sessionStartMs = 1;        // server login time (epoch ms) for Server::sessionKeyUDP()
    /// foreignServer(): servers by "ip:tcpPort"; probe() moves Unknown ones to Pending.
    std::map<QString, ForeignServer> foreignServers;
    std::vector<QString> foreignProbes;               // foreignServer(..., probe) that pinged
    std::vector<QString> foreignKeyRefreshes;
    bool dialUnlisted = true;
    std::set<QString> banned;
    bool connected = false;
    bool quic = false;                // quicAvailable()
    bool quicLinkUp = false;          // quicActive()

    // -- records --
    std::vector<SentUdp> udp;
    std::vector<SentTcp> tcp;
    std::vector<std::pair<uint32, Address>> dials;   // attempt id, address
    std::vector<uint32> failed;                      // attempt ids
    std::vector<uint32> quicArms;                    // attempt ids
    std::vector<std::pair<uint32, Endpoint>> quicDials;
    std::vector<uint8> quicFrames;                   // frame types handed over

    FakeNatEnv()
    {
        for (int i = 0; i < 16; ++i)
            hash[static_cast<size_t>(i)] = static_cast<uint8>(0xA0 + i);
    }

    /// Logged in to @p addr:port with @p tcpFlags.
    Server& logIn(const Address& addr, uint16 port, uint32 tcpFlags)
    {
        server.emplace(addr, port);
        server->setTCPFlags(tcpFlags);
        sessionAddress = addr;
        return *server;
    }

    [[nodiscard]] std::vector<SentUdp> udpWith(uint8 prot, uint8 opcode) const
    {
        std::vector<SentUdp> out;
        for (const auto& u : udp) {
            if (u.kind != SentUdp::Kind::Raw && u.prot == prot && u.opcode == opcode)
                out.push_back(u);
        }
        return out;
    }

    [[nodiscard]] std::vector<SentUdp> raw() const
    {
        std::vector<SentUdp> out;
        for (const auto& u : udp) {
            if (u.kind == SentUdp::Kind::Raw)
                out.push_back(u);
        }
        return out;
    }

    // -- NatEnv --
    [[nodiscard]] uint64 now() const override { return clock; }
    [[nodiscard]] Hash16 ourHash() const override { return hash; }
    [[nodiscard]] uint32 ourClientID() const override { return server ? clientID : 0; }
    [[nodiscard]] uint16 ourUdpPort() const override { return udpPort; }
    [[nodiscard]] uint16 ourTcpPort() const override { return tcpPort; }
    [[nodiscard]] uint8 ourConnectOptions() const override { return connectOptions; }
    [[nodiscard]] uint32 ourPublicIPv4() const override { return publicIPv4; }
    [[nodiscard]] uint32 serverSeenIPv4() const override { return serverSeenIP; }
    [[nodiscard]] uint32 udpSeenIPv4() const override { return udpSeenIP; }
    [[nodiscard]] Address ourPublicIPv6() const override { return publicIPv6; }
    [[nodiscard]] bool firewalled() const override { return isFirewalled; }
    [[nodiscard]] bool kadConnected() const override { return isKadConnected; }
    [[nodiscard]] bool enabled() const override { return isEnabled; }
    [[nodiscard]] const Server* currentServer() const override { return server ? &*server : nullptr; }
    [[nodiscard]] Address serverSessionAddress() const override { return sessionAddress; }
    [[nodiscard]] ForeignServer foreignServer(const Address& addr, uint16 port, bool probe) override
    {
        const QString key = QStringLiteral("%1:%2").arg(addr.toString()).arg(port);
        if (auto it = foreignServers.find(key); it != foreignServers.end())
            return it->second;
        ForeignServer out;
        out.ipv4 = addr;
        out.tcpPort = port;
        // unlisted: one probe, then pending until the test says otherwise
        if (probe && dialUnlisted) {
            foreignProbes.push_back(key);
            out.state = ForeignServer::State::Pending;
            foreignServers[key] = out;
        }
        return out;
    }
    void refreshForeignServerKey(const Address& addr, uint16 port) override
    {
        foreignKeyRefreshes.push_back(QStringLiteral("%1:%2").arg(addr.toString()).arg(port));
    }
    /// A server that answered with flag 0x100000 (listed or probed).
    void foreignReady(const Address& addr, uint16 port, uint32 key, uint16 obfPort, bool listed = true)
    {
        ForeignServer fs;
        fs.state = ForeignServer::State::Ready;
        fs.ipv4 = addr;
        fs.tcpPort = port;
        fs.crypt = {key, obfPort};
        fs.listed = listed;
        foreignServers[QStringLiteral("%1:%2").arg(addr.toString()).arg(port)] = fs;
    }
    [[nodiscard]] bool admit(const Address& addr) const override
    {
        return !addr.isNull() && !banned.contains(addr.toString());
    }

    void sendPeerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, const uint8* cryptHash) override
    {
        SentUdp s;
        s.kind = SentUdp::Kind::Peer;
        fill(s, *packet, to);
        if (cryptHash) {
            s.encrypted = true;
            std::memcpy(s.cryptHash.data(), cryptHash, 16);
        }
        udp.push_back(s);
    }
    void sendServerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, uint32 serverKey) override
    {
        SentUdp s;
        s.kind = SentUdp::Kind::Server;
        fill(s, *packet, to);
        s.serverKey = serverKey;
        udp.push_back(s);
    }
    [[nodiscard]] ServerUdpCrypt serverUdpCrypt() const override
    {
        return server ? ServerUdpCrypt{server->sessionKeyUDP(sessionStartMs), server->obfuscationPortUDP()}
                      : ServerUdpCrypt{};
    }
    void refreshServerKey() override { ++keyRefreshes; }
    void sendRawUdp(const QByteArray& data, const Endpoint& to) override
    {
        SentUdp s;
        s.kind = SentUdp::Kind::Raw;
        s.payload = data;
        s.to = to;
        udp.push_back(s);
    }
    bool sendServerTcp(std::unique_ptr<Packet> packet) override
    {
        if (!server)
            return false;
        tcp.push_back({packet->opcode, QByteArray(packet->pBuffer, static_cast<qsizetype>(packet->size))});
        return true;
    }
    void dialPeer(const Attempt& attempt, const Address& ipv6) override { dials.emplace_back(attempt.id, ipv6); }
    [[nodiscard]] bool peerConnected(const Attempt&) const override { return connected; }
    void attemptFailed(const Attempt& attempt) override { failed.push_back(attempt.id); }
    [[nodiscard]] bool quicAvailable() const override { return quic; }
    void armQuic(const Attempt& attempt) override { quicArms.push_back(attempt.id); }
    void dialPeerQuic(const Attempt& attempt) override { quicDials.emplace_back(attempt.id, attempt.quicEndpoint()); }
    [[nodiscard]] bool quicActive(const Attempt&) const override { return quicLinkUp; }
    bool handleQuicFrame(uint8 type, const uint8*, uint32, const Endpoint&, bool) override
    {
        quicFrames.push_back(type);
        return quic;
    }

private:
    static void fill(SentUdp& s, const Packet& p, const Endpoint& to)
    {
        s.prot = p.prot;
        s.opcode = p.opcode;
        s.payload = QByteArray(p.pBuffer, static_cast<qsizetype>(p.size));
        s.to = to;
    }
};

/// Hub over a FakeNatEnv; env() stays reachable for the test.
struct HubFixture {
    FakeNatEnv* env = nullptr;
    std::unique_ptr<NatRendezvous> hub;

    HubFixture()
    {
        auto e = std::make_unique<FakeNatEnv>();
        env = e.get();
        hub = std::make_unique<NatRendezvous>(std::move(e));
    }

    void advance(uint64 ms)
    {
        env->clock += ms;
        hub->process();
    }

    /// A LowID peer on the server we are logged in to.
    [[nodiscard]] PeerInfo peerOnServer(uint32 id, bool withHash = true) const
    {
        PeerInfo p;
        p.clientID = id;
        p.serverAddress = env->server->ipAddress();
        p.serverPort = env->server->port();
        if (withHash) {
            for (int i = 0; i < 16; ++i)
                p.hash[static_cast<size_t>(i)] = static_cast<uint8>(0x10 + i + id);
            p.hashKnown = true;
        }
        return p;
    }
};

/// OP_NATT_FRAME_IPV6HANDOFF payload as a peer would send it.
inline QByteArray handoff(const Hash16& hash, uint32 clientID, uint16 tcpPort,
                          const Address& v6 = {}, bool answer = false)
{
    QByteArray b;
    b.append(char(1));
    b.append(char((v6.isIPv6() ? 0x01 : 0) | (answer ? 0x02 : 0)));
    char id[4];
    pokeUInt32(id, clientID);
    b.append(id, 4);
    b.append(reinterpret_cast<const char*>(hash.data()), 16);
    b.append(char(CONNECT_OPT_CRYPT_SUPPORTED));
    char port[2];
    pokeUInt16(port, tcpPort);
    b.append(port, 2);
    if (v6.isIPv6())
        b.append(reinterpret_cast<const char*>(v6.ipv6Bytes().data()), 16);
    return b;
}

inline const uint8* bytes(const QByteArray& b)
{
    return reinterpret_cast<const uint8*>(b.constData());
}

} // namespace eMule::natt::test
