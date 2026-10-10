/// @file tst_KadUDPListener.cpp
/// @brief Tests for KadUDPListener.h — Kad UDP packet handler.

#include "TestFixtures.h"
#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadEntry.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadNodeCensus.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>
#include <QtEndian>

using namespace eMule;
using namespace eMule::kad;

class tst_KadUDPListener : public QObject {
    Q_OBJECT

private slots:
    void construct_basic();
    void processPacket_unknownOpcode();
    void sendPacket_emitsSignal();
    void buildWirePacket_packsLargePayloads();
    void sendNullPacket_basic();
    void findNodeIDByIP_queued();
    void expireClientSearch_noRequester();

    // Answers nobody asked for
    void findBuddyRes_unrequestedIsIgnored();
    void firewalledAckRes_countsOnlyAskedNodesOnce();
    void firewalledReq_repeatedMakesOneClient();

    // Search requests we serve
    void searchSourceReq_minimalPacketIsServed();
    void searchNotesReq_minimalPacketIsServed();
    void publishSource_sizeFeedsTheServingFilter();
    void publishSource_countsReachabilityPerSourceType();
    void searchSourceReq_pagingCountsBeforeTheSizeFilter();
    void publishNotes_rejectedNoteGetsNoAnswer();

    // Statistics
    void helloReq_firewalledNodeIsSeenButNotAdded();

    // createSearchExpressionTree
    void searchExprTree_tokenizesStringTerm();
    void searchExprTree_keepsShortAndQuotedTokens();
    void searchExprTree_metaTagIsLowercasedAndKeyed();
    void searchExprTree_rejectsRunawayNesting();
};

void tst_KadUDPListener::construct_basic()
{
    KademliaUDPListener listener;
    // Construction should succeed without crash
    QVERIFY(true);
}

void tst_KadUDPListener::processPacket_unknownOpcode()
{
    KademliaUDPListener listener;

    // Unknown opcode — should not crash, just log and return
    uint8 data[] = {0xFF}; // invalid opcode
    KadUDPKey senderKey(0);
    listener.processPacket(data, sizeof(data), 0x0A000001, 4672, false, senderKey);
    QVERIFY(true); // no crash
}

void tst_KadUDPListener::sendPacket_emitsSignal()
{
    KademliaUDPListener listener;
    // Without a bound socket sendPacket is a no-op — verify no crash.
    SafeMemFile file;
    file.writeUInt8(0x42); // dummy data
    KadUDPKey targetKey(0);
    listener.sendPacket(file, KADEMLIA2_BOOTSTRAP_REQ, 0x0A000001, 4672,
                        targetKey, nullptr);
    QVERIFY(true);
}

void tst_KadUDPListener::buildWirePacket_packsLargePayloads()
{
    // MFC SendPacket: a payload over 200 bytes goes out zlib-packed when that is smaller.
    QByteArray small(1 + 100, 'a');
    small[0] = char(KADEMLIA2_RES);
    auto plain = KademliaUDPListener::buildWirePacket(small);
    QVERIFY(plain);
    QCOMPARE(plain->prot, uint8{OP_KADEMLIAHEADER});
    QCOMPARE(plain->size, uint32{100});

    QByteArray large(1 + 400, 'a');
    large[0] = char(KADEMLIA2_RES);
    auto packed = KademliaUDPListener::buildWirePacket(large);
    QVERIFY(packed);
    QCOMPARE(packed->prot, uint8{OP_KADEMLIAPACKEDPROT});
    QCOMPARE(packed->opcode, uint8{KADEMLIA2_RES});
    QVERIFY(packed->size < 400);

    // what the receive path does with it
    QVERIFY(packed->unPackPacket());
    QCOMPARE(packed->prot, uint8{OP_KADEMLIAHEADER});
    QCOMPARE(QByteArray(packed->pBuffer, packed->size), large.mid(1));

    QVERIFY(!KademliaUDPListener::buildWirePacket({}));
}

void tst_KadUDPListener::sendNullPacket_basic()
{
    KademliaUDPListener listener;
    // Without a bound socket sendNullPacket is a no-op — verify no crash.
    KadUDPKey targetKey(0);
    listener.sendNullPacket(KADEMLIA2_BOOTSTRAP_REQ, 0x0A000001, 4672,
                            targetKey, nullptr);
    QVERIFY(true);
}

void tst_KadUDPListener::findNodeIDByIP_queued()
{
    KademliaUDPListener listener;

    // With null requester, should return false
    bool result = listener.findNodeIDByIP(nullptr, 0x0A000001, 4662, 4672);
    QVERIFY(!result);
}

void tst_KadUDPListener::expireClientSearch_noRequester()
{
    KademliaUDPListener listener;

    // Expire with no requester should not crash
    listener.expireClientSearch(nullptr);
    QVERIFY(true);
}

// ---------------------------------------------------------------------------
// Answers nobody asked for
// ---------------------------------------------------------------------------

namespace {

constexpr uint32 kNodeIP = 0x4D010203;   // 77.1.2.3, host order
constexpr uint16 kNodeUdp = 4672;
constexpr uint16 kNodeTcp = 4662;

/// KADEMLIA_FINDBUDDY_RES as a helper node would send it to us.
QByteArray buddyAnswer()
{
    UInt128 check(Kademlia::getInstancePrefs()->kadId());
    check.xorWith(UInt128(true));
    SafeMemFile io;
    io.writeUInt8(KADEMLIA_FINDBUDDY_RES);
    io::writeUInt128(io, check);
    io::writeUInt128(io, UInt128(uint32{0xB0DD1E}));
    io.writeUInt16(kNodeTcp);
    return io.buffer();
}

void deliver(const QByteArray& packet, uint32 ip)
{
    Kademlia::getInstanceUDPListener()->processPacket(
        reinterpret_cast<const uint8*>(packet.constData()), static_cast<uint32>(packet.size()),
        ip, kNodeUdp, false, KadUDPKey(0));
}

} // namespace

// The check value is our Kad ID inverted, which anyone can compute: without the
// "did we ask" test one datagram made its sender our callback relay.
void tst_KadUDPListener::findBuddyRes_unrequestedIsIgnored()
{
    ClientList clients;
    theApp.clientList = &clients;
    Kademlia::setClientList(&clients);
    eMule::testing::KadFixture kadFixture;

    deliver(buddyAnswer(), kNodeIP);
    QVERIFY(clients.findByConnIP(qToBigEndian(kNodeIP), kNodeTcp) == nullptr);

    // Asked: the same answer is taken, once.
    Kademlia::getInstanceUDPListener()->sendNullPacket(KADEMLIA_FINDBUDDY_REQ, kNodeIP, kNodeUdp,
                                                       KadUDPKey(0), nullptr);
    deliver(buddyAnswer(), kNodeIP);
    UpDownClient* buddy = clients.findByConnIP(qToBigEndian(kNodeIP), kNodeTcp);
    QVERIFY(buddy != nullptr);
    QCOMPARE(buddy->kadState(), KadState::QueuedBuddy);

    clients.deleteAll();
    Kademlia::setClientList(nullptr);
    theApp.clientList = nullptr;
}

// Each request used to allocate a client of its own, with whatever TCP port it named.
void tst_KadUDPListener::firewalledReq_repeatedMakesOneClient()
{
    ClientList clients;
    theApp.clientList = &clients;
    Kademlia::setClientList(&clients);
    eMule::testing::KadFixture kadFixture;

    SafeMemFile io;
    io.writeUInt8(KADEMLIA_FIREWALLED_REQ);
    io.writeUInt16(kNodeTcp);
    deliver(io.buffer(), kNodeIP);
    deliver(io.buffer(), kNodeIP);

    QCOMPARE(clients.clientCount(), 1);
    UpDownClient* probe = clients.findByConnIP(qToBigEndian(kNodeIP), kNodeTcp);
    QVERIFY(probe != nullptr);
    QCOMPARE(probe->kadState(), KadState::QueuedFwCheck);

    clients.deleteAll();
    Kademlia::setClientList(nullptr);
    theApp.clientList = nullptr;
}

// A source request is exactly 26 bytes (MFC KademliaUDPListener.cpp:1093-1101). A
// 32-byte floor dropped every one of them, so stored sources were never handed out.
void tst_KadUDPListener::searchSourceReq_minimalPacketIsServed()
{
    eMule::testing::KadFixture kadFixture;
    auto* indexed = Kademlia::getInstanceIndexed();
    QTRY_VERIFY(indexed->isLoaded());

    const UInt128 fileID(uint32{0x51C0FFEE});
    auto* entry = new Entry();
    entry->m_address = Address::fromHostOrder(0x4D0A0B0C);
    entry->m_tcpPort = 4662;
    entry->m_udpPort = 4672;
    entry->addTag(Tag(uint8{FT_SOURCETYPE}, uint32{1}));
    uint8 load = 0;
    QVERIFY(indexed->addSources(fileID, UInt128(uint32{0x50}), entry, load));

    int answers = 0;
    QObject::connect(Kademlia::getInstanceUDPListener(), &KademliaUDPListener::packetToSend,
                     this, [&answers](const QByteArray& data) {
                         if (!data.isEmpty() && uint8(data[0]) == KADEMLIA2_SEARCH_RES)
                             ++answers;
                     });

    SafeMemFile io;
    io.writeUInt8(KADEMLIA2_SEARCH_SOURCE_REQ);
    io::writeUInt128(io, fileID);
    io.writeUInt16(0);
    io.writeUInt64(0);
    QCOMPARE(io.buffer().size(), qsizetype{27});   // opcode + 26

    // One byte short: MFC's reader throws, we drop it.
    deliver(io.buffer().left(26), kNodeIP);
    QCOMPARE(answers, 0);

    deliver(io.buffer(), kNodeIP);
    QCOMPARE(answers, 1);

    QObject::disconnect(Kademlia::getInstanceUDPListener(), nullptr, this, nullptr);
}

namespace {

constexpr uint32 kLanIP = 0xC0A80105;   // 192.168.1.5: publishes pass the distance check

/// Collects what the listener sends: result counts of SEARCH_RES, number of PUBLISH_RES.
struct SentSpy {
    int results = 0;
    int publishAnswers = 0;
    QMetaObject::Connection conn;

    SentSpy()
    {
        conn = QObject::connect(Kademlia::getInstanceUDPListener(),
                                &KademliaUDPListener::packetToSend,
                                [this](const QByteArray& data) {
            if (data.isEmpty())
                return;
            if (uint8(data[0]) == KADEMLIA2_PUBLISH_RES)
                ++publishAnswers;
            // opcode, sender ID, key ID, count
            if (uint8(data[0]) == KADEMLIA2_SEARCH_RES && data.size() >= 35)
                results += qFromLittleEndian<quint16>(data.constData() + 33);
        });
    }
    ~SentSpy() { QObject::disconnect(conn); }
};

QByteArray publishRequest(uint8 opcode, const UInt128& fileID, const UInt128& sourceID,
                          const std::vector<Tag>& tags)
{
    SafeMemFile io;
    io.writeUInt8(opcode);
    io::writeUInt128(io, fileID);
    io::writeUInt128(io, sourceID);
    io::writeKadTagList(io, tags);
    return io.buffer();
}

QByteArray sourceRequest(const UInt128& fileID, uint16 start, uint64 size)
{
    SafeMemFile io;
    io.writeUInt8(KADEMLIA2_SEARCH_SOURCE_REQ);
    io::writeUInt128(io, fileID);
    io.writeUInt16(start);
    io.writeUInt64(size);
    return io.buffer();
}

} // namespace

// C90: the publisher's size went into the tag list, never into the field the
// serving filter compares (MFC KademliaUDPListener.cpp:1322-1330).
void tst_KadUDPListener::publishSource_sizeFeedsTheServingFilter()
{
    eMule::testing::KadFixture kadFixture;
    QTRY_VERIFY(Kademlia::getInstanceIndexed()->isLoaded());
    const UInt128 fileID(uint32{0x51C0FFE1});
    SentSpy spy;

    deliver(publishRequest(KADEMLIA2_PUBLISH_SOURCE_REQ, fileID, UInt128(uint32{0x50}),
                           {Tag(uint8{FT_SOURCETYPE}, uint32{1}),
                            Tag(uint8{FT_SOURCEPORT}, uint32{4662}),
                            Tag(uint8{FT_FILESIZE}, uint32{1000})}),
            kLanIP);
    QCOMPARE(spy.publishAnswers, 1);

    deliver(sourceRequest(fileID, 0, 2000), kNodeIP);   // another file size
    QCOMPARE(spy.results, 0);
    deliver(sourceRequest(fileID, 0, 1000), kNodeIP);
    QCOMPARE(spy.results, 1);
    deliver(sourceRequest(fileID, 0, 0), kNodeIP);      // size unknown to the asker
    QCOMPARE(spy.results, 2);
}

// Stored source publishes are counted by how the publisher says it is reachable.
void tst_KadUDPListener::publishSource_countsReachabilityPerSourceType()
{
    eMule::testing::ScopedStatistics stats;
    eMule::testing::KadFixture kadFixture;
    QTRY_VERIFY(Kademlia::getInstanceIndexed()->isLoaded());
    const UInt128 fileID(uint32{0x51C0FFE2});
    SentSpy spy;

    uint32 nextSource = 0x60;
    const auto publish = [&](std::vector<Tag> tags) {
        tags.emplace_back(uint8{FT_SOURCEPORT}, uint32{4662});
        // one sender each: the flood limit is per IP
        deliver(publishRequest(KADEMLIA2_PUBLISH_SOURCE_REQ, fileID, UInt128(nextSource), tags),
                kLanIP + (nextSource << 8));
        ++nextSource;
    };
    const Tag buddyIP(uint8{FT_SERVERIP}, uint32{0x0A0B0C0D});

    publish({Tag(uint8{FT_SOURCETYPE}, uint32{1})});
    publish({Tag(uint8{FT_SOURCETYPE}, uint32{4})});
    publish({Tag(uint8{FT_SOURCETYPE}, uint32{3}), buddyIP});
    publish({Tag(uint8{FT_SOURCETYPE}, uint32{5})});            // firewalled, no buddy
    publish({Tag(uint8{FT_SOURCETYPE}, uint32{6})});
    publish({Tag(uint8{FT_SOURCETYPE}, uint32{9})});            // unknown type
    publish({});                                                // not a source: not stored
    QCOMPARE(spy.publishAnswers, 6);

    const KadCounters& kad = stats->kadSession();
    QCOMPARE(kad.sourcesOpen, uint64{2});
    QCOMPARE(kad.sourcesBuddy, uint64{1});
    QCOMPARE(kad.sourcesNoBuddy, uint64{1});
    QCOMPARE(kad.sourcesDirectCallback, uint64{1});
}

// C99: the start position counts stored sources, whatever their size
// (MFC Indexed.cpp:723-728) — else pages overlap for a client that pages.
void tst_KadUDPListener::searchSourceReq_pagingCountsBeforeTheSizeFilter()
{
    eMule::testing::KadFixture kadFixture;
    auto* indexed = Kademlia::getInstanceIndexed();
    QTRY_VERIFY(indexed->isLoaded());
    const UInt128 fileID(uint32{0x51C0FFE2});

    const auto add = [&](uint32 ip, uint32 sourceID, uint64 size) {
        auto* entry = new Entry();
        entry->m_address = Address::fromHostOrder(ip);
        entry->m_tcpPort = 4662;
        entry->m_udpPort = 4672;
        entry->m_size = size;
        entry->addTag(Tag(uint8{FT_SOURCETYPE}, uint32{1}));
        uint8 load = 0;
        return indexed->addSources(fileID, UInt128(sourceID), entry, load);
    };
    // newest first in the list: [2000, 1000]
    QVERIFY(add(0x4D0A0B01, 0x51, 1000));
    QVERIFY(add(0x4D0A0B02, 0x52, 2000));

    SentSpy spy;
    deliver(sourceRequest(fileID, 1, 1000), kNodeIP);
    QCOMPARE(spy.results, 1);
}

// C100: a rejected note (here: no tags) gets no PUBLISH_RES. C90: a note's size
// is a field the serving filter reads (MFC KademliaUDPListener.cpp:1540-1574).
void tst_KadUDPListener::publishNotes_rejectedNoteGetsNoAnswer()
{
    eMule::testing::KadFixture kadFixture;
    QTRY_VERIFY(Kademlia::getInstanceIndexed()->isLoaded());
    const UInt128 fileID(uint32{0x0707E6});
    SentSpy spy;

    deliver(publishRequest(KADEMLIA2_PUBLISH_NOTES_REQ, fileID, UInt128(uint32{0x60}), {}),
            kLanIP);
    QCOMPARE(spy.publishAnswers, 0);

    deliver(publishRequest(KADEMLIA2_PUBLISH_NOTES_REQ, fileID, UInt128(uint32{0x60}),
                           {Tag(uint8{FT_FILENAME}, QStringLiteral("a.avi")),
                            Tag(uint8{FT_FILESIZE}, uint32{1000}),
                            Tag(uint8{FT_FILERATING}, uint32{4})}),
            kLanIP);
    QCOMPARE(spy.publishAnswers, 1);

    // the size is a field now: a request for another size finds nothing
    SafeMemFile io;
    io.writeUInt8(KADEMLIA2_SEARCH_NOTES_REQ);
    io::writeUInt128(io, fileID);
    io.writeUInt64(2000);
    deliver(io.buffer(), kNodeIP);
    QCOMPARE(spy.results, 0);
}

// A notes request is 24 bytes (MFC KademliaUDPListener.cpp:1457-1464).
void tst_KadUDPListener::searchNotesReq_minimalPacketIsServed()
{
    eMule::testing::KadFixture kadFixture;
    auto* indexed = Kademlia::getInstanceIndexed();
    QTRY_VERIFY(indexed->isLoaded());

    const UInt128 fileID(uint32{0x0707E5});
    auto* entry = new Entry();
    entry->m_address = Address::fromHostOrder(0x4D0A0B0C);
    entry->addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("Great file!")));
    uint8 load = 0;
    QVERIFY(indexed->addNotes(fileID, UInt128(uint32{0x60}), entry, load));

    int answers = 0;
    QObject::connect(Kademlia::getInstanceUDPListener(), &KademliaUDPListener::packetToSend,
                     this, [&answers](const QByteArray& data) {
                         if (!data.isEmpty() && uint8(data[0]) == KADEMLIA2_SEARCH_RES)
                             ++answers;
                     });

    SafeMemFile io;
    io.writeUInt8(KADEMLIA2_SEARCH_NOTES_REQ);
    io::writeUInt128(io, fileID);
    io.writeUInt64(0);
    QCOMPARE(io.buffer().size(), qsizetype{25});   // opcode + 24

    deliver(io.buffer().left(24), kNodeIP);
    QCOMPARE(answers, 0);

    deliver(io.buffer(), kNodeIP);
    QCOMPARE(answers, 1);

    QObject::disconnect(Kademlia::getInstanceUDPListener(), nullptr, this, nullptr);
}

// A UDP-firewalled node never enters the routing table, but it did talk to us:
// it belongs in the census and in the firewalled ratio.
void tst_KadUDPListener::helloReq_firewalledNodeIsSeenButNotAdded()
{
    eMule::testing::ScopedStatistics stats;
    KadNodeCensus census;
    theApp.kadNodeCensus = &census;
    {
        eMule::testing::KadFixture kadFixture;

        const auto hello = [](uint32 idSeed, uint32 miscOptions) {
            SafeMemFile io;
            io.writeUInt8(KADEMLIA2_HELLO_REQ);
            io::writeUInt128(io, UInt128(idSeed));
            io.writeUInt16(kNodeTcp);
            io.writeUInt8(KADEMLIA_VERSION);
            io.writeUInt8(1);   // tag count
            io::writeKadTag(io, Tag(FT_KADMISCOPTIONS, miscOptions));
            return io.buffer();
        };

        deliver(hello(0xF1FE, 0x01), kNodeIP);          // UDP firewalled
        deliver(hello(0xF1FE, 0x01), kNodeIP);          // the same node again
        deliver(hello(0x0BE4, 0x00), kNodeIP + 0x100);  // open, another /24

        QCOMPARE(census.contacted(KadNodeCensus::Scope::Session), uint64{2});
        QCOMPARE(stats->kadSession().udpFirewalledNodes, uint64{2});   // per HELLO, as MFC counts
        QCOMPARE(stats->kadSession().udpOpenNodes, uint64{1});
        QCOMPARE(stats->kadSession().tcpOpenNodes, uint64{3});

        auto* zone = Kademlia::getInstanceRoutingZone();
        QVERIFY(zone->getContact(UInt128(uint32{0xF1FE})) == nullptr);
        QVERIFY(zone->getContact(UInt128(uint32{0x0BE4})) != nullptr);
        QCOMPARE(stats->kadSession().contactsAdded, uint64{1});
    }
    theApp.kadNodeCensus = nullptr;
}

void tst_KadUDPListener::firewalledAckRes_countsOnlyAskedNodesOnce()
{
    ClientList clients;
    theApp.clientList = &clients;
    eMule::testing::KadFixture kadFixture;
    KadPrefs* prefs = Kademlia::getInstancePrefs();
    prefs->setFirewalled();   // counter 0
    prefs->incFirewalled();   // one honest witness
    QVERIFY(prefs->firewalled());

    const QByteArray ack(1, static_cast<char>(KADEMLIA_FIREWALLED_ACK_RES));

    // Two forged datagrams used to be enough to look reachable.
    deliver(ack, kNodeIP);
    deliver(ack, kNodeIP);
    QVERIFY(prefs->firewalled());

    // A node we asked counts, but only once.
    prefs->setFirewalled();
    clients.addKadFirewallRequest(qToBigEndian(kNodeIP));
    deliver(ack, kNodeIP);
    deliver(ack, kNodeIP);
    QVERIFY(prefs->firewalled());

    // A second asked node completes it.
    clients.addKadFirewallRequest(qToBigEndian(kNodeIP + 0x10000));
    deliver(ack, kNodeIP + 0x10000);
    QVERIFY(!prefs->firewalled());

    theApp.clientList = nullptr;
}

// ---------------------------------------------------------------------------
// createSearchExpressionTree — decodes the blob a keyword search travels with
// ---------------------------------------------------------------------------

namespace {

/// Encode `01 <u16 len> <utf8>` — a string search term.
QByteArray encodeStringTerm(const QByteArray& s)
{
    QByteArray r;
    r += char(0x01);
    r += char(s.size() & 0xFF);
    r += char((s.size() >> 8) & 0xFF);
    r += s;
    return r;
}

std::unique_ptr<SearchTerm> decode(const QByteArray& blob)
{
    SafeMemFile io(reinterpret_cast<const uint8*>(blob.constData()),
                   static_cast<qint64>(blob.size()));
    return KademliaUDPListener::createSearchExpressionTree(io, 0);
}

} // namespace

void tst_KadUDPListener::searchExprTree_tokenizesStringTerm()
{
    // A string term carries several words and is matched as an AND of all of
    // them. Keeping it as one unsplit string meant it could never match a
    // tokenized file name.
    auto term = decode(encodeStringTerm(QByteArray("Ubuntu Desktop AMD64")));
    QVERIFY(term != nullptr);
    QCOMPARE(term->type, SearchTerm::Type::String);
    QCOMPARE(term->strings.size(), std::size_t{3});
    QCOMPARE(term->strings[0], QStringLiteral("ubuntu"));
    QCOMPARE(term->strings[1], QStringLiteral("desktop"));
    QCOMPARE(term->strings[2], QStringLiteral("amd64"));
}

// MFC keeps every token of a string term (TokenizeOptQuotedSearchTerm). The keyword
// splitter drops words under three bytes, so the term left over from "terminator 2"
// tokenized to nothing and an empty term matches no entry at all.
void tst_KadUDPListener::searchExprTree_keepsShortAndQuotedTokens()
{
    auto shortTerm = decode(encodeStringTerm(QByteArray("2")));
    QVERIFY(shortTerm != nullptr);
    QCOMPARE(shortTerm->strings.size(), std::size_t{1});
    QCOMPARE(shortTerm->strings[0], QStringLiteral("2"));

    // A short last word is not an extension to pop, and repeats are kept.
    auto trailing = decode(encodeStringTerm(QByteArray("mint iso mint")));
    QVERIFY(trailing != nullptr);
    QCOMPARE(trailing->strings.size(), std::size_t{3});
    QCOMPARE(trailing->strings[1], QStringLiteral("iso"));

    // A quoted run is one token, delimiters included.
    auto quoted = decode(encodeStringTerm(QByteArray("\"foo bar\" baz.x")));
    QVERIFY(quoted != nullptr);
    QCOMPARE(quoted->strings.size(), std::size_t{3});
    QCOMPARE(quoted->strings[0], QStringLiteral("foo bar"));
    QCOMPARE(quoted->strings[1], QStringLiteral("baz"));
    QCOMPARE(quoted->strings[2], QStringLiteral("x"));

    // Unterminated quote is skipped, an empty one adds nothing.
    auto open = decode(encodeStringTerm(QByteArray("\"\" \"foo bar")));
    QVERIFY(open != nullptr);
    QCOMPARE(open->strings.size(), std::size_t{2});
    QCOMPARE(open->strings[0], QStringLiteral("foo"));
    QCOMPARE(open->strings[1], QStringLiteral("bar"));
}

void tst_KadUDPListener::searchExprTree_metaTagIsLowercasedAndKeyed()
{
    // 02 <u16 len> <utf8 value> <u16 namelen=1> <tag id>
    QByteArray blob;
    blob += char(0x02);
    blob += char(0x05); blob += char(0x00);
    blob += QByteArray("AuDiO");
    blob += char(0x01); blob += char(0x00);
    blob += char(FT_FILETYPE);

    auto term = decode(blob);
    QVERIFY(term != nullptr);
    QCOMPARE(term->type, SearchTerm::Type::MetaTag);
    // Value lowercased — entry data is compared in lower case.
    QCOMPARE(term->tag.strValue(), QStringLiteral("audio"));
    // A single-byte name is a numeric tag ID, normalized the same way
    // io::readKadTag normalizes stored entry tags. Without this the term keyed
    // on a raw byte-array name that no entry ever carries, so no meta or
    // numeric filter could match.
    QCOMPARE(term->tag.nameId(), uint8{FT_FILETYPE});
}

void tst_KadUDPListener::searchExprTree_rejectsRunawayNesting()
{
    // Deeply nested boolean operators must not blow the stack.
    QByteArray blob;
    for (int i = 0; i < 64; ++i) {
        blob += char(0x00);
        blob += char(0x00); // AND
    }
    QVERIFY(decode(blob) == nullptr);
}

QTEST_GUILESS_MAIN(tst_KadUDPListener)
#include "tst_KadUDPListener.moc"
