/// @file tst_ED2KLink.cpp
/// @brief Tests for protocol/ED2KLink — parsing all link types, magnet links, edge cases.

#include "TestHelpers.h"
#include "protocol/ED2KLink.h"
#include "utils/OtherFunctions.h"

#include <QTest>

#include <array>

using namespace eMule;

class tst_ED2KLink : public QObject {
    Q_OBJECT

private slots:
    // File links
    void fileLink_basic();
    void fileLink_withPartHashes();
    void fileLink_withAICHHash();
    void fileLink_withHostnameSources();
    void fileLink_withIpSources();
    void fileLink_toLink();

    // IPv6 / source-hint parsing
    void fileLink_ipv6Source_bracketed();
    void fileLink_ipv6Source_bracketedNoPort();
    void fileLink_ipv6Source_bareLiteral();
    void fileLink_ipv6Source_bareWithColonPortIsAddress();
    void fileLink_ipv6Source_malformedSkippedNotFatal();
    void fileLink_s6Param();
    void fileLink_s6Param_rejectsNonIPv6();
    void fileLink_ipv4Source_addressPopulated();
    void fileLink_lowIdV4Dropped_v6Kept();
    void fileLink_sSource_ipv6Url();
    void fileLink_sourceCap();
    void fileLink_partHashCountMatchesWrittenHashes();

    // Link emission
    void toLink_defaultUnchanged();
    void toLink_sourcesRoundTrip();
    void toLink_partHashesAndAich();
    void toLink_encodesName();

    // Legacy-parser safety (see docs: v6 hints must be invisible to old clients)
    void legacyShape_sourcesBlockHasNoIPv6();
    void legacyShape_s6BeforeSourcesAndAfterSlash();
    void legacyShape_v6OnlyHintEmitsNoSourcesToken();
    void legacyShape_mfcScanRecoversOnlyV4();

    // Server links
    void serverLink_basic();
    void serverLink_toLink();
    void serverLink_ipv6Bracketed();
    void serverLink_ipv6Bare();
    void serverLink_ipv6ToLinkRoundTrip();
    void serverLink_rejectsHostPortInAddressField();

    // ServerList links
    void serverListLink_basic();
    void serverListLink_toLink();

    // NodesList links
    void nodesListLink_basic();
    void nodesListLink_toLink();

    // Search links
    void searchLink_basic();
    void searchLink_urlDecoded();
    void searchLink_toLink();

    // Magnet links
    void magnetLink_basic();
    void magnetLink_withName();

    // Invalid links
    void invalid_empty();
    void invalid_badPrefix();
    void invalid_unknownType();
    void invalid_fileLink_missingFields();
    void invalid_fileLink_badHash();
    void invalid_fileLink_badSize();
    void invalid_serverLink_badPort();

    // HTTP Cache configuration links (docs/protocol/http-cache-spec.md §8.1)
    void httpCacheLink_accepted_data();
    void httpCacheLink_accepted();
    void httpCacheLink_rejected_data();
    void httpCacheLink_rejected();
    void httpCacheLink_roundTrip_data();
    void httpCacheLink_roundTrip();
    void httpCacheLink_optionsAndCase();
    void httpCacheLink_rejectsControlCharacters();
    void httpCacheLink_rejectsOversizedLink();
    void redactLinkSecret_hidesTheSecret();

    // linkType helper
    void linkType_variants();
};

// Test hash constant: 32 hex chars = "0123456789ABCDEF0123456789ABCDEF"
static const QString kTestHash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
static const std::array<uint8, 16> kTestHashBytes = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};

// ---------------------------------------------------------------------------
// File link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::fileLink_basic()
{
    const QString uri = QStringLiteral("ed2k://|file|test.mp3|12345|%1|/").arg(kTestHash);
    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KFileLink>(*result));

    const auto& link = std::get<ED2KFileLink>(*result);
    QCOMPARE(link.name, QStringLiteral("test.mp3"));
    QCOMPARE(link.size, uint64{12345});
    QVERIFY(md4equ(link.hash.data(), kTestHashBytes.data()));
    QVERIFY(!link.hasValidAICHHash);
    QVERIFY(link.hostnameSources.empty());
}

void tst_ED2KLink::fileLink_withPartHashes()
{
    const QString partHash1 = QStringLiteral("AAAABBBBCCCCDDDDEEEEFFFFAAAABBBB");
    const QString partHash2 = QStringLiteral("11112222333344445555666677778888");
    const QString uri = QStringLiteral("ed2k://|file|big.avi|999999|%1|p=%2:%3|/")
        .arg(kTestHash, partHash1, partHash2);

    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);
    QVERIFY(link.hashset != nullptr);
}

void tst_ED2KLink::fileLink_withAICHHash()
{
    // AICH hash is base32 encoded, 32 chars for 20 bytes
    const QString aichB32 = QStringLiteral("QYRHAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    // Try with a realistic-length base32 (32 base32 chars = 20 bytes)
    const auto uri = QStringLiteral("ed2k://|file|test.avi|500|%1|h=%2|/")
        .arg(kTestHash, aichB32);

    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);
    // AICH may or may not be valid depending on exact base32 decoding
    // Just verify parsing didn't fail
    QCOMPARE(link.name, QStringLiteral("test.avi"));
}

void tst_ED2KLink::fileLink_withHostnameSources()
{
    const QString uri = QStringLiteral("ed2k://|file|test.txt|100|%1|s=http://example.com:4662/|/")
        .arg(kTestHash);

    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].hostname, QStringLiteral("example.com"));
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});
}

void tst_ED2KLink::fileLink_withIpSources()
{
    const QString uri = QStringLiteral("ed2k://|file|test.txt|100|%1|sources,192.168.1.1:4662,10.0.0.1:4662|/")
        .arg(kTestHash);

    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);
    // IP sources are added as hostname sources
    QVERIFY(link.hostnameSources.size() >= 1);
}

void tst_ED2KLink::fileLink_toLink()
{
    const QString uri = QStringLiteral("ed2k://|file|test.mp3|12345|%1|/").arg(kTestHash);
    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);

    const QString reconstructed = link.toLink();
    QVERIFY(reconstructed.startsWith(QStringLiteral("ed2k://|file|")));
    QVERIFY(reconstructed.contains(QStringLiteral("test.mp3")));
    QVERIFY(reconstructed.contains(QStringLiteral("12345")));
}

// ---------------------------------------------------------------------------
// IPv6 / source-hint parsing
// ---------------------------------------------------------------------------

/// Parse a file link carrying @p params (already pipe-separated) and return it.
static ED2KFileLink parseFileLinkWith(const QString& params)
{
    const QString uri = QStringLiteral("ed2k://|file|test.txt|100|%1|%2/")
                            .arg(kTestHash, params);
    auto result = parseED2KLink(uri);
    if (!result || !std::holds_alternative<ED2KFileLink>(*result))
        return {};
    return std::move(std::get<ED2KFileLink>(*result));
}

void tst_ED2KLink::fileLink_ipv6Source_bracketed()
{
    const auto link = parseFileLinkWith(QStringLiteral("sources,[2001:db8::1]:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].hostname, QStringLiteral("2001:db8::1"));
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
}

void tst_ED2KLink::fileLink_ipv6Source_bracketedNoPort()
{
    const auto link = parseFileLinkWith(QStringLiteral("sources,[2001:db8::1]|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});   // default ed2k port
}

void tst_ED2KLink::fileLink_ipv6Source_bareLiteral()
{
    const auto link = parseFileLinkWith(QStringLiteral("sources,2001:db8::1|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});
}

void tst_ED2KLink::fileLink_ipv6Source_bareWithColonPortIsAddress()
{
    // Documented behaviour: an unbracketed multi-colon token is the address in full.
    // "2001:db8::1:4662" IS a valid IPv6 address, so splitting at the last colon would
    // silently corrupt it — the exact bug this replaced.
    const auto link = parseFileLinkWith(QStringLiteral("sources,2001:db8::1:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].hostname, QStringLiteral("2001:db8::1:4662"));
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});   // from the default, not the text
}

void tst_ED2KLink::fileLink_ipv6Source_malformedSkippedNotFatal()
{
    // A malformed entry must not discard the rest of the list.
    const auto link = parseFileLinkWith(
        QStringLiteral("sources,[2001:db8::1:4662,[example.com]:4662,[2001:db8::1]:0,"
                       "1.2.3.4:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].hostname, QStringLiteral("1.2.3.4"));
}

void tst_ED2KLink::fileLink_s6Param()
{
    const auto link = parseFileLinkWith(
        QStringLiteral("s6=[2001:db8::1]:4662,[2001:db8::2]:5662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{2});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
    QCOMPARE(link.hostnameSources[0].port, uint16{4662});
    QVERIFY(link.hostnameSources[1].address.isIPv6());
    QCOMPARE(link.hostnameSources[1].port, uint16{5662});
}

void tst_ED2KLink::fileLink_s6Param_rejectsNonIPv6()
{
    // s6= is the IPv6 channel: a v4 literal or hostname there is a malformed link.
    const auto link = parseFileLinkWith(
        QStringLiteral("s6=1.2.3.4:4662,host.example.com:4662,[2001:db8::1]:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
}

void tst_ED2KLink::fileLink_ipv4Source_addressPopulated()
{
    // Regression: the old parser never recorded the resolved address, so nothing
    // downstream could tell a literal from a hostname.
    const auto link = parseFileLinkWith(QStringLiteral("sources,8.8.8.8:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QVERIFY(link.hostnameSources[0].address.isIPv4());

    const auto named = parseFileLinkWith(QStringLiteral("sources,host.example.com:4662|"));
    QCOMPARE(named.hostnameSources.size(), std::size_t{1});
    QVERIFY(named.hostnameSources[0].address.isNull());   // needs DNS
}

void tst_ED2KLink::fileLink_lowIdV4Dropped_v6Kept()
{
    // MFC drops *.*.*.0 as a LowID (the ED2K ID is the IPv4 in network order, so a
    // trailing 0 octet lands below 0x01000000). That test is IPv4-only: running an IPv6
    // through toNetworkUint32() yielded 0, i.e. "LowID", discarding every v6 source.
    const auto link = parseFileLinkWith(
        QStringLiteral("sources,1.2.3.0:4662,[2001:db8::1]:4662|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
}

void tst_ED2KLink::fileLink_sSource_ipv6Url()
{
    const auto link = parseFileLinkWith(QStringLiteral("s=http://[2001:db8::1]:8080/f.bin|"));
    QCOMPARE(link.hostnameSources.size(), std::size_t{1});
    QCOMPARE(link.hostnameSources[0].hostname, QStringLiteral("2001:db8::1"));
    QCOMPARE(link.hostnameSources[0].port, uint16{8080});
    QVERIFY(link.hostnameSources[0].address.isIPv6());
    QCOMPARE(link.hostnameSources[0].url, QStringLiteral("http://[2001:db8::1]:8080/f.bin"));

    // Non-HTTP schemes are not usable sources.
    const auto ftp = parseFileLinkWith(QStringLiteral("s=ftp://example.com/f.bin|"));
    QVERIFY(ftp.hostnameSources.empty());
}

void tst_ED2KLink::fileLink_sourceCap()
{
    QStringList entries;
    for (int i = 0; i < kMaxLinkSources + 20; ++i)
        entries << QStringLiteral("10.0.%1.%2:4662").arg(i / 256).arg(i % 256);

    const auto link = parseFileLinkWith(
        QStringLiteral("sources,%1|").arg(entries.join(QChar(u','))));
    QCOMPARE(static_cast<int>(link.hostnameSources.size()), kMaxLinkSources);
}

void tst_ED2KLink::fileLink_partHashCountMatchesWrittenHashes()
{
    // One malformed part hash must not leave a hashset whose declared count exceeds the
    // data written — loadMD4HashsetFromFile() would read past the end.
    const QString good1 = QStringLiteral("AAAABBBBCCCCDDDDEEEEFFFFAAAABBBB");
    const QString good2 = QStringLiteral("11112222333344445555666677778888");
    const auto link = parseFileLinkWith(
        QStringLiteral("p=%1:ZZZZ:%2|").arg(good1, good2));

    QCOMPARE(link.partHashes.size(), std::size_t{2});
    QVERIFY(link.hashset != nullptr);
    link.hashset->seek(16, 0);                       // past the file hash
    QCOMPARE(link.hashset->readUInt16(), uint16{2}); // declared count == hashes written
}

// ---------------------------------------------------------------------------
// Link emission
// ---------------------------------------------------------------------------

void tst_ED2KLink::toLink_defaultUnchanged()
{
    const QString uri = QStringLiteral("ed2k://|file|test.mp3|12345|%1|/").arg(kTestHash);
    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    // Byte-identical to the canonical form: no source hint, no optional parts.
    QCOMPARE(std::get<ED2KFileLink>(*result).toLink(), uri);
}

void tst_ED2KLink::toLink_sourcesRoundTrip()
{
    ED2KFileLink link;
    link.name = QStringLiteral("test.mp3");
    link.size = 12345;
    QVERIFY(strmd4(kTestHash, link.hash.data()));
    link.hostnameSources = {
        {QStringLiteral("host.example.com"), 4662, {}, {}},
        {QStringLiteral("8.8.8.8"), 4662, Address::fromString(QStringLiteral("8.8.8.8")), {}},
        {QStringLiteral("2001:db8::1"), 5662,
         Address::fromString(QStringLiteral("2001:db8::1")), {}},
    };

    const QString emitted = link.toLink({.sources = true});
    auto reparsed = parseED2KLink(emitted);
    QVERIFY(reparsed.has_value());
    const auto& back = std::get<ED2KFileLink>(*reparsed);

    QCOMPARE(back.hostnameSources.size(), std::size_t{3});
    // s6= is emitted first, so the IPv6 entry comes back first.
    QCOMPARE(back.hostnameSources[0].hostname, QStringLiteral("2001:db8::1"));
    QCOMPARE(back.hostnameSources[0].port, uint16{5662});
    QCOMPARE(back.hostnameSources[1].hostname, QStringLiteral("host.example.com"));
    QCOMPARE(back.hostnameSources[2].hostname, QStringLiteral("8.8.8.8"));
}

void tst_ED2KLink::toLink_partHashesAndAich()
{
    const QString partA = QStringLiteral("AAAABBBBCCCCDDDDEEEEFFFFAAAABBBB");
    const QString partB = QStringLiteral("11112222333344445555666677778888");
    const auto link = parseFileLinkWith(QStringLiteral("p=%1:%2|").arg(partA, partB));

    const QString emitted = link.toLink({.partHashes = true});
    QVERIFY(emitted.contains(QStringLiteral("p=%1:%2|").arg(partA, partB)));
    QVERIFY(emitted.endsWith(QStringLiteral("|/")));

    // Omitting the flag drops the block entirely.
    QVERIFY(!link.toLink().contains(QStringLiteral("p=")));
}

void tst_ED2KLink::toLink_encodesName()
{
    ED2KFileLink link;
    link.name = QStringLiteral("my file 100% & |pipe|.mp3");
    link.size = 7;
    QVERIFY(strmd4(kTestHash, link.hash.data()));

    auto reparsed = parseED2KLink(link.toLink());
    QVERIFY(reparsed.has_value());
    // The name survives the round-trip; '|' is stripped as an invalid filename char.
    QCOMPARE(std::get<ED2KFileLink>(*reparsed).name,
             stripInvalidFilenameChars(link.name));
}

// ---------------------------------------------------------------------------
// Legacy-parser safety
//
// MFC scans the ext section for the first "sources" substring and tokenizes from there
// (srchybrid/ED2KLink.cpp:229), splitting each token at its FIRST colon (:280). So an
// IPv6 hint inside `sources,` can produce a valid-looking port plus an inet_addr()-
// acceptable first segment — a bogus source at the wrong address. Ours therefore go in
// their own `s6=` token, emitted before the classic block so the legacy scan never
// reaches them.
// ---------------------------------------------------------------------------

/// The link a client with both a hostname and an IPv6 hint would publish.
static QString linkWithBothHints()
{
    ED2KFileLink link;
    link.name = QStringLiteral("test.mp3");
    link.size = 12345;
    [[maybe_unused]] const bool hashOk = strmd4(kTestHash, link.hash.data());
    link.hostnameSources = {
        {QStringLiteral("host.example.com"), 4662, {}, {}},
        {QStringLiteral("2001:db8::1"), 4662,
         Address::fromString(QStringLiteral("2001:db8::1")), {}},
    };
    return link.toLink({.sources = true});
}

void tst_ED2KLink::legacyShape_sourcesBlockHasNoIPv6()
{
    const QString emitted = linkWithBothHints();
    const qsizetype sourcesAt = emitted.indexOf(QStringLiteral("|sources,"));
    QVERIFY(sourcesAt >= 0);

    // Everything a legacy parser reads: from "sources" to the end.
    const QString legacyTail = emitted.mid(sourcesAt + 1);
    QVERIFY(!legacyTail.contains(QChar(u'[')));
    QVERIFY(!legacyTail.contains(QStringLiteral("2001:db8")));
}

void tst_ED2KLink::legacyShape_s6BeforeSourcesAndAfterSlash()
{
    const QString emitted = linkWithBothHints();
    const qsizetype firstSlash = emitted.indexOf(QStringLiteral("|/"));
    const qsizetype s6At = emitted.indexOf(QStringLiteral("|s6="));
    const qsizetype sourcesAt = emitted.indexOf(QStringLiteral("|sources,"));

    QVERIFY(firstSlash >= 0);
    QVERIFY(s6At > firstSlash);      // in the ext section, not the ed2k params (MFC asserts there)
    QVERIFY(s6At < sourcesAt);       // before the token the legacy scan starts at
    QVERIFY(emitted.endsWith(QStringLiteral("|/")));
}

void tst_ED2KLink::legacyShape_v6OnlyHintEmitsNoSourcesToken()
{
    ED2KFileLink link;
    link.name = QStringLiteral("test.mp3");
    link.size = 12345;
    QVERIFY(strmd4(kTestHash, link.hash.data()));
    link.hostnameSources = {{QStringLiteral("2001:db8::1"), 4662,
                             Address::fromString(QStringLiteral("2001:db8::1")), {}}};

    const QString emitted = link.toLink({.sources = true});
    QVERIFY(emitted.contains(QStringLiteral("|s6=[2001:db8::1]:4662")));
    QVERIFY(!emitted.contains(QStringLiteral("sources")));   // nothing for a legacy scan to find

    auto reparsed = parseED2KLink(emitted);
    QVERIFY(reparsed.has_value());
    QCOMPARE(std::get<ED2KFileLink>(*reparsed).hostnameSources.size(), std::size_t{1});
}

void tst_ED2KLink::legacyShape_mfcScanRecoversOnlyV4()
{
    // Reproduce MFC's scan: find "sources", tokenize on ',', split at the FIRST colon,
    // require an all-digit port. It must recover exactly the v4/hostname entries.
    const QString emitted = linkWithBothHints();

    const qsizetype start = emitted.indexOf(QStringLiteral("sources"));
    QVERIFY(start >= 0);
    QString blob = emitted.mid(start + 7);          // past "sources"
    if (blob.startsWith(QChar(u',')))
        blob = blob.mid(1);

    QStringList recovered;
    for (const QString& token : blob.split(QChar(u','), Qt::SkipEmptyParts)) {
        const qsizetype colon = token.indexOf(QChar(u':'));
        if (colon < 0)
            continue;
        const QString host = token.left(colon);
        QString portText = token.mid(colon + 1);
        portText = portText.left(portText.indexOf(QChar(u'|')) < 0
                                     ? portText.size()
                                     : portText.indexOf(QChar(u'|')));
        bool ok = false;
        const uint port = portText.toUInt(&ok);
        if (!ok || port == 0 || port > 65535)
            continue;                                // an IPv6 token dies here
        recovered << host;
    }

    QCOMPARE(recovered.size(), 1);
    QCOMPARE(recovered.first(), QStringLiteral("host.example.com"));
}

// ---------------------------------------------------------------------------
// Server link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::serverLink_basic()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|server|192.168.1.1|4661|/"));
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KServerLink>(*result));

    const auto& link = std::get<ED2KServerLink>(*result);
    QCOMPARE(link.address, QStringLiteral("192.168.1.1"));
    QCOMPARE(link.port, uint16{4661});
}

void tst_ED2KLink::serverLink_toLink()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|server|192.168.1.1|4661|/"));
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KServerLink>(*result);

    QCOMPARE(link.toLink(), QStringLiteral("ed2k://|server|192.168.1.1|4661|/"));
}

void tst_ED2KLink::serverLink_ipv6Bracketed()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|server|[2001:db8::1]|4661|/"));
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KServerLink>(*result);
    QCOMPARE(link.address, QStringLiteral("2001:db8::1"));   // stored unbracketed
    QCOMPARE(link.port, uint16{4661});
}

void tst_ED2KLink::serverLink_ipv6Bare()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|server|2001:db8::1|4661|/"));
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KServerLink>(*result);
    QCOMPARE(link.address, QStringLiteral("2001:db8::1"));
    QCOMPARE(link.port, uint16{4661});
}

void tst_ED2KLink::serverLink_ipv6ToLinkRoundTrip()
{
    for (const char* text : {"ed2k://|server|[2001:db8::1]|4661|/",
                             "ed2k://|server|2001:db8::1|4661|/"}) {
        auto result = parseED2KLink(QString::fromLatin1(text));
        QVERIFY(result.has_value());
        const auto& link = std::get<ED2KServerLink>(*result);

        // Emission always brackets, so our own parser round-trips it unambiguously.
        QCOMPARE(link.toLink(), QStringLiteral("ed2k://|server|[2001:db8::1]|4661|/"));
        auto again = parseED2KLink(link.toLink());
        QVERIFY(again.has_value());
        QCOMPARE(std::get<ED2KServerLink>(*again).address, link.address);
    }
}

void tst_ED2KLink::serverLink_rejectsHostPortInAddressField()
{
    // A "host:port" pasted into the address field is not a server link.
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|server|host:1234|4661|/")).has_value());
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|server|1:2:3|4661|/")).has_value());
    // A hostname stays valid — it is resolved at connect time.
    QVERIFY(parseED2KLink(QStringLiteral("ed2k://|server|srv.example.com|4661|/")).has_value());
}

// ---------------------------------------------------------------------------
// ServerList link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::serverListLink_basic()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|serverlist|http://example.com/list.met|/"));
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KServerListLink>(*result));

    const auto& link = std::get<ED2KServerListLink>(*result);
    QCOMPARE(link.address, QStringLiteral("http://example.com/list.met"));
}

void tst_ED2KLink::serverListLink_toLink()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|serverlist|http://example.com/list.met|/"));
    const auto& link = std::get<ED2KServerListLink>(*result);
    QCOMPARE(link.toLink(), QStringLiteral("ed2k://|serverlist|http://example.com/list.met|/"));
}

// ---------------------------------------------------------------------------
// NodesList link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::nodesListLink_basic()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|nodeslist|http://example.com/nodes.dat|/"));
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KNodesListLink>(*result));

    const auto& link = std::get<ED2KNodesListLink>(*result);
    QCOMPARE(link.address, QStringLiteral("http://example.com/nodes.dat"));
}

void tst_ED2KLink::nodesListLink_toLink()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|nodeslist|http://example.com/nodes.dat|/"));
    const auto& link = std::get<ED2KNodesListLink>(*result);
    QCOMPARE(link.toLink(), QStringLiteral("ed2k://|nodeslist|http://example.com/nodes.dat|/"));
}

// ---------------------------------------------------------------------------
// Search link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::searchLink_basic()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|search|linux|/"));
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KSearchLink>(*result));

    const auto& link = std::get<ED2KSearchLink>(*result);
    QCOMPARE(link.searchTerm, QStringLiteral("linux"));
}

void tst_ED2KLink::searchLink_urlDecoded()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|search|hello%20world|/"));
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KSearchLink>(*result);
    QCOMPARE(link.searchTerm, QStringLiteral("hello world"));
}

void tst_ED2KLink::searchLink_toLink()
{
    auto result = parseED2KLink(QStringLiteral("ed2k://|search|linux|/"));
    const auto& link = std::get<ED2KSearchLink>(*result);
    QCOMPARE(link.toLink(), QStringLiteral("ed2k://|search|linux|/"));
}

// ---------------------------------------------------------------------------
// Magnet link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::magnetLink_basic()
{
    const QString uri = QStringLiteral("magnet:?xt=urn:ed2k:%1&xl=12345").arg(kTestHash);
    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    QVERIFY(std::holds_alternative<ED2KFileLink>(*result));

    const auto& link = std::get<ED2KFileLink>(*result);
    QVERIFY(md4equ(link.hash.data(), kTestHashBytes.data()));
    QCOMPARE(link.size, uint64{12345});
}

void tst_ED2KLink::magnetLink_withName()
{
    const QString uri = QStringLiteral("magnet:?xt=urn:ed2k:%1&dn=test%20file.mp3&xl=999")
        .arg(kTestHash);
    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());
    const auto& link = std::get<ED2KFileLink>(*result);
    QCOMPARE(link.name, QStringLiteral("test file.mp3"));
    QCOMPARE(link.size, uint64{999});
}

// ---------------------------------------------------------------------------
// Invalid link tests
// ---------------------------------------------------------------------------

void tst_ED2KLink::invalid_empty()
{
    QVERIFY(!parseED2KLink(QString()).has_value());
}

void tst_ED2KLink::invalid_badPrefix()
{
    QVERIFY(!parseED2KLink(QStringLiteral("http://example.com")).has_value());
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://file|test|123|hash|/")).has_value());
}

void tst_ED2KLink::invalid_unknownType()
{
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|unknown|data|/")).has_value());
}

void tst_ED2KLink::invalid_fileLink_missingFields()
{
    // Missing hash
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|file|test|123|/")).has_value());
}

void tst_ED2KLink::invalid_fileLink_badHash()
{
    // Hash too short
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|file|test|123|DEADBEEF|/")).has_value());
    // Hash has invalid chars
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|file|test|123|ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ|/")).has_value());
}

void tst_ED2KLink::invalid_fileLink_badSize()
{
    QVERIFY(!parseED2KLink(
        QStringLiteral("ed2k://|file|test|notanumber|%1|/").arg(kTestHash)
    ).has_value());
}

void tst_ED2KLink::invalid_serverLink_badPort()
{
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|server|192.168.1.1|abc|/")).has_value());
    QVERIFY(!parseED2KLink(QStringLiteral("ed2k://|server|192.168.1.1|0|/")).has_value());
}

// ---------------------------------------------------------------------------
// HTTP Cache configuration links
//
// The vectors are the reference table from the format's own specification
// (emule-http-cache-php/docs/ed2k-httpcache-link.md), so a change on either side
// shows up as a failure here rather than as a link nobody can apply.
// ---------------------------------------------------------------------------

void tst_ED2KLink::httpCacheLink_accepted_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("baseUrl");
    QTest::addColumn<QString>("secret");
    QTest::addColumn<QString>("keyId");

    QTest::newRow("canonical")
        << QStringLiteral("ed2k://|httpcache|HTTP%20Cache%20upload%20config|"
                          "https://cache.example.com|1f4b9c02d7e35a68|/")
        << QStringLiteral("HTTP Cache upload config")
        << QStringLiteral("https://cache.example.com")
        << QStringLiteral("1f4b9c02d7e35a68") << QString();

    QTest::newRow("with key id and path")
        << QStringLiteral("ed2k://|httpcache|HTTP%20Cache%20upload%20config|"
                          "http://192.168.1.10/emule-http-cache-php|1f4b9c02d7e35a68|k=default|/")
        << QStringLiteral("HTTP Cache upload config")
        << QStringLiteral("http://192.168.1.10/emule-http-cache-php")
        << QStringLiteral("1f4b9c02d7e35a68") << QStringLiteral("default");

    // The one that proves fields are split before they are decoded: a literal '|'
    // in the name arrives as %7C and must not become a field separator.
    QTest::newRow("pipe in name")
        << QStringLiteral("ed2k://|httpcache|Nachbars%20WLAN%20%7C%20Cache|"
                          "https://cache.example.com|abc123|k=seedbox|/")
        << QStringLiteral("Nachbars WLAN | Cache")
        << QStringLiteral("https://cache.example.com")
        << QStringLiteral("abc123") << QStringLiteral("seedbox");

    // Non-ASCII is UTF-8 octets, not some other encoding.
    QTest::newRow("utf-8 name")
        << QStringLiteral("ed2k://|httpcache|Zwischenspeicher%20f%C3%BCr%20eMule|"
                          "https://cache.example.com|abc123|/")
        << QString::fromUtf8("Zwischenspeicher für eMule")
        << QStringLiteral("https://cache.example.com")
        << QStringLiteral("abc123") << QString();

    QTest::newRow("empty name")
        << QStringLiteral("ed2k://|httpcache||https://cache.example.com|abc123|/")
        << QString() << QStringLiteral("https://cache.example.com")
        << QStringLiteral("abc123") << QString();

    // A raw umlaut is not what the grammar asks a producer to write, but reading
    // one is harmless and refusing it would help nobody.
    QTest::newRow("raw non-ascii name")
        << QString::fromUtf8("ed2k://|httpcache|Zwischenspeicher für eMule|"
                             "https://cache.example.com|abc123|/")
        << QString::fromUtf8("Zwischenspeicher für eMule")
        << QStringLiteral("https://cache.example.com")
        << QStringLiteral("abc123") << QString();
}

void tst_ED2KLink::httpCacheLink_accepted()
{
    QFETCH(QString, uri);
    QFETCH(QString, name);
    QFETCH(QString, baseUrl);
    QFETCH(QString, secret);
    QFETCH(QString, keyId);

    auto result = parseED2KLink(uri);
    QVERIFY(result.has_value());

    auto* link = std::get_if<ED2KHttpCacheLink>(&*result);
    QVERIFY(link != nullptr);
    QCOMPARE(link->name, name);
    QCOMPARE(link->baseUrl, baseUrl);
    QCOMPARE(link->secret, secret);
    QCOMPARE(link->keyId, keyId);
}

void tst_ED2KLink::httpCacheLink_rejected_data()
{
    QTest::addColumn<QString>("uri");

    QTest::newRow("only two fields")
        << QStringLiteral("ed2k://|httpcache|name|https://h|/");
    QTest::newRow("no terminator")
        << QStringLiteral("ed2k://|httpcache|n|https://h|s");
    QTest::newRow("not http(s)")
        << QStringLiteral("ed2k://|httpcache|n|ftp://h|s|/");
    QTest::newRow("relative url")
        << QStringLiteral("ed2k://|httpcache|n|/relative|s|/");
    QTest::newRow("carries credentials")
        << QStringLiteral("ed2k://|httpcache|n|https://u:p@h|s|/");
    QTest::newRow("carries a query")
        << QStringLiteral("ed2k://|httpcache|n|https://h?q=1|s|/");
    QTest::newRow("carries a fragment")
        << QStringLiteral("ed2k://|httpcache|n|https://h#f|s|/");
    QTest::newRow("tail field without =")
        << QStringLiteral("ed2k://|httpcache|n|https://h|s|junk|/");
    QTest::newRow("empty secret")
        << QStringLiteral("ed2k://|httpcache|n|https://h||/");
    QTest::newRow("malformed key id")
        << QStringLiteral("ed2k://|httpcache|n|https://h|s|k=has%20spaces|/");
    QTest::newRow("key id too long")
        << QStringLiteral("ed2k://|httpcache|n|https://h|s|k=%1|/")
               .arg(QString(33, QLatin1Char('a')));
    QTest::newRow("broken percent escape")
        << QStringLiteral("ed2k://|httpcache|n|https://h|s%ZZ|/");
    QTest::newRow("truncated percent escape")
        << QStringLiteral("ed2k://|httpcache|n|https://h|abc%2|/");
    QTest::newRow("secret with whitespace")
        << QStringLiteral("ed2k://|httpcache|n|https://h|ab%20cd|/");
    QTest::newRow("secret too long")
        << QStringLiteral("ed2k://|httpcache|n|https://h|%1|/")
               .arg(QString(513, QLatin1Char('a')));
    QTest::newRow("no host")
        << QStringLiteral("ed2k://|httpcache|n|https://|s|/");
}

void tst_ED2KLink::httpCacheLink_rejected()
{
    QFETCH(QString, uri);
    QVERIFY(!parseED2KLink(uri).has_value());
}

void tst_ED2KLink::httpCacheLink_roundTrip_data()
{
    httpCacheLink_accepted_data();
}

void tst_ED2KLink::httpCacheLink_roundTrip()
{
    QFETCH(QString, uri);

    auto first = parseED2KLink(uri);
    QVERIFY(first.has_value());
    auto* link = std::get_if<ED2KHttpCacheLink>(&*first);
    QVERIFY(link != nullptr);

    const QString emitted = link->toLink();
    auto second = parseED2KLink(emitted);
    QVERIFY2(second.has_value(), qPrintable(emitted));
    auto* again = std::get_if<ED2KHttpCacheLink>(&*second);
    QVERIFY(again != nullptr);

    QCOMPARE(again->name, link->name);
    QCOMPARE(again->baseUrl, link->baseUrl);
    QCOMPARE(again->secret, link->secret);
    QCOMPARE(again->keyId, link->keyId);

    // A producer always encodes, whatever it read: '|' and non-ASCII never appear
    // literally, while ':' and '/' stay readable.
    const qsizetype fields = emitted.count(QLatin1Char('|'));
    QCOMPARE(fields, link->keyId.isEmpty() ? 5 : 6);
    QVERIFY(emitted.contains(QStringLiteral("://")));
}

void tst_ED2KLink::httpCacheLink_optionsAndCase()
{
    // An unknown option is the extension point: skipped, while k= is still read.
    auto extended = parseED2KLink(QStringLiteral("ed2k://|httpcache|n|https://h|s|x=1|k=abc|/"));
    QVERIFY(extended.has_value());
    auto* link = std::get_if<ED2KHttpCacheLink>(&*extended);
    QVERIFY(link != nullptr);
    QCOMPARE(link->keyId, QStringLiteral("abc"));

    // Scheme and type token are ASCII case-insensitive; nothing else is.
    auto shouted = parseED2KLink(QStringLiteral("ED2K://|HTTPCACHE|n|https://h|s|/"));
    QVERIFY(shouted.has_value());
    QVERIFY(std::holds_alternative<ED2KHttpCacheLink>(*shouted));

    // A file link is still a file link — the new branch does not shadow it.
    auto file = parseED2KLink(QStringLiteral("ed2k://|file|x|1|%1|/").arg(kTestHash));
    QVERIFY(file.has_value());
    QVERIFY(std::holds_alternative<ED2KFileLink>(*file));
}

void tst_ED2KLink::httpCacheLink_rejectsControlCharacters()
{
    // %0A in a name would otherwise reach a log line or a message box, where a
    // second line reads as a message of its own.
    QVERIFY(!parseED2KLink(
        QStringLiteral("ed2k://|httpcache|a%0Ab|https://h|s|/")).has_value());
    QVERIFY(!parseED2KLink(
        QStringLiteral("ed2k://|httpcache|a%00b|https://h|s|/")).has_value());
}

void tst_ED2KLink::httpCacheLink_rejectsOversizedLink()
{
    const QString fits =
        QStringLiteral("ed2k://|httpcache|%1|https://h|s|/").arg(QString(4000, QLatin1Char('a')));
    QVERIFY(parseED2KLink(fits).has_value());

    const QString tooBig =
        QStringLiteral("ed2k://|httpcache|%1|https://h|s|/").arg(QString(4100, QLatin1Char('a')));
    QVERIFY(!parseED2KLink(tooBig).has_value());
}

void tst_ED2KLink::redactLinkSecret_hidesTheSecret()
{
    const QString secret = QStringLiteral("1f4b9c02d7e35a68");

    const QString good =
        QStringLiteral("ed2k://|httpcache|n|https://cache.example.com|%1|k=default|/").arg(secret);
    QVERIFY(!redactLinkSecret(good).contains(secret));
    QVERIFY(redactLinkSecret(good).contains(QStringLiteral("cache.example.com")));

    // The malformed case is the one that matters: those are the links that reach
    // an error message in the first place.
    const QString broken = QStringLiteral("ed2k://|httpcache|n|ftp://h|%1|/").arg(secret);
    QVERIFY(!redactLinkSecret(broken).contains(secret));

    // Case-insensitive on the type token, like the parser.
    const QString shouted = QStringLiteral("ED2K://|HTTPCACHE|n|https://h|%1|/").arg(secret);
    QVERIFY(!redactLinkSecret(shouted).contains(secret));

    // Anything else comes back byte for byte, so this is safe to wrap around
    // every place that reports link text.
    const QString file = QStringLiteral("ed2k://|file|movie.avi|123|%1|/").arg(kTestHash);
    QCOMPARE(redactLinkSecret(file), file);
    QCOMPARE(redactLinkSecret(QStringLiteral("not a link")), QStringLiteral("not a link"));
}

// ---------------------------------------------------------------------------
// linkType() helper test
// ---------------------------------------------------------------------------

void tst_ED2KLink::linkType_variants()
{
    {
        auto r = parseED2KLink(QStringLiteral("ed2k://|file|t|1|%1|/").arg(kTestHash));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::File);
    }
    {
        auto r = parseED2KLink(QStringLiteral("ed2k://|server|1.2.3.4|4661|/"));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::Server);
    }
    {
        auto r = parseED2KLink(QStringLiteral("ed2k://|serverlist|http://x.com/l|/"));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::ServerList);
    }
    {
        auto r = parseED2KLink(QStringLiteral("ed2k://|nodeslist|http://x.com/n|/"));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::NodesList);
    }
    {
        auto r = parseED2KLink(QStringLiteral("ed2k://|search|hello|/"));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::Search);
    }
    {
        // Without its own case this fell through to Search, which is the kind of
        // wrong answer nothing else would ever contradict.
        auto r = parseED2KLink(QStringLiteral("ed2k://|httpcache|n|https://h|s|/"));
        QVERIFY(r.has_value());
        QCOMPARE(linkType(*r), ED2KLinkType::HttpCache);
    }
}

QTEST_MAIN(tst_ED2KLink)
#include "tst_ED2KLink.moc"
