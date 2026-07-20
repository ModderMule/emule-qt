/// @file tst_KadSearchExpr.cpp
/// @brief Round-trip tests for the Kad keyword search expression tree.
///
/// A Kad keyword search publishes its boolean expression alongside the keyword
/// hash so the serving node can filter. That requires three pieces to agree on
/// one format:
///
///   buildSearchTermsPayload()                      — encode (search module)
///   KademliaUDPListener::createSearchExpressionTree — decode (Kad listener)
///   KeyEntry::startSearchTermsMatch()               — evaluate (Kad entry)
///
/// These tests drive all three end to end. Before the expression tree was
/// wired up, every multi-word Kad search degenerated to a bare single-keyword
/// query and remote nodes returned everything under the first keyword hash.

#include "TestHelpers.h"

#include "kademlia/KadEntry.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadUDPListener.h"
#include "protocol/Tag.h"
#include "search/SearchExprParser.h"
#include "search/SearchParams.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadSearchExpr : public QObject {
    Q_OBJECT

private slots:
    void keyword_isFirstWord();
    void payload_isEmptyForKeywordOnlySearch();
    void andTerms_matchAllWordsPresent();
    void andTerms_rejectMissingWord();
    void notTerm_rejectsExcludedWord();
    void orTerm_matchesEitherBranch();
    void sizeFilter_rejectsUndersizedEntry();
    void typeFilter_matchesMetaTag();

private:
    /// Encode params for a Kad search, decode the blob back into a term tree.
    static std::unique_ptr<SearchTerm> roundTrip(const SearchParams& params);

    /// Build a candidate entry the way Search::processResultKeyword does.
    static bool matches(const SearchTerm* term, const QString& fileName,
                        uint64 size, const std::vector<Tag>& extraTags = {});
};

std::unique_ptr<SearchTerm> tst_KadSearchExpr::roundTrip(const SearchParams& params)
{
    const QString keyword = kadSearchKeyword(params.expression);
    const QByteArray blob = buildSearchTermsPayload(params, keyword);
    if (blob.isEmpty())
        return nullptr;

    SafeMemFile io(reinterpret_cast<const uint8*>(blob.constData()),
                   static_cast<qint64>(blob.size()));
    return KademliaUDPListener::createSearchExpressionTree(io, 0);
}

bool tst_KadSearchExpr::matches(const SearchTerm* term, const QString& fileName,
                                uint64 size, const std::vector<Tag>& extraTags)
{
    KeyEntry entry;
    entry.setFileName(fileName);
    entry.m_size = size;
    for (const auto& t : extraTags)
        entry.addTag(t);
    return entry.startSearchTermsMatch(*term);
}

// ---------------------------------------------------------------------------

void tst_KadSearchExpr::keyword_isFirstWord()
{
    // The search target is hashed from the first word, so the blob must be
    // built against that same word — both go through kadSearchKeyword().
    QCOMPARE(kadSearchKeyword(QStringLiteral("Ubuntu Desktop ISO")),
             QStringLiteral("ubuntu"));
    QCOMPARE(kadSearchKeyword(QStringLiteral("   ")), QString());
}

void tst_KadSearchExpr::payload_isEmptyForKeywordOnlySearch()
{
    // A single-word search needs no expression: the keyword hash already says
    // everything, and sending a redundant term would only cost bytes.
    SearchParams params;
    params.expression = QStringLiteral("ubuntu");
    params.type = eMule::SearchType::Kademlia;

    const QByteArray blob = buildSearchTermsPayload(params, kadSearchKeyword(params.expression));
    QVERIFY(blob.isEmpty());
}

void tst_KadSearchExpr::andTerms_matchAllWordsPresent()
{
    SearchParams params;
    params.expression = QStringLiteral("ubuntu desktop amd64");
    params.type = eMule::SearchType::Kademlia;

    auto term = roundTrip(params);
    QVERIFY2(term != nullptr, "multi-word search must produce an expression tree");

    // The keyword itself is dropped from the terms (the hash covers it), so the
    // remaining words are "desktop" and "amd64".
    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-desktop-amd64.iso"), 1000));
}

void tst_KadSearchExpr::andTerms_rejectMissingWord()
{
    // Regression for two coupled bugs: the decoder used to keep a multi-word
    // string term unsplit, and the matcher used to accept a term if ANY word
    // matched. Either one alone makes this file pass the filter.
    SearchParams params;
    params.expression = QStringLiteral("ubuntu desktop amd64");
    params.type = eMule::SearchType::Kademlia;

    auto term = roundTrip(params);
    QVERIFY(term != nullptr);

    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-desktop-i386.iso"), 1000));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-server-amd64.iso"), 1000));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu.iso"), 1000));
}

void tst_KadSearchExpr::notTerm_rejectsExcludedWord()
{
    SearchParams params;
    params.expression = QStringLiteral("ubuntu desktop NOT beta");
    params.type = eMule::SearchType::Kademlia;

    auto term = roundTrip(params);
    QVERIFY(term != nullptr);

    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-desktop-final.iso"), 1000));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-desktop-beta.iso"), 1000));
}

void tst_KadSearchExpr::orTerm_matchesEitherBranch()
{
    SearchParams params;
    params.expression = QStringLiteral("ubuntu AND (desktop OR server)");
    params.type = eMule::SearchType::Kademlia;

    auto term = roundTrip(params);
    QVERIFY(term != nullptr);

    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-desktop.iso"), 1000));
    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-server.iso"), 1000));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-core.iso"), 1000));
}

void tst_KadSearchExpr::sizeFilter_rejectsUndersizedEntry()
{
    SearchParams params;
    params.expression = QStringLiteral("ubuntu desktop");
    params.type = eMule::SearchType::Kademlia;
    params.minSize = 5000;

    auto term = roundTrip(params);
    QVERIFY(term != nullptr);

    const std::vector<Tag> small{ Tag(FT_FILESIZE, static_cast<uint64>(1000)) };
    const std::vector<Tag> big{ Tag(FT_FILESIZE, static_cast<uint64>(9000)) };

    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-desktop.iso"), 9000, big));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-desktop.iso"), 1000, small));
}

void tst_KadSearchExpr::typeFilter_matchesMetaTag()
{
    SearchParams params;
    params.expression = QStringLiteral("ubuntu desktop");
    params.type = eMule::SearchType::Kademlia;
    params.fileType = QStringLiteral("Iso");

    auto term = roundTrip(params);
    QVERIFY(term != nullptr);

    const std::vector<Tag> iso{ Tag(FT_FILETYPE, QStringLiteral("iso")) };
    const std::vector<Tag> audio{ Tag(FT_FILETYPE, QStringLiteral("audio")) };

    QVERIFY(matches(term.get(), QStringLiteral("ubuntu-desktop.iso"), 1000, iso));
    QVERIFY(!matches(term.get(), QStringLiteral("ubuntu-desktop.iso"), 1000, audio));
}

QTEST_GUILESS_MAIN(tst_KadSearchExpr)
#include "tst_KadSearchExpr.moc"
