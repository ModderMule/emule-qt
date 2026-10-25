/// @file tst_NatRendezvousPaths.cpp
/// @brief Wire formats of the Lugdunum, eNode-go PR_NAT and Kad buddy rendezvous paths.

#include "NatTestEnv.h"

#include "app/AppContext.h"
#include "crypto/MD5Hash.h"
#include "net/EncryptedDatagramSocket.h"
#include "net/natt/EnodeNatPath.h"
#include "net/natt/KadBuddyNatPath.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

using namespace eMule;
using namespace eMule::natt;
using namespace eMule::natt::test;

namespace {

const Address kServerIP = Address::fromString(QStringLiteral("93.184.216.34"));
constexpr uint16 kServerPort = 4661;
constexpr uint16 kObfPort = 4675;
constexpr uint32 kServerKey = 0x11223344;
const Address kPeerIP = Address::fromString(QStringLiteral("8.8.4.4"));
constexpr uint16 kPeerUdp = 31000;
const Address kBuddyIP = Address::fromString(QStringLiteral("9.9.9.9"));
constexpr uint16 kBuddyPort = 4672;
const Address kOurIP = Address::fromString(QStringLiteral("198.51.100.20"));

/// Lugdunum notice: <ip 4><port 2><id 4><flags 1>[<hash 16>]
QByteArray notice(const Address& ip, uint16 port, uint32 id, uint8 flags, const Hash16* hash = nullptr)
{
    QByteArray b(11, '\0');
    auto* p = reinterpret_cast<uint8*>(b.data());
    pokeUInt32(p, ip.toNetworkUint32());
    pokeUInt16(p + 4, port);
    pokeUInt32(p + 6, id);
    p[10] = flags;
    if (hash)
        b.append(reinterpret_cast<const char*>(hash->data()), 16);
    return b;
}

/// Lugdunum-ready fixture: 24-byte OP_IDCHANGE, flag 0x1000, UDP key + obfuscation port.
void logInLugdunum(HubFixture& f)
{
    theApp.setPublicIP(kOurIP.toNetworkUint32());
    Server& s = f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::LugdunumNat);
    s.setIdChangeLength(24);
    s.setObfuscationPortUDP(kObfPort);
    s.setUDPFlags(SrvUdpFlag::UdpObfuscation);
    s.setServerKeyUDP(kServerKey);
}

QByteArray serverDatagram(uint8 opcode, const QByteArray& payload)
{
    QByteArray b;
    b.append(char(OP_EDONKEYPROT));
    b.append(char(opcode));
    b.append(payload);
    return b;
}

/// Server->client obfuscated frame (magic 0xA5), as tst_EncryptedDatagram builds it.
QByteArray encryptForServer(const QByteArray& plain, uint32 baseKey)
{
    constexpr uint8  kMagicServerClient = 0xA5;
    constexpr uint32 kSyncServer        = 0x13EF24D5u;
    const uint16 randomKeyPart = getRandomUInt16();
    uint8 keyData[7];
    pokeUInt32(keyData, baseKey);
    keyData[4] = kMagicServerClient;
    pokeUInt16(&keyData[5], randomKeyPart);
    MD5Hasher md5(keyData, sizeof keyData);
    RC4Key key = rc4CreateKey({md5.getRawHash(), 16}, true);
    QByteArray frame(3 + 4 + 1 + plain.size(), '\0');
    auto* buf = reinterpret_cast<uint8*>(frame.data());
    buf[0] = 0x01;
    pokeUInt16(&buf[1], randomKeyPart);
    pokeUInt32(&buf[3], kSyncServer);
    buf[7] = 0;
    std::memcpy(&buf[8], plain.constData(), size_t(plain.size()));
    rc4Crypt(&buf[3], uint32(4 + 1 + plain.size()), key);
    return frame;
}

QByteArray natPayload(std::initializer_list<QByteArray> parts)
{
    QByteArray b;
    for (const auto& p : parts)
        b.append(p);
    return b;
}

QByteArray be32(uint32 v)
{
    char b[4];
    qToBigEndian(v, b);
    return {b, 4};
}

QByteArray be16(uint16 v)
{
    char b[2];
    qToBigEndian(v, b);
    return {b, 2};
}

QByteArray hashBytes(const Hash16& h)
{
    return {reinterpret_cast<const char*>(h.data()), 16};
}

} // namespace

class tst_NatRendezvousPaths : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { theApp.setPublicIP(0); }

    // Lugdunum
    void lugdunum_needsLongIdChange();
    void lugdunum_skipsEvenLowId();
    void lugdunum_waitsForServerKey();
    void lugdunum_unansweredRequests_refreshKeyOnce();
    void lugdunum_requestIsObfuscatedToObfPort();
    void lugdunum_tcpNotice_targetAnswersMutually();
    void lugdunum_noticeWithHash();
    void lugdunum_udpNotice_plainAndObfuscated();
    void lugdunum_otherServerDatagram_notConsumed();
    void lugdunum_selfNotice_ignored();
    void lugdunum_skipsIPv6Session();

    // eNode-go: Lugdunum and PR_NAT on one server
    void enode_prefersPrNatWhenHashKnown();
    void enode_timedOutPathFallsThrough();

    // eNode-go PR_NAT
    void prnat_registersAndKeepsAlive();
    void prnat_sync2AndSyncV4();
    void prnat_syncIPv6_andUnsolicited();
    void prnat_ipv6Session();
    void prnat_failed();
    void prnat_badSourceOrFrame();
    void prnat_failedFallsThroughToLugdunum();
    void prnat_reregistersWhenPingsStop();
    void prnat_wake_noStandingRegistration();
    void prnat_wake_registersOnWake();
    void prnat_wake_needsOurLoginBit();
    void prnat_wake_requesterRegistersOtherFamily();

    // Kad buddy
    void kad_requestEnvelopeAndPunches();
    void kad_endpointHint();
    void kad_buddyRelay_sendsHint();
    void kad_rendezvousFromBuddy();
};

// ---------------------------------------------------------------------------
// Lugdunum
// ---------------------------------------------------------------------------

void tst_NatRendezvousPaths::lugdunum_needsLongIdChange()
{
    HubFixture f;
    logInLugdunum(f);
    const PeerInfo peer = f.peerOnServer(77, false);
    QVERIFY(f.hub->canStart(peer));
    f.env->server->setIdChangeLength(16);         // flag alone is not enough
    QVERIFY(!f.hub->canStart(peer));
    f.env->server->setIdChangeLength(24);
    f.env->server->setObfuscationPortUDP(0);      // key and port come later, from a stat ping
    QVERIFY(f.hub->canStart(peer));
}

void tst_NatRendezvousPaths::lugdunum_skipsEvenLowId()
{
    // Even LowID = target never set 0x1000: no 0xA6 for it
    HubFixture f;
    logInLugdunum(f);
    QVERIFY(!f.hub->canStart(f.peerOnServer(78, false)));
    QVERIFY(!f.hub->start(f.peerOnServer(78, false)));
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());
    QVERIFY(f.hub->canStart(f.peerOnServer(79, false)));
}

void tst_NatRendezvousPaths::lugdunum_waitsForServerKey()
{
    // Sources arrive ~0.2 s after login, the stat ping answers seconds later
    HubFixture f;
    logInLugdunum(f);
    f.env->server->setServerKeyUDP(0);
    f.env->server->setObfuscationPortUDP(0);
    QVERIFY(f.hub->start(f.peerOnServer(77, false)));
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());   // never plaintext
    QVERIFY(f.env->keyRefreshes > 0);
    f.env->server->setServerKeyUDP(kServerKey);
    f.env->server->setObfuscationPortUDP(kObfPort);
    f.advance(1000);
    auto req = f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST);
    QCOMPARE(req.size(), size_t{1});
    QCOMPARE(req[0].serverKey, kServerKey);
    QCOMPARE(req[0].to, Endpoint(kServerIP, kObfPort));
}

void tst_NatRendezvousPaths::lugdunum_unansweredRequests_refreshKeyOnce()
{
    // A restarted eserver silently ignores 0xA6 under our old key
    HubFixture f;
    logInLugdunum(f);
    QVERIFY(f.hub->start(f.peerOnServer(77, false)));
    f.advance(5000);
    QCOMPARE(f.env->keyRefreshes, 0);
    f.advance(5000);                               // third request
    QCOMPARE(f.env->keyRefreshes, 1);
    f.advance(5000);
    f.advance(5000);
    QCOMPARE(f.env->keyRefreshes, 1);
}

void tst_NatRendezvousPaths::lugdunum_requestIsObfuscatedToObfPort()
{
    HubFixture f;
    logInLugdunum(f);
    QVERIFY(f.hub->start(f.peerOnServer(77, false)));
    auto req = f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST);
    QCOMPARE(req.size(), size_t{1});
    QCOMPARE(req[0].kind, SentUdp::Kind::Server);
    QCOMPARE(req[0].serverKey, kServerKey);        // never plaintext
    QCOMPARE(req[0].to, Endpoint(kServerIP, kObfPort));
    QCOMPARE(req[0].payload.size(), 8);
    QCOMPARE(peekUInt32(bytes(req[0].payload)), 77u);
    QCOMPARE(peekUInt32(bytes(req[0].payload) + 4), f.env->clientID);
    // repeated every 5 s while unanswered
    f.advance(5000);
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).size(), size_t{2});
}

void tst_NatRendezvousPaths::lugdunum_tcpNotice_targetAnswersMutually()
{
    HubFixture f;
    logInLugdunum(f);
    QVERIFY(f.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, notice(kPeerIP, kPeerUdp, 55, 0x01)));
    auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Target);
    QCOMPARE(list[0]->peer.clientID, 55u);
    QVERIFY(!list[0]->peer.hashKnown);
    QCOMPARE(list[0]->peerUdp, Endpoint(kPeerIP, kPeerUdp));
    // reciprocal 0xA6 naming the requester, so it learns our endpoint
    auto req = f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST);
    QCOMPARE(req.size(), size_t{1});
    QCOMPARE(peekUInt32(bytes(req[0].payload)), 55u);
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).size(), size_t{1});
    // without a hash the handoff goes out plain
    QVERIFY(!f.env->udpWith(OP_UDPRESERVEDPROT2, OP_NATT_FRAME_IPV6HANDOFF).front().encrypted);
}

void tst_NatRendezvousPaths::lugdunum_noticeWithHash()
{
    HubFixture f;
    logInLugdunum(f);
    Hash16 h{};
    h.fill(0x7E);
    f.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, notice(kPeerIP, kPeerUdp, 55, 0x83, &h));
    const auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QVERIFY(list[0]->peer.hashKnown);
    QVERIFY(list[0]->peer.hash == h);
    // truncated notice: ignored
    HubFixture g;
    logInLugdunum(g);
    g.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, notice(kPeerIP, kPeerUdp, 55, 0x01).left(10));
    QVERIFY(g.hub->attempts().empty());
}

void tst_NatRendezvousPaths::lugdunum_udpNotice_plainAndObfuscated()
{
    HubFixture f;
    logInLugdunum(f);
    QVERIFY(f.hub->start(f.peerOnServer(77, false)));
    // the target's reciprocal request reaches us as 0xA7 naming it
    const QByteArray plain = serverDatagram(OP_NAT_CALLBACKREQUESTED_UDP, notice(kPeerIP, kPeerUdp, 77, 0x01));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, kObfPort), bytes(plain), plain.size()));
    QCOMPARE(f.hub->attempts().front()->state, Attempt::State::Punching);
    QCOMPARE(f.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, kPeerUdp));

    HubFixture g;
    logInLugdunum(g);
    QVERIFY(g.hub->start(g.peerOnServer(77, false)));
    const QByteArray enc = encryptForServer(plain, kServerKey);
    QVERIFY(g.hub->handleServerDatagram(Endpoint(kServerIP, kObfPort), bytes(enc), enc.size()));
    QCOMPARE(g.hub->attempts().front()->state, Attempt::State::Punching);
}

void tst_NatRendezvousPaths::lugdunum_otherServerDatagram_notConsumed()
{
    HubFixture f;
    logInLugdunum(f);
    const QByteArray other = encryptForServer(serverDatagram(OP_GLOBSERVSTATRES, QByteArray(12, '\1')), kServerKey);
    const QByteArray before = other;
    QVERIFY(!f.hub->handleServerDatagram(Endpoint(kServerIP, kObfPort), bytes(other), other.size()));
    QCOMPARE(other, before);
    // a 0xA7 from someone else is not a notice
    const QByteArray plain = serverDatagram(OP_NAT_CALLBACKREQUESTED_UDP, notice(kPeerIP, kPeerUdp, 77, 0x01));
    QVERIFY(!f.hub->handleServerDatagram(Endpoint(kPeerIP, kObfPort), bytes(plain), plain.size()));
}

void tst_NatRendezvousPaths::lugdunum_selfNotice_ignored()
{
    HubFixture f;
    logInLugdunum(f);
    f.hub->handleServerTcp(OP_NAT_CALLBACKREQUESTED, notice(kPeerIP, kPeerUdp, f.env->clientID, 0x01));
    QVERIFY(f.hub->attempts().empty());
}

// ---------------------------------------------------------------------------
// eNode-go PR_NAT
// ---------------------------------------------------------------------------

void tst_NatRendezvousPaths::lugdunum_skipsIPv6Session()
{
    // The callback is IPv4 only: on an IPv6 session the server cannot tie our 0xA6 to us
    HubFixture f;
    logInLugdunum(f);
    f.env->sessionAddress = Address::fromString(QStringLiteral("2a00:1450:4001:800::77"));
    QVERIFY(!f.hub->start(f.peerOnServer(77, false)));
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());
}

// ---------------------------------------------------------------------------
// eNode-go: both Lugdunum (0x1000) and PR_NAT (0x40000)
// ---------------------------------------------------------------------------

namespace {

void logInEnode(HubFixture& f)
{
    logInLugdunum(f);
    f.env->server->setTCPFlags(SrvTcpFlag::LugdunumNat | SrvTcpFlag::NatRendezvous);
}

} // namespace

void tst_NatRendezvousPaths::enode_prefersPrNatWhenHashKnown()
{
    HubFixture f;
    logInEnode(f);
    QVERIFY(f.hub->start(f.peerOnServer(77)));                  // hash known: PR_NAT
    QCOMPARE(f.hub->attempts().front()->path, PathKind::EnodeNat);
    QVERIFY(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());

    QVERIFY(f.hub->start(f.peerOnServer(79, false)));           // no hash: only Lugdunum can
    QCOMPARE(f.hub->attempts().back()->path, PathKind::Lugdunum);
    QCOMPARE(f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).size(), size_t{1});
}

void tst_NatRendezvousPaths::enode_timedOutPathFallsThrough()
{
    HubFixture f;
    logInEnode(f);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    QCOMPARE(f.hub->attempts().front()->path, PathKind::EnodeNat);

    f.advance(NatRendezvous::kAttemptTimeoutMs);                // PR_NAT never answered
    QCOMPARE(f.hub->attempts().size(), size_t{1});
    QCOMPARE(f.hub->attempts().front()->path, PathKind::Lugdunum);
    QVERIFY(!f.hub->hasFailed(peer));
    QVERIFY(!f.env->udpWith(OP_EDONKEYPROT, OP_NAT_CALLBACKREQUEST).empty());

    f.advance(NatRendezvous::kAttemptTimeoutMs);                // nothing left to try
    QVERIFY(f.hub->attempts().empty());
    QVERIFY(f.hub->hasFailed(peer));
}

void tst_NatRendezvousPaths::prnat_registersAndKeepsAlive()
{
    HubFixture f;
    Server& s = f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    s.setNatPort(2010);
    f.env->publicIPv6 = Address::fromString(QStringLiteral("2a00:1450:4001:800::1"));
    f.hub->process();
    auto raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{1});              // server has no IPv6: v4 only
    QCOMPARE(raw[0].to, Endpoint(kServerIP, 2010));
    QCOMPARE(raw[0].payload, EnodeNatPath::frame(OP_NAT_REGISTER, hashBytes(f.env->hash)));
    QCOMPARE(uint8(raw[0].payload[0]), uint8{PR_NAT});
    QCOMPARE(peekUInt32(bytes(raw[0].payload) + 1), 17u);

    // re-registered every 5 s until acked
    f.advance(5000);
    QCOMPARE(f.env->raw().size(), size_t{2});
    const QByteArray ack = EnodeNatPath::frame(OP_NAT_REGISTER, natPayload({be16(2010), be32(kServerIP.toUint32())}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, 2010), bytes(ack), ack.size()));
    f.advance(5000);
    QCOMPARE(f.env->raw().size(), size_t{2});
    f.advance(15000);                             // 20 s after the last register
    raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{3});
    QCOMPARE(raw.back().payload, EnodeNatPath::frame(OP_NAT_KEEPALIVE, {}));

    // not registered while reachable
    HubFixture g;
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    g.env->isFirewalled = false;
    g.hub->process();
    QVERIFY(g.env->raw().empty());
}

void tst_NatRendezvousPaths::prnat_sync2AndSyncV4()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);   // no ST_NAT_PORT -> 2004
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    auto raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{1});
    QCOMPARE(raw[0].to, Endpoint(kServerIP, EnodeNatPath::kDefaultPort));
    const QByteArray& sync2 = raw[0].payload;
    QCOMPARE(sync2.size(), 6 + 36);
    QCOMPARE(uint8(sync2[5]), uint8{OP_NAT_SYNC2});
    QCOMPARE(sync2.mid(6, 16), hashBytes(f.env->hash));
    const uint32 connAck = qFromBigEndian<quint32>(sync2.constData() + 22);
    QCOMPARE(connAck, f.hub->attempts().front()->connAck);
    QCOMPARE(sync2.mid(26, 16), hashBytes(peer.hash));

    // SYNC: big-endian endpoint
    const QByteArray sync = EnodeNatPath::frame(OP_NAT_SYNC, natPayload({be32(kPeerIP.toUint32()), be16(kPeerUdp),
                                                                      hashBytes(peer.hash), be32(connAck)}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, EnodeNatPath::kDefaultPort), bytes(sync), sync.size()));
    QCOMPARE(f.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, kPeerUdp));
    QCOMPARE(f.hub->attempts().front()->state, Attempt::State::Punching);
}

void tst_NatRendezvousPaths::prnat_syncIPv6_andUnsolicited()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    Hash16 other{};
    other.fill(0x64);
    const Address peerV6 = Address::fromString(QStringLiteral("2a00:1450:4001:801::9"));
    QByteArray p6 = QByteArray(reinterpret_cast<const char*>(peerV6.ipv6Bytes().data()), 16);
    const QByteArray sync = EnodeNatPath::frame(OP_NAT_SYNC_IPV6,
        natPayload({p6, be16(5555), hashBytes(other), be32(9), QByteArray(1, '\0')}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, kServerPort + 4), bytes(sync), sync.size()));
    const auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Target);
    QCOMPARE(list[0]->peerUdp, Endpoint(peerV6, 5555));
    QCOMPARE(list[0]->peerUdp6, Endpoint(peerV6, 5555));
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).front().to, Endpoint(peerV6, 5555));
}

void tst_NatRendezvousPaths::prnat_ipv6Session()
{
    // Logged in over IPv6: server sources carry no server IPv4, the server copy has no IPv4
    HubFixture f;
    const Address serverV6 = Address::fromString(QStringLiteral("2a00:1450:4001:800::77"));
    f.env->logIn(serverV6, kServerPort, SrvTcpFlag::NatRendezvous);
    f.env->publicIPv6 = Address::fromString(QStringLiteral("2a00:1450:4001:801::1"));
    f.hub->process();
    auto raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{1});              // v6 register only
    QCOMPARE(raw[0].to, Endpoint(serverV6, EnodeNatPath::kDefaultPort));

    PeerInfo peer = f.peerOnServer(77);
    peer.serverAddress = {};                      // DownloadQueue stamps 0 on a v6 session
    QVERIFY(f.hub->mayKeepLowIdSource(peer));
    QVERIFY(f.hub->start(peer));
    raw = f.env->raw();
    QCOMPARE(raw.back().to, Endpoint(serverV6, EnodeNatPath::kDefaultPort));
    QCOMPARE(uint8(raw.back().payload[5]), uint8{OP_NAT_SYNC2});

    // another server's LowID with no address stays out
    peer.serverPort = kServerPort + 1;
    QVERIFY(!f.hub->mayKeepLowIdSource(peer));
}

void tst_NatRendezvousPaths::prnat_failed()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    const QByteArray failed = EnodeNatPath::frame(OP_NAT_FAILED, natPayload({QByteArray(1, '\1'), hashBytes(peer.hash)}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, EnodeNatPath::kDefaultPort), bytes(failed), failed.size()));
    QCOMPARE(f.env->failed.size(), size_t{1});
}

void tst_NatRendezvousPaths::prnat_badSourceOrFrame()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    Hash16 other{};
    other.fill(0x64);
    const QByteArray sync = EnodeNatPath::frame(OP_NAT_SYNC, natPayload({be32(kPeerIP.toUint32()), be16(kPeerUdp),
                                                                      hashBytes(other), be32(1)}));
    // from a peer, not the server: left to the client path
    QVERIFY(!f.hub->handleServerDatagram(Endpoint(kPeerIP, EnodeNatPath::kDefaultPort), bytes(sync), sync.size()));
    // size field longer than the datagram: consumed, ignored
    QByteArray cut = sync.left(20);
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, EnodeNatPath::kDefaultPort), bytes(cut), cut.size()));
    QVERIFY(f.hub->attempts().empty());
    // server without the flag
    f.env->server->setTCPFlags(0);
    QVERIFY(!f.hub->handleServerDatagram(Endpoint(kServerIP, EnodeNatPath::kDefaultPort), bytes(sync), sync.size()));
}

void tst_NatRendezvousPaths::prnat_failedFallsThroughToLugdunum()
{
    // FAILED (e.g. a woken target that never registered) is not the end: Lugdunum next.
    HubFixture f;
    logInEnode(f);
    const PeerInfo peer = f.peerOnServer(77);
    QVERIFY(f.hub->start(peer));
    QCOMPARE(f.hub->attempts().front()->path, PathKind::EnodeNat);
    const QByteArray failed = EnodeNatPath::frame(OP_NAT_FAILED, natPayload({QByteArray(1, '\1'), hashBytes(peer.hash)}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, EnodeNatPath::kDefaultPort), bytes(failed), failed.size()));
    QCOMPARE(f.hub->attempts().front()->path, PathKind::Lugdunum);
    QVERIFY(!f.hub->hasFailed(peer));
    QVERIFY(f.env->failed.empty());
}

void tst_NatRendezvousPaths::prnat_reregistersWhenPingsStop()
{
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous);
    const Endpoint nat(kServerIP, EnodeNatPath::kDefaultPort);
    f.hub->process();
    const QByteArray ack = EnodeNatPath::frame(OP_NAT_REGISTER, natPayload({be16(2004), be32(kServerIP.toUint32())}));
    QVERIFY(f.hub->handleServerDatagram(nat, bytes(ack), ack.size()));
    const QByteArray ping = EnodeNatPath::frame(OP_NAT_PING, {});
    // answered keepalives: no new REGISTER
    for (int i = 0; i < 3; ++i) {
        f.advance(EnodeNatPath::kKeepaliveMs);
        QVERIFY(f.hub->handleServerDatagram(nat, bytes(ping), ping.size()));
    }
    const QByteArray reg = EnodeNatPath::frame(OP_NAT_REGISTER, hashBytes(f.env->hash));
    auto registers = [&] {
        return std::ranges::count_if(f.env->raw(), [&](const auto& r) { return r.payload == reg; });
    };
    QCOMPARE(registers(), 1);
    // the server stops answering: register again after kPingLossMs
    for (uint64 t = 0; t <= EnodeNatPath::kPingLossMs; t += 1000)
        f.advance(1000);
    QCOMPARE(registers(), 2);
}

void tst_NatRendezvousPaths::prnat_wake_noStandingRegistration()
{
    HubFixture f;
    Server& s = f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous | SrvTcpFlag::NatWake);
    s.setLoginCaps(SRVCAP_NAT_WAKE);
    f.hub->process();
    f.advance(60000);
    QVERIFY(f.env->raw().empty());
}

void tst_NatRendezvousPaths::prnat_wake_registersOnWake()
{
    HubFixture f;
    Server& s = f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatWake);   // wake without 0x40000
    s.setLoginCaps(SRVCAP_NAT_WAKE);
    s.setNatPort(2010);
    QVERIFY(f.hub->handleServerTcp(OP_NAT_WAKE, QByteArray(1, char(EnodeNatPath::kWakeIPv4 | EnodeNatPath::kWakeIPv6))));
    auto raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{1});              // the server has no IPv6: v4 only
    QCOMPARE(raw[0].to, Endpoint(kServerIP, 2010));
    QCOMPARE(raw[0].payload, EnodeNatPath::frame(OP_NAT_REGISTER, hashBytes(f.env->hash)));
    // the SYNC that follows is accepted from the NAT port
    Hash16 other{};
    other.fill(0x64);
    const QByteArray sync = EnodeNatPath::frame(OP_NAT_SYNC, natPayload({be32(kPeerIP.toUint32()), be16(kPeerUdp),
                                                                      hashBytes(other), be32(5)}));
    QVERIFY(f.hub->handleServerDatagram(Endpoint(kServerIP, 2010), bytes(sync), sync.size()));
    QCOMPARE(f.hub->attempts().size(), size_t{1});
    QCOMPARE(f.hub->attempts().front()->role, Role::Target);

    // not a usable target: consumed, nothing sent
    HubFixture g;
    g.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatWake).setLoginCaps(SRVCAP_NAT_WAKE);
    g.env->isFirewalled = false;
    QVERIFY(g.hub->handleServerTcp(OP_NAT_WAKE, QByteArray(1, char(EnodeNatPath::kWakeIPv4))));
    QVERIFY(g.env->raw().empty());
}

void tst_NatRendezvousPaths::prnat_wake_needsOurLoginBit()
{
    // Flag set, but our login did not carry the bit: the server will not wake us.
    HubFixture f;
    f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous | SrvTcpFlag::NatWake);
    QVERIFY(!f.env->server->supportsNatWake());
    f.hub->process();
    QCOMPARE(f.env->raw().size(), size_t{1});
    QCOMPARE(f.env->raw()[0].payload, EnodeNatPath::frame(OP_NAT_REGISTER, hashBytes(f.env->hash)));
}

void tst_NatRendezvousPaths::prnat_wake_requesterRegistersOtherFamily()
{
    HubFixture f;
    const Address serverV6 = Address::fromString(QStringLiteral("2a00:1450:4001:800::77"));
    Server& s = f.env->logIn(kServerIP, kServerPort, SrvTcpFlag::NatRendezvous | SrvTcpFlag::NatWake);
    s.setLoginCaps(SRVCAP_NAT_WAKE);
    s.addAddress(serverV6);
    f.env->publicIPv6 = Address::fromString(QStringLiteral("2a00:1450:4001:801::1"));
    QVERIFY(f.hub->start(f.peerOnServer(77)));
    const auto raw = f.env->raw();
    QCOMPARE(raw.size(), size_t{2});
    QCOMPARE(raw[0].to, Endpoint(kServerIP, EnodeNatPath::kDefaultPort));     // REGISTER over IPv4
    QCOMPARE(raw[0].payload, EnodeNatPath::frame(OP_NAT_REGISTER, hashBytes(f.env->hash)));
    QCOMPARE(raw[1].to, Endpoint(serverV6, EnodeNatPath::kDefaultPort));      // SYNC2 over IPv6
    QCOMPARE(uint8(raw[1].payload[5]), uint8{OP_NAT_SYNC2});
}

// ---------------------------------------------------------------------------
// Kad buddy
// ---------------------------------------------------------------------------

namespace {

PeerInfo kadPeer()
{
    PeerInfo p;
    p.hashKnown = true;
    p.hash.fill(0x42);
    p.clientID = 1;
    p.buddyValid = true;
    p.buddyEndpoint = Endpoint(kBuddyIP, kBuddyPort);
    p.buddyID.fill(0xB0);
    p.kadEndpoint = Endpoint(kPeerIP, kPeerUdp);
    p.fileHash.fill(0xF0);
    return p;
}

} // namespace

void tst_NatRendezvousPaths::kad_requestEnvelopeAndPunches()
{
    HubFixture f;
    f.env->isKadConnected = true;
    f.env->publicIPv4 = kOurIP.toNetworkUint32();
    const PeerInfo peer = kadPeer();
    QVERIFY(f.hub->start(peer));

    const auto punches = f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH);
    QCOMPARE(punches.size(), size_t{KadBuddyNatPath::kInitialPunches});
    QCOMPARE(punches[0].to, peer.kadEndpoint);

    const auto env = f.env->udpWith(OP_EMULEPROT, OP_REASKCALLBACKUDP);
    QCOMPARE(env.size(), size_t{1});
    QCOMPARE(env[0].to, peer.buddyEndpoint);
    QVERIFY(!env[0].encrypted);
    const QByteArray& e = env[0].payload;
    QCOMPARE(e.size(), 73);
    QCOMPARE(e.mid(0, 16), hashBytes(peer.buddyID));
    QCOMPARE(e.mid(16, 16), QByteArray(16, '\0'));
    QCOMPARE(uint8(e[32]), uint8{OP_RENDEZVOUS});
    QCOMPARE(e.mid(33, 16), hashBytes(f.env->hash));
    QCOMPARE(uint8(e[49]) & CONNECT_OPT_NATT_ENDPOINT_HINT, CONNECT_OPT_NATT_ENDPOINT_HINT);
    QCOMPARE(e.mid(50, 16), hashBytes(peer.fileHash));
    QCOMPARE(Address::fromNetworkOrder(peekUInt32(bytes(e) + 66)), kOurIP);
    QCOMPARE(peekUInt16(bytes(e) + 70), f.env->udpPort);
    QCOMPARE(uint8(e[72]), uint8{0});

    // without a known public IPv4 the tail is left off
    HubFixture g;
    g.env->isKadConnected = true;
    QVERIFY(g.hub->start(peer));
    QCOMPARE(g.env->udpWith(OP_EMULEPROT, OP_REASKCALLBACKUDP).front().payload.size(), 66);
}

void tst_NatRendezvousPaths::kad_endpointHint()
{
    HubFixture f;
    f.env->isKadConnected = true;
    const PeerInfo peer = kadPeer();
    QVERIFY(f.hub->start(peer));

    QByteArray hint(56, '\0');
    auto* p = reinterpret_cast<uint8*>(hint.data());
    p[0] = 1;
    std::memcpy(p + 1, peer.hash.data(), 16);
    std::memcpy(p + 17, peer.buddyID.data(), 16);
    std::memcpy(p + 33, peer.fileHash.data(), 16);
    const Address targetIP = Address::fromString(QStringLiteral("8.8.8.8"));
    pokeUInt32(p + 49, targetIP.toNetworkUint32());
    pokeUInt16(p + 53, 4444);

    // from someone other than the buddy: dropped
    f.hub->handlePeerPacket(OP_NATT_ENDPOINT_HINT, p, 56, Endpoint(kPeerIP, kBuddyPort));
    QCOMPARE(f.hub->attempts().front()->state, Attempt::State::Requested);
    // wrong buddy ID: dropped
    QByteArray bad = hint;
    bad[20] = 0;
    f.hub->handlePeerPacket(OP_NATT_ENDPOINT_HINT, bytes(bad), 56, peer.buddyEndpoint);
    QCOMPARE(f.hub->attempts().front()->state, Attempt::State::Requested);

    f.hub->handlePeerPacket(OP_NATT_ENDPOINT_HINT, p, 56, peer.buddyEndpoint);
    QCOMPARE(f.hub->attempts().front()->state, Attempt::State::Punching);
    QCOMPARE(f.hub->attempts().front()->peerUdp, Endpoint(targetIP, 4444));

    // the target's own punch also tells us where it is
    HubFixture g;
    g.env->isKadConnected = true;
    QVERIFY(g.hub->start(peer));
    g.hub->handlePeerPacket(OP_HOLEPUNCH, nullptr, 0, Endpoint(kPeerIP, 40001));
    QCOMPARE(g.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, 40001));
}

void tst_NatRendezvousPaths::kad_buddyRelay_sendsHint()
{
    HubFixture f;
    auto* kad = dynamic_cast<KadBuddyNatPath*>(f.hub->path(PathKind::KadBuddy));
    QVERIFY(kad);
    KadBuddyNatPath::ServedPeer served;
    served.hash.fill(0x55);
    served.udp = Endpoint(kPeerIP, kPeerUdp);
    served.connectOptions = CONNECT_OPT_CRYPT_SUPPORTED;

    QByteArray env(73, '\0');
    auto* e = reinterpret_cast<uint8*>(env.data());
    std::memset(e, 0xB0, 16);
    e[32] = OP_RENDEZVOUS;
    std::memset(e + 33, 0x42, 16);
    e[49] = CONNECT_OPT_NATT_ENDPOINT_HINT;
    std::memset(e + 50, 0xF0, 16);
    const Endpoint requester(Address::fromString(QStringLiteral("1.0.0.1")), 6000);

    kad->onBuddyRelay(requester, e, 73, served);
    const auto hints = f.env->udpWith(OP_EMULEPROT, OP_NATT_ENDPOINT_HINT);
    QCOMPARE(hints.size(), size_t{1});
    QCOMPARE(hints[0].to, requester);
    const QByteArray& h = hints[0].payload;
    QCOMPARE(h.size(), 56);
    QCOMPARE(uint8(h[0]), uint8{1});
    QCOMPARE(h.mid(1, 16), hashBytes(served.hash));
    QCOMPARE(h.mid(17, 16), QByteArray(16, char(0xB0)));
    QCOMPARE(h.mid(33, 16), QByteArray(16, char(0xF0)));
    QCOMPARE(Address::fromNetworkOrder(peekUInt32(bytes(h) + 49)), kPeerIP);
    QCOMPARE(peekUInt16(bytes(h) + 53), kPeerUdp);
    QCOMPARE(uint8(h[55]), uint8{CONNECT_OPT_CRYPT_SUPPORTED});
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).size(), size_t{1});

    // a stock reask (real file hash) or no hint option: nothing extra
    f.env->udp.clear();
    e[49] = 0;
    kad->onBuddyRelay(requester, e, 73, served);
    std::memset(e + 16, 1, 16);
    e[49] = CONNECT_OPT_NATT_ENDPOINT_HINT;
    kad->onBuddyRelay(requester, e, 73, served);
    QVERIFY(f.env->udp.empty());
}

void tst_NatRendezvousPaths::kad_rendezvousFromBuddy()
{
    HubFixture f;
    QByteArray r(33 + 7, '\0');
    auto* p = reinterpret_cast<uint8*>(r.data());
    std::memset(p, 0x42, 16);                       // requester hash
    p[16] = CONNECT_OPT_NATT_ENDPOINT_HINT;
    std::memset(p + 17, 0xF0, 16);                  // file hash
    pokeUInt32(p + 33, kPeerIP.toNetworkUint32());  // self-reported tail
    pokeUInt16(p + 37, 7777);

    const Endpoint seen(Address::fromString(QStringLiteral("8.8.8.8")), 6000);
    QVERIFY(f.hub->handleRendezvousFromBuddy(seen, p, uint32(r.size())));
    auto list = f.hub->attempts();
    QCOMPARE(list.size(), size_t{1});
    QCOMPARE(list[0]->role, Role::Target);
    QCOMPARE(list[0]->path, PathKind::KadBuddy);
    QCOMPARE(list[0]->peerUdp, seen);               // what the buddy saw wins
    QCOMPARE(f.env->udpWith(OP_EMULEPROT, OP_HOLEPUNCH).front().to, seen);

    // a private observed address falls back to the tail
    HubFixture g;
    QVERIFY(g.hub->handleRendezvousFromBuddy(Endpoint(Address::fromString(QStringLiteral("10.0.0.5")), 6000),
                                             p, uint32(r.size())));
    QCOMPARE(g.hub->attempts().front()->peerUdp, Endpoint(kPeerIP, 7777));

    // too short
    HubFixture h;
    QVERIFY(!h.hub->handleRendezvousFromBuddy(seen, p, 20));
}

QTEST_GUILESS_MAIN(tst_NatRendezvousPaths)
#include "tst_NatRendezvousPaths.moc"
