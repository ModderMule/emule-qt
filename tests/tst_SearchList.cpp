/// @file tst_SearchList.cpp
/// @brief Tests for search/SearchList — session management, dedup, spam, persistence, signals.

#include "TestHelpers.h"
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
    void newSearch_initializesCounters();
    void addToList_newParent();
    void addToList_duplicate_merges();
    void addToList_duplicate_sameName_merges();
    void addToList_fileTypeFilter();
    void removeResults_clearsSearch();
    void processSearchAnswer_tcp();
    void spamRating_hashHit();
    void spamRating_nameHit();
    void spamRating_belowThreshold();
    void markFileAsSpam_addsToFilter();
    void markFileAsNotSpam_removesFromFilter();
    void saveAndLoadSpamFilter_roundTrip();
    void storeAndLoadSearches_roundTrip();
    void signal_resultAdded();
    void signal_resultUpdated();
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
        true, 0xC0A80001, 4661);

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

QTEST_MAIN(tst_SearchList)
#include "tst_SearchList.moc"
