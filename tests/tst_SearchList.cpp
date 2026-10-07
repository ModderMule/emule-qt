/// @file tst_SearchList.cpp
/// @brief Tests for search/SearchList — session management, dedup, spam, persistence, signals.

#include "TestHelpers.h"
#include "client/UpDownClient.h"
#include "crypto/AICHData.h"
#include "search/SearchList.h"
#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QSignalSpy>
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
    void addToList_duplicate_sameName_merges();
    void addToList_aichRoots_data();
    void addToList_aichRoots();
    void addToList_newNameChildCountsItsSources();
    void addToList_kadOrigin_serverResultWins_data();
    void addToList_kadOrigin_serverResultWins();
    void addToList_kadOrigin_keptWhenAllAnswersAreKad();
    void addToList_fileTypeFilter();
    void removeResults_clearsSearch();
    void processSearchAnswer_tcp();
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
        Tag(FT_META_NETWORK, uint32{META_NETWORK_KAD}).writeNewEd2kTag(mem);
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
    SearchParams params;
    uint32 id = original.newSearch({}, params);

    uint8 hash[16];
    std::memset(hash, 0x66, 16);

    QByteArray packet = buildSingleResultPacket(hash, QStringLiteral("spammy.exe"), 100000);
    SafeMemFile data(packet);
    auto* file = new SearchFile(data, true);
    file->setSearchID(id);
    original.addToList(file);

    SearchFile* found = original.searchFileByHash(hash, id);
    original.markFileAsSpam(found);

    // Save
    original.saveSpamFilter(tempDir.path());

    // Load into new list
    SearchList loaded;
    loaded.loadSpamFilter(tempDir.path());

    // Verify the loaded list recognizes the spam hash
    QByteArray packet2 = buildSingleResultPacket(hash, QStringLiteral("test.exe"), 100001);
    SafeMemFile data2(packet2);
    auto* file2 = new SearchFile(data2, true);

    loaded.doSpamRating(file2, false);
    QVERIFY(file2->spamRating() >= 100); // hash hit

    delete file2;
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
