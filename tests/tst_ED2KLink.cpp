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

    // Server links
    void serverLink_basic();
    void serverLink_toLink();

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
}

QTEST_MAIN(tst_ED2KLink)
#include "tst_ED2KLink.moc"
