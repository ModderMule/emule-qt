/// @file tst_SearchList.cpp
/// @brief Tests for search/SearchList — session management, dedup, spam, persistence, signals.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "crypto/AICHData.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "search/SearchList.h"
#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "search/SearchQueue.h"
#include "utils/Opcodes.h"
#include "search/SearchStarter.h"
#include "search/SeenFileIndex.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <cstring>

using namespace eMule;

/// Helper: build a search result packet containing `count` files
static QByteArray buildTCPSearchPacket(int count, const uint8* baseHash = nullptr,
                                       const QString& baseName = QStringLiteral("file"),
                                       uint32 baseSize = 1000,
                                       uint32 sources = 5)
{
    SafeMemFile mem;
    mem.writeUInt32(static_cast<uint32>(count));

    uint8 hash[16];
    if (baseHash)
        std::memcpy(hash, baseHash, 16);
    else
        std::memset(hash, 0, 16);

    for (int i = 0; i < count; ++i) {
        // Vary hash per file by setting first byte
        hash[0] = static_cast<uint8>(i + 1);

        mem.write(hash, 16);
        mem.writeUInt32(0x0A000001u + static_cast<uint32>(i)); // clientID
        mem.writeUInt16(4662);           // clientPort

        uint32 tagCount = 3;
        mem.writeUInt32(tagCount);

        QString name = baseName + QStringLiteral("_%1.mp3").arg(i);
        Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::Raw);
        Tag(FT_FILESIZE, baseSize + static_cast<uint32>(i) * 100).writeNewEd2kTag(mem);
        Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);
    }

    return mem.takeBuffer();
}

/// Helper: build a single search result packet (for UDP)
static QByteArray buildSingleResultPacket(const uint8* hash,
                                          const QString& name,
                                          uint32 size, uint32 sources = 3)
{
    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0x0A000001); // clientID
    mem.writeUInt16(4662);
    mem.writeUInt32(3); // tags
    Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, size).writeNewEd2kTag(mem);
    Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);
    return mem.takeBuffer();
}

class tst_SearchList : public QObject {
    Q_OBJECT

private slots:
    void construct();
    void newSearch_withoutARequestLeavesTheRunningOneItsAnswers();
    void newSearch_initializesCounters();
    void addToList_newParent();
    void addToList_duplicate_merges();
    void addToList_parentCountIsSumOfChildren();
    void addToList_duplicate_sameName_merges();
    void addToList_aichRoots_data();
    void addToList_aichRoots();
    void addToList_newNameChildCountsItsSources();
    void addToList_ownFilesDontCountTowardsTheLimit();
    void addToList_kadOrigin_serverResultWins_data();
    void addToList_kadOrigin_serverResultWins();
    void addToList_kadOrigin_keptWhenAllAnswersAreKad();
    void addToList_fileTypeFilter();
    void removeResults_clearsSearch();
    void processSearchAnswer_tcp();
    void lateServerAnswer_afterTheSearchEndedIsDropped();
    void startSearch_offlineIsQueuedNotDropped();

    // SearchQueue, against a network of our own
    void queue_waitsForTheServerThenSends();
    void queue_oneServerSearchAtATimeNextGoesAtOnce();
    void queue_kadSearchesDoNotHoldEachOtherUp();
    void queue_capAndDuplicates();
    void queue_givesUpAfterTheMaxWait();
    void queue_retriesASendThatFailedThenFails();
    void queue_aLostSessionAsksAgainOnTheNext();
    void queue_answerTimeoutAndSweepEndFinishASearch();
    void queue_refusalsLeaveNothingBehind();
    void queue_automaticIsResolvedWhenSent();
    void queue_stopAndRemove();
    void queue_metaSearchHoldsNoLaneAndEndsWhenToldTo();
    void queue_metaSearchWaitsForServerInfoAndIsCancelled();
    void queue_metaSearchFetchesTheNextPageOnlyWhenAsked();
    void queue_serverSearchOffersMoreUntilTheNextSearch();
    void startSearch_metaWithoutAServerIsRefused();
    void addMetaSearchResult_skipsTheTypeFilterAndAClosedTab();

    // SeenFileIndex
    void seenIndex_remembersAcrossAReopen();
    void seenIndex_keepsTheMostSeenNames();
    void seenIndex_dropsWhatIsOldAndWhatIsOverTheCap();
    void seenIndex_switchedOffRecordsNothing();
    void seenIndex_leavesANewerFileAlone();
    void seenMarker_isSetForAFileMetBeforeThisSearch();
    void processUDPSearchAnswer_ipv6OnlyFromAskedServer();
    void spamRating_hashHit();
    void spamRating_nameHit();
    void spamRating_belowThreshold();
    void markFileAsSpam_addsToFilter();
    void markFileAsNotSpam_removesFromFilter();
    void saveAndLoadSpamFilter_roundTrip();
    void storeAndLoadSearches_roundTrip();
    void processSearchAnswer_truncatedKeepsWhatWasRead();
    void signal_resultAdded();
    void signal_resultUpdated();
    void clientSharedFiles_opensOwnTab();
    void clientSharedFiles_marksWhatThePeerCanPreview();
    void clientSharedFiles_reusesTabThenReopensAfterClose();
    void clientSharedFiles_emptyListStillOpensTab();
    void kadKeywordResult_setsKadFlagAndMaxesSources();
    void kadKeywordResult_keepsTheBestPublishTrust();
    void fakeVerdict_followsTheNamesOfAHash();
    void fakeVerdict_followsSpamMarkAndNotes();
    void fakeVerdict_usesNamesOnRecord();
    void recalculateSpamRatings_signalsAChange();
    void kadKeywordResult_adoptsTheOneAgreedAICHHash();
    void kadKeywordResult_ignoresRareOrCompetingAICHHashes();
    void storeAndLoadSearches_keepsKadFlag();
};

void tst_SearchList::construct()
{
    SearchList list;
    QCOMPARE(list.currentSearchID(), uint32{0});
}

void tst_SearchList::newSearch_initializesCounters()
{
    SearchList list;
    SearchParams params;

    uint32 id = list.newSearch(QStringLiteral("Audio"), params);
    QVERIFY(id > 0);
    QCOMPARE(list.currentSearchID(), id);
    QCOMPARE(list.foundFiles(id), uint32{0});
    QCOMPARE(list.foundSources(id), uint32{0});
    QCOMPARE(list.resultCount(id), uint32{0});
}

void tst_SearchList::addToList_newParent()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xAA, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("test.mp3"), 5000, 3);
    SafeMemFile data(packet);

    auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
    file->setSearchID(id);

    QSignalSpy addedSpy(&list, &SearchList::resultAdded);
    list.addToList(file);

    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(list.foundFiles(id), uint32{1});
    QCOMPARE(list.resultCount(id), uint32{1});
}

void tst_SearchList::addToList_duplicate_merges()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xBB, 16);

    // Add first file
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("file_v1.avi"), 10000, 5);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    }

    // Add duplicate hash with different name
    QSignalSpy updatedSpy(&list, &SearchList::resultUpdated);
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("file_v2.avi"), 10000, 3);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80002, 4662);
        file->setSearchID(id);
        list.addToList(file);
    }

    QCOMPARE(updatedSpy.count(), 1);
    QCOMPARE(list.resultCount(id), uint32{1}); // still 1 parent

    // The parent should now have children
    SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->listChildCount(), uint32{2}); // original + new name
}

using ResultRows = QList<QPair<int, int>>;

void tst_SearchList::addToList_aichRoots_data()
{
    // One row per result: name index and AICH root (0 = none), then what the parent ends with.
    QTest::addColumn<ResultRows>("results");
    QTest::addColumn<int>("parentRoot");

    using R = ResultRows;
    QTest::newRow("one root") << R{{0, 1}} << 1;
    QTest::newRow("same name, same root") << R{{0, 1}, {0, 1}} << 1;
    QTest::newRow("same name, late root is adopted") << R{{0, 0}, {0, 1}} << 1;
    QTest::newRow("same name, different roots") << R{{0, 1}, {0, 2}} << 0;
    QTest::newRow("a third answer cannot bring one back") << R{{0, 1}, {0, 2}, {0, 1}} << 0;
    QTest::newRow("new name brings the first root") << R{{0, 0}, {1, 1}} << 1;
    QTest::newRow("different names, same root") << R{{0, 1}, {1, 1}} << 1;
    QTest::newRow("different names, different roots") << R{{0, 1}, {1, 2}} << 0;
    QTest::newRow("conflict in one name beats a root in another") << R{{0, 1}, {0, 2}, {1, 1}} << 0;
}

// The parent's root is seeded as Verified by a download, and every source reporting
// another root is then dropped. So one bogus answer arriving first must not win
// (MFC SearchList.cpp:489-503, 548-600).
void tst_SearchList::addToList_aichRoots()
{
    QFETCH(ResultRows, results);
    QFETCH(int, parentRoot);

    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xA7, 16);

    const auto root = [](int n) {
        uint8 raw[kAICHHashSize];
        std::memset(raw, 0x40 + n, sizeof(raw));
        return AICHHash(raw);
    };

    for (const auto& [nameIndex, rootIndex] : results) {
        const QByteArray packet = buildSingleResultPacket(
            hash, QStringLiteral("name%1.avi").arg(nameIndex), 10000, 1);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        if (rootIndex != 0)
            file->fileIdentifier().setAICHHash(root(rootIndex));
        list.addToList(file);
    }

    const SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->fileIdentifier().hasAICHHash(), parentRoot != 0);
    if (parentRoot != 0)
        QCOMPARE(parent->fileIdentifier().getAICHHash(), root(parentRoot));
}

void tst_SearchList::addToList_ownFilesDontCountTowardsTheLimit()
{
    // MFC AddResultCount: a file we share or download is not counted.
    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    SharedFileList* const savedShared = theApp.sharedFileList;
    theApp.sharedFileList = &shared;

    uint8 hash[16];
    std::memset(hash, 0xA9, 16);
    auto* own = new KnownFile();
    own->setFileHash(hash);
    own->setFileName(QStringLiteral("own.avi"));
    own->setFileSize(10000);
    knownFiles.safeAddKFile(own);
    QVERIFY(shared.safeAddKFile(own));

    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);
    for (const uint8 b : {uint8{0xA9}, uint8{0xAB}}) {
        std::memset(hash, b, 16);
        const QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("x.avi"), 10000, 7);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    }
    theApp.sharedFileList = savedShared;

    QCOMPARE(list.foundFiles(id), uint32{2});
    QCOMPARE(list.foundSources(id), uint32{7});
}

void tst_SearchList::addToList_newNameChildCountsItsSources()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xA8, 16);

    for (const auto& [name, sources] : { std::pair{QStringLiteral("a.avi"), 5u},
                                         std::pair{QStringLiteral("b.avi"), 3u} }) {
        const QByteArray packet = buildSingleResultPacket(hash, name, 10000, sources);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    }

    QCOMPARE(list.foundSources(id), uint32{8});
}

// The parent is recomputed from its children on every merge: eD2K sums them. Adding
// to the parent's running count instead counted earlier answers again each time
// (5, 3, 4 gave 21). MFC SearchList.cpp:541-592.
void tst_SearchList::addToList_parentCountIsSumOfChildren()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xA9, 16);

    const auto answer = [&](const QString& name, uint32 sources, uint32 serverIP) {
        const QByteArray packet = buildSingleResultPacket(hash, name, 10000, sources);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, serverIP, 4661);
        file->setSearchID(id);
        list.addToList(file);
    };

    answer(QStringLiteral("a.avi"), 5, 0xC0A80001);
    SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->sourceCount(), uint32{5});

    // Same name from a second server, differing only in case: one child, 5 + 3.
    answer(QStringLiteral("A.avi"), 3, 0xC0A80002);
    QCOMPARE(parent->listChildCount(), uint32{1});
    QCOMPARE(parent->sourceCount(), uint32{8});

    // A second name: 8 + 4, and the parent keeps the better-known name.
    answer(QStringLiteral("b.avi"), 4, 0xC0A80003);
    QCOMPARE(parent->listChildCount(), uint32{2});
    QCOMPARE(parent->sourceCount(), uint32{12});
    QCOMPARE(parent->fileName(), QStringLiteral("a.avi"));

    // Once the other name is the more available one, the parent takes it.
    answer(QStringLiteral("b.avi"), 9, 0xC0A80004);
    QCOMPARE(parent->sourceCount(), uint32{21});
    QCOMPARE(parent->fileName(), QStringLiteral("b.avi"));
    QCOMPARE(list.foundSources(id), uint32{21});
}

void tst_SearchList::addToList_duplicate_sameName_merges()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xCC, 16);

    // Add first file
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("same.mp3"), 8000, 5);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    }

    // Add duplicate with SAME name — should merge sources into existing child
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("same.mp3"), 8000, 3);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80002, 4662);
        file->setSearchID(id);
        list.addToList(file);
    }

    SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    // Should have 1 child (first copy), and that child has merged sources
    QCOMPARE(parent->listChildCount(), uint32{1});
}

// One search answer for @p hash; with @p kad the server found the file on Kad and
// says so with FT_META_NETWORK.
static SearchFile* makeAnswer(const uint8* hash, const QString& name, uint32 sources, bool kad,
                              uint32 serverIP, uint32 searchID)
{
    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0);
    mem.writeUInt16(0);
    mem.writeUInt32(kad ? 4 : 3);
    Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, uint32{8000}).writeNewEd2kTag(mem);
    Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);
    if (kad)
        Tag(FT_META_NETWORK, uint32{FT_META_NETWORK_KAD}).writeNewEd2kTag(mem);
    QByteArray packet = mem.takeBuffer();
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true, serverIP, 4661);
    file->setSearchID(searchID);
    return file;
}

static bool hasNetworkTag(const SearchFile* file)
{
    return std::ranges::any_of(file->tags(),
                               [](const Tag& t) { return t.nameId() == FT_META_NETWORK; });
}

void tst_SearchList::addToList_kadOrigin_serverResultWins_data()
{
    QTest::addColumn<bool>("kadFirst");
    QTest::addColumn<QString>("kadName");

    // The Kad answer's name once its "[kad …] " prefix is gone: the same as the
    // server's, or another one, which makes it a second name variant
    QTest::newRow("server then kad, same name") << false << QStringLiteral("[kad emule-qt.org] same.avi");
    QTest::newRow("kad then server, same name") << true << QStringLiteral("[kad emule-qt.org] same.avi");
    QTest::newRow("server then kad, other name") << false << QStringLiteral("[kad] other.avi");
    QTest::newRow("kad then server, other name") << true << QStringLiteral("[kad] other.avi");
}

void tst_SearchList::addToList_kadOrigin_serverResultWins()
{
    QFETCH(bool, kadFirst);
    QFETCH(QString, kadName);

    eMule::testing::TempDir tempDir;
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xCD, 16);

    // One server has the file itself, another only found it on Kad
    auto* own = makeAnswer(hash, QStringLiteral("same.avi"), 5, false, 0xC0A80001, id);
    auto* kad = makeAnswer(hash, kadName, 3, true, 0xC0A80002, id);
    QVERIFY(kad->isKadOrigin());
    QVERIFY(!own->isKadOrigin());
    qInfo() << "input: kadFirst =" << kadFirst << "kad name =" << kadName;

    list.addToList(kadFirst ? kad : own);
    SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->isKadOrigin(), kadFirst);
    list.addToList(kadFirst ? own : kad);

    qInfo() << "output: parent" << parent->fileName() << "kadOrigin =" << parent->isKadOrigin()
            << "children =" << parent->listChildCount();
    QVERIFY(!parent->isKadOrigin());
    QVERIFY(!hasNetworkTag(parent));
    QVERIFY(!parent->fileName().startsWith(QLatin1Char('[')));
    QVERIFY(parent->listChildCount() >= 1);
    for (const auto* child : parent->listChildren()) {
        qInfo() << "output: child" << child->fileName() << "kadOrigin =" << child->isKadOrigin();
        QVERIFY(!child->isKadOrigin());
        QVERIFY(!hasNetworkTag(child));
    }

    // A stored search must not bring the flag back
    list.storeSearches(tempDir.path());
    SearchList loaded;
    loaded.loadSearches(tempDir.path());
    SearchFile* restored = loaded.searchFileByHash(hash, id);
    QVERIFY(restored != nullptr);
    qInfo() << "output: restored kadOrigin =" << restored->isKadOrigin();
    QVERIFY(!restored->isKadOrigin());
}

void tst_SearchList::addToList_kadOrigin_keptWhenAllAnswersAreKad()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xCE, 16);

    // Two servers, both found the file on Kad only
    list.addToList(makeAnswer(hash, QStringLiteral("[kad] same.avi"), 3, true, 0xC0A80001, id));
    list.addToList(makeAnswer(hash, QStringLiteral("[kad emule-qt.org] same.avi"), 4, true, 0xC0A80002, id));

    SearchFile* parent = list.searchFileByHash(hash, id);
    QVERIFY(parent != nullptr);
    qInfo() << "output: parent" << parent->fileName() << "kadOrigin =" << parent->isKadOrigin()
            << "children =" << parent->listChildCount();
    QVERIFY(parent->isKadOrigin());
    QVERIFY(hasNetworkTag(parent));
    QCOMPARE(parent->listChildCount(), uint32{1});
    QVERIFY(parent->listChildren().front()->isKadOrigin());
}

void tst_SearchList::addToList_fileTypeFilter()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch(QStringLiteral("Audio"), params);

    uint8 hash[16];
    std::memset(hash, 0xDD, 16);

    // Build a file with Video type — should be filtered out
    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0x0A000001);
    mem.writeUInt16(4662);
    mem.writeUInt32(3);
    Tag(FT_FILENAME, QStringLiteral("movie.avi")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, uint32{50000}).writeNewEd2kTag(mem);
    Tag(FT_FILETYPE, QStringLiteral("Video")).writeNewEd2kTag(mem, UTF8Mode::Raw);

    QByteArray buf = mem.takeBuffer();
    SafeMemFile data(buf);

    auto* file = new SearchFile(data, true);
    file->setSearchID(id);

    QSignalSpy addedSpy(&list, &SearchList::resultAdded);
    list.addToList(file);

    QCOMPARE(addedSpy.count(), 0); // filtered out
    QCOMPARE(list.resultCount(id), uint32{0});
}

void tst_SearchList::removeResults_clearsSearch()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16] = {};
    hash[0] = 1;
    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("f.txt"), 100);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    list.addToList(file);

    QCOMPARE(list.resultCount(id), uint32{1});

    list.removeResults(id);
    QCOMPARE(list.resultCount(id), uint32{0});
    QCOMPARE(list.foundFiles(id), uint32{0});
}

void tst_SearchList::processUDPSearchAnswer_ipv6OnlyFromAskedServer()
{
    // The sent-request set was uint32-keyed: every IPv6 server collapsed to 0, so
    // asking one let any IPv6 sender in.
    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    const Address asked = Address::fromString(QStringLiteral("2001:678:6d4:9202::278"));
    const Address stranger = Address::fromString(QStringLiteral("2a01:4f8::1"));

    uint8 hash[16] = {0x42};
    const QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("v6.iso"), 1000);
    const auto* data = reinterpret_cast<const uint8*>(packet.constData());
    const auto size = static_cast<uint32>(packet.size());

    QSignalSpy added(&list, &SearchList::resultAdded);
    list.addSentUDPRequestIP(id, asked);
    list.processUDPSearchAnswer(data, size, true, Endpoint(stranger, 5555));
    QCOMPARE(added.count(), 0);
    list.processUDPSearchAnswer(data, size, true, Endpoint(asked, 5555));
    QCOMPARE(added.count(), 1);
}

// A short record used to throw through the server socket and drop the connection.
void tst_SearchList::processSearchAnswer_truncatedKeepsWhatWasRead()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    QByteArray packet = buildTCPSearchPacket(3);
    packet.chop(9);   // into the third record

    QSignalSpy addedSpy(&list, &SearchList::resultAdded);
    bool more = true;
    try {
        more = list.processSearchAnswer(reinterpret_cast<const uint8*>(packet.constData()),
                                        static_cast<uint32>(packet.size()), true,
                                        Endpoint::fromHostOrder(0x0A000001, 4661));
    } catch (...) {
        QFAIL("a malformed answer must not throw");
    }
    QVERIFY(!more);
    QCOMPARE(addedSpy.count(), 2);
    QCOMPARE(list.resultCount(id), uint32{2});
}

void tst_SearchList::processSearchAnswer_tcp()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    QByteArray packet = buildTCPSearchPacket(2);

    QSignalSpy addedSpy(&list, &SearchList::resultAdded);
    QSignalSpy headerSpy(&list, &SearchList::tabHeaderUpdated);

    bool moreResults = list.processSearchAnswer(
        reinterpret_cast<const uint8*>(packet.constData()),
        static_cast<uint32>(packet.size()),
        true, Endpoint(Address::fromString(QStringLiteral("192.168.0.1")), 4661));

    QVERIFY(!moreResults); // no trailing byte
    QCOMPARE(addedSpy.count(), 2);
    QVERIFY(headerSpy.count() > 0);
    QCOMPARE(list.resultCount(id), uint32{2});
}

// The answer of a finished or closed server search used to land in whichever search
// was started next.
void tst_SearchList::lateServerAnswer_afterTheSearchEndedIsDropped()
{
    SearchList list;
    const Endpoint server(Address::fromString(QStringLiteral("192.168.0.1")), 4661);
    const QByteArray packet = buildTCPSearchPacket(2);
    const auto answer = [&] {
        list.processSearchAnswer(reinterpret_cast<const uint8*>(packet.constData()),
                                 static_cast<uint32>(packet.size()), true, server);
    };

    const uint32 first = list.reserveSearch();
    list.beginSearch(first, {}, /*ed2k*/ true);
    QCOMPARE(list.ed2kSearchInFlight(), first);
    answer();
    QCOMPARE(list.resultCount(first), uint32{2});

    // Over: the next search exists but has not been sent yet.
    list.releaseEd2kRouting(first);
    const uint32 second = list.reserveSearch();
    QCOMPARE(list.ed2kSearchInFlight(), uint32{0});
    answer();
    QCOMPARE(list.resultCount(first), uint32{2});
    QCOMPARE(list.resultCount(second), uint32{0});

    // Releasing a search that is not the one in flight changes nothing.
    list.beginSearch(second, {}, true);
    list.releaseEd2kRouting(first);
    QCOMPARE(list.ed2kSearchInFlight(), second);
    answer();
    QCOMPARE(list.resultCount(second), uint32{2});
}

// With no network at all a search used to come back as "started: false" and stay an
// empty tab for good.
void tst_SearchList::startSearch_offlineIsQueuedNotDropped()
{
    SearchList list;
    SearchParams params;
    params.expression = QStringLiteral("holiday");
    params.type = SearchType::Ed2kServer;

    const SearchStartResult server = startSearch(list, params);
    QVERIFY(server.ok);
    QVERIFY(!server.started);
    QCOMPARE(server.state, SearchRunState::Queued);
    QCOMPARE(server.reason, QString::fromLatin1(SearchWait::ServerConnection));
    QVERIFY(list.hasSearch(server.searchID));

    params.type = SearchType::Kademlia;
    const SearchStartResult kad = startSearch(list, params);
    QVERIFY(kad.ok);
    QCOMPARE(kad.reason, QString::fromLatin1(SearchWait::Kad));
    QVERIFY(kad.searchID != server.searchID);

    params.type = SearchType::Automatic;
    const SearchStartResult automatic = startSearch(list, params);
    QVERIFY(automatic.ok);
    QCOMPARE(automatic.reason, QString::fromLatin1(SearchWait::Connection));

    // What can never be sent is still refused on the spot.
    params.type = SearchType::Kademlia;
    params.expression = QStringLiteral("ab");
    QVERIFY(!startSearch(list, params).ok);
    params.type = SearchType::UsenetIndexer;
    QVERIFY(!startSearch(list, params).ok);

    QVERIFY(removeSearch(list, server.searchID));
    QVERIFY(!list.hasSearch(server.searchID));
    QVERIFY(!list.queue().status(server.searchID));
    clearAllSearches(list);
    QCOMPARE(list.queue().queuedCount(), 0);
}

namespace {

/// The world as the queue sees it, under the test's control.
struct FakeNet {
    bool server = false;
    bool kad = false;
    qint64 now = 1000;
    uint32 nextId = 1;
    SearchDispatch::Outcome outcome = SearchDispatch::Outcome::Sent;
    bool sweep = false;
    QString refusal = QStringLiteral("no");
    std::vector<uint32> sent;
    std::vector<uint32> ended;
    std::vector<uint32> discarded;
    std::vector<uint32> metaCancelled;
    std::vector<uint32> metaContinued;
    std::vector<uint32> serverContinued;
    bool metaServerKnown = true;
    QSet<uint32> kadAlive;

    [[nodiscard]] SearchQueueBackend backend()
    {
        SearchQueueBackend b;
        b.validate = [](const SearchParams& p) {
            return p.expression.isEmpty() ? QStringLiteral("empty") : QString();
        };
        b.resolve = [this](const SearchParams& p) -> std::optional<SearchType> {
            if (p.type != SearchType::Automatic)
                return p.type;
            if (server)
                return SearchType::Ed2kServer;
            if (kad)
                return SearchType::Kademlia;
            return std::nullopt;
        };
        b.waitReason = [this](SearchType type) {
            if (type == SearchType::Kademlia)
                return kad ? QString() : QString::fromLatin1(SearchWait::Kad);
            if (isMetaSearchType(type))
                return metaServerKnown ? QString() : QString::fromLatin1(SearchWait::ServerInfo);
            return server ? QString() : QString::fromLatin1(SearchWait::ServerConnection);
        };
        b.create = [this](const SearchParams&) { return nextId++; };
        b.discard = [this](uint32 id) { discarded.push_back(id); };
        b.dispatch = [this](uint32 id, SearchType type, const SearchParams&) {
            SearchDispatch out;
            out.outcome = outcome;
            out.error = refusal;
            out.awaitsSweep = sweep;
            out.awaitsMeta = isMetaSearchType(type);
            if (outcome == SearchDispatch::Outcome::Sent) {
                sent.push_back(id);
                if (type == SearchType::Kademlia)
                    kadAlive.insert(id);
            }
            return out;
        };
        b.endServerSearch = [this](uint32 id) { ended.push_back(id); };
        b.kadSearchAlive = [this](uint32 id) { return kadAlive.contains(id); };
        b.cancelMetaSearch = [this](uint32 id) { metaCancelled.push_back(id); };
        b.continueMetaSearch = [this](uint32 id) { metaContinued.push_back(id); };
        b.continueServerSearch = [this](uint32 id, const SearchParams&) {
            if (server)
                serverContinued.push_back(id);
            return server;
        };
        b.nowMs = [this] { return now; };
        return b;
    }
};

SearchParams query(const QString& words, SearchType type = SearchType::Ed2kServer)
{
    SearchParams params;
    params.expression = words;
    params.type = type;
    return params;
}

SearchRunState stateOf(const SearchQueue& queue, uint32 id)
{
    const auto status = queue.status(id);
    return status ? status->state : SearchRunState::Failed;
}

QString reasonOf(const SearchQueue& queue, uint32 id)
{
    const auto status = queue.status(id);
    return status ? status->reason : QString();
}

} // namespace

void tst_SearchList::queue_waitsForTheServerThenSends()
{
    FakeNet net;
    SearchQueue queue(net.backend());
    QSignalSpy states(&queue, &SearchQueue::stateChanged);

    const auto queued = queue.enqueue(query(QStringLiteral("holiday")));
    QVERIFY(queued.ok);
    const uint32 id = queued.status.searchID;
    QCOMPARE(queued.status.state, SearchRunState::Queued);
    QCOMPARE(queued.status.reason, QString::fromLatin1(SearchWait::ServerConnection));
    QVERIFY(net.sent.empty());
    QCOMPARE(states.count(), 1);

    // Still nothing: no change, no second report.
    queue.tick();
    QCOMPARE(states.count(), 1);

    net.server = true;
    queue.onServerConnected();
    QCOMPARE(net.sent, std::vector<uint32>{id});
    QCOMPARE(stateOf(queue, id), SearchRunState::Running);
    QCOMPARE(queue.serverSearchInFlight(), id);
    QCOMPARE(states.count(), 2);
    QCOMPARE(states.last().first().value<SearchStatus>().state, SearchRunState::Running);
}

void tst_SearchList::queue_oneServerSearchAtATimeNextGoesAtOnce()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());

    const uint32 a = queue.enqueue(query(QStringLiteral("a one"))).status.searchID;
    const uint32 b = queue.enqueue(query(QStringLiteral("b two"))).status.searchID;
    const uint32 c = queue.enqueue(query(QStringLiteral("c three"))).status.searchID;
    QCOMPARE(net.sent, std::vector<uint32>{a});
    QCOMPARE(reasonOf(queue, b), QString::fromLatin1(SearchWait::PreviousSearch));
    QCOMPARE(reasonOf(queue, c), QString::fromLatin1(SearchWait::PreviousSearch));

    // The answer ends a, frees the lane, and b leaves in the same breath — the clock
    // has not moved and nobody ticked.
    queue.onServerAnswer();
    QCOMPARE(stateOf(queue, a), SearchRunState::Finished);
    QCOMPARE(net.ended, std::vector<uint32>{a});
    QCOMPARE(net.sent, (std::vector<uint32>{a, b}));
    QCOMPARE(stateOf(queue, c), SearchRunState::Queued);

    queue.onServerAnswer();
    QCOMPARE(net.sent, (std::vector<uint32>{a, b, c}));
    queue.onServerAnswer();
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});
    QCOMPARE(net.ended, (std::vector<uint32>{a, b, c}));

    // An answer nobody waits for changes nothing.
    queue.onServerAnswer();
    QCOMPARE(net.ended.size(), size_t{3});
}

void tst_SearchList::queue_kadSearchesDoNotHoldEachOtherUp()
{
    FakeNet net;
    net.kad = true;
    SearchQueue queue(net.backend());

    const uint32 waiting = queue.enqueue(query(QStringLiteral("server"))).status.searchID;
    const uint32 k1 = queue.enqueue(query(QStringLiteral("first"), SearchType::Kademlia)).status.searchID;
    const uint32 k2 = queue.enqueue(query(QStringLiteral("second"), SearchType::Kademlia)).status.searchID;
    QCOMPARE(net.sent, (std::vector<uint32>{k1, k2}));
    QCOMPARE(stateOf(queue, waiting), SearchRunState::Queued);
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    // A keyword still being searched: waits, then goes when it is free.
    net.outcome = SearchDispatch::Outcome::Busy;
    const uint32 k3 = queue.enqueue(query(QStringLiteral("third"), SearchType::Kademlia)).status.searchID;
    QCOMPARE(reasonOf(queue, k3), QString::fromLatin1(SearchWait::PreviousSearch));
    net.outcome = SearchDispatch::Outcome::Sent;
    queue.tick();
    QCOMPARE(stateOf(queue, k3), SearchRunState::Running);

    // A Kad search that has run its course is finished.
    net.kadAlive.remove(k1);
    queue.tick();
    QCOMPARE(stateOf(queue, k1), SearchRunState::Finished);
    QCOMPARE(stateOf(queue, k2), SearchRunState::Running);
}

void tst_SearchList::queue_capAndDuplicates()
{
    FakeNet net;
    SearchQueue queue(net.backend());

    std::vector<uint32> ids;
    for (int i = 0; i < SearchQueue::kMaxQueued; ++i) {
        const auto r = queue.enqueue(query(QStringLiteral("word%1").arg(i)));
        QVERIFY(r.ok);
        ids.push_back(r.status.searchID);
    }
    QCOMPARE(queue.queuedCount(), 20);

    // Full.
    const auto over = queue.enqueue(query(QStringLiteral("one too many")));
    QVERIFY(!over.ok);
    QVERIFY(!over.error.isEmpty());
    QCOMPARE(queue.queuedCount(), 20);

    // The same question again is the search that already waits — full or not.
    const auto again = queue.enqueue(query(QStringLiteral("  WORD3 ")));
    QVERIFY(again.ok);
    QVERIFY(again.duplicate);
    QCOMPARE(again.status.searchID, ids[3]);
    QCOMPARE(queue.queuedCount(), 20);

    // Another network, or another filter, is another question.
    queue.remove(ids[0]);
    SearchParams filtered = query(QStringLiteral("word3"));
    filtered.fileType = QStringLiteral("Audio");
    const auto different = queue.enqueue(filtered);
    QVERIFY(different.ok);
    QVERIFY(!different.duplicate);
    QVERIFY(different.status.searchID != ids[3]);

    // Once a search is finished the same words start a new one.
    queue.stop(ids[5]);
    queue.remove(ids[6]);
    const auto fresh = queue.enqueue(query(QStringLiteral("word5")));
    QVERIFY(fresh.ok);
    QVERIFY(!fresh.duplicate);
}

void tst_SearchList::queue_givesUpAfterTheMaxWait()
{
    FakeNet net;
    SearchQueue queue(net.backend());
    const uint32 id = queue.enqueue(query(QStringLiteral("holiday"))).status.searchID;

    net.now += SearchQueue::kMaxWaitMs - 1;
    queue.tick();
    QCOMPARE(stateOf(queue, id), SearchRunState::Queued);

    net.now += 1;
    queue.tick();
    QCOMPARE(stateOf(queue, id), SearchRunState::Failed);
    QVERIFY(!queue.status(id)->error.isEmpty());

    // Too late now.
    net.server = true;
    queue.onServerConnected();
    QVERIFY(net.sent.empty());
}

void tst_SearchList::queue_retriesASendThatFailedThenFails()
{
    FakeNet net;
    net.server = true;
    net.outcome = SearchDispatch::Outcome::SendFailed;
    SearchQueue queue(net.backend());

    const auto first = queue.enqueue(query(QStringLiteral("holiday")));
    QVERIFY(first.ok);
    const uint32 id = first.status.searchID;
    QCOMPARE(first.status.reason, QString::fromLatin1(SearchWait::ServerConnection));

    for (int i = 1; i < SearchQueue::kMaxSendRetries; ++i) {
        queue.pump();
        QCOMPARE(stateOf(queue, id), SearchRunState::Queued);
    }
    queue.pump();   // the try after the last retry
    QCOMPARE(stateOf(queue, id), SearchRunState::Failed);
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    // One that gets through on a retry is just a search.
    net.outcome = SearchDispatch::Outcome::SendFailed;
    const uint32 lucky = queue.enqueue(query(QStringLiteral("other"))).status.searchID;
    net.outcome = SearchDispatch::Outcome::Sent;
    queue.pump();
    QCOMPARE(stateOf(queue, lucky), SearchRunState::Running);
}

void tst_SearchList::queue_aLostSessionAsksAgainOnTheNext()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());
    const uint32 id = queue.enqueue(query(QStringLiteral("holiday"))).status.searchID;
    QCOMPARE(stateOf(queue, id), SearchRunState::Running);

    // The server went before it answered.
    net.server = false;
    queue.onServerDisconnected();
    QCOMPARE(stateOf(queue, id), SearchRunState::Queued);
    QCOMPARE(reasonOf(queue, id), QString::fromLatin1(SearchWait::ServerConnection));
    QCOMPARE(net.ended, std::vector<uint32>{id});
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    net.server = true;
    queue.onServerConnected();
    QCOMPARE(net.sent, (std::vector<uint32>{id, id}));
    QCOMPARE(stateOf(queue, id), SearchRunState::Running);

    // A global search walks the other servers over UDP and is left alone.
    queue.onServerAnswer();
    net.sweep = true;
    const uint32 global = queue.enqueue(query(QStringLiteral("wide"), SearchType::Ed2kGlobal)).status.searchID;
    queue.onServerDisconnected();
    QCOMPARE(stateOf(queue, global), SearchRunState::Running);
}

void tst_SearchList::queue_answerTimeoutAndSweepEndFinishASearch()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());

    // A server that says nothing.
    const uint32 silent = queue.enqueue(query(QStringLiteral("silent"))).status.searchID;
    net.now += SearchQueue::kAnswerTimeoutMs - 1;
    queue.tick();
    QCOMPARE(stateOf(queue, silent), SearchRunState::Running);
    net.now += 1;
    queue.tick();
    QCOMPARE(stateOf(queue, silent), SearchRunState::Finished);
    QCOMPARE(net.ended, std::vector<uint32>{silent});

    // A global search: the TCP answer is only its first part.
    net.sweep = true;
    const uint32 global = queue.enqueue(query(QStringLiteral("wide"), SearchType::Ed2kGlobal)).status.searchID;
    const uint32 behind = queue.enqueue(query(QStringLiteral("behind"))).status.searchID;
    queue.onServerAnswer();
    QCOMPARE(stateOf(queue, global), SearchRunState::Running);
    net.now += SearchQueue::kAnswerTimeoutMs + 1;
    queue.tick();
    QCOMPARE(stateOf(queue, global), SearchRunState::Running);
    QCOMPARE(stateOf(queue, behind), SearchRunState::Queued);

    queue.onSweepFinished(silent);          // somebody else's sweep
    QCOMPARE(stateOf(queue, global), SearchRunState::Running);
    queue.onSweepFinished(global);
    QCOMPARE(stateOf(queue, global), SearchRunState::Finished);
    QCOMPARE(stateOf(queue, behind), SearchRunState::Running);
}

void tst_SearchList::queue_refusalsLeaveNothingBehind()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());

    // Not acceptable at all: nothing is created.
    const auto invalid = queue.enqueue(query(QString()));
    QVERIFY(!invalid.ok);
    QCOMPARE(invalid.error, QStringLiteral("empty"));
    QCOMPARE(net.nextId, uint32{1});

    // Refused when sent on the spot: created, then taken back.
    net.outcome = SearchDispatch::Outcome::Refused;
    net.refusal = QStringLiteral("nothing to search for");
    const auto refused = queue.enqueue(query(QStringLiteral("holiday")));
    QVERIFY(!refused.ok);
    QCOMPARE(refused.error, QStringLiteral("nothing to search for"));
    QCOMPARE(net.discarded, std::vector<uint32>{1});
    QVERIFY(!queue.status(1));
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    // Refused later, after waiting: the caller has the id, so it stays and says why.
    net.server = false;
    const uint32 waited = queue.enqueue(query(QStringLiteral("later"))).status.searchID;
    net.server = true;
    queue.onServerConnected();
    QCOMPARE(stateOf(queue, waited), SearchRunState::Failed);
    QCOMPARE(queue.status(waited)->error, QStringLiteral("nothing to search for"));
    QCOMPARE(net.discarded.size(), size_t{1});
}

void tst_SearchList::queue_automaticIsResolvedWhenSent()
{
    FakeNet net;
    SearchQueue queue(net.backend());

    const auto queued = queue.enqueue(query(QStringLiteral("holiday"), SearchType::Automatic));
    QVERIFY(queued.ok);
    const uint32 id = queued.status.searchID;
    QCOMPARE(queued.status.reason, QString::fromLatin1(SearchWait::Connection));
    QCOMPARE(queued.status.type, SearchType::Automatic);

    // Kad is what comes up, so Kad it is.
    net.kad = true;
    queue.onKadConnected();
    QCOMPARE(stateOf(queue, id), SearchRunState::Running);
    QCOMPARE(queue.status(id)->type, SearchType::Kademlia);
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});
}

void tst_SearchList::queue_stopAndRemove()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());

    const uint32 a = queue.enqueue(query(QStringLiteral("a one"))).status.searchID;
    const uint32 b = queue.enqueue(query(QStringLiteral("b two"))).status.searchID;
    const uint32 c = queue.enqueue(query(QStringLiteral("c three"))).status.searchID;

    // Cancelled while waiting: never sent.
    queue.stop(b);
    QCOMPARE(stateOf(queue, b), SearchRunState::Finished);

    // Closing the one in flight frees the lane for the next that still wants it.
    queue.remove(a);
    QVERIFY(!queue.status(a));
    QCOMPARE(net.ended, std::vector<uint32>{a});
    QCOMPARE(net.sent, (std::vector<uint32>{a, c}));

    // Stopping the one in flight ends it.
    queue.stop(c);
    QCOMPARE(stateOf(queue, c), SearchRunState::Finished);
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    queue.clear();
    QVERIFY(!queue.status(b));
}

// A Usenet / torrent search is an HTTP call to a server's Meta API: it neither waits
// for the server lane nor takes it, needs no server connection, and ends when its
// runner says so — not with a TCP answer and not on Kad's clock.
void tst_SearchList::queue_metaSearchHoldsNoLaneAndEndsWhenToldTo()
{
    FakeNet net;   // no server, no Kad
    SearchQueue queue(net.backend());

    const uint32 usenet = queue.enqueue(query(QStringLiteral("a one"), SearchType::MetaUsenet)).status.searchID;
    QCOMPARE(stateOf(queue, usenet), SearchRunState::Running);
    QCOMPARE(queue.status(usenet)->type, SearchType::MetaUsenet);
    QCOMPARE(queue.serverSearchInFlight(), uint32{0});

    net.server = true;
    const uint32 server = queue.enqueue(query(QStringLiteral("b two"))).status.searchID;
    const uint32 torrent = queue.enqueue(query(QStringLiteral("a one"), SearchType::MetaTorrent)).status.searchID;
    QCOMPARE(stateOf(queue, server), SearchRunState::Running);
    QCOMPARE(stateOf(queue, torrent), SearchRunState::Running);   // another network: another search
    QVERIFY(torrent != usenet);

    // neither the server's answer nor a dead Kad lookup ends it
    queue.onServerAnswer();
    queue.onServerDisconnected();
    net.now += 60 * 1000;
    queue.tick();
    QCOMPARE(stateOf(queue, usenet), SearchRunState::Running);

    queue.onMetaSearchFinished(usenet);
    QCOMPARE(stateOf(queue, usenet), SearchRunState::Finished);
    queue.onMetaSearchFinished(torrent, QStringLiteral("srv needs an account"));
    QCOMPARE(stateOf(queue, torrent), SearchRunState::Failed);
    QCOMPARE(queue.status(torrent)->error, QStringLiteral("srv needs an account"));
    QVERIFY(net.metaCancelled.empty());

    // a runner that never reports is not waited for for ever
    const uint32 stuck = queue.enqueue(query(QStringLiteral("c three"), SearchType::MetaUsenet)).status.searchID;
    net.now += SearchQueue::kMetaTimeoutMs;
    queue.tick();
    QCOMPARE(stateOf(queue, stuck), SearchRunState::Finished);
    QCOMPARE(net.metaCancelled, std::vector<uint32>{stuck});
    queue.onMetaSearchFinished(stuck, QStringLiteral("late"));   // ignored
    QCOMPARE(stateOf(queue, stuck), SearchRunState::Finished);
}

void tst_SearchList::queue_metaSearchWaitsForServerInfoAndIsCancelled()
{
    FakeNet net;
    net.metaServerKnown = false;   // logging in; the ident is not here yet
    SearchQueue queue(net.backend());

    const uint32 waiting = queue.enqueue(query(QStringLiteral("a one"), SearchType::MetaUsenet)).status.searchID;
    QCOMPARE(stateOf(queue, waiting), SearchRunState::Queued);
    QCOMPARE(reasonOf(queue, waiting), QString::fromLatin1(SearchWait::ServerInfo));
    // it does not hold up a server search behind it
    net.server = true;
    const uint32 server = queue.enqueue(query(QStringLiteral("b two"))).status.searchID;
    QCOMPARE(stateOf(queue, server), SearchRunState::Running);

    net.metaServerKnown = true;
    queue.tick();
    QCOMPARE(stateOf(queue, waiting), SearchRunState::Running);

    // stopping or closing a running one tells the runner, once
    queue.stop(waiting);
    QCOMPARE(stateOf(queue, waiting), SearchRunState::Finished);
    QCOMPARE(net.metaCancelled, std::vector<uint32>{waiting});
    queue.remove(waiting);
    QCOMPARE(net.metaCancelled, std::vector<uint32>{waiting});

    const uint32 closed = queue.enqueue(query(QStringLiteral("c three"), SearchType::MetaTorrent)).status.searchID;
    queue.remove(closed);
    QCOMPARE(net.metaCancelled, (std::vector<uint32>{waiting, closed}));

    // no server offers it: refused on the spot, nothing left behind
    net.outcome = SearchDispatch::Outcome::Refused;
    const SearchQueue::Result refused = queue.enqueue(query(QStringLiteral("d four"), SearchType::MetaUsenet));
    QVERIFY(!refused.ok);
    QCOMPARE(refused.error, QStringLiteral("no"));
}

// The real backend, with nothing registered to run the search and no server list:
// an answer the user can read, never an eD2K search sent in its place.
// One page a request: the search finishes with the next page on offer, and only
// more() fetches it.
void tst_SearchList::queue_metaSearchFetchesTheNextPageOnlyWhenAsked()
{
    FakeNet net;
    SearchQueue queue(net.backend());

    const uint32 id = queue.enqueue(query(QStringLiteral("a one"), SearchType::MetaUsenet)).status.searchID;
    QVERIFY(!queue.more(id));   // running: nothing to fetch yet
    queue.onMetaSearchFinished(id, {}, true);
    QCOMPARE(queue.status(id)->state, SearchRunState::Finished);
    QVERIFY(queue.status(id)->hasMore);
    QVERIFY(net.metaContinued.empty());

    QVERIFY(queue.more(id));
    QCOMPARE(queue.status(id)->state, SearchRunState::Running);
    QVERIFY(!queue.status(id)->hasMore);
    QCOMPARE(net.metaContinued, std::vector<uint32>{id});
    QVERIFY(!queue.more(id));   // one page at a time

    // the last page: nothing more to ask for
    queue.onMetaSearchFinished(id);
    QCOMPARE(queue.status(id)->state, SearchRunState::Finished);
    QVERIFY(!queue.status(id)->hasMore);
    QVERIFY(!queue.more(id));
    queue.remove(id);
    QVERIFY(net.metaCancelled.empty());

    // a page that fails leaves no further one
    const uint32 failing = queue.enqueue(query(QStringLiteral("b two"), SearchType::MetaTorrent)).status.searchID;
    queue.onMetaSearchFinished(failing, QStringLiteral("gone"), true);
    QVERIFY(!queue.status(failing)->hasMore);

    // stopping keeps the page on offer; closing the tab drops it at the runner
    const uint32 parked = queue.enqueue(query(QStringLiteral("c three"), SearchType::MetaUsenet)).status.searchID;
    queue.onMetaSearchFinished(parked, {}, true);
    queue.stop(parked);
    QVERIFY(queue.status(parked)->hasMore);
    QVERIFY(net.metaCancelled.empty());
    queue.remove(parked);
    QCOMPARE(net.metaCancelled, std::vector<uint32>{parked});

    // an eD2K search has one only when its server says so
    net.server = true;
    const uint32 ed2k = queue.enqueue(query(QStringLiteral("d four"))).status.searchID;
    queue.onServerAnswer();
    QVERIFY(!queue.more(ed2k));
}

// MFC's More button: OP_QUERY_MORE_RESULT, at most MAX_MORE_SEARCH_REQ times, for
// the last server search of the session only (srchybrid/SearchResultsWnd.cpp:470,
// :1240-1245, :1273-1289).
void tst_SearchList::queue_serverSearchOffersMoreUntilTheNextSearch()
{
    FakeNet net;
    net.server = true;
    SearchQueue queue(net.backend());

    const uint32 id = queue.enqueue(query(QStringLiteral("a one"))).status.searchID;
    QVERIFY(!queue.more(id));   // running
    queue.onServerAnswer(true);
    QCOMPARE(stateOf(queue, id), SearchRunState::Finished);
    QVERIFY(queue.status(id)->hasMore);
    QVERIFY(net.serverContinued.empty());

    // every page holds the lane like the search did; the cap is MFC's
    for (int i = 1; i <= MAX_MORE_SEARCH_REQ; ++i) {
        QVERIFY(queue.more(id));
        QCOMPARE(stateOf(queue, id), SearchRunState::Running);
        QCOMPARE(queue.serverSearchInFlight(), id);
        QVERIFY(!queue.status(id)->hasMore);
        QVERIFY(!queue.more(id));
        queue.onServerAnswer(true);
        QCOMPARE(stateOf(queue, id), SearchRunState::Finished);
        QCOMPARE(queue.status(id)->hasMore, i < MAX_MORE_SEARCH_REQ);
    }
    QCOMPARE(static_cast<int>(net.serverContinued.size()), MAX_MORE_SEARCH_REQ);
    QVERIFY(!queue.more(id));
    queue.remove(id);
    QVERIFY(net.metaCancelled.empty());   // not the Meta runner's page

    // a page that never comes ends the search without a further one
    const uint32 silent = queue.enqueue(query(QStringLiteral("b two"))).status.searchID;
    queue.onServerAnswer(true);
    QVERIFY(queue.more(silent));
    net.now += SearchQueue::kAnswerTimeoutMs + 1;
    queue.tick();
    QCOMPARE(stateOf(queue, silent), SearchRunState::Finished);
    QVERIFY(!queue.status(silent)->hasMore);

    // the server keeps the rest of its last answer only
    const uint32 first = queue.enqueue(query(QStringLiteral("c three"))).status.searchID;
    queue.onServerAnswer(true);
    QVERIFY(queue.status(first)->hasMore);
    const uint32 second = queue.enqueue(query(QStringLiteral("d four"))).status.searchID;
    QVERIFY(!queue.status(first)->hasMore);
    QVERIFY(!queue.more(first));
    queue.onServerAnswer(true);
    QVERIFY(queue.status(second)->hasMore);

    // a Kad search asks no server: the page stays on offer
    net.kad = true;
    (void)queue.enqueue(query(QStringLiteral("e five"), SearchType::Kademlia));
    QVERIFY(queue.status(second)->hasMore);

    // ... and a lost session takes it along
    queue.onServerDisconnected();
    QVERIFY(!queue.status(second)->hasMore);
    QVERIFY(!queue.more(second));

    // a global search offers it once its sweep is over, also when stopped
    net.sweep = true;
    const uint32 global = queue.enqueue(query(QStringLiteral("f six"), SearchType::Ed2kGlobal)).status.searchID;
    queue.onServerAnswer(true);
    QCOMPARE(stateOf(queue, global), SearchRunState::Running);
    QVERIFY(!queue.status(global)->hasMore);
    queue.stop(global);
    QCOMPARE(stateOf(queue, global), SearchRunState::Finished);
    QVERIFY(queue.status(global)->hasMore);
    QVERIFY(queue.more(global));
    queue.onServerAnswer(false);   // the page ends with the TCP answer, no sweep
    QCOMPARE(stateOf(queue, global), SearchRunState::Finished);
    QVERIFY(!queue.status(global)->hasMore);
}

void tst_SearchList::startSearch_metaWithoutAServerIsRefused()
{
    SearchList list;
    SearchParams params;
    params.expression = QStringLiteral("holiday");
    for (const SearchType type : {SearchType::MetaUsenet, SearchType::MetaTorrent}) {
        params.type = type;
        const SearchStartResult result = startSearch(list, params);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }
    params.expression = QStringLiteral("a OR b");
    const SearchStartResult result = startSearch(list, params);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("OR")));
    QCOMPARE(list.queue().queuedCount(), 0);
}

void tst_SearchList::addMetaSearchResult_skipsTheTypeFilterAndAClosedTab()
{
    SearchList list;
    const uint32 metaSearch = list.reserveSearch();
    // a later eD2K search set the list's type filter; it is not this search's
    const uint32 audioSearch = list.reserveSearch();
    list.beginSearch(audioSearch, QStringLiteral("Audio"), true);

    auto row = [](uint8 n) {
        auto* file = new SearchFile;
        uint8 hash[16]{};
        hash[0] = n;
        file->setFileHash(hash);
        file->setFileName(QStringLiteral("release %1").arg(n), true);
        file->setFileType(QStringLiteral("Video"));
        file->setFileSize(1000);
        return file;
    };
    list.addMetaSearchResult(metaSearch, row(1));
    list.addMetaSearchResult(metaSearch, row(2));
    QCOMPARE(list.resultCount(metaSearch), uint32{2});
    QCOMPARE(list.resultCount(audioSearch), uint32{0});

    // the tab was closed while the page was on its way: dropped, not resurrected
    list.removeResults(metaSearch);
    list.addMetaSearchResult(metaSearch, row(3));
    QVERIFY(!list.hasSearch(metaSearch));
}

namespace {

std::array<uint8, 16> seenHash(uint8 n)
{
    std::array<uint8, 16> hash{};
    hash[0] = n;
    hash[15] = static_cast<uint8>(n ^ 0x5A);
    return hash;
}

} // namespace

void tst_SearchList::seenIndex_remembersAcrossAReopen()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("seenfiles.db"));
    const auto film = seenHash(1);
    const auto other = seenHash(2);
    // Recent: opening the store drops what has not been seen for half a year.
    const qint64 t0 = QDateTime::currentSecsSinceEpoch() - 5000;

    {
        SeenFileIndex index;
        QVERIFY(index.open(path));
        QVERIFY(!index.lookup(film.data()).known);

        // Known at once, from memory, before anything is written.
        index.note(film.data(), QStringLiteral("Film.2020.mkv"), 700, t0);
        SeenFileIndex::Info info = index.lookup(film.data());
        QVERIFY(info.known);
        QCOMPARE(info.firstSeen, t0);
        QCOMPARE(info.names, 1);
        QVERIFY(!info.seenBefore(t0));         // this very sighting does not count
        QVERIFY(info.seenBefore(t0 + 1));

        index.flush();
        QCOMPARE(index.pendingCount(), 0);
        info = index.lookup(film.data());
        QVERIFY(info.known);
        QCOMPARE(info.firstSeen, t0);

        // Again under another name, and once more under the first: written and
        // waiting sightings add up.
        index.note(film.data(), QStringLiteral("film (2020) HD.mkv"), 700, t0 + 50);
        index.note(film.data(), QStringLiteral("Film.2020.mkv"), 700, t0 + 60);
        info = index.lookup(film.data());
        QCOMPARE(info.firstSeen, t0);
        QCOMPARE(info.lastSeen, t0 + 60);
        QCOMPARE(info.names, 2);
        QCOMPARE(info.seenCount, uint32{3});
        QVERIFY(!index.lookup(other.data()).known);
    }   // written on the way out

    SeenFileIndex index;
    QVERIFY(index.open(path));
    const SeenFileIndex::Info info = index.lookup(film.data());
    QVERIFY(info.known);
    QCOMPARE(info.firstSeen, t0);
    QCOMPARE(info.lastSeen, t0 + 60);
    QCOMPARE(info.names, 2);
    QCOMPARE(info.seenCount, uint32{3});
    QVERIFY(!index.lookup(other.data()).known);
}

void tst_SearchList::seenIndex_keepsTheMostSeenNames()
{
    eMule::testing::TempDir tmp;
    SeenFileIndex index;
    SeenFileIndex::Limits limits;
    limits.maxNamesPerFile = 3;
    index.setLimits(limits);
    QVERIFY(index.open(tmp.filePath(QStringLiteral("seenfiles.db"))));

    // Six names; "name4" and "name5" are seen most, then "name3".
    const auto file = seenHash(7);
    for (int n = 0; n < 6; ++n)
        for (int times = 0; times <= n; ++times)
            index.note(file.data(), QStringLiteral("name%1").arg(n), 10, 1000 + n);
    index.flush();
    QCOMPARE(index.lookup(file.data()).names, 3);

    // A seventh, seen once, does not push a well-known name out.
    index.note(file.data(), QStringLiteral("newcomer"), 10, 2000);
    index.flush();
    QCOMPARE(index.lookup(file.data()).names, 3);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("check"));
    db.setDatabaseName(tmp.filePath(QStringLiteral("seenfiles.db")));
    QVERIFY(db.open());
    QStringList kept;
    {
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("SELECT name FROM names ORDER BY name")));
        while (query.next())
            kept << query.value(0).toString();
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("check"));
    QCOMPARE(kept, (QStringList{QStringLiteral("name3"), QStringLiteral("name4"),
                                QStringLiteral("name5")}));
}

void tst_SearchList::seenIndex_dropsWhatIsOldAndWhatIsOverTheCap()
{
    eMule::testing::TempDir tmp;
    SeenFileIndex index;
    SeenFileIndex::Limits limits;
    limits.maxFiles = 5;
    limits.maxAgeSecs = 1000;
    index.setLimits(limits);
    QVERIFY(index.open(tmp.filePath(QStringLiteral("seenfiles.db"))));

    // Recent: opening the store drops what has not been seen for half a year.
    const qint64 t0 = QDateTime::currentSecsSinceEpoch() - 5000;
    for (uint8 n = 0; n < 8; ++n)
        index.note(seenHash(n).data(), QStringLiteral("file%1").arg(n), 10, t0 + n);
    index.flush();

    // Over the cap: the three not seen for longest go.
    index.prune(t0 + 10);
    for (uint8 n = 0; n < 8; ++n)
        QCOMPARE(index.lookup(seenHash(n).data()).known, n >= 3);

    // Seen again: young once more.
    index.note(seenHash(3).data(), QStringLiteral("file3"), 10, t0 + 900);
    index.flush();
    index.prune(t0 + 1005);          // 4 (t0+4) is now older than the limit, 5 is not
    QVERIFY(index.lookup(seenHash(3).data()).known);
    QVERIFY(!index.lookup(seenHash(4).data()).known);
    QVERIFY(index.lookup(seenHash(5).data()).known);
    QCOMPARE(index.lookup(seenHash(4).data()).names, 0);   // its names went with it
}

void tst_SearchList::seenIndex_switchedOffRecordsNothing()
{
    eMule::testing::TempDir tmp;
    SeenFileIndex index;
    QVERIFY(index.open(tmp.filePath(QStringLiteral("seenfiles.db"))));
    const auto file = seenHash(9);
    index.note(file.data(), QStringLiteral("kept.bin"), 10, 1000);
    index.flush();

    index.setEnabled(false);
    QVERIFY(!index.isActive());
    index.note(seenHash(10).data(), QStringLiteral("ignored.bin"), 10, 2000);
    QCOMPARE(index.pendingCount(), 0);
    QVERIFY(!index.lookup(file.data()).known);          // not looked up either

    // What was there stays, for when it is switched on again.
    index.setEnabled(true);
    QVERIFY(index.lookup(file.data()).known);
    QVERIFY(!index.lookup(seenHash(10).data()).known);

    // Never opened: inert, whatever is asked of it.
    SeenFileIndex closed;
    closed.note(file.data(), QStringLiteral("x"), 1, 1);
    QVERIFY(!closed.lookup(file.data()).known);
    closed.flush();
}

void tst_SearchList::seenIndex_leavesANewerFileAlone()
{
    eMule::testing::TempDir tmp;
    const QString path = tmp.filePath(QStringLiteral("seenfiles.db"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("future"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE tomorrow (x INTEGER)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=99")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("future"));

    SeenFileIndex index;
    QVERIFY(!index.open(path));
    QVERIFY(!index.isActive());
    index.note(seenHash(1).data(), QStringLiteral("x"), 1, 1);
    QVERIFY(!index.lookup(seenHash(1).data()).known);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("future"));
    db.setDatabaseName(path);
    QVERIFY(db.open());
    {
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'files'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("future"));
}

void tst_SearchList::seenMarker_isSetForAFileMetBeforeThisSearch()
{
    eMule::testing::TempDir tmp;
    SeenFileIndex index;
    QVERIFY(index.open(tmp.filePath(QStringLiteral("seenfiles.db"))));
    theApp.seenFileIndex = &index;
    const auto restore = qScopeGuard([] { theApp.seenFileIndex = nullptr; });

    uint8 base[16];
    std::memset(base, 0x40, 16);
    const QByteArray packet = buildTCPSearchPacket(2, base);
    const Endpoint server(Address::fromString(QStringLiteral("192.168.0.1")), 4661);
    const auto answer = [&](SearchList& list) {
        list.processSearchAnswer(reinterpret_cast<const uint8*>(packet.constData()),
                                 static_cast<uint32>(packet.size()), true, server);
    };
    const auto rows = [](SearchList& list, uint32 id) {
        std::vector<const SearchFile*> out;
        list.forEachResult(id, [&](const SearchFile* f) { out.push_back(f); });
        return out;
    };

    // First time these two files turn up: nothing to say about them, and getting
    // the same answer twice within one search does not change that.
    SearchList first;
    const uint32 one = first.reserveSearch();
    first.beginSearch(one, {}, true);
    answer(first);
    answer(first);
    const auto fresh = rows(first, one);
    QVERIFY(fresh.size() >= 2);
    QByteArray knownHash;
    for (const SearchFile* f : fresh) {
        QVERIFY(!f->seenBefore());
        if (f->listParent() == nullptr && knownHash.isEmpty())
            knownHash = QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16);
    }
    QCOMPARE(index.pendingCount(), 2);

    // As if that had been a while ago, and under another name as well.
    index.flush();
    const auto* hash = reinterpret_cast<const uint8*>(knownHash.constData());
    const qint64 then = QDateTime::currentSecsSinceEpoch() - 3600;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("age"));
        db.setDatabaseName(tmp.filePath(QStringLiteral("seenfiles.db")));
        QVERIFY(db.open());
        QSqlQuery query(db);
        query.prepare(QStringLiteral("UPDATE files SET first_seen = ?"));
        query.addBindValue(then);
        QVERIFY(query.exec());
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("age"));
    index.note(hash, QStringLiteral("the same thing renamed.avi"), 1, then + 10);

    SearchList second;
    const uint32 two = second.reserveSearch();
    second.beginSearch(two, {}, true);
    answer(second);
    int marked = 0;
    for (const SearchFile* f : rows(second, two)) {
        QVERIFY(f->seenBefore());
        QCOMPARE(f->firstSeen(), then);
        if (QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16) == knownHash)
            QCOMPARE(f->seenNames(), 2);
        else
            QCOMPARE(f->seenNames(), 1);
        ++marked;
    }
    QCOMPARE(marked, 2);

    // Switched off: no marker, and nothing more recorded.
    index.flush();
    index.setEnabled(false);
    SearchList third;
    const uint32 three = third.reserveSearch();
    third.beginSearch(three, {}, true);
    answer(third);
    for (const SearchFile* f : rows(third, three))
        QVERIFY(!f->seenBefore());
    QCOMPARE(index.pendingCount(), 0);
}

void tst_SearchList::spamRating_hashHit()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0xEE, 16);

    // Pre-mark hash as spam
    list.markFileAsSpam(nullptr, false); // no-op for null

    // Create and add a file
    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("spam.exe"), 500000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    list.addToList(file);

    // Now mark it as spam
    SearchFile* found = list.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);
    list.markFileAsSpam(found);

    // Create a new file with same hash — it should get a high spam rating
    QByteArray packet2 = buildSingleResultPacket(hash, QStringLiteral("spam2.exe"), 500001);
    SafeMemFile data2(packet2);
    auto* file2 = new SearchFile(data2, true);
    file2->setSearchID(id);

    list.doSpamRating(file2, false);
    QVERIFY(file2->spamRating() >= 100); // hash hit = 100 pts

    delete file2; // not added to list, we own it
}

void tst_SearchList::spamRating_nameHit()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash1[16], hash2[16];
    std::memset(hash1, 0x11, 16);
    std::memset(hash2, 0x22, 16);

    // Create and mark a file as spam
    QByteArray packet1 = buildSingleResultPacket(hash1, QStringLiteral("badfile.exe"), 1000);
    SafeMemFile data1(packet1);
    auto* file1 = new SearchFile(data1, true);
    file1->setSearchID(id);
    list.addToList(file1);
    SearchFile* found1 = list.searchFileByHash(hash1, id);
    list.markFileAsSpam(found1);

    // Create a new file with exact same name but different hash
    QByteArray packet2 = buildSingleResultPacket(hash2, QStringLiteral("badfile.exe"), 1001);
    SafeMemFile data2(packet2);
    auto* file2 = new SearchFile(data2, true);
    file2->setSearchID(id);

    list.doSpamRating(file2, false);
    QVERIFY(file2->spamRating() >= 80); // exact name hit = 80 pts

    delete file2;
}

void tst_SearchList::spamRating_belowThreshold()
{
    SearchList list;
    SearchParams params;
    list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x33, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("clean_file.mp3"), 5000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);

    list.doSpamRating(file, false);
    QVERIFY(file->spamRating() < SEARCH_SPAM_THRESHOLD);
    QVERIFY(!file->isConsideredSpam());

    delete file;
}

void tst_SearchList::markFileAsSpam_addsToFilter()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x44, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("spam.zip"), 50000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    list.addToList(file);

    SearchFile* found = list.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);

    QSignalSpy spamSpy(&list, &SearchList::spamStatusChanged);
    list.markFileAsSpam(found);

    QVERIFY(found->isConsideredSpam());
    QCOMPARE(spamSpy.count(), 1);
}

void tst_SearchList::markFileAsNotSpam_removesFromFilter()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x55, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("notspam.doc"), 30000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    list.addToList(file);

    SearchFile* found = list.searchFileByHash(hash, id);
    list.markFileAsSpam(found);
    QVERIFY(found->isConsideredSpam());

    list.markFileAsNotSpam(found);
    QVERIFY(!found->isConsideredSpam());
    QCOMPARE(found->spamRating(), uint32{0});
}

void tst_SearchList::saveAndLoadSpamFilter_roundTrip()
{
    eMule::testing::TempDir tempDir;

    // Create list and add some spam entries
    SearchList original;
    original.loadSpamFilter(tempDir.path());   // nothing there yet; arms the save
    SearchParams params;
    uint32 id = original.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x66, 16);

    const QString spamName = QString::fromUtf8("sp\xc3\xa4mmy file.exe");
    QByteArray packet = buildSingleResultPacket(hash, spamName, 100000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    file->addClient({0x0A0B0C0D, 4662, 0, 0});
    file->addServer({0x01020304, 4665, 1, true});
    original.addToList(file);

    SearchFile* found = original.searchFileByHash(hash, id);
    original.markFileAsSpam(found);
    original.m_udpServerRecords[0x01020304] = {7, 3};

    // Save
    original.saveSpamFilter(tempDir.path());

    // The layout is MFC's: header byte, record count, tags — no version byte.
    QFile raw(tempDir.filePath(QStringLiteral("SearchSpam.met")));
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QByteArray head = raw.read(5);
    raw.close();
    QCOMPARE(static_cast<uint8>(head[0]), static_cast<uint8>(MET_HEADER_I64TAGS));
    const uint32 records = static_cast<uint32>(original.m_knownSpamNames.size()
        + original.m_knownSimilarSpamNames.size() + 1 /*size*/ + 1 /*hash*/
        + 1 /*server*/ + original.m_knownSpamSourcesIPs.size() + 1 /*ratio*/);
    QCOMPARE(peekUInt32(reinterpret_cast<const uint8*>(head.constData()) + 1), records);

    // Load into new list — twice: a reload replaces, it does not append.
    SearchList loaded;
    loaded.loadSpamFilter(tempDir.path());
    loaded.loadSpamFilter(tempDir.path());

    QCOMPARE(loaded.m_knownSpamNames, original.m_knownSpamNames);
    QVERIFY(loaded.m_knownSpamNames.contains(spamName));
    QCOMPARE(loaded.m_knownSimilarSpamNames, original.m_knownSimilarSpamNames);
    QCOMPARE(loaded.m_knownSpamSizes, std::vector<uint64>{100000});
    QVERIFY(loaded.m_knownSpamSourcesIPs.contains(0x0A0B0C0D));
    QVERIFY(loaded.m_knownSpamServerIPs.contains(0x01020304));
    QCOMPARE(loaded.m_udpServerRecords.at(0x01020304).totalResults, 7u);
    QCOMPARE(loaded.m_udpServerRecords.at(0x01020304).spamResults, 3u);

    // Verify the loaded list recognizes the spam hash
    QByteArray packet2 = buildSingleResultPacket(hash, QStringLiteral("test.exe"), 100001);
    SafeMemFile data2(packet2);
    auto* file2 = new SearchFile(data2, true);

    loaded.doSpamRating(file2, false);
    QVERIFY(file2->spamRating() >= 100); // hash hit

    delete file2;

    // A list that never read the file must not replace it with an empty one.
    SearchList untouched;
    untouched.saveSpamFilter(tempDir.path());
    SearchList again;
    again.loadSpamFilter(tempDir.path());
    QVERIFY(again.m_knownSpamServerIPs.contains(0x01020304));
}

void tst_SearchList::storeAndLoadSearches_roundTrip()
{
    eMule::testing::TempDir tempDir;

    // Create list with some results
    SearchList original;
    SearchParams params;
    uint32 id = original.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x77, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("saved.mp3"), 77777, 10);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
    file->setSearchID(id);
    original.addToList(file);

    QCOMPARE(original.resultCount(id), uint32{1});

    // Store
    original.storeSearches(tempDir.path());

    // Load into new list
    SearchList loaded;
    loaded.loadSearches(tempDir.path());

    // Verify loaded data
    QCOMPARE(loaded.resultCount(id), uint32{1});

    SearchFile* found = loaded.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->fileName(), QStringLiteral("saved.mp3"));
}

void tst_SearchList::signal_resultAdded()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    QSignalSpy spy(&list, &SearchList::resultAdded);

    uint8 hash[16];
    std::memset(hash, 0x88, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("signal_test.txt"), 500);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    list.addToList(file);

    QCOMPARE(spy.count(), 1);
    auto* emitted = spy.at(0).at(0).value<SearchFile*>();
    QVERIFY(emitted != nullptr);
    QCOMPARE(emitted->fileName(), QStringLiteral("signal_test.txt"));
}

void tst_SearchList::signal_resultUpdated()
{
    SearchList list;
    SearchParams params;
    uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x99, 16);

    // Add first file
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("dup.avi"), 10000, 5);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    }

    QSignalSpy spy(&list, &SearchList::resultUpdated);

    // Add duplicate — should emit resultUpdated
    {
        QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("dup_renamed.avi"), 10000, 3);
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80002, 4662);
        file->setSearchID(id);
        list.addToList(file);
    }

    QCOMPARE(spy.count(), 1);
}

void tst_SearchList::clientSharedFiles_opensOwnTab()
{
    // A peer's list used to land in whatever ED2K search was current (and was
    // filtered by its file type). MFC gives it its own tab: SearchList.cpp:182-220.
    SearchList list;
    SearchParams params;
    params.type = SearchType::Ed2kGlobal;
    const uint32 ed2kID = list.newSearch(QStringLiteral("Video"), params);

    UpDownClient alice;
    alice.setUserName(QStringLiteral("Alice"));
    QSignalSpy headerSpy(&list, &SearchList::tabHeaderUpdated);

    const QByteArray packet = buildTCPSearchPacket(2);
    const uint32 id = list.processClientSharedFiles(
        alice, reinterpret_cast<const uint8*>(packet.constData()),
        static_cast<uint32>(packet.size()), QStringLiteral("Music"));

    QVERIFY(id != 0);
    QVERIFY(id != ed2kID);
    QCOMPARE(alice.searchID(), id);
    QCOMPARE(list.currentSearchID(), ed2kID);   // the running search keeps its routing
    QCOMPARE(list.resultCount(ed2kID), uint32{0});
    QCOMPARE(list.resultCount(id), uint32{2});  // .mp3 despite the "Video" filter

    const auto* entry = list.searchEntry(id);
    QVERIFY(entry != nullptr);
    QVERIFY(entry->clientSharedFiles);
    QCOMPARE(entry->title, QStringLiteral("Alice"));

    list.forEachResult(id, [](const SearchFile* f) {
        QCOMPARE(f->directory(), QStringLiteral("Music"));
    });
    QCOMPARE(headerSpy.count(), 1);
    QCOMPARE(headerSpy.at(0).at(0).toUInt(), id);
}

// MFC SearchList.cpp:218 — only a browsed file of a peer that advertises preview.
void tst_SearchList::clientSharedFiles_marksWhatThePeerCanPreview()
{
    SearchList list;

    const auto listOf = [](std::initializer_list<QString> names) {
        SafeMemFile mem;
        mem.writeUInt32(static_cast<uint32>(names.size()));
        uint8 hash[16] = {0x70};
        for (const QString& name : names) {
            ++hash[1];
            const QByteArray one = buildSingleResultPacket(hash, name, 5000);
            mem.write(one.constData(), static_cast<uint32>(one.size()));
        }
        return mem.takeBuffer();
    };
    const QByteArray packet = listOf({QStringLiteral("clip.avi"), QStringLiteral("shot.png"),
                                      QStringLiteral("song.mp3")});
    const auto previewable = [&](UpDownClient& peer) {
        const uint32 id = list.processClientSharedFiles(
            peer, reinterpret_cast<const uint8*>(packet.constData()),
            static_cast<uint32>(packet.size()));
        QStringList names;
        list.forEachResult(id, [&](const SearchFile* f) {
            if (f->isPreviewPossible())
                names << f->fileName();
        });
        names.sort();
        return names;
    };

    UpDownClient capable;
    capable.setSupportsPreview(true);
    QCOMPARE(previewable(capable), QStringList({QStringLiteral("clip.avi"), QStringLiteral("shot.png")}));

    UpDownClient plain;
    QCOMPARE(previewable(plain), QStringList());
}

void tst_SearchList::clientSharedFiles_reusesTabThenReopensAfterClose()
{
    SearchList list;
    UpDownClient bob;
    bob.setUserName(QStringLiteral("Bob"));

    uint8 otherBase[16];
    std::memset(otherBase, 0x55, 16);
    const QByteArray first = buildTCPSearchPacket(1);
    const QByteArray second = buildTCPSearchPacket(1, otherBase);

    const uint32 id = list.processClientSharedFiles(
        bob, reinterpret_cast<const uint8*>(first.constData()), static_cast<uint32>(first.size()));
    // A second directory answer goes into the same tab
    QCOMPARE(list.processClientSharedFiles(
                 bob, reinterpret_cast<const uint8*>(second.constData()),
                 static_cast<uint32>(second.size())),
             id);
    QCOMPARE(list.resultCount(id), uint32{2});

    // The user closed the tab (RemoveSearch): the next answer opens a fresh one
    list.removeResults(id);
    const uint32 reopened = list.processClientSharedFiles(
        bob, reinterpret_cast<const uint8*>(first.constData()), static_cast<uint32>(first.size()));
    QVERIFY(reopened != id);
    QCOMPARE(bob.searchID(), reopened);
    QVERIFY(list.searchEntry(reopened) != nullptr);
    QCOMPARE(list.resultCount(reopened), uint32{1});
}

void tst_SearchList::clientSharedFiles_emptyListStillOpensTab()
{
    SearchList list;
    UpDownClient carol;
    carol.setUserName(QStringLiteral("Carol"));

    const QByteArray empty = buildTCPSearchPacket(0);
    const uint32 id = list.processClientSharedFiles(
        carol, reinterpret_cast<const uint8*>(empty.constData()), static_cast<uint32>(empty.size()));
    QVERIFY(id != 0);
    QVERIFY(list.searchEntry(id) != nullptr);
    QCOMPARE(list.resultCount(id), uint32{0});
}

// ---------------------------------------------------------------------------
// Kad keyword results
// ---------------------------------------------------------------------------

namespace {

/// TAG_KADAICHHASHRESULT blob: count, then (popularity, hash[20]) per entry.
/// Each hash is 20 bytes of its fill value.
Tag kadAICHVotes(const std::vector<std::pair<uint8, uint8>>& popularityAndFill)
{
    QByteArray blob(1, static_cast<char>(popularityAndFill.size()));
    for (const auto& [popularity, fill] : popularityAndFill) {
        blob.append(static_cast<char>(popularity));
        blob.append(QByteArray(20, static_cast<char>(fill)));
    }
    return Tag::makeBsob(QByteArrayLiteral(TAG_KADAICHHASHRESULT), std::move(blob));
}

Tag kadPublishInfo(uint8 publishers)
{
    return Tag(QByteArrayLiteral(TAG_PUBLISHINFO),
               (uint32{1} << 24) | (uint32{publishers} << 16) | uint32{100});
}

} // namespace

// Our own Kad search: the result knows it, so counts take the max (every node reports
// the total) and completeness is unknown. The flag used to stay false.
void tst_SearchList::kadKeywordResult_setsKadFlagAndMaxesSources()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x5A, 16);

    list.addKadKeywordResult(id, hash, QStringLiteral("kad.mp3"), 4242, QStringLiteral("Audio"), 5, 2);
    list.addKadKeywordResult(id, hash, QStringLiteral("kad.mp3"), 4242, QStringLiteral("Audio"), 3, 1);

    SearchFile* found = list.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);
    QVERIFY(found->isKadResult());
    QCOMPARE(found->sourceCount(), uint32{5});
    QCOMPARE(found->isComplete(), -1);
    for (const SearchFile* child : found->listChildren()) {
        QVERIFY(child->isKadResult());
        QCOMPARE(child->sourceCount(), uint32{5});
    }
}

void tst_SearchList::kadKeywordResult_keepsTheBestPublishTrust()
{
    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    uint8 hash[16];
    std::memset(hash, 0x5B, 16);
    // names << 24 | publishers << 16 | trust x100
    const auto info = [](uint32 trust) {
        return std::vector<Tag>{Tag(QByteArrayLiteral(TAG_PUBLISHINFO), (1u << 24) | (5u << 16) | trust)};
    };

    list.addKadKeywordResult(id, hash, QStringLiteral("Some Film 2024.mkv"), 4242,
                             QStringLiteral("Video"), 5, 2, info(80));
    SearchFile* found = list.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->fakeVerdict().band, Confidence::LooksGood);

    // A later node knows it better; an earlier low figure must not stick
    list.addKadKeywordResult(id, hash, QStringLiteral("Some Film 2024.mkv"), 4242,
                             QStringLiteral("Video"), 5, 2, info(450));
    list.addKadKeywordResult(id, hash, QStringLiteral("Some Film 2024.mkv"), 4242,
                             QStringLiteral("Video"), 5, 2, info(120));
    QCOMPARE(found->kadPublishInfo() & 0xFFFF, uint32{450});
    // One name: many publishers alone do not make it genuine
    QCOMPARE(found->fakeVerdict().band, Confidence::LooksGood);

    list.addKadKeywordResult(id, hash, QStringLiteral("Some.Film.(2024).BluRay.x264.mkv"), 4242,
                             QStringLiteral("Video"), 5, 2, info(100));
    QCOMPARE(found->kadPublishInfo() & 0xFFFF, uint32{450});
    QCOMPARE(found->fakeVerdict().band, Confidence::Genuine);
}

void tst_SearchList::fakeVerdict_followsTheNamesOfAHash()
{
    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    uint8 hash[16];
    std::memset(hash, 0x5C, 16);
    const auto add = [&](const QString& name) {
        const QByteArray data = buildSingleResultPacket(hash, name, 700u << 20);
        SafeMemFile mem(data);
        auto* file = new SearchFile(mem, true);
        file->setSearchID(id);
        list.addToList(file);
    };

    add(QStringLiteral("Il Diavolo Veste Prada (2006 - David Frankel).avi"));
    SearchFile* row = list.searchFileByHash(hash, id);
    QVERIFY(row != nullptr);
    QVERIFY(row->hasFakeVerdict());
    QCOMPARE(row->fakeVerdict().score, 0);

    // The same film under another name changes nothing
    add(QStringLiteral("Il.Diavolo.Veste.Prada.2006.iTA.DVDRip.XviD.avi"));
    QCOMPARE(row->fakeVerdict().score, 0);

    // Unrelated names, and of another kind of file: the mark of a fake
    add(QStringLiteral("Microsoft Office 2010 Pro.zip"));
    add(QStringLiteral("Iron Maiden Discography.rar"));
    const FakeFileVerdict& verdict = row->fakeVerdict();
    QVERIFY(verdict.has(FakeReason::MultipleNames));
    QVERIFY(verdict.has(FakeReason::NamesSpanKinds));
    QCOMPARE(verdict.band, Confidence::Suspect);

    // Children carry no verdict of their own
    for (const SearchFile* child : row->listChildren())
        QVERIFY(!child->hasFakeVerdict());
}

void tst_SearchList::fakeVerdict_followsSpamMarkAndNotes()
{
    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    uint8 hash[16];
    std::memset(hash, 0x5D, 16);
    const QByteArray data = buildSingleResultPacket(hash, QStringLiteral("Some Film 2024.mkv"), 700u << 20);
    SafeMemFile mem(data);
    auto* file = new SearchFile(mem, true);
    file->setSearchID(id);
    list.addToList(file);
    SearchFile* row = list.searchFileByHash(hash, id);
    QVERIFY(row != nullptr);
    QCOMPARE(row->fakeVerdict().band, Confidence::LooksGood);

    list.markFileAsSpam(row, true);
    QCOMPARE(row->fakeVerdict().band, Confidence::Spam);
    list.markFileAsNotSpam(row, true);
    QCOMPARE(row->fakeVerdict().band, Confidence::LooksGood);

    // A Kad note calling it a fake, with a telling comment
    QVERIFY(list.addNotes(hash, QByteArray(16, 'p'), 1, QStringLiteral("FAKE - wrong file")));
    const FakeFileVerdict& verdict = row->fakeVerdict();
    QVERIFY(verdict.has(FakeReason::BadSignalComment));
    QVERIFY(verdict.has(FakeReason::BadRating));
    QVERIFY(verdict.score >= 35);
}

void tst_SearchList::fakeVerdict_usesNamesOnRecord()
{
    eMule::testing::TempDir tmp;
    SeenFileIndex index;
    QVERIFY(index.open(tmp.filePath(QStringLiteral("seenfiles.db"))));
    theApp.seenFileIndex = &index;
    const auto restore = qScopeGuard([] { theApp.seenFileIndex = nullptr; });

    uint8 hash[16];
    std::memset(hash, 0x5E, 16);
    // Earlier searches met the hash under these
    index.note(hash, QStringLiteral("Microsoft Office 2010 Pro.zip"), 700u << 20, 1000);
    index.note(hash, QStringLiteral("Iron Maiden Discography.rar"), 700u << 20, 1001);
    index.flush();
    const SeenFileIndex::Info info = index.lookup(hash);
    QCOMPARE(info.names, 2);
    QCOMPARE(info.nameList, (QStringList{QStringLiteral("Iron Maiden Discography.rar"),
                                         QStringLiteral("Microsoft Office 2010 Pro.zip")}));

    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    const QByteArray data = buildSingleResultPacket(
        hash, QStringLiteral("Il Diavolo Veste Prada (2006).avi"), 700u << 20);
    SafeMemFile mem(data);
    auto* file = new SearchFile(mem, true);
    file->setSearchID(id);
    list.addToList(file);

    // One name in this search, but the record says otherwise
    const SearchFile* row = list.searchFileByHash(hash, id);
    QVERIFY(row != nullptr);
    QVERIFY(row->fakeVerdict().has(FakeReason::MultipleNames));
    QVERIFY(row->fakeVerdict().has(FakeReason::NamesSpanKinds));
}

void tst_SearchList::recalculateSpamRatings_signalsAChange()
{
    SearchList list;
    const uint32 id = list.newSearch({}, SearchParams{});
    uint8 hashA[16];
    uint8 hashB[16];
    std::memset(hashA, 0x61, 16);
    std::memset(hashB, 0x62, 16);
    for (const uint8* hash : {hashA, hashB}) {
        // Same name: marking one teaches the filter the other
        const QByteArray data = buildSingleResultPacket(hash, QStringLiteral("Same Spam Name.avi"), 1000);
        SafeMemFile mem(data);
        auto* file = new SearchFile(mem, true);
        file->setSearchID(id);
        list.addToList(file);
    }
    SearchFile* a = list.searchFileByHash(hashA, id);
    SearchFile* b = list.searchFileByHash(hashB, id);
    QVERIFY(a && b);
    list.markFileAsSpam(a, true);
    QVERIFY(!b->isConsideredSpam());

    QSignalSpy changed(&list, &SearchList::spamStatusChanged);
    list.recalculateSpamRatings(id);
    QVERIFY(b->isConsideredSpam());
    QCOMPARE(b->fakeVerdict().band, Confidence::Spam);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().first().value<SearchFile*>(), b);
}

void tst_SearchList::kadKeywordResult_adoptsTheOneAgreedAICHHash()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x5B, 16);

    // 6 publishers, 2 of them reported the hash: 6 / 2 <= 3
    list.addKadKeywordResult(id, hash, QStringLiteral("aich.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(6), kadAICHVotes({{2, 0xA1}})});

    SearchFile* found = list.searchFileByHash(hash, id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->kadPublishInfo() >> 16 & 0xFF, uint32{6});
    QVERIFY(found->fileIdentifier().hasAICHHash());
    const QByteArray expected(20, static_cast<char>(0xA1));
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(found->fileIdentifier().getAICHHash().getRawHash()), 20),
             expected);
    // Result properties, never tags: a tag would travel into the download
    for (const Tag& tag : found->tags())
        QVERIFY(tag.name() != QByteArrayLiteral(TAG_PUBLISHINFO)
                && tag.name() != QByteArrayLiteral(TAG_KADAICHHASHRESULT));

    // The nodes that said so are remembered with the root: each is one voice, whatever
    // popularity it claims. A repeat from the same node adds nobody.
    QVERIFY(found->aichVoters().empty());          // no sender given above
    list.addKadKeywordResult(id, hash, QStringLiteral("aich.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(6), kadAICHVotes({{2, 0xA1}})}, 0x58010001u);
    list.addKadKeywordResult(id, hash, QStringLiteral("aich.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(6), kadAICHVotes({{2, 0xA1}})}, 0x59010001u);
    list.addKadKeywordResult(id, hash, QStringLiteral("aich.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(6), kadAICHVotes({{2, 0xA1}})}, 0x59010001u);
    std::vector<Address> voters;
    for (const SearchFile* row : found->listChildren())
        voters.insert(voters.end(), row->aichVoters().begin(), row->aichVoters().end());
    QCOMPARE(voters.size(), size_t{2});
    for (const SearchFile* row : found->listChildren())
        QVERIFY(!row->isAICHVouchedDirectly());
}

void tst_SearchList::kadKeywordResult_ignoresRareOrCompetingAICHHashes()
{
    SearchList list;
    SearchParams params;
    const uint32 id = list.newSearch({}, params);

    uint8 rare[16], competing[16], truncated[16], noInfo[16];
    std::memset(rare, 0x5C, 16);
    std::memset(competing, 0x5D, 16);
    std::memset(truncated, 0x5E, 16);
    std::memset(noInfo, 0x5F, 16);

    // 8 publishers, 2 reported it: 8 / 2 > 3
    list.addKadKeywordResult(id, rare, QStringLiteral("rare.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(8), kadAICHVotes({{2, 0xA1}})});
    // Two different hashes
    list.addKadKeywordResult(id, competing, QStringLiteral("competing.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(4), kadAICHVotes({{2, 0xA1}, {2, 0xB2}})});
    // A count byte promising more than the blob holds
    Tag shortBlob = Tag::makeBsob(QByteArrayLiteral(TAG_KADAICHHASHRESULT),
                                  QByteArray("\x02\x02", 2) + QByteArray(20, '\xA1'));
    list.addKadKeywordResult(id, truncated, QStringLiteral("truncated.bin"), 4242, {}, 5, 0,
                             {kadPublishInfo(2), shortBlob});
    // No publish info to weigh the vote against
    list.addKadKeywordResult(id, noInfo, QStringLiteral("noinfo.bin"), 4242, {}, 5, 0,
                             {kadAICHVotes({{2, 0xA1}})});

    for (const uint8* hash : {rare, competing, truncated, noInfo}) {
        SearchFile* found = list.searchFileByHash(hash, id);
        QVERIFY(found != nullptr);
        QVERIFY(!found->fileIdentifier().hasAICHHash());
    }
}

// The stored file has no search type of its own: without the Kad byte a reloaded Kad
// result came back as an eD2K one.
void tst_SearchList::storeAndLoadSearches_keepsKadFlag()
{
    eMule::testing::TempDir tempDir;

    SearchList original;
    SearchParams params;
    const uint32 kadID = original.newSearch({}, params);
    const uint32 ed2kID = original.newSearch({}, params);

    uint8 kadHash[16], ed2kHash[16];
    std::memset(kadHash, 0x61, 16);
    std::memset(ed2kHash, 0x62, 16);

    original.addKadKeywordResult(kadID, kadHash, QStringLiteral("kad.mp3"), 4242, {}, 5, 0);

    QByteArray packet = buildSingleResultPacket(ed2kHash, QStringLiteral("ed2k.mp3"), 77777, 10);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
    file->setSearchID(ed2kID);
    original.addToList(file);

    original.storeSearches(tempDir.path());

    SearchList loaded;
    loaded.loadSearches(tempDir.path());

    SearchFile* kadFound = loaded.searchFileByHash(kadHash, kadID);
    QVERIFY(kadFound != nullptr);
    QVERIFY(kadFound->isKadResult());
    QCOMPARE(kadFound->fileName(), QStringLiteral("kad.mp3"));

    SearchFile* ed2kFound = loaded.searchFileByHash(ed2kHash, ed2kID);
    QVERIFY(ed2kFound != nullptr);
    QVERIFY(!ed2kFound->isKadResult());
}

// A search that could send nothing (REST or web while offline) must not take the
// server answers away from the one still collecting them.
void tst_SearchList::newSearch_withoutARequestLeavesTheRunningOneItsAnswers()
{
    SearchList list;
    SearchParams params;
    params.type = SearchType::Ed2kServer;
    const uint32 running = list.newSearch({}, params);
    const uint32 idle = list.newSearch(QStringLiteral("Audio"), params, 0, /*takeEd2kRouting*/ false);
    QVERIFY(idle != running);
    QVERIFY(list.hasSearch(idle));

    const QByteArray packet = buildTCPSearchPacket(2);
    list.processSearchAnswer(reinterpret_cast<const uint8*>(packet.constData()),
                             static_cast<uint32>(packet.size()), true,
                             Endpoint::fromHostOrder(0x0A000001, 4661));
    QCOMPARE(list.resultCount(running), uint32{2});
    QCOMPARE(list.resultCount(idle), uint32{0});
}

QTEST_MAIN(tst_SearchList)
#include "tst_SearchList.moc"
