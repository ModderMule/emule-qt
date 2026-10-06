/// @file tst_HttpCacheOffer.cpp
/// @brief Tests for httpcache/HttpCacheOffer — the OP_HTTPCACHE codec.
///
/// This codec is the only place a remote peer's bytes are interpreted, so the
/// hostile-input cases matter as much as the round trips.

#include "TestHelpers.h"
#include "crypto/AesCbc.h"
#include "httpcache/HttpCacheManager.h"
#include "httpcache/HttpCacheOffer.h"
#include "httpcache/HttpCacheReach.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QScopeGuard>
#include <QTest>
#include <QUrl>

using namespace eMule;

Q_DECLARE_METATYPE(eMule::CacheReach)

namespace {

HttpCacheOffer goodOffer()
{
    HttpCacheOffer offer;
    offer.fileHash = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    offer.partIndex = 7;
    offer.plainLength = PARTSIZE;
    offer.cipherLength = AesCbcEncryptor::cipherLengthFor(PARTSIZE);
    offer.url = QStringLiteral("http://localhost/emule-http-cache-php/v1/chunks/")
                + QString(32, QLatin1Char('a'));
    offer.key = QByteArray(kAesKeySize, '\x11');
    offer.iv = QByteArray(kAesIvSize, '\x22');
    offer.cipherSha256 = QByteArray(32, '\x33');
    offer.expiresAt = 1755500000;
    return offer;
}

/// Payload of a built packet, i.e. what parse() gets on the wire.
QByteArray payloadOf(const std::unique_ptr<Packet>& packet)
{
    return QByteArray(packet->pBuffer, static_cast<qsizetype>(packet->size));
}

HttpCacheCodec::Parsed parsePayload(const QByteArray& payload)
{
    return HttpCacheCodec::parse(reinterpret_cast<const uint8*>(payload.constData()),
                                 static_cast<uint32>(payload.size()));
}

} // namespace

class tst_HttpCacheOffer : public QObject {
    Q_OBJECT

private slots:
    void wellFormed_acceptsAGoodOffer();
    void wellFormed_rejectsBadFields_data();
    void wellFormed_rejectsBadFields();

    void offer_roundTrips();
    void offer_usesTheExtendedProtocolAndOpcode();
    void offer_refusesToBuildFromAMalformedOffer();

    void report_roundTrips();
    void report_declineUsesTheNoneSubOpcode();
    void report_clampsAnOutOfRangeResultCode();

    void cancel_roundTrips();

    void parse_rejectsTooShort();
    void parse_rejectsWrongVersion();
    void parse_rejectsUnknownSubOpcode();
    void parse_rejectsTruncatedTagBlock();
    void parse_rejectsMissingHashOrPart();
    void parse_skipsUnknownTags();
    void parse_rejectsOversizedUrl();
    void parse_rejectsWrongKeyIvLength();

    void urlIsAcceptable_screensBothFamilies_data();
    void urlIsAcceptable_screensBothFamilies();
    void urlIsAcceptable_labModeOpensUpIPv6Only();
    void urlIsAcceptable_localHostNeedsALocalSender();

    void reach_classifiesAddresses_data();
    void reach_classifiesAddresses();
    void reach_classifiesHostLiterals();
    void reach_allowsPeer_data();
    void reach_allowsPeer();
    void chooseServer_skipsIneligibleServers();
};

// ---------------------------------------------------------------------------
// Structural validation
// ---------------------------------------------------------------------------

void tst_HttpCacheOffer::wellFormed_acceptsAGoodOffer()
{
    const HttpCacheOffer offer = goodOffer();
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));
}

void tst_HttpCacheOffer::wellFormed_rejectsBadFields_data()
{
    QTest::addColumn<QString>("field");

    QTest::newRow("null file hash") << QStringLiteral("hash");
    QTest::newRow("zero plaintext") << QStringLiteral("plain0");
    QTest::newRow("oversized plaintext") << QStringLiteral("plainBig");
    QTest::newRow("ciphertext disagrees with plaintext") << QStringLiteral("cipherMismatch");
    QTest::newRow("short key") << QStringLiteral("key");
    QTest::newRow("short iv") << QStringLiteral("iv");
    QTest::newRow("short digest") << QStringLiteral("sha");
    QTest::newRow("empty url") << QStringLiteral("urlEmpty");
    QTest::newRow("oversized url") << QStringLiteral("urlLong");
    QTest::newRow("non-http scheme") << QStringLiteral("urlScheme");
    QTest::newRow("url without a host") << QStringLiteral("urlNoHost");
}

void tst_HttpCacheOffer::wellFormed_rejectsBadFields()
{
    QFETCH(QString, field);

    HttpCacheOffer offer = goodOffer();

    if (field == QLatin1String("hash"))
        offer.fileHash = {};
    else if (field == QLatin1String("plain0"))
        offer.plainLength = 0;
    else if (field == QLatin1String("plainBig"))
        offer.plainLength = PARTSIZE + 1;
    else if (field == QLatin1String("cipherMismatch"))
        offer.cipherLength = offer.plainLength; // forgot the pad block
    else if (field == QLatin1String("key"))
        offer.key = QByteArray(16, '\x11');
    else if (field == QLatin1String("iv"))
        offer.iv = QByteArray(8, '\x22');
    else if (field == QLatin1String("sha"))
        offer.cipherSha256 = QByteArray(16, '\x33');
    else if (field == QLatin1String("urlEmpty"))
        offer.url.clear();
    else if (field == QLatin1String("urlLong"))
        offer.url = QStringLiteral("http://h/") + QString(HTTPCACHE_MAX_URL_LEN, QLatin1Char('x'));
    else if (field == QLatin1String("urlScheme"))
        offer.url = QStringLiteral("file:///etc/passwd");
    else if (field == QLatin1String("urlNoHost"))
        offer.url = QStringLiteral("http:///nohost");

    QVERIFY(!offer.isWellFormed());
    QVERIFY(!offer.malformedReason().isEmpty());
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

void tst_HttpCacheOffer::offer_roundTrips()
{
    const HttpCacheOffer sent = goodOffer();

    auto packet = HttpCacheCodec::buildOffer(sent);
    QVERIFY(packet != nullptr);

    const auto parsed = parsePayload(payloadOf(packet));
    QVERIFY2(parsed.kind == HttpCacheCodec::Kind::Offer, qPrintable(parsed.error));

    const HttpCacheOffer& got = parsed.offer;
    QCOMPARE(got.fileHash, sent.fileHash);
    QCOMPARE(got.partIndex, sent.partIndex);
    QCOMPARE(got.plainLength, sent.plainLength);
    QCOMPARE(got.cipherLength, sent.cipherLength);
    QCOMPARE(got.url, sent.url);
    QCOMPARE(got.key, sent.key);
    QCOMPARE(got.iv, sent.iv);
    QCOMPARE(got.cipherSha256, sent.cipherSha256);
    QCOMPARE(got.expiresAt, sent.expiresAt);
}

void tst_HttpCacheOffer::offer_usesTheExtendedProtocolAndOpcode()
{
    auto packet = HttpCacheCodec::buildOffer(goodOffer());
    QVERIFY(packet != nullptr);

    // 0xBC on OP_EMULEPROT. A legacy peer never sees this — the capability bit
    // gates it — but if it ever did, it must not collide with a live opcode.
    QCOMPARE(packet->prot, uint8{OP_EMULEPROT});
    QCOMPARE(packet->opcode, uint8{OP_HTTPCACHE});

    const QByteArray payload = payloadOf(packet);
    QCOMPARE(static_cast<uint8>(payload.at(0)), uint8{HCPCK_VERSION});
    QCOMPARE(static_cast<uint8>(payload.at(1)), uint8{HCOP_OFFER});
    QCOMPARE(static_cast<uint8>(payload.at(2)), uint8{8}); // tag count
}

void tst_HttpCacheOffer::offer_refusesToBuildFromAMalformedOffer()
{
    HttpCacheOffer offer = goodOffer();
    offer.url = QStringLiteral("ftp://example.com/chunk");

    QVERIFY(HttpCacheCodec::buildOffer(offer) == nullptr);
}

void tst_HttpCacheOffer::report_roundTrips()
{
    HttpCacheReport sent;
    sent.fileHash = goodOffer().fileHash;
    sent.partIndex = 42;
    sent.result = HttpCacheResult::Corrupt;
    sent.bytesFetched = 1234567;

    auto packet = HttpCacheCodec::buildReport(sent, false);
    QVERIFY(packet != nullptr);

    const auto parsed = parsePayload(payloadOf(packet));
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Report);
    QCOMPARE(parsed.report.fileHash, sent.fileHash);
    QCOMPARE(parsed.report.partIndex, sent.partIndex);
    QCOMPARE(parsed.report.result, sent.result);
    QCOMPARE(parsed.report.bytesFetched, sent.bytesFetched);
}

void tst_HttpCacheOffer::report_declineUsesTheNoneSubOpcode()
{
    HttpCacheReport sent;
    sent.fileHash = goodOffer().fileHash;
    sent.partIndex = 3;
    sent.result = HttpCacheResult::Busy;

    auto packet = HttpCacheCodec::buildReport(sent, true);
    QVERIFY(packet != nullptr);

    const QByteArray payload = payloadOf(packet);
    QCOMPARE(static_cast<uint8>(payload.at(1)), uint8{HCOP_NONE});

    // A decline still parses as a Report — the result code is what distinguishes
    // "I did not try" from "I tried and it went wrong".
    const auto parsed = parsePayload(payload);
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Report);
    QCOMPARE(parsed.report.result, HttpCacheResult::Busy);
}

void tst_HttpCacheOffer::report_clampsAnOutOfRangeResultCode()
{
    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_RESULT);
    data.writeUInt8(3);
    Tag(HCTAG_FILEID, goodOffer().fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, uint32{1}).writeNewEd2kTag(data);
    Tag(HCTAG_RESULT, uint32{250}).writeNewEd2kTag(data); // not a HttpCacheResult

    const auto parsed = parsePayload(data.buffer());
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Report);
    // Must land on a defined enumerator rather than becoming an invalid value.
    QCOMPARE(parsed.report.result, HttpCacheResult::HttpFailed);
}

void tst_HttpCacheOffer::cancel_roundTrips()
{
    const auto hash = goodOffer().fileHash;

    auto packet = HttpCacheCodec::buildCancel(hash, 9);
    QVERIFY(packet != nullptr);

    const auto parsed = parsePayload(payloadOf(packet));
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Cancel);
    QCOMPARE(parsed.report.fileHash, hash);
    QCOMPARE(parsed.report.partIndex, uint32{9});
}

// ---------------------------------------------------------------------------
// Hostile input
// ---------------------------------------------------------------------------

void tst_HttpCacheOffer::parse_rejectsTooShort()
{
    QCOMPARE(HttpCacheCodec::parse(nullptr, 0).kind, HttpCacheCodec::Kind::Invalid);

    const QByteArray stub(2, '\x01');
    QCOMPARE(parsePayload(stub).kind, HttpCacheCodec::Kind::Invalid);
}

void tst_HttpCacheOffer::parse_rejectsWrongVersion()
{
    QByteArray payload = payloadOf(HttpCacheCodec::buildOffer(goodOffer()));
    payload[0] = '\x02';

    const auto parsed = parsePayload(payload);
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Invalid);
    QVERIFY(parsed.error.contains(QStringLiteral("version")));
}

void tst_HttpCacheOffer::parse_rejectsUnknownSubOpcode()
{
    QByteArray payload = payloadOf(HttpCacheCodec::buildOffer(goodOffer()));
    payload[1] = '\x7F';

    // Unlike an unknown tag, an unknown sub-opcode is not skippable: the sender
    // is waiting on a reply we would never send.
    const auto parsed = parsePayload(payload);
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Invalid);
    QVERIFY(parsed.error.contains(QStringLiteral("sub-opcode")));
}

void tst_HttpCacheOffer::parse_rejectsTruncatedTagBlock()
{
    const QByteArray full = payloadOf(HttpCacheCodec::buildOffer(goodOffer()));

    // Cut at every length; none may crash, over-read, or come back as an Offer.
    for (qsizetype cut = 3; cut < full.size(); ++cut) {
        const auto parsed = parsePayload(full.left(cut));
        QVERIFY2(parsed.kind != HttpCacheCodec::Kind::Offer,
                 qPrintable(QStringLiteral("truncation at %1 parsed as a valid offer").arg(cut)));
    }
}

void tst_HttpCacheOffer::parse_rejectsMissingHashOrPart()
{
    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_OFFER);
    data.writeUInt8(1);
    Tag(HCTAG_PARTINDEX, uint32{4}).writeNewEd2kTag(data); // no HCTAG_FILEID

    const auto parsed = parsePayload(data.buffer());
    QCOMPARE(parsed.kind, HttpCacheCodec::Kind::Invalid);
}

void tst_HttpCacheOffer::parse_skipsUnknownTags()
{
    const HttpCacheOffer sent = goodOffer();
    QByteArray keyIv = sent.key;
    keyIv.append(sent.iv);

    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_OFFER);
    data.writeUInt8(10);

    Tag(HCTAG_FILEID, sent.fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, sent.partIndex).writeNewEd2kTag(data);
    Tag(HCTAG_PLAINLEN, static_cast<uint32>(sent.plainLength)).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERLEN, static_cast<uint32>(sent.cipherLength)).writeNewEd2kTag(data);
    Tag(HCTAG_URL, sent.url).writeNewEd2kTag(data);
    Tag(HCTAG_KEYIV, keyIv).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERSHA, sent.cipherSha256).writeNewEd2kTag(data);
    Tag(HCTAG_EXPIRES, sent.expiresAt).writeNewEd2kTag(data);
    // Two tags from a hypothetical future version, in the middle and at the end.
    Tag(uint8{0x7E}, QStringLiteral("something new")).writeNewEd2kTag(data);
    Tag(uint8{0x7F}, uint32{12345}).writeNewEd2kTag(data);

    const auto parsed = parsePayload(data.buffer());
    QVERIFY2(parsed.kind == HttpCacheCodec::Kind::Offer, qPrintable(parsed.error));
    QCOMPARE(parsed.offer.url, sent.url);
    QCOMPARE(parsed.offer.key, sent.key);
}

void tst_HttpCacheOffer::parse_rejectsOversizedUrl()
{
    const HttpCacheOffer sent = goodOffer();
    QByteArray keyIv = sent.key;
    keyIv.append(sent.iv);

    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_OFFER);
    data.writeUInt8(8);

    Tag(HCTAG_FILEID, sent.fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, sent.partIndex).writeNewEd2kTag(data);
    Tag(HCTAG_PLAINLEN, static_cast<uint32>(sent.plainLength)).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERLEN, static_cast<uint32>(sent.cipherLength)).writeNewEd2kTag(data);
    Tag(HCTAG_URL, QStringLiteral("http://h/") + QString(HTTPCACHE_MAX_URL_LEN + 100,
                                                        QLatin1Char('x')))
        .writeNewEd2kTag(data);
    Tag(HCTAG_KEYIV, keyIv).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERSHA, sent.cipherSha256).writeNewEd2kTag(data);
    Tag(HCTAG_EXPIRES, sent.expiresAt).writeNewEd2kTag(data);

    // The URL is dropped rather than stored, which then fails validation — an
    // oversized URL must never be copied into the offer at all.
    QCOMPARE(parsePayload(data.buffer()).kind, HttpCacheCodec::Kind::Invalid);
}

void tst_HttpCacheOffer::parse_rejectsWrongKeyIvLength()
{
    const HttpCacheOffer sent = goodOffer();

    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_OFFER);
    data.writeUInt8(8);

    Tag(HCTAG_FILEID, sent.fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, sent.partIndex).writeNewEd2kTag(data);
    Tag(HCTAG_PLAINLEN, static_cast<uint32>(sent.plainLength)).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERLEN, static_cast<uint32>(sent.cipherLength)).writeNewEd2kTag(data);
    Tag(HCTAG_URL, sent.url).writeNewEd2kTag(data);
    Tag(HCTAG_KEYIV, QByteArray(20, '\x11')).writeNewEd2kTag(data); // not 32+16
    Tag(HCTAG_CIPHERSHA, sent.cipherSha256).writeNewEd2kTag(data);
    Tag(HCTAG_EXPIRES, sent.expiresAt).writeNewEd2kTag(data);

    QCOMPARE(parsePayload(data.buffer()).kind, HttpCacheCodec::Kind::Invalid);
}

QTEST_MAIN(tst_HttpCacheOffer)
// ---------------------------------------------------------------------------
// URL screening
//
// urlIsAcceptable() decides whether we will dial a host somebody else picked — a
// peer's offer or a Kad chunk record — so it is the gate that keeps the feature
// from being an open redirector into the local network. It used to look at IPv4
// literals only, which let every IPv6 literal and every ::ffff:a.b.c.d through.
// ---------------------------------------------------------------------------

void tst_HttpCacheOffer::urlIsAcceptable_screensBothFamilies_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("acceptable");

    QTest::newRow("hostname")            << QStringLiteral("http://example.com/x")        << true;
    QTest::newRow("https hostname")      << QStringLiteral("https://example.com/x")       << true;
    QTest::newRow("public v4")           << QStringLiteral("http://93.184.216.34/x")      << true;
    QTest::newRow("public v6")           << QStringLiteral("http://[2606:4700::1]/x")     << true;

    QTest::newRow("v4 loopback")         << QStringLiteral("http://127.0.0.1/x")          << false;
    QTest::newRow("v4 private")          << QStringLiteral("http://192.168.1.1/x")        << false;
    QTest::newRow("v6 loopback")         << QStringLiteral("http://[::1]/x")              << false;
    QTest::newRow("v6 ULA")              << QStringLiteral("http://[fd00::1]/x")          << false;
    QTest::newRow("v6 link-local")       << QStringLiteral("http://[fe80::1]/x")          << false;
    QTest::newRow("v6 documentation")    << QStringLiteral("http://[2001:db8::1]/x")      << false;
    QTest::newRow("v6 multicast")        << QStringLiteral("http://[ff02::1]/x")          << false;
    QTest::newRow("v6 6to4")             << QStringLiteral("http://[2002::1]/x")          << false;
    // The one Qt itself hides: protocol() calls a v4-mapped literal IPv6, so it walked
    // straight past a check written as "if this is IPv4".
    QTest::newRow("v4-mapped loopback")  << QStringLiteral("http://[::ffff:127.0.0.1]/x") << false;
    QTest::newRow("v4 wildcard")         << QStringLiteral("http://0.0.0.0/x")            << false;
    QTest::newRow("v6 unspecified")      << QStringLiteral("http://[::]/x")               << false;

    QTest::newRow("wrong scheme")        << QStringLiteral("ftp://example.com/x")         << false;
    QTest::newRow("file scheme")         << QStringLiteral("file:///etc/passwd")          << false;
    QTest::newRow("no host")             << QStringLiteral("http:///nohost")              << false;
    QTest::newRow("not a url")           << QStringLiteral("not a url")                   << false;
    QTest::newRow("empty")               << QString()                                     << false;
}

void tst_HttpCacheOffer::urlIsAcceptable_screensBothFamilies()
{
    QFETCH(QString, url);
    QFETCH(bool, acceptable);

    // Both switches are process-wide and decide half these rows, so pin them: lab mode
    // widens the IPv6 rules and filterLANIPs the IPv4 ones. This case asserts the
    // production settings.
    const ScopedLabNetworkMode labOff{false};
    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(true);
    const auto restore = qScopeGuard([savedFilter] { thePrefs.setFilterLANIPs(savedFilter); });

    // A local sender, so every refusal below is the URL's doing and not the sender's.
    QCOMPARE(HttpCacheManager::urlIsAcceptable(url, Address::fromString(QStringLiteral("192.168.7.23"))),
             acceptable);
}

void tst_HttpCacheOffer::urlIsAcceptable_labModeOpensUpIPv6Only()
{
    // An operator who clears filterLANIPs has declared this a private network, and a
    // cache server on ::1 or a ULA is then the normal case — the interop rigs run that
    // way. The relaxation is deliberately IPv6-only, so the IPv4 side takes its own
    // switch; asserting both here keeps the two from being quietly merged.
    const ScopedLabNetworkMode labOn{true};
    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(false);
    const auto restore = qScopeGuard([savedFilter] { thePrefs.setFilterLANIPs(savedFilter); });

    const Address loopbackPeer = Address::fromString(QStringLiteral("127.0.0.3"));

    QVERIFY(HttpCacheManager::urlIsAcceptable(QStringLiteral("http://[::1]:8080/x"), loopbackPeer));
    QVERIFY(HttpCacheManager::urlIsAcceptable(QStringLiteral("http://[fd00::1]/x"), loopbackPeer));
    QVERIFY(HttpCacheManager::urlIsAcceptable(QStringLiteral("http://127.0.0.1:8080/x"), loopbackPeer));

    // Still not peer addresses under any configuration.
    QVERIFY(!HttpCacheManager::urlIsAcceptable(QStringLiteral("http://[ff02::1]/x"), loopbackPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(QStringLiteral("http://[::]/x"), loopbackPeer));
}

void tst_HttpCacheOffer::urlIsAcceptable_localHostNeedsALocalSender()
{
    // LAN mode opens local hosts up, but only to somebody who is local too: without
    // this any peer on the internet — or any Kad record — could aim us at this
    // machine or at a box on its LAN.
    const ScopedLabNetworkMode labOn{true};
    const bool savedFilter = thePrefs.filterLANIPs();
    thePrefs.setFilterLANIPs(false);
    const auto restore = qScopeGuard([savedFilter] { thePrefs.setFilterLANIPs(savedFilter); });

    const Address lanPeer = Address::fromString(QStringLiteral("192.168.7.23"));
    const Address loopbackPeer = Address::fromString(QStringLiteral("127.0.0.3"));
    const Address publicPeer = Address::fromString(QStringLiteral("87.65.43.21"));
    const Address nobody;   // a Kad record

    const QString lanUrl = QStringLiteral("http://192.168.7.9:8080/x");
    const QString loopbackUrl = QStringLiteral("http://127.0.0.1:8080/x");
    const QString publicUrl = QStringLiteral("http://93.184.216.34/x");

    QVERIFY(HttpCacheManager::urlIsAcceptable(lanUrl, lanPeer));
    QVERIFY(HttpCacheManager::urlIsAcceptable(lanUrl, loopbackPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(lanUrl, publicPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(lanUrl, nobody));

    // A loopback URL is this machine: a LAN peer's 127.0.0.1 is not ours.
    QVERIFY(HttpCacheManager::urlIsAcceptable(loopbackUrl, loopbackPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(loopbackUrl, lanPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(loopbackUrl, publicPeer));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(loopbackUrl, nobody));
    QVERIFY(!HttpCacheManager::urlIsAcceptable(QStringLiteral("http://[::1]:8080/x"), publicPeer));

    // A public host never depended on the sender.
    QVERIFY(HttpCacheManager::urlIsAcceptable(publicUrl, publicPeer));
    QVERIFY(HttpCacheManager::urlIsAcceptable(publicUrl, nobody));
}

// ---------------------------------------------------------------------------
// Local-address rule
// ---------------------------------------------------------------------------

void tst_HttpCacheOffer::reach_classifiesAddresses_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<CacheReach>("reach");

    QTest::newRow("public v4")     << QStringLiteral("93.184.216.34") << CacheReach::Public;
    QTest::newRow("public v6")     << QStringLiteral("2606:4700::1")  << CacheReach::Public;
    QTest::newRow("v4 loopback")   << QStringLiteral("127.0.0.1")     << CacheReach::Loopback;
    QTest::newRow("v4 loopback 2") << QStringLiteral("127.8.9.10")    << CacheReach::Loopback;
    QTest::newRow("v6 loopback")   << QStringLiteral("::1")           << CacheReach::Loopback;
    QTest::newRow("10/8")          << QStringLiteral("10.20.30.40")   << CacheReach::Lan;
    QTest::newRow("172.16/12")     << QStringLiteral("172.20.1.2")    << CacheReach::Lan;
    QTest::newRow("192.168/16")    << QStringLiteral("192.168.7.23")  << CacheReach::Lan;
    QTest::newRow("v4 link-local") << QStringLiteral("169.254.3.4")   << CacheReach::Lan;
    QTest::newRow("v6 link-local") << QStringLiteral("fe80::12:34")   << CacheReach::Lan;
    QTest::newRow("v6 ULA")        << QStringLiteral("fd00::12:34")   << CacheReach::Lan;
    // Just outside the private ranges.
    QTest::newRow("172.32")        << QStringLiteral("172.32.1.2")    << CacheReach::Public;
    QTest::newRow("192.169")       << QStringLiteral("192.169.7.23")  << CacheReach::Public;
}

void tst_HttpCacheOffer::reach_classifiesAddresses()
{
    QFETCH(QString, address);
    QFETCH(CacheReach, reach);

    QCOMPARE(classifyCacheAddress(Address::fromString(address)), reach);
}

void tst_HttpCacheOffer::reach_classifiesHostLiterals()
{
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("localhost")), CacheReach::Loopback);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("LocalHost")), CacheReach::Loopback);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("cache.localhost")), CacheReach::Loopback);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("127.0.0.1")), CacheReach::Loopback);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("::ffff:127.0.0.1")), CacheReach::Loopback);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("::ffff:192.168.7.23")), CacheReach::Lan);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("176.125.242.230")), CacheReach::Public);

    // A name says nothing until it is resolved, and the wildcard is nowhere.
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("danielmac.local")), CacheReach::Unknown);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("example.com")), CacheReach::Unknown);
    QCOMPARE(classifyCacheHostLiteral(QStringLiteral("0.0.0.0")), CacheReach::Unknown);
    QCOMPARE(classifyCacheAddress(Address{}), CacheReach::Unknown);

    QCOMPARE(strictestCacheReach(CacheReach::Public, CacheReach::Lan), CacheReach::Lan);
    QCOMPARE(strictestCacheReach(CacheReach::Loopback, CacheReach::Lan), CacheReach::Loopback);
    QCOMPARE(strictestCacheReach(CacheReach::Public, CacheReach::Public), CacheReach::Public);
}

void tst_HttpCacheOffer::reach_allowsPeer_data()
{
    QTest::addColumn<CacheReach>("reach");
    QTest::addColumn<QString>("peer");
    QTest::addColumn<bool>("lanMode");
    QTest::addColumn<bool>("allowed");

    const QString pub = QStringLiteral("87.65.43.21");
    const QString lan = QStringLiteral("192.168.7.23");
    const QString loop = QStringLiteral("127.0.0.3");

    QTest::newRow("public server, public peer")       << CacheReach::Public   << pub  << false << true;
    QTest::newRow("public server, lan peer")          << CacheReach::Public   << lan  << false << true;

    QTest::newRow("lan server, lan peer, lan mode")   << CacheReach::Lan      << lan  << true  << true;
    QTest::newRow("lan server, loop peer, lan mode")  << CacheReach::Lan      << loop << true  << true;
    QTest::newRow("lan server, public peer")          << CacheReach::Lan      << pub  << true  << false;
    QTest::newRow("lan server, lan peer, filtered")   << CacheReach::Lan      << lan  << false << false;
    QTest::newRow("lan server, v6 ULA peer")          << CacheReach::Lan
                                                      << QStringLiteral("fd00::12:34") << true << true;

    QTest::newRow("loop server, loop peer, lan mode") << CacheReach::Loopback << loop << true  << true;
    QTest::newRow("loop server, lan peer")            << CacheReach::Loopback << lan  << true  << false;
    QTest::newRow("loop server, public peer")         << CacheReach::Loopback << pub  << true  << false;
    QTest::newRow("loop server, loop peer, filtered") << CacheReach::Loopback << loop << false << false;

    QTest::newRow("unresolved, lan peer")             << CacheReach::Unknown  << lan  << true  << false;
    QTest::newRow("unresolved, public peer")          << CacheReach::Unknown  << pub  << true  << false;
}

void tst_HttpCacheOffer::reach_allowsPeer()
{
    QFETCH(CacheReach, reach);
    QFETCH(QString, peer);
    QFETCH(bool, lanMode);
    QFETCH(bool, allowed);

    QCOMPARE(cacheReachAllowsPeer(reach, Address::fromString(peer), lanMode), allowed);
}

void tst_HttpCacheOffer::chooseServer_skipsIneligibleServers()
{
    // The filter is how process() passes over a server on a local address that the
    // peers waiting for a chunk cannot reach. It is a skip like any other: the
    // rotation moves on to the next server instead of giving up.
    const QList<HttpCacheServerConfig> servers{
        {QStringLiteral("local"), QStringLiteral("http://192.168.7.9:8080"),
         QStringLiteral("key"), {}, true},
        {QStringLiteral("public"), QStringLiteral("http://176.125.242.230:8080"),
         QStringLiteral("key"), {}, true},
    };
    const QHash<QString, HttpCacheManager::ServerHealth> healthy;

    const auto publicOnly = [](const HttpCacheServerConfig& server) {
        return classifyCacheHostLiteral(QUrl(server.baseUrl).host()) == CacheReach::Public;
    };

    QCOMPARE(HttpCacheManager::chooseServer(servers, healthy, 0, 1000), 0);
    QCOMPARE(HttpCacheManager::chooseServer(servers, healthy, 0, 1000, publicOnly), 1);
    QCOMPARE(HttpCacheManager::chooseServer(servers, healthy, 1, 1000, publicOnly), 1);

    const auto nobody = [](const HttpCacheServerConfig&) { return false; };
    QCOMPARE(HttpCacheManager::chooseServer(servers, healthy, 0, 1000, nobody), -1);
}

#include "tst_HttpCacheOffer.moc"
