/// @file tst_HttpCacheOffer.cpp
/// @brief Tests for httpcache/HttpCacheOffer — the OP_HTTPCACHE codec.
///
/// This codec is the only place a remote peer's bytes are interpreted, so the
/// hostile-input cases matter as much as the round trips.

#include "TestHelpers.h"
#include "crypto/AesCbc.h"
#include "httpcache/HttpCacheOffer.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;

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
#include "tst_HttpCacheOffer.moc"
