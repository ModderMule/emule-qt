/// @file tst_NatRendezvous.cpp
/// @brief NAT rendezvous hub: gating, punch, IPv6 handoff, dial rules, timeouts,
///        driven through the ed2k-server path (0x60/0x61/0x62, 0x9F).

#include "NatTestEnv.h"

#include "app/AppContext.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::natt;
using namespace eMule::natt::test;

namespace {

const Address kServerIP = Address::fromString(QStringLiteral("93.184.216.34"));
constexpr uint16 kServerPort = 4661;
const Address kPeerIP = Address::fromString(QStringLiteral("8.8.4.4"));
constexpr uint16 kPeerUdp = 31000;
constexpr uint16 kPeerTcp = 31001;
const Address kPeerV6 = Address::fromString(QStringLiteral("2a00:1450:4001:801::7"));

/// 0x61 payload; @p withId adds the 0.9.84 peer_id field.
QByteArray info(const Address& ip, uint16 tcp, uint16 udp, const Hash16& hash, uint32 peerId, bool withId = true)
{
    QByteArray b(withId ? 29 : 25, '\0');
    auto* p = reinterpret_cast<uint8*>(b.data());
    pokeUInt32(p, ip.toNetworkUint32());
    pokeUInt16(p + 4, tcp);
    pokeUInt16(p + 6, udp);
    std::memcpy(p + 8, hash.data(), 16);
    p[24] = 0;
    if (withId)
        pokeUInt32(p + 25, peerId);
    return b;
}

constexpr uint16 kObfPort = 4675;
constexpr uint32 kServerKey = 0x11223344;

/// 30-byte 0x61 of an extension pair.
QByteArray infoExt(const Hash16& hash, uint32 peerId, uint8 role, uint8 peerCrypt)
{
    QByteArray b = info(kPeerIP, kPeerTcp, kPeerUdp, hash, peerId);
    b[24] = char(role);
    b.append(char(peerCrypt));
    return b;
}

/// Lugdunum's 0x37 body: <ip 4><port 2><id 4><flags 1>
QByteArray wakeUp(uint32 requesterId, uint8 flags)
{
    QByteArray b(11, '\0');
    auto* p = reinterpret_cast<uint8*>(b.data());
    pokeUInt32(p, kPeerIP.toNetworkUint32());
    pokeUInt16(p + 4, kPeerUdp);
    pokeUInt32(p + 6, requesterId);
    p[10] = flags;
    return b;
}

/// ed2k-server 0.9.85 with the extension; we announced 0x100000 | 0x1000.
Server& logInExtension(HubFixture& f, bool withKey = true)
{
    // the server key is valid only for a known public IP
    theApp.setPublicIP(Address::fromString(QStringLiteral("198.51.100.20")).toNetworkUint32());
    Server& s = f.env->logIn(kServerIP, kServerPort,
                             SrvTcpFlag::LowIdHolePunch | SrvTcpFlag::LugdunumNat | SrvTcpFlag::HolePunchExtension);
    s.setIdChangeLength(24);
    s.setObfuscationPortUDP(kObfPort);
    s.setUDPFlags(SrvUdpFlag::UdpObfuscation);
    if (withKey)
        s.setServerKeyUDP(kServerKey);
    return s;
}

} // namespace

class tst_NatRendezvous : public QObject {
    Q_OBJECT

private slots:
    void canStart_gates();
    void start_sendsHolePunchRequest();
    void pathOrder_prefersSameServerPaths();
    void info_startsPunchAndHandoff();
    void info_withoutPeerId_matchesSingleRequest();
    void info_unsolicited_targetDialsOnHandoff();
    void requester_dialsAfterDelay();
    void handoff_fromStrangerOrWrongHash_dropped();
    void handoff_withoutIPv6_failsAndMarks();
    void attempt_timesOut();
    void holePunch_updatesPort();
    void notice_rateCappedPerIp();
    void notice_bannedEndpoint_dropped();
    void fail_0x62_endsRequest();
    void keepalive_0x9F_every60s();
    void connected_finishesAttempt();
    void info_repeatAfterConnect_ignored();
    // hole-punch extension (ed2k-server#39)
    void ext_keepaliveObfuscatedOnceKeyKnown();
    void ext_requestCarriesKeepalive();
    void ext_wakeUp_burstsAndWaitsForInfo();
    void ext_info_roleDecidesDialer();
    void ext_noInfo_fallsBackToLugdunum();
    void ext_peerCrypt_obfuscatesOrFails();
    // QUIC NAT-T
    void quic_requesterDialsWithoutHandoff();
    void quic_targetWaitsThenFallsBack();
    void quic_ipv6HandoffWins();
    void quic_framesGoToTransport();
};

void tst_NatRendezvous::canStart_gates()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->canStart(peer));

    f.env->isEnabled = false;
    QVERIFY(!f.hub->canStart(peer));
    f.env->isEnabled = true;

    f.env->isFirewalled = false;            // we are reachable: a plain callback does it
    QVERIFY(!f.hub->canStart(peer));
    f.env->isFirewalled = true;

    f.env->publicIPv6 = Address();           // no data path yet without IPv6
    QVERIFY(!f.hub->canStart(peer));
    f.env->publicIPv6 = Address::fromString(QStringLiteral("2a00:1450:4001:800::1"));

    PeerInfo other = peer;
    other.serverAddress = Address::fromString(QStringLiteral("1.2.3.4"));
    QVERIFY(!f.hub->canStart(other));        // not on our server, no buddy

    PeerInfo high = peer;
    high.clientID = 0x05060708;              // HighID
    QVERIFY(!f.hub->canStart(high));

    f.env->server->setTCPFlags(0);           // server does not coordinate
    QVERIFY(!f.hub->canStart(peer));
}

void tst_NatRendezvous::start_sendsHolePunchRequest()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    QVERIFY(f.hub->start(f.peerOnServer(77)));
    QCOMPARE(f.env->tcp.size(), size_t{1});
    QCOMPARE(f.env->tcp[0].opcode, uint8{OP_LOWID_HOLEPUNCH_REQUEST});
    QCOMPARE(f.env->tcp[0].payload.size(), 6);
    QCOMPARE(peekUInt32(bytes(f.env->tcp[0].payload)), 77u);
    QCOMPARE(peekUInt16(bytes(f.env->tcp[0].payload) + 4), f.env->udpPort);

    // joining an active attempt sends nothing new
    QVERIFY(f.hub->start(f.peerOnServer(77)));
    QCOMPARE(f.env->tcp.size(), size_t{1});

    // re-asked every 10 s while nothing came back, at most 3 times
    for (int i = 0; i < 4; ++i)
        f.advance(10000);
    QCOMPARE(f.env->tcp.size(), size_t{3});
}

void tst_NatRendezvous::pathOrder_prefersSameServerPaths()
{
    {
        HubFixture f;
        f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch | SrvTcpFlag::NatRendezvous);
        QVERIFY(f.hub->start(f.peerOnServer(77)));
        QCOMPARE(f.hub->attempts().front()->path, PathKind::Ed2kServer);
    }
    {
        HubFixture f;
        f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
        QVERIFY(!f.hub->canStart(f.peerOnServer(77, false)));   // PR_NAT pairs by hash
        QVERIFY(f.hub->start(f.peerOnServer(77)));
        QCOMPARE(f.hub->attempts().front()->path, PathKind::EnodeNat);
    }
    {
        HubFixture f;
        f.env->isKadConnected = true;
        PeerInfo p;
        p.hashKnown = true;
        p.hash.fill(0x42);
        p.clientID = 1;
        p.buddyValid = true;
        p.buddyEndpoint = Endpoint(Address::fromString(QStringLiteral("9.9.9.9")), 4672);
        QVERIFY(f.hub->start(p));
        QCOMPARE(f.hub->attempts().front()->path, PathKind::KadBuddy);
    }
}

void tst_NatRendezvous::info_startsPunchAndHandoff()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QSignalSpy ready(f.hub.get(), &NatRendezvous::peerEndpointReady);
    QVERIFY(f.hub->start(peer));

    QVERIFY(f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77)));
    QCOMPARE(ready.count(), 1);
    const Endpoint peerEp(kPeerIP, kPeerUdp);
    QCOMPARE(ready.first().at(1).value<Endpoint>(), peerEp);

    const auto punches = f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH);
    QCOMPARE(punches.size(), size_t{1});
    QCOMPARE(punches[0].to, peerEp);
    QVERIFY(punches[0].payload.isEmpty());

    const auto handoffs = f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF);
    QCOMPARE(handoffs.size(), size_t{1});
    const QByteArray& h = handoffs[0].payload;
    QCOMPARE(h.size(), 41);
    QCOMPARE(uint8(h[0]), uint8{1});
    QCOMPARE(uint8(h[1]), uint8{0x01});                             // has IPv6, not an answer
    QCOMPARE(peekUInt32(bytes(h) + 2), f.env->clientID);
    QCOMPARE(std::memcmp(h.constData() + 6, f.env->hash.data(), 16), 0);
    QCOMPARE(peekUInt16(bytes(h) + 23), f.env->tcpPort);
    QCOMPARE(Address::fromIPv6Bytes(bytes(h) + 25), f.env->publicIPv6);
    QVERIFY(handoffs[0].encrypted);                                  // keyed on the peer hash
    QVERIFY(handoffs[0].cryptHash == peer.hash);

    // punch every second for the window, handoff every 2 s until answered
    f.advance(1000);
    f.advance(1000);
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).size(), size_t{3});
    QCOMPARE(f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF).size(), size_t{2});
}

void tst_NatRendezvous::info_withoutPeerId_matchesSingleRequest()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    PeerInfo peer = f.peerOnServer(77, false);   // server source without hash
    QVERIFY(f.hub->start(peer));
    Hash16 hash{};
    hash.fill(0x5A);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, hash, 0, false));
    const auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Requester);
    QVERIFY(list[0]->peer.hashKnown);
    QVERIFY(list[0]->peer.hash == hash);
    QCOMPARE(list[0]->state, Attempt::State::Punching);
}

void tst_NatRendezvous::info_unsolicited_targetDialsOnHandoff()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    Hash16 hash{};
    hash.fill(0x33);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, hash, 55));
    auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Target);
    QCOMPARE(list[0]->peerTcpPort, kPeerTcp);

    const QByteArray h = handoff(hash, 55, kPeerTcp, kPeerV6);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);

    // the target is the connector: it dials right away, and answers the handoff
    QCOMPARE(f.env->dials.size(), size_t{1});
    QCOMPARE(f.env->dials[0].second, kPeerV6);
    const auto handoffs = f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF);
    QCOMPARE(handoffs.size(), size_t{2});
    QCOMPARE(uint8(handoffs.back().payload[1]) & 0x02, 0x02);
}

void tst_NatRendezvous::requester_dialsAfterDelay()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    const QByteArray h = handoff(peer.hash, 77, kPeerTcp, kPeerV6, true);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(f.env->dials.empty());
    // an answer is not answered again
    QCOMPARE(f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF).size(), size_t{1});

    f.advance(1000);
    f.advance(3000);
    QVERIFY(f.env->dials.empty());
    f.advance(1000);                          // 5 s after the handoff
    QCOMPARE(f.env->dials.size(), size_t{1});
    QCOMPARE(f.env->dials[0].second, kPeerV6);

    // the target got there first: no dial
    HubFixture g;
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    QVERIFY(g.hub->start(peer));
    g.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    g.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    g.env->connected = true;
    g.advance(1000);
    g.advance(6000);
    QVERIFY(g.env->dials.empty());
    QVERIFY(g.hub->attempts().empty());
}

void tst_NatRendezvous::handoff_fromStrangerOrWrongHash_dropped()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));

    QByteArray h = handoff(peer.hash, 77, kPeerTcp, kPeerV6);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()),
                       Endpoint(Address::fromString(QStringLiteral("8.8.8.8")), kPeerUdp), true);
    QVERIFY(!f.hub->attempts().front()->peerHandoffSeen);

    Hash16 other{};
    other.fill(0x99);
    h = handoff(other, 77, kPeerTcp, kPeerV6);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(!f.hub->attempts().front()->peerHandoffSeen);

    // truncated
    h = handoff(peer.hash, 77, kPeerTcp).left(20);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(!f.hub->attempts().front()->peerHandoffSeen);

    // filtered address in the handoff: seen, but nothing to dial -> fails
    f.env->banned.insert(kPeerV6.toString());
    h = handoff(peer.hash, 77, kPeerTcp, kPeerV6);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(f.env->dials.empty());
    QCOMPARE(f.env->failed.size(), size_t{1});
}

void tst_NatRendezvous::handoff_withoutIPv6_failsAndMarks()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QSignalSpy finished(f.hub.get(), &NatRendezvous::attemptFinished);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    const QByteArray h = handoff(peer.hash, 77, kPeerTcp);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), false);
    QCOMPARE(f.env->failed.size(), size_t{1});
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(1).toBool(), false);
    QVERIFY(f.hub->hasFailed(peer));
    f.advance(1000);
    QVERIFY(!f.hub->canStart(peer));
    QVERIFY(!f.hub->mayKeepLowIdSource(peer));
}

void tst_NatRendezvous::attempt_timesOut()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.advance(NatRendezvous::kAttemptTimeoutMs - 1000);
    QVERIFY(f.env->failed.empty());
    f.advance(1000);
    QCOMPARE(f.env->failed.size(), size_t{1});
    QVERIFY(f.hub->attempts().empty());
}

void tst_NatRendezvous::holePunch_updatesPort()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    f.hub->handlePeerPacket(OP_HOLEPUNCH, nullptr, 0, Endpoint(kPeerIP, 40000));
    QCOMPARE(f.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, 40000));
    f.advance(1000);
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).back().to, Endpoint(kPeerIP, 40000));
    // a stranger's punch changes nothing
    f.hub->handlePeerPacket(OP_HOLEPUNCH, nullptr, 0, Endpoint(Address::fromString(QStringLiteral("1.1.1.1")), 5));
    QCOMPARE(f.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, 40000));
}

void tst_NatRendezvous::notice_rateCappedPerIp()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    Hash16 a{}, b{};
    a.fill(1);
    b.fill(2);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, a, 55));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp + 1, b, 56));
    QCOMPARE(f.hub->attempts().size(), size_t{1});
    f.advance(NatRendezvous::kNoticePerIpMs);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp + 1, b, 56));
    QCOMPARE(f.hub->attempts().size(), size_t{2});
}

void tst_NatRendezvous::notice_bannedEndpoint_dropped()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    f.env->banned.insert(kPeerIP.toString());
    Hash16 a{};
    a.fill(1);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, a, 55));
    QVERIFY(f.hub->attempts().empty());
    QVERIFY(f.env->udp.empty());

    f.env->isEnabled = false;
    f.env->banned.clear();
    QVERIFY(!f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, a, 55)));
    QVERIFY(f.hub->attempts().empty());
}

void tst_NatRendezvous::fail_0x62_endsRequest()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    QVERIFY(f.hub->start(f.peerOnServer(77)));
    QByteArray fail(5, '\0');
    pokeUInt32(fail.data(), 78);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_FAIL, fail);
    QVERIFY(f.env->failed.empty());          // other target
    pokeUInt32(fail.data(), 77);
    fail[4] = 2;
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_FAIL, fail);
    QCOMPARE(f.env->failed.size(), size_t{1});
}

void tst_NatRendezvous::keepalive_0x9F_every60s()
{
    HubFixture f;
    f.hub->process();
    QVERIFY(f.env->udp.empty());              // offline
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    f.hub->process();
    auto ka = f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE);
    QCOMPARE(ka.size(), size_t{1});
    QCOMPARE(ka[0].kind, SentUdp::Kind::Server);
    QCOMPARE(ka[0].serverKey, 0u);           // plain
    QCOMPARE(ka[0].to, Endpoint(kServerIP, kServerPort + 4));
    QCOMPARE(ka[0].payload, QByteArray(reinterpret_cast<const char*>(f.env->hash.data()), 16));
    f.advance(59000);
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{1});
    f.advance(1000);
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{2});

    // not for a server without the flag
    HubFixture g;
    g.env->logIn(kServerIP, kServerPort, 0);
    g.hub->process();
    QVERIFY(g.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).empty());
}

void tst_NatRendezvous::connected_finishesAttempt()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QSignalSpy finished(f.hub.get(), &NatRendezvous::attemptFinished);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    f.env->connected = true;
    f.advance(1000);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(1).toBool(), true);
    QVERIFY(f.hub->attempts().empty());
    QVERIFY(!f.hub->hasFailed(peer));
}

// ed2k-server re-sends 0x61 for a few s; a copy after the attempt ended must not start a
// target attempt (seen live: requester turned target and dialed its own source again).
void tst_NatRendezvous::info_repeatAfterConnect_ignored()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    f.env->connected = true;
    f.advance(1000);
    QVERIFY(f.hub->attempts().empty());

    f.env->connected = false;
    f.advance(1000);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    QVERIFY(f.hub->attempts().empty());

    // A fresh request from the peer later is served again.
    f.advance(NatRendezvous::kRepeatNoticeMs);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    QCOMPARE(f.hub->attempts().size(), size_t{1});
    QCOMPARE(f.hub->attempts().front()->role, Role::Target);
}

// ---------------------------------------------------------------------------
// Hole-punch extension (ed2k-server#39)
// ---------------------------------------------------------------------------

void tst_NatRendezvous::ext_keepaliveObfuscatedOnceKeyKnown()
{
    // no key yet: plain, as on 0.9.84
    HubFixture f;
    Server& s = logInExtension(f, false);
    f.hub->process();
    auto ka = f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE);
    QCOMPARE(ka.size(), size_t{1});
    QCOMPARE(ka[0].serverKey, 0u);
    QCOMPARE(ka[0].to, Endpoint(kServerIP, kServerPort + 4));

    // key known: obfuscated to the obfuscation port
    s.setServerKeyUDP(kServerKey);
    f.advance(60000);
    ka = f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE);
    QCOMPARE(ka.size(), size_t{2});
    QCOMPARE(ka[1].serverKey, kServerKey);
    QCOMPARE(ka[1].to, Endpoint(kServerIP, kObfPort));

    // key lost: no plain one (the server would ignore it), ask for a fresh key
    s.setServerKeyUDP(0);
    const int refreshes = f.env->keyRefreshes;
    f.advance(60000);
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{2});
    QVERIFY(f.env->keyRefreshes > refreshes);

    // a 0.9.84 server gets it plain even with a key
    HubFixture g;
    theApp.setPublicIP(Address::fromString(QStringLiteral("198.51.100.20")).toNetworkUint32());
    Server& old = g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    old.setObfuscationPortUDP(kObfPort);
    old.setServerKeyUDP(kServerKey);
    g.hub->process();
    ka = g.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE);
    QCOMPARE(ka.size(), size_t{1});
    QCOMPARE(ka[0].serverKey, 0u);
}

void tst_NatRendezvous::ext_requestCarriesKeepalive()
{
    HubFixture f;
    logInExtension(f);
    f.hub->process();
    f.env->udp.clear();
    QVERIFY(f.hub->start(f.peerOnServer(78)));     // even: ed2k-server path only
    QCOMPARE(f.env->tcp.size(), size_t{1});
    auto ka = f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE);
    QCOMPARE(ka.size(), size_t{1});
    QCOMPARE(ka[0].serverKey, kServerKey);
    f.advance(10000);                              // repeat 0x60, again with a 0x9F
    QCOMPARE(f.env->tcp.size(), size_t{2});
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{2});

    // not without the extension
    HubFixture g;
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    g.hub->process();
    g.env->udp.clear();
    QVERIFY(g.hub->start(g.peerOnServer(78)));
    QVERIFY(g.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).empty());
}

void tst_NatRendezvous::ext_wakeUp_burstsAndWaitsForInfo()
{
    HubFixture f;
    logInExtension(f);
    f.hub->process();
    f.env->udp.clear();

    QVERIFY(f.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, wakeUp(77, CONNECT_OPT_CRYPT_SUPPORTED)));
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());   // no reciprocal 0xA6
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).size(), size_t{1});    // punching at once
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{1});
    auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Target);
    QCOMPARE(list[0]->path, PathKind::Ed2kServer);
    QVERIFY(list[0]->awaitingInfo);

    f.env->clock += 499;
    f.hub->processFast();
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{1});
    f.env->clock += 1;
    f.hub->processFast();
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{2});
    f.env->clock += 500;
    f.hub->processFast();
    f.env->clock += 500;
    f.hub->processFast();
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_SERVER_NATT_KEEPALIVE).size(), size_t{3});

    // no dial before 0x61, even with the peer's handoff in
    Hash16 hash{};
    hash.fill(0x44);
    const QByteArray h = handoff(hash, 77, kPeerTcp, kPeerV6);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(f.env->dials.empty());

    // the 0x61 that releases us joins the same attempt
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(hash, 77, 0, CONNECT_OPT_CRYPT_SUPPORTED));
    list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QVERIFY(!list[0]->awaitingInfo);
    QCOMPARE(list[0]->serverRole, int8{0});
    QVERIFY(list[0]->peer.hashKnown);
    f.advance(1000);
    QCOMPARE(f.env->dials.size(), size_t{1});
}

void tst_NatRendezvous::ext_info_roleDecidesDialer()
{
    // requester told to wait: never dials, also not after the usual delay
    {
        HubFixture f;
        logInExtension(f);
        const PeerInfo peer = f.peerOnServer(78);
        QVERIFY(f.hub->start(peer));
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(peer.hash, 78, 1, CONNECT_OPT_CRYPT_SUPPORTED));
        const QByteArray h = handoff(peer.hash, 78, kPeerTcp, kPeerV6);
        f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
        for (int i = 0; i < 10; ++i)
            f.advance(1000);
        QVERIFY(f.env->dials.empty());
    }
    // requester told to dial: at once, no 5 s wait
    {
        HubFixture f;
        logInExtension(f);
        const PeerInfo peer = f.peerOnServer(78);
        QVERIFY(f.hub->start(peer));
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(peer.hash, 78, 0, CONNECT_OPT_CRYPT_SUPPORTED));
        const QByteArray h = handoff(peer.hash, 78, kPeerTcp, kPeerV6, true);
        f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
        QCOMPARE(f.env->dials.size(), size_t{1});
    }
    // 29 bytes: role stays advisory, today's rule (target dials)
    {
        HubFixture f;
        logInExtension(f);
        Hash16 hash{};
        hash.fill(0x33);
        QByteArray b = info(kPeerIP, kPeerTcp, kPeerUdp, hash, 55);
        b[24] = 1;
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, b);
        QCOMPARE(f.hub->attempts().front()->serverRole, int8{-1});
        const QByteArray h = handoff(hash, 55, kPeerTcp, kPeerV6);
        f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
        QCOMPARE(f.env->dials.size(), size_t{1});
    }
}

void tst_NatRendezvous::ext_noInfo_fallsBackToLugdunum()
{
    // a 0x37 from a plain 0xA6 (Lugdunum-only requester): no 0x61 ever comes
    HubFixture f;
    logInExtension(f);
    f.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, wakeUp(77, CONNECT_OPT_CRYPT_SUPPORTED));
    for (int i = 0; i < 3; ++i)
        f.advance(1000);
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());
    f.advance(1000);                               // 4 s
    const auto req = f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST);
    QCOMPARE(req.size(), size_t{1});
    QCOMPARE(peekUInt32(bytes(req[0].payload)), 77u);
    QCOMPARE(req[0].serverKey, kServerKey);
    QCOMPARE(f.hub->attempts().front()->path, PathKind::Lugdunum);
    QVERIFY(!f.hub->attempts().front()->awaitingInfo);
}

void tst_NatRendezvous::ext_peerCrypt_obfuscatesOrFails()
{
    Hash16 hash{};
    hash.fill(0x33);
    // peer supports crypt: punch obfuscated from the first one
    {
        HubFixture f;
        logInExtension(f);
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(hash, 55, 1, CONNECT_OPT_CRYPT_SUPPORTED));
        const auto punch = f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH);
        QCOMPARE(punch.size(), size_t{1});
        QVERIFY(punch[0].encrypted);
        QCOMPARE(punch[0].cryptHash, hash);
        QVERIFY(f.hub->attempts().front()->peer.cryptKnown);
    }
    // peer can't: everything plain, the handoff too
    {
        HubFixture f;
        logInExtension(f);
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(hash, 55, 1, 0));
        QVERIFY(!f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).front().encrypted);
        QVERIFY(!f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF).front().encrypted);
    }
    // we require it, the peer can't: our own request fails
    {
        HubFixture f;
        logInExtension(f);
        f.env->connectOptions = CONNECT_OPT_CRYPT_SUPPORTED | CONNECT_OPT_CRYPT_REQUESTED | CONNECT_OPT_CRYPT_REQUIRED;
        const PeerInfo peer = f.peerOnServer(78);
        QVERIFY(f.hub->start(peer));
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(peer.hash, 78, 0, 0));
        QCOMPARE(f.env->failed.size(), size_t{1});
        QVERIFY(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).empty());
        // and an unsolicited one starts nothing
        f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, infoExt(hash, 55, 1, 0));
        QVERIFY(f.hub->attempts().empty() || f.hub->attempts().front()->finished);
    }
}

void tst_NatRendezvous::quic_requesterDialsWithoutHandoff()
{
    HubFixture f;
    f.env->quic = true;
    f.env->publicIPv6 = Address();
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    QVERIFY(f.hub->hasDataPathNow());
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    QCOMPARE(f.env->quicArms.size(), size_t{1});
    QVERIFY(f.env->quicDials.empty());
    f.advance(1000);                       // the dial time is set on the first tick
    QVERIFY(f.env->quicDials.empty());
    f.advance(1000);
    QCOMPARE(f.env->quicDials.size(), size_t{1});
    QCOMPARE(f.env->quicDials[0].second, Endpoint(kPeerIP, kPeerUdp));
    QVERIFY(f.env->dials.empty());
    // once dialled, never again
    f.advance(1000);
    QCOMPARE(f.env->quicDials.size(), size_t{1});
}

void tst_NatRendezvous::quic_targetWaitsThenFallsBack()
{
    HubFixture f;
    f.env->quic = true;
    f.env->publicIPv6 = Address();
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    Hash16 hash{};
    hash.fill(0x33);
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, hash, 55));
    QCOMPARE(f.hub->attempts().front()->role, Role::Target);
    QCOMPARE(f.env->quicArms.size(), size_t{1});
    f.advance(1000);
    f.advance(5000);
    QVERIFY(f.env->quicDials.empty());     // the downloader dials, not us
    f.advance(1000);
    QCOMPARE(f.env->quicDials.size(), size_t{1});   // nobody came: a peer that cannot dial

    // a link that came in meanwhile stops the fallback
    HubFixture g;
    g.env->quic = true;
    g.env->publicIPv6 = Address();
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    g.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, hash, 55));
    g.env->quicLinkUp = true;
    g.advance(1000);
    g.advance(7000);
    QVERIFY(g.env->quicDials.empty());
}

void tst_NatRendezvous::quic_ipv6HandoffWins()
{
    HubFixture f;
    f.env->quic = true;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    f.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    const QByteArray h = handoff(peer.hash, 77, kPeerTcp, kPeerV6, true);
    f.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h), uint32(h.size()), Endpoint(kPeerIP, kPeerUdp), true);
    for (int i = 0; i < 5; ++i)
        f.advance(1000);
    QCOMPARE(f.env->dials.size(), size_t{1});
    QVERIFY(f.env->quicDials.empty());

    // a handoff without IPv6 no longer fails the attempt: QUIC carries it
    HubFixture g;
    g.env->quic = true;
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LowIdHolePunch);
    QVERIFY(g.hub->start(peer));
    g.hub->handleServerTcp(OP_LOWID_HOLEPUNCH_INFO, info(kPeerIP, kPeerTcp, kPeerUdp, peer.hash, 77));
    const QByteArray h4 = handoff(peer.hash, 77, kPeerTcp, Address(), true);
    g.hub->handleFrame(OP_NATT_FRAME_IPV6HANDOFF, bytes(h4), uint32(h4.size()), Endpoint(kPeerIP, kPeerUdp), true);
    QVERIFY(g.env->failed.empty());
    g.advance(1000);
    g.advance(1000);
    QCOMPARE(g.env->quicDials.size(), size_t{1});
}

void tst_NatRendezvous::quic_framesGoToTransport()
{
    HubFixture f;
    f.env->quic = true;
    const uint8 frame[4] = {0xC3, 0, 0, 1};
    for (uint8 type : {uint8(OP_NATT_FRAME_QUIC), uint8(OP_NATT_FRAME_CAPS), uint8(OP_NATT_FRAME_CAPS_ACK),
                       uint8(OP_NATT_FRAME_KEY)})
        f.hub->handleFrame(type, frame, sizeof(frame), Endpoint(kPeerIP, kPeerUdp), true);
    QCOMPARE(f.env->quicFrames.size(), size_t{4});
}

QTEST_GUILESS_MAIN(tst_NatRendezvous)
#include "tst_NatRendezvous.moc"
