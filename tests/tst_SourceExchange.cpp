/// @file tst_SourceExchange.cpp
/// @brief Wire-format tests for eD2K Source Exchange (SX1/SX2).
///
/// These lock down the byte layout produced by KnownFile::createSrcInfoPacket
/// and PartFile::createSrcInfoPacket, and the byte-order rule applied when
/// parsing source records back in PartFile::addClientSources.
///
/// Reference implementation: eMule 0.70b srchybrid/KnownFile.cpp:1113-1150
/// and srchybrid/PartFile.cpp:3836-3900.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "net/Address.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;

namespace {

/// SX2 payload prefix: version byte (1) + file hash (16).
constexpr int kHeaderSize = 1 + 16;

/// Per-source record size for a given SX version.
/// Mirrors the validator official eMule runs on receive
/// (srchybrid/PartFile.cpp:3838-3852) — if our count and our record count
/// disagree, a real peer discards the entire packet.
constexpr int entrySize(uint8 version)
{
    int n = 4 + 2 + 4 + 2;          // userId, port, serverIP, serverPort
    if (version >= 2)
        n += 16;                    // userHash
    if (version >= 4)
        n += 1;                     // cryptOptions
    return n;
}

/// Little-endian read helpers over a raw packet payload.
uint32 readU32(const char* p, int off)
{
    uint32 v;
    std::memcpy(&v, p + off, 4);
    return v;
}

uint16 readU16(const char* p, int off)
{
    uint16 v;
    std::memcpy(&v, p + off, 2);
    return v;
}

/// Build a client that looks like one which completed a hello handshake:
/// userAddress and userIDHybrid both populated and mutually consistent, and in an
/// upload state that makes it eligible to be handed out as a source.
UpDownClient* makeHighIdClient(const QString& ip, uint16 port)
{
    auto* c = new UpDownClient;
    const Address addr = Address::fromString(ip);
    c->setUserAddress(addr);
    c->setUserIDHybrid(addr.toUint32());   // host order — hybrid format
    c->setUserPort(port);
    c->setServerAddress(Address::fromString(QStringLiteral("1.2.3.4")));
    c->setServerPort(4661);
    c->setUploadState(UploadState::Uploading);

    uint8 hash[16];
    std::memset(hash, 0xAB, sizeof(hash));
    c->setUserHash(hash);
    return c;
}

/// A low-ID client: hybrid ID below 2^24, so hasLowID() is true.
UpDownClient* makeLowIdClient(uint32 lowId)
{
    auto* c = new UpDownClient;
    c->setUserAddress(Address::fromString(QStringLiteral("5.6.7.8")));
    c->setUserIDHybrid(lowId);
    c->setUserPort(4662);
    c->setServerAddress(Address::fromString(QStringLiteral("1.2.3.4")));
    c->setServerPort(4661);
    c->setUploadState(UploadState::Uploading);

    uint8 hash[16];
    std::memset(hash, 0xCD, sizeof(hash));
    c->setUserHash(hash);
    return c;
}

/// The peer asking us for sources. Reports no chunk status, which is the
/// "send anything you have" case, and speaks SX2 unless a test says otherwise.
UpDownClient* makeRequester(bool supportsSX2 = true, uint8 sx1Version = 4)
{
    auto* c = makeHighIdClient(QStringLiteral("99.98.97.96"), 4665);
    c->setSupportsSourceExchange2(supportsSX2);
    c->setSourceExchange1Ver(sx1Version);
    return c;
}

void appendU16(QByteArray& b, uint16 v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}

void appendU32(QByteArray& b, uint32 v)
{
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xFF));
}

/// Build the portion of a source answer that addClientSources sees: a declared
/// count followed by `recordCount` records laid out for `version`. Declaring a
/// count that disagrees with the records present is how a corrupt packet is
/// simulated.
QByteArray makeSxBody(uint16 declaredCount, int recordCount, uint8 version)
{
    QByteArray b;
    appendU16(b, declaredCount);
    for (int i = 0; i < recordCount; ++i) {
        appendU32(b, Address::fromString(QStringLiteral("77.66.55.44")).toUint32());
        appendU16(b, uint16(4662 + i));
        appendU32(b, Address::fromString(QStringLiteral("1.2.3.4")).toNetworkUint32());
        appendU16(b, 4661);
        if (version >= 2)
            b.append(16, char(0xEE));
        if (version >= 4)
            b.append(char(0x01));
    }
    return b;
}

/// Feed a body to addClientSources and report how far it read. A rejected packet
/// stops after the 2-byte count; an accepted one consumes every record.
qint64 parseAndReportPosition(PartFile& pf, const QByteArray& body,
                              uint8 clientSXVersion, bool isSX2)
{
    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    pf.addClientSources(io, clientSXVersion, isSX2, nullptr);
    return io.position();
}

} // namespace

class tst_SourceExchange : public QObject {
    Q_OBJECT

private slots:
    void cleanup();

    // Bug 1 — declared count must equal records actually written
    void knownFile_countMatchesEntries_withLowIdSources();
    void knownFile_countMatchesEntries_allVersions_data();
    void knownFile_countMatchesEntries_allVersions();
    void knownFile_allLowIdSources_returnsNull();

    // Bug 2 — userId byte order on write
    void knownFile_userIdIsHybridAtV3Plus_data();
    void knownFile_userIdIsHybridAtV3Plus();
    void partFile_userIdIsHybridAtV3Plus();
    void serverIpIsAlwaysNetworkOrder();

    // Bug 3 — parse-side byte order rule
    void roundTrip_recoversSourceAddress_data();
    void roundTrip_recoversSourceAddress();
    void trailingZeroOctet_survivesAsHighId();

    // Bug 4 — direct UDP callback bit must never be published
    void cryptOptions_bit3NeverSet();

    // SX1 vs SX2 answer dialect
    void sx1Answer_hasNoVersionByteAndSx1Opcode();
    void sx2Answer_keepsVersionByteAndSx2Opcode();

    // Parse-side length validation
    void parse_rejectsMismatchedLength_sx2();
    void parse_rejectsUnknownVersion_sx2();
    void parse_rejectsUnknownLength_sx1();
    void parse_sx1DetectsVersionFromRecordSize();

    // Source eligibility filters
    void knownFile_skipsRequesterAndIneligibleClients();
    void directUdpCallback_requiresKadPortAndValidHash();
    void parse_dropsLowIdSourcesWhenFirewalled();

    // Extended Source Exchange (ExtSX) — IPv6 tag-block records
    void extSX_answerIsVersion1TagBlock();
    void extSX_roundTrip_recoversIPv6Source();
    void extSX_skipsUnknownTagsWithoutDesync();
    void extSX_keepsLowIdSourceWithIPv6ButClassicDropsIt();
    void extSX_sourceCapIs500NotV1Default();
    void extSX_lowIdUsesHybridIdNotAddress();
    void extSX_ipv6OnlySourceSurvivesRoundTrip();
    void extSX_userHashAndCryptTagsOnlyForTolerantPeer();
    void extSX_userHashAndCryptTagsRoundTrip();

    // Public-IPv6 confidence gate — peer corroboration + server override
    void peerCorroboration_adoptsOnlyAfterThresholdDistinctPeers();
    void reflectedIPv6NotHeldLocally_isRejected();
    void publicIPv6_tierPrecedenceAndAdvertiseGate();

private:
    std::vector<UpDownClient*> m_clients;
    UpDownClient* m_requester = nullptr;

    UpDownClient* track(UpDownClient* c)
    {
        m_clients.push_back(c);
        return c;
    }

    /// The default SX2-speaking peer asking us for sources, created on first use.
    UpDownClient* requester()
    {
        if (!m_requester)
            m_requester = track(makeRequester());
        return m_requester;
    }

    /// A KnownFile with a deterministic hash and the given uploading clients.
    static std::unique_ptr<KnownFile> makeFile(const std::vector<UpDownClient*>& srcs)
    {
        auto f = std::make_unique<KnownFile>();
        uint8 hash[16];
        std::memset(hash, 0x11, sizeof(hash));
        f->setFileHash(hash);
        for (auto* c : srcs)
            f->addUploadingClient(c);
        return f;
    }
};

void tst_SourceExchange::cleanup()
{
    qDeleteAll(m_clients);
    m_clients.clear();
    m_requester = nullptr;
}

// ---------------------------------------------------------------------------
// Bug 1 — count / entry mismatch
// ---------------------------------------------------------------------------

void tst_SourceExchange::knownFile_countMatchesEntries_withLowIdSources()
{
    // Two high-ID sources interleaved with two low-ID ones. Low-ID sources are
    // skipped, so a count computed before the loop would over-declare by 2 and
    // a real peer would drop the whole packet.
    auto file = makeFile({
        track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)),
        track(makeLowIdClient(0x00000123)),
        track(makeHighIdClient(QStringLiteral("11.22.33.44"), 4663)),
        track(makeLowIdClient(0x00000456)),
    });

    auto packet = file->createSrcInfoPacket(requester(), 4, 0);
    QVERIFY(packet != nullptr);

    const uint16 count = readU16(packet->pBuffer, kHeaderSize);
    QCOMPARE(count, uint16(2));

    // This is exactly the check official eMule performs on receive.
    QCOMPARE(int(packet->size), kHeaderSize + 2 + count * entrySize(4));
}

void tst_SourceExchange::knownFile_countMatchesEntries_allVersions_data()
{
    QTest::addColumn<uint8>("version");
    QTest::newRow("v1") << uint8(1);
    QTest::newRow("v2") << uint8(2);
    QTest::newRow("v3") << uint8(3);
    QTest::newRow("v4") << uint8(4);
}

void tst_SourceExchange::knownFile_countMatchesEntries_allVersions()
{
    QFETCH(uint8, version);

    auto file = makeFile({
        track(makeLowIdClient(0x00000001)),
        track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)),
        track(makeLowIdClient(0x00000002)),
        track(makeHighIdClient(QStringLiteral("11.22.33.44"), 4663)),
        track(makeHighIdClient(QStringLiteral("12.23.34.45"), 4664)),
    });

    auto packet = file->createSrcInfoPacket(requester(), version, 0);
    QVERIFY(packet != nullptr);

    QCOMPARE(uint8(packet->pBuffer[0]), version);

    const uint16 count = readU16(packet->pBuffer, kHeaderSize);
    QCOMPARE(count, uint16(3));
    QCOMPARE(int(packet->size), kHeaderSize + 2 + count * entrySize(version));
}

void tst_SourceExchange::knownFile_allLowIdSources_returnsNull()
{
    // Nothing to say — must not emit a packet declaring zero sources.
    auto file = makeFile({
        track(makeLowIdClient(0x00000001)),
        track(makeLowIdClient(0x00000002)),
    });

    QVERIFY(file->createSrcInfoPacket(requester(), 4, 0) == nullptr);
}

// ---------------------------------------------------------------------------
// Bug 2 — userId byte order
// ---------------------------------------------------------------------------

void tst_SourceExchange::knownFile_userIdIsHybridAtV3Plus_data()
{
    QTest::addColumn<uint8>("version");
    QTest::addColumn<bool>("expectHybrid");
    QTest::newRow("v1 -> network") << uint8(1) << false;
    QTest::newRow("v2 -> network") << uint8(2) << false;
    QTest::newRow("v3 -> hybrid")  << uint8(3) << true;
    QTest::newRow("v4 -> hybrid")  << uint8(4) << true;
}

void tst_SourceExchange::knownFile_userIdIsHybridAtV3Plus()
{
    QFETCH(uint8, version);
    QFETCH(bool, expectHybrid);

    const Address addr = Address::fromString(QStringLiteral("10.20.30.40"));
    auto file = makeFile({ track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)) });

    auto packet = file->createSrcInfoPacket(requester(), version, 0);
    QVERIFY(packet != nullptr);

    const uint32 wireId = readU32(packet->pBuffer, kHeaderSize + 2);
    const uint32 expected = expectHybrid ? addr.toUint32() : addr.toNetworkUint32();

    QCOMPARE(wireId, expected);
    QCOMPARE(readU16(packet->pBuffer, kHeaderSize + 2 + 4), uint16(4662));
}

void tst_SourceExchange::partFile_userIdIsHybridAtV3Plus()
{
    // PartFile has its own copy of the writer; it must agree with KnownFile's.
    const Address addr = Address::fromString(QStringLiteral("10.20.30.40"));

    PartFile pf;
    uint8 hash[16];
    std::memset(hash, 0x22, sizeof(hash));
    pf.setFileHash(hash);
    pf.addSource(track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)));

    auto v4 = pf.createSrcInfoPacket(requester(), 4, 0);
    QVERIFY(v4 != nullptr);
    QCOMPARE(readU32(v4->pBuffer, kHeaderSize + 2), addr.toUint32());

    auto v2 = pf.createSrcInfoPacket(requester(), 2, 0);
    QVERIFY(v2 != nullptr);
    QCOMPARE(readU32(v2->pBuffer, kHeaderSize + 2), addr.toNetworkUint32());
}

void tst_SourceExchange::serverIpIsAlwaysNetworkOrder()
{
    // Only the userId field switched to hybrid at v3; serverIP never did.
    const Address server = Address::fromString(QStringLiteral("1.2.3.4"));
    auto file = makeFile({ track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)) });

    for (uint8 v : { uint8(1), uint8(2), uint8(3), uint8(4) }) {
        auto packet = file->createSrcInfoPacket(requester(), v, 0);
        QVERIFY(packet != nullptr);
        QCOMPARE(readU32(packet->pBuffer, kHeaderSize + 2 + 4 + 2), server.toNetworkUint32());
    }
}

// ---------------------------------------------------------------------------
// Bug 3 — parse-side byte order
// ---------------------------------------------------------------------------

void tst_SourceExchange::roundTrip_recoversSourceAddress_data()
{
    QTest::addColumn<uint8>("version");
    QTest::addColumn<QString>("ip");

    for (uint8 v : { uint8(1), uint8(2), uint8(3), uint8(4) }) {
        const QByteArray tag = QByteArrayLiteral("v") + QByteArray::number(v) + ' ';
        for (const char* ip : { "10.20.30.40", "200.100.50.25" }) {
            const QByteArray row = tag + ip;
            QTest::newRow(row.constData()) << v << QString::fromLatin1(ip);
        }
    }
}

void tst_SourceExchange::roundTrip_recoversSourceAddress()
{
    QFETCH(uint8, version);
    QFETCH(QString, ip);

    const Address addr = Address::fromString(ip);
    auto file = makeFile({ track(makeHighIdClient(ip, 4662)) });

    auto packet = file->createSrcInfoPacket(requester(), version, 0);
    QVERIFY(packet != nullptr);

    const uint32 wireId = readU32(packet->pBuffer, kHeaderSize + 2);

    // The rule PartFile::addClientSources applies for its validation checks.
    const uint32 userIdEd2k = (version < 3) ? wireId : htonl(wireId);
    QVERIFY(!isLowID(userIdEd2k));
    QCOMPARE(userIdEd2k, addr.toNetworkUint32());

    // And the rule it applies when constructing the source: the raw wire value
    // plus the ed2kID flag. The constructor does its own conversion, which is
    // why addClientSources must not pre-mutate the id.
    UpDownClient parsed(4662, wireId, 0, 0, nullptr, version < 3);
    QCOMPARE(parsed.userIDHybrid(), addr.toUint32());
    QVERIFY(!parsed.hasLowID());
    QCOMPARE(parsed.connectAddress().toString(), ip);
}

void tst_SourceExchange::trailingZeroOctet_survivesAsHighId()
{
    // The entire reason the hybrid format exists: 10.20.30.0 in network order
    // is 0x001E140A, which is below 2^24 and would be misread as a low ID.
    const QString ip = QStringLiteral("10.20.30.0");
    const Address addr = Address::fromString(ip);

    QVERIFY(isLowID(addr.toNetworkUint32()));   // the trap
    QVERIFY(!isLowID(addr.toUint32()));         // hybrid avoids it

    auto file = makeFile({ track(makeHighIdClient(ip, 4662)) });

    auto packet = file->createSrcInfoPacket(requester(), 4, 0);
    QVERIFY(packet != nullptr);

    const uint32 wireId = readU32(packet->pBuffer, kHeaderSize + 2);
    QCOMPARE(wireId, addr.toUint32());

    UpDownClient parsed(4662, wireId, 0, 0, nullptr, /*ed2kID*/ false);
    QVERIFY(!parsed.hasLowID());
    QCOMPARE(parsed.connectAddress().toString(), ip);
}

// ---------------------------------------------------------------------------
// Bug 4 — direct UDP callback bit
// ---------------------------------------------------------------------------

void tst_SourceExchange::cryptOptions_bit3NeverSet()
{
    // SX records carry no Kad UDP port, so bit 3 is unusable by the receiver.
    // Official eMule has it commented out (srchybrid/KnownFile.cpp:1136).
    auto* src = track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662));
    src->setConnectOptions(0x0F, true, true);
    src->setKadPort(4672);                  // so the callback flag is genuinely live
    QVERIFY(src->supportsDirectUDPCallback());

    auto file = makeFile({ src });
    auto packet = file->createSrcInfoPacket(requester(), 4, 0);
    QVERIFY(packet != nullptr);

    const int cryptOff = kHeaderSize + 2 + (4 + 2 + 4 + 2 + 16);
    const uint8 cryptOpts = uint8(packet->pBuffer[cryptOff]);

    QCOMPARE(cryptOpts & 0x08, 0);          // callback bit suppressed
    QCOMPARE(cryptOpts & 0x07, 0x07);       // crypt bits still round-trip
}

// ---------------------------------------------------------------------------
// SX1 vs SX2 answer dialect
// ---------------------------------------------------------------------------

void tst_SourceExchange::sx1Answer_hasNoVersionByteAndSx1Opcode()
{
    // A peer that never announced SX2 asks over the deprecated OP_REQUESTSOURCES,
    // which carries no version. The answer must be in the SX1 dialect: no leading
    // version byte, and the version taken from what it announced at handshake.
    auto* peer = track(makeRequester(/*supportsSX2*/ false, /*sx1Version*/ 3));
    auto file = makeFile({
        track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)),
        track(makeHighIdClient(QStringLiteral("11.22.33.44"), 4663)),
    });

    auto packet = file->createSrcInfoPacket(peer, 0, 0);
    QVERIFY(packet != nullptr);

    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES));

    // SX1 payload is hash(16) + count(2) + records; no version byte in front.
    constexpr int sx1Header = 16;
    const uint16 count = readU16(packet->pBuffer, sx1Header);
    QCOMPARE(count, uint16(2));
    QCOMPARE(int(packet->size), sx1Header + 2 + count * entrySize(3));

    // v3 means hybrid IDs, and the record must start right after the count.
    QCOMPARE(readU32(packet->pBuffer, sx1Header + 2),
             Address::fromString(QStringLiteral("10.20.30.40")).toUint32());
}

void tst_SourceExchange::sx2Answer_keepsVersionByteAndSx2Opcode()
{
    // Guards the SX1 change against regressing the SX2 path.
    auto file = makeFile({ track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662)) });

    auto packet = file->createSrcInfoPacket(requester(), 4, 0);
    QVERIFY(packet != nullptr);

    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES2));
    QCOMPARE(uint8(packet->pBuffer[0]), uint8(4));

    const uint16 count = readU16(packet->pBuffer, kHeaderSize);
    QCOMPARE(count, uint16(1));
    QCOMPARE(int(packet->size), kHeaderSize + 2 + count * entrySize(4));
}

// ---------------------------------------------------------------------------
// Parse-side length validation
// ---------------------------------------------------------------------------

void tst_SourceExchange::parse_rejectsMismatchedLength_sx2()
{
    PartFile pf;

    // Declares 5 sources but carries 2. Without a length check the loop would
    // read three records' worth of bytes past the end of the buffer.
    const QByteArray bad = makeSxBody(/*declaredCount*/ 5, /*recordCount*/ 2, 4);
    QCOMPARE(parseAndReportPosition(pf, bad, 4, /*isSX2*/ true), qint64(2));

    // The honest version of the same packet is accepted and fully consumed.
    const QByteArray good = makeSxBody(2, 2, 4);
    QCOMPARE(parseAndReportPosition(pf, good, 4, /*isSX2*/ true), qint64(good.size()));
}

void tst_SourceExchange::parse_rejectsUnknownVersion_sx2()
{
    PartFile pf;
    const QByteArray body = makeSxBody(1, 1, 4);

    // Version 0 and anything past what we implement are not misunderstandings.
    QCOMPARE(parseAndReportPosition(pf, body, 0, /*isSX2*/ true), qint64(2));
    QCOMPARE(parseAndReportPosition(pf, body, SOURCEEXCHANGE2_VERSION + 1, true), qint64(2));
}

void tst_SourceExchange::parse_rejectsUnknownLength_sx1()
{
    PartFile pf;

    // SX1 infers its version from the record size, so a body matching no known
    // record size can't be interpreted at all.
    QByteArray odd = makeSxBody(1, 1, 4);
    odd.append(3, char(0x00));
    QCOMPARE(parseAndReportPosition(pf, odd, 4, /*isSX2*/ false), qint64(2));
}

void tst_SourceExchange::parse_sx1DetectsVersionFromRecordSize()
{
    PartFile pf;

    // v1-shaped records (no user hash) from a client that announced v1: accepted.
    const QByteArray v1Body = makeSxBody(2, 2, 1);
    QCOMPARE(parseAndReportPosition(pf, v1Body, 1, /*isSX2*/ false), qint64(v1Body.size()));

    // Same bytes from a client that announced v0 — it can't have sent this, so the
    // packet is bogus and must not be trusted.
    QCOMPARE(parseAndReportPosition(pf, v1Body, 0, /*isSX2*/ false), qint64(2));

    // v4-shaped records from a client announcing only v2: the size says v4, which
    // the peer claims not to speak. Rejected rather than mis-sliced.
    const QByteArray v4Body = makeSxBody(2, 2, 4);
    QCOMPARE(parseAndReportPosition(pf, v4Body, 2, /*isSX2*/ false), qint64(2));
}

// ---------------------------------------------------------------------------
// Source eligibility filters
// ---------------------------------------------------------------------------

void tst_SourceExchange::knownFile_skipsRequesterAndIneligibleClients()
{
    auto* peer = requester();
    auto* connecting = track(makeHighIdClient(QStringLiteral("11.22.33.44"), 4663));
    connecting->setUploadState(UploadState::Connecting);

    auto* banned = track(makeHighIdClient(QStringLiteral("12.23.34.45"), 4664));
    banned->setUploadState(UploadState::Banned);

    auto* good = track(makeHighIdClient(QStringLiteral("13.24.35.46"), 4665));

    // The requester is in our own upload list — telling it about itself is useless.
    auto file = makeFile({ peer, connecting, banned, good });

    auto packet = file->createSrcInfoPacket(peer, 4, 0);
    QVERIFY(packet != nullptr);

    const uint16 count = readU16(packet->pBuffer, kHeaderSize);
    QCOMPARE(count, uint16(1));
    QCOMPARE(int(packet->size), kHeaderSize + 2 + count * entrySize(4));

    QCOMPARE(readU32(packet->pBuffer, kHeaderSize + 2),
             Address::fromString(QStringLiteral("13.24.35.46")).toUint32());
}

void tst_SourceExchange::directUdpCallback_requiresKadPortAndValidHash()
{
    // Without a Kad port there is nowhere to send the callback, so claiming
    // support would just strand us waiting for a reply that can't arrive.
    auto* c = track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662));
    c->setConnectOptions(0x0F, true, true);
    QVERIFY(c->hasValidHash());
    QCOMPARE(c->kadPort(), uint16(0));
    QVERIFY(!c->supportsDirectUDPCallback());

    c->setKadPort(4672);
    QVERIFY(c->supportsDirectUDPCallback());

    // A client with no user hash can't be addressed either.
    auto* noHash = track(makeHighIdClient(QStringLiteral("11.22.33.44"), 4663));
    noHash->setConnectOptions(0x0F, true, true);
    noHash->setKadPort(4672);
    uint8 zero[16] = {};
    noHash->setUserHash(zero);
    QVERIFY(!noHash->hasValidHash());
    QVERIFY(!noHash->supportsDirectUDPCallback());
}

void tst_SourceExchange::parse_dropsLowIdSourcesWhenFirewalled()
{
    // With no server connection and no Kad, theApp.isFirewalled() is true. Two
    // firewalled clients can never accept each other's connection, so a low-ID
    // source learned via SX is dead weight.
    QVERIFY(theApp.isFirewalled());

    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    auto* pf = new PartFile;   // owned by the queue once added
    uint8 hash[16];
    std::memset(hash, 0x33, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    // One low-ID source (small raw hybrid id) followed by one high-ID source.
    QByteArray body;
    appendU16(body, 2);
    for (uint32 id : { uint32(0x00000123),
                       Address::fromString(QStringLiteral("77.66.55.44")).toUint32() }) {
        appendU32(body, id);
        appendU16(body, 4662);
        appendU32(body, Address::fromString(QStringLiteral("1.2.3.4")).toNetworkUint32());
        appendU16(body, 4661);
        body.append(16, char(0xEE));
        body.append(char(0x00));
    }

    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    pf->addClientSources(io, 4, /*isSX2*/ true, nullptr);

    // Only the high-ID source survives.
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("77.66.55.44")).toUint32());

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

// ---------------------------------------------------------------------------
// Extended Source Exchange (ExtSX) — IPv6 tag-block records
// ---------------------------------------------------------------------------

void tst_SourceExchange::extSX_answerIsVersion1TagBlock()
{
    // An ExtSX-capable requester gets version byte 1 and, per source, a variable tag
    // block (tagCount + tags) in place of the fixed serverIP/serverPort fields.
    auto* peer = track(makeRequester());
    peer->setSupportsExtendedXS(true);

    auto* src = track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662));
    src->setUserIPv6(Address::fromString(QStringLiteral("2606:4700::1")));
    src->setOpenIPv6(true);

    auto file = makeFile({ src });
    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES2));
    QCOMPARE(uint8(packet->pBuffer[0]), uint8(SOURCEEXCHANGEEXT_VERSION));   // version 1
    QCOMPARE(readU16(packet->pBuffer, kHeaderSize), uint16(1));              // one source

    // Record: ID(4, network order at v1) + port(2) + tagCount(1).
    QCOMPARE(readU32(packet->pBuffer, kHeaderSize + 2),
             Address::fromString(QStringLiteral("10.20.30.40")).toNetworkUint32());
    QCOMPARE(readU16(packet->pBuffer, kHeaderSize + 2 + 4), uint16(4662));
    const uint8 tagCount = uint8(packet->pBuffer[kHeaderSize + 2 + 4 + 2]);
    QCOMPARE(tagCount, uint8(3));   // CT_EMULE_SERVERIP, CT_EMULE_SERVERTCP, CT_MOD_IP_V6
}

void tst_SourceExchange::extSX_userHashAndCryptTagsOnlyForTolerantPeer()
{
    // The user hash and crypt options are only safe to add for a peer that advertised
    // MODMISC_EXTXS_SKIPTAGS. The compatibility target's reader records any unrecognised
    // tag in an error string and then returns out of the entire source-exchange parse, so
    // sending these unconditionally would cost it every source in the packet.
    auto* src = track(makeHighIdClient(QStringLiteral("10.20.30.40"), 4662));
    uint8 srcHash[16];
    std::memset(srcHash, 0x5A, sizeof(srcHash));
    src->setUserHash(srcHash);
    src->setConnectOptions(0x01, true, false);   // supports crypt layer

    const auto tagCountFor = [&](bool tolerant) {
        auto* peer = track(makeRequester());
        peer->setSupportsExtendedXS(true);
        peer->setSupportsExtSXSkipTags(tolerant);

        auto file = makeFile({ src });
        auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
        Q_ASSERT(packet != nullptr);
        return uint8(packet->pBuffer[kHeaderSize + 2 + 4 + 2]);
    };

    // Legacy peer: exactly the three tags it got before this feature existed.
    QCOMPARE(tagCountFor(false), uint8(2));   // CT_EMULE_SERVERIP + CT_EMULE_SERVERTCP
    // Tolerant peer: plus CT_EMULE_USERHASH and CT_EMULE_CONOPTS.
    QCOMPARE(tagCountFor(true), uint8(4));
}

void tst_SourceExchange::extSX_userHashAndCryptTagsRoundTrip()
{
    // A source learned over ExtSX used to arrive with no user hash (breaking credits,
    // secure identification and obfuscated-UDP keying) and unknown crypt capability,
    // because both lived in the classic record's version-gated tail that ExtSX omits.
    auto* peer = track(makeRequester());
    peer->setSupportsExtendedXS(true);
    peer->setSupportsExtSXSkipTags(true);

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662));
    uint8 srcHash[16];
    std::memset(srcHash, 0x77, sizeof(srcHash));
    src->setUserHash(srcHash);
    src->setConnectOptions(0x03, true, false);   // supports + requests crypt

    auto file = makeFile({ src });
    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    DownloadQueue queue;
    theApp.downloadQueue = &queue;
    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x55, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    SafeMemFile io(reinterpret_cast<const uint8*>(packet->pBuffer + kHeaderSize),
                   packet->size - kHeaderSize);
    pf->addClientSources(io, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, peer);

    QCOMPARE(pf->sourceCount(), 1);
    auto* learned = pf->srcList().front();
    QVERIFY(learned->hasValidHash());
    QCOMPARE(std::memcmp(learned->userHash(), srcHash, 16), 0);
    QVERIFY(learned->supportsCryptLayer());
    QVERIFY(learned->requestsCryptLayer());

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

void tst_SourceExchange::extSX_roundTrip_recoversIPv6Source()
{
    // Build an ExtSX answer with one v6-open source and one plain v4 source, then parse
    // it straight back: both must survive and the v6 source keeps its exact address.
    auto* peer = track(makeRequester());
    peer->setSupportsExtendedXS(true);

    // Public IPv4s: a dual-stack HighID source has a routable IPv4 (a LAN address would be
    // dropped by the parser's isGoodIP check, exactly as the reference does).
    auto* v6src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662));
    v6src->setUserIPv6(Address::fromString(QStringLiteral("2606:4700::1")));
    v6src->setOpenIPv6(true);
    auto* v4src = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663));

    auto file = makeFile({ v6src, v4src });
    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    DownloadQueue queue;
    theApp.downloadQueue = &queue;
    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x44, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    // addClientSources reads from the count onwards (after the version byte + file hash).
    SafeMemFile io(reinterpret_cast<const uint8*>(packet->pBuffer + kHeaderSize),
                   packet->size - kHeaderSize);
    pf->addClientSources(io, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, peer);

    QCOMPARE(pf->sourceCount(), 2);
    int v6count = 0;
    for (auto* s : pf->srcList()) {
        if (s->openIPv6()) {
            ++v6count;
            QCOMPARE(s->userIPv6().toString(), QStringLiteral("2606:4700::1"));
        }
    }
    QCOMPARE(v6count, 1);

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

void tst_SourceExchange::extSX_skipsUnknownTagsWithoutDesync()
{
    // The self-describing tag block is desync-proof: an unknown tag in the middle of a
    // record must be skipped by type, leaving the following record perfectly aligned.
    auto* peer = track(makeRequester());
    peer->setSupportsExtendedXS(true);

    DownloadQueue queue;
    theApp.downloadQueue = &queue;
    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x55, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    const Address v6a = Address::fromString(QStringLiteral("2606:4700::2"));
    const Address v6b = Address::fromString(QStringLiteral("2606:4700::3"));

    SafeMemFile w;
    w.writeUInt16(2);   // two sources

    // Record 1: an UNKNOWN tag between real tags. A reader that mis-sizes it desyncs.
    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(uint8(0x77), uint32(0xDEADBEEF));   // unknown numeric tag
        tags.emplace_back(CT_MOD_IP_V6, v6a.ipv6Bytes().data());
        w.writeUInt8(static_cast<uint8>(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    // Record 2: reachable only if record 1 was consumed to the exact byte.
    w.writeUInt32(Address::fromString(QStringLiteral("51.61.71.81")).toNetworkUint32());
    w.writeUInt16(4663);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6, v6b.ipv6Bytes().data());
        w.writeUInt8(static_cast<uint8>(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    const QByteArray body = w.buffer();
    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    pf->addClientSources(io, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, peer);

    QCOMPARE(pf->sourceCount(), 2);
    QStringList v6s;
    for (auto* s : pf->srcList())
        v6s << s->userIPv6().toString();
    v6s.sort();
    QCOMPARE(v6s, (QStringList{ QStringLiteral("2606:4700::2"), QStringLiteral("2606:4700::3") }));

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

void tst_SourceExchange::extSX_keepsLowIdSourceWithIPv6ButClassicDropsIt()
{
    // A LowID source that has a reachable public IPv6 is dead weight to a classic peer
    // (dropped) but useful to an ExtSX peer, which reaches it directly over IPv6 (kept).
    auto* lowV6 = track(makeLowIdClient(0x00000123));
    lowV6->setUserIPv6(Address::fromString(QStringLiteral("2606:4700::9")));
    lowV6->setOpenIPv6(true);

    auto file = makeFile({ lowV6 });

    // Classic SX2 peer: the LowID source is dropped, so there is nothing to send.
    auto* classic = track(makeRequester());   // supportsExtendedXS() == false
    QVERIFY(file->createSrcInfoPacket(classic, 4, 0) == nullptr);

    // ExtSX peer: the source is kept and carried as a v6 tag block.
    auto* ext = track(makeRequester());
    ext->setSupportsExtendedXS(true);
    auto packet = file->createSrcInfoPacket(ext, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(readU16(packet->pBuffer, kHeaderSize), uint16(1));
}

// ExtSX pins the version byte to 1, and the source cap used to key off that version
// alone — so our best-equipped peers silently got the 50-source branch instead of 500,
// ten times fewer sources than the same peer would receive over plain SX2 v4.
void tst_SourceExchange::extSX_sourceCapIs500NotV1Default()
{
    constexpr int kSources = 120;   // comfortably over the old 50 cap, well under 500

    std::vector<UpDownClient*> srcs;
    srcs.reserve(kSources);
    for (int i = 0; i < kSources; ++i) {
        // Spread over 60.x.y.z so every address is public and distinct.
        srcs.push_back(track(makeHighIdClient(
            QStringLiteral("60.%1.%2.90").arg(i / 250).arg(i % 250 + 1),
            static_cast<uint16>(4662 + i))));
    }
    auto file = makeFile(srcs);

    // Anything past the old cap exceeds 354 bytes and is therefore zlib-packed, so the
    // count has to be read after unpacking.
    const auto declaredCount = [](std::unique_ptr<Packet> p) {
        if (p->prot == OP_PACKEDPROT)
            [&] { QVERIFY(p->unPackPacket()); }();
        return readU16(p->pBuffer, kHeaderSize);
    };

    auto* ext = track(makeRequester());
    ext->setSupportsExtendedXS(true);
    auto extPacket = file->createSrcInfoPacket(ext, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(extPacket != nullptr);
    QCOMPARE(declaredCount(std::move(extPacket)), uint16(kSources));

    // Plain SX2 v4 already had the 500 cap; the two must now agree.
    auto* classic = track(makeRequester());
    auto v4Packet = file->createSrcInfoPacket(classic, 4, 0);
    QVERIFY(v4Packet != nullptr);
    QCOMPARE(declaredCount(std::move(v4Packet)), uint16(kSources));

    // A v1 peer that is NOT ExtSX still gets the old 50 cap — that branch is untouched.
    auto* v1 = track(makeRequester());
    auto v1Packet = file->createSrcInfoPacket(v1, 1, 0);
    QVERIFY(v1Packet != nullptr);
    QCOMPARE(declaredCount(std::move(v1Packet)), uint16(50));
}

// On the ExtSX path the ID field is htonl(userIDHybrid), matching the reference
// (eMuleAI PartFile.cpp:5093-5095), NOT the source's address. For a HighID IPv4 source
// the two are identical; they diverge for a LowID source, where the address would be
// read back as a bogus HighID.
void tst_SourceExchange::extSX_lowIdUsesHybridIdNotAddress()
{
    constexpr uint32 kLowId = 0x00000123;
    auto* lowV6 = track(makeLowIdClient(kLowId));
    lowV6->setUserIPv6(Address::fromString(QStringLiteral("2606:4700::9")));
    lowV6->setOpenIPv6(true);

    auto file = makeFile({ lowV6 });
    auto* ext = track(makeRequester());
    ext->setSupportsExtendedXS(true);

    auto packet = file->createSrcInfoPacket(ext, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    const uint32 wireId = readU32(packet->pBuffer, kHeaderSize + 2);
    QCOMPARE(wireId, htonl(kLowId));
    // Not the 5.6.7.8 address makeLowIdClient assigns — that is the old, wrong value.
    QVERIFY(wireId != Address::fromString(QStringLiteral("5.6.7.8")).toNetworkUint32());
}

// A source reachable only over IPv6 must survive the whole round trip. It used to be
// discarded on parse: the unusable IPv4 field failed isGoodIP and the record was
// dropped before the already-parsed CT_MOD_IP_V6 could be used.
void tst_SourceExchange::extSX_ipv6OnlySourceSurvivesRoundTrip()
{
    auto* peer = track(makeRequester());
    peer->setSupportsExtendedXS(true);

    // No usable IPv4: the reference marks an IPv6-only peer with the HighID sentinel
    // 0xFFFFFFFF, which reads back as the broadcast address.
    auto* v6only = track(makeHighIdClient(QStringLiteral("70.80.90.100"), 4662));
    v6only->setUserIDHybrid(0xFFFFFFFFu);
    v6only->setUserIPv6(Address::fromString(QStringLiteral("2606:4700::abcd")));
    v6only->setOpenIPv6(true);

    auto file = makeFile({ v6only });
    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(readU32(packet->pBuffer, kHeaderSize + 2), htonl(0xFFFFFFFFu));

    DownloadQueue queue;
    theApp.downloadQueue = &queue;
    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x55, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    SafeMemFile io(reinterpret_cast<const uint8*>(packet->pBuffer + kHeaderSize),
                   packet->size - kHeaderSize);
    pf->addClientSources(io, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, peer);

    QCOMPARE(pf->sourceCount(), 1);
    auto* parsed = pf->srcList().front();
    QVERIFY(parsed->openIPv6());
    QCOMPARE(parsed->userIPv6().toString(), QStringLiteral("2606:4700::abcd"));
    // Constructed as a LowID client so we never dial 255.255.255.255.
    QVERIFY(parsed->hasLowID());

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

// ---------------------------------------------------------------------------
// Public-IPv6 confidence gate — peer corroboration + server override
// ---------------------------------------------------------------------------

void tst_SourceExchange::peerCorroboration_adoptsOnlyAfterThresholdDistinctPeers()
{
    thePrefs.setIpv6PublicPeerConfirmThreshold(3);
    thePrefs.setIpv6PublicPeerConfirmWindowSecs(300);
    theApp.clearPublicIPv6Observed();

    const Address cand     = Address::fromString(QStringLiteral("2606:4700::abcd"));
    const Address observed = Address::fromString(QStringLiteral("2606:4700::1234"));

    // Every reflected address must be one we actually hold, so the test injects the
    // interface set that LocalIPv6::updatePublicIPv6() supplies in production.
    theApp.setLocalIPv6Addresses({cand, observed});

    theApp.recordPeerObservedIPv6(cand, QByteArrayLiteral("[2001:db8::1]"));
    theApp.recordPeerObservedIPv6(cand, QByteArrayLiteral("[2001:db8::2]"));
    QVERIFY(theApp.peerCorroboratedIPv6().isNull());          // 2 < 3

    // A repeat from an existing peer is not a new confirmation.
    theApp.recordPeerObservedIPv6(cand, QByteArrayLiteral("[2001:db8::1]"));
    QVERIFY(theApp.peerCorroboratedIPv6().isNull());          // still 2 distinct

    // A non-public claim is ignored entirely (does not count toward the threshold).
    theApp.recordPeerObservedIPv6(Address::fromString(QStringLiteral("fe80::1")),
                                  QByteArrayLiteral("[2001:db8::9]"));
    QVERIFY(theApp.peerCorroboratedIPv6().isNull());

    theApp.recordPeerObservedIPv6(cand, QByteArrayLiteral("[2001:db8::3]"));
    QCOMPARE(theApp.peerCorroboratedIPv6().toString(), cand.toString());   // 3 distinct → adopted

    // A server-observed egress address always outranks peer corroboration.
    theApp.setPublicIPv6Observed(observed);
    QCOMPARE(theApp.publicIPv6().toString(), observed.toString());

    // With no server observation, the peer-corroborated value is used again.
    theApp.clearPublicIPv6Observed();
    QCOMPARE(theApp.publicIPv6().toString(), cand.toString());
}

void tst_SourceExchange::reflectedIPv6NotHeldLocally_isRejected()
{
    thePrefs.setIpv6PublicPeerConfirmThreshold(3);
    thePrefs.setIpv6PublicPeerConfirmWindowSecs(300);
    thePrefs.setPublicIPv6Override(QString());
    theApp.clearPublicIPv6Observed();

    const Address held    = Address::fromString(QStringLiteral("2606:4700::beef"));
    const Address foreign = Address::fromString(QStringLiteral("2606:4700::dead"));
    theApp.setLocalIPv6Addresses({held});
    theApp.setPublicIPv6Local(held);

    // Peer corroboration cannot make us claim an address this host does not hold, no
    // matter how many distinct peers assert it — otherwise three forged hellos would
    // be enough to kill our inbound IPv6.
    for (int i = 0; i < 6; ++i) {
        theApp.recordPeerObservedIPv6(
            foreign, QStringLiteral("[2001:db8::%1]").arg(i).toUtf8());
    }
    QVERIFY(theApp.peerCorroboratedIPv6().isNull());
    QCOMPARE(theApp.publicIPv6().toString(), held.toString());

    // Nor can a server: a reflected egress we do not hold is refused, and the address
    // we already chose survives.
    theApp.setPublicIPv6Observed(foreign, QStringLiteral("server.example"));
    QCOMPARE(theApp.publicIPv6().toString(), held.toString());

    // The same server naming an address we do hold is accepted and outranks tier 4.
    theApp.setLocalIPv6Addresses({held, foreign});
    theApp.setPublicIPv6Observed(foreign, QStringLiteral("server.example"));
    QCOMPARE(theApp.publicIPv6().toString(), foreign.toString());

    // Losing the address locally drops the reflection again (prefix renumber).
    theApp.setLocalIPv6Addresses({held});
    QCOMPARE(theApp.publicIPv6().toString(), held.toString());
}

void tst_SourceExchange::publicIPv6_tierPrecedenceAndAdvertiseGate()
{
    thePrefs.setIpv6PublicPeerConfirmThreshold(1);
    thePrefs.setIpv6PublicPeerConfirmWindowSecs(300);
    theApp.clearPublicIPv6Observed();

    const Address local    = Address::fromString(QStringLiteral("2606:4700::100"));
    const Address pinned   = Address::fromString(QStringLiteral("2606:4700::200"));
    const Address peerSeen = Address::fromString(QStringLiteral("2606:4700::300"));
    const Address observed = Address::fromString(QStringLiteral("2606:4700::400"));
    theApp.setLocalIPv6Addresses({local, pinned, peerSeen, observed});

    theApp.setPublicIPv6Override(Address{});
    theApp.setPublicIPv6Local(local);
    QCOMPARE(theApp.publicIPv6().toString(), local.toString());

    // Peer corroboration beats the plain interface pick...
    theApp.recordPeerObservedIPv6(peerSeen, QByteArrayLiteral("[2001:db8::1]"));
    QCOMPARE(theApp.publicIPv6().toString(), peerSeen.toString());

    // ...the operator's pin beats peers...
    theApp.setPublicIPv6Override(pinned);
    QCOMPARE(theApp.publicIPv6().toString(), pinned.toString());

    // ...and what the server actually saw beats everything.
    theApp.setPublicIPv6Observed(observed, QStringLiteral("server.example"));
    QCOMPARE(theApp.publicIPv6().toString(), observed.toString());

    // The advertise gate is confidence AND a non-failing probe verdict.
    QVERIFY(theApp.shouldAdvertisePublicIPv6());
    theApp.setPublicIPv6Status(IPV6ST_HAVE | IPV6ST_PROBED);        // probed, not reachable
    QVERIFY(theApp.publicIPv6ProbedUnreachable());
    QVERIFY(theApp.hasConfidentPublicIPv6());                        // still know the address
    QVERIFY(!theApp.shouldAdvertisePublicIPv6());                    // but must not publish it
    theApp.setPublicIPv6Status(IPV6ST_HAVE | IPV6ST_PROBED | IPV6ST_REACHABLE);
    QVERIFY(theApp.shouldAdvertisePublicIPv6());

    theApp.clearPublicIPv6Observed();
    theApp.setPublicIPv6Override(Address{});
    theApp.setLocalIPv6Addresses({});
}

QTEST_MAIN(tst_SourceExchange)
#include "tst_SourceExchange.moc"
