/// @file tst_NatRendezvous.cpp
/// @brief NAT rendezvous hub: gating, punch, IPv6 handoff, dial rules, timeouts,
///        driven through the ed2k-server path (0x60/0x61/0x62, 0x9F).

#include "NatTestEnv.h"

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

QTEST_GUILESS_MAIN(tst_NatRendezvous)
#include "tst_NatRendezvous.moc"
