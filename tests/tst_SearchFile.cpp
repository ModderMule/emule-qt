/// @file tst_SearchFile.cpp
/// @brief Tests for search/SearchFile — construction, source counting, serialization, tag conversion.

#include "TestHelpers.h"
#include "search/SearchFile.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QTest>
#include <cstring>

using namespace eMule;

class tst_SearchFile : public QObject {
    Q_OBJECT

private slots:
    void construct_fromStream();
    void construct_copy();
    void addSources_ed2k_additive();
    void addSources_kad_max();
    void addCompleteSources_ed2k_additive();
    void addCompleteSources_kad_max();
    void isComplete_ed2k();
    void isComplete_kad();
    void isConsideredSpam_threshold();
    void storeAndLoad_roundTrip();
    void convertED2KTag_mediaLength();
    void isValidSearchResultClientIPPort_valid();
    void isValidSearchResultClientIPPort_invalid();
    void serverEquality();
    void clientManagement();
};

/// Helper: build a minimal search result packet with one file
static QByteArray buildSearchResultPacket(const uint8* hash,
                                          uint32 clientID, uint16 clientPort,
                                          const QString& fileName, uint32 fileSize,
                                          uint32 sources = 0)
{
    SafeMemFile mem;

    // Hash
    mem.write(hash, 16);

    // Client ID/port
    mem.writeUInt32(clientID);
    mem.writeUInt16(clientPort);

    // Tags: filename + size + sources
    uint32 tagCount = 2;
    if (sources > 0) ++tagCount;
    mem.writeUInt32(tagCount);

    Tag(FT_FILENAME, fileName).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, fileSize).writeNewEd2kTag(mem);
    if (sources > 0)
        Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);

    return mem.takeBuffer();
}

void tst_SearchFile::construct_fromStream()
{
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);

    QByteArray packet = buildSearchResultPacket(hash, 0x0A010203, 4662,
                                                QStringLiteral("test.mp3"), 12345, 10);
    SafeMemFile data(packet);

    SearchFile file(data, true, 0xC0A80001, 4661);

    QCOMPARE(file.fileName(), QStringLiteral("test.mp3"));
    QCOMPARE(static_cast<uint64>(file.fileSize()), uint64{12345});
    QVERIFY(md4equ(file.fileHash(), hash));
    QCOMPARE(file.sourceCount(), uint32{10});
    QVERIFY(!file.clients().empty());
    QVERIFY(!file.servers().empty());
    QCOMPARE(file.servers().front().ip, uint32{0xC0A80001});
}

void tst_SearchFile::construct_copy()
{
    uint8 hash[16];
    std::memset(hash, 0xCD, 16);

    QByteArray packet = buildSearchResultPacket(hash, 0x01020304, 4662,
                                                QStringLiteral("video.avi"), 999999, 5);
    SafeMemFile data(packet);
    SearchFile original(data, true, 0x0A000001, 4661);
    original.setSearchID(7);
    original.setSpamRating(42);

    SearchFile copy(&original);
    QCOMPARE(copy.fileName(), QStringLiteral("video.avi"));
    QCOMPARE(static_cast<uint64>(copy.fileSize()), uint64{999999});
    QVERIFY(md4equ(copy.fileHash(), hash));
    QCOMPARE(copy.sourceCount(), uint32{5});
    QCOMPARE(copy.searchID(), uint32{7});
    QCOMPARE(copy.spamRating(), uint32{42});
    QVERIFY(!copy.clients().empty());
}

void tst_SearchFile::addSources_ed2k_additive()
{
    SearchFile file;
    // Default is not Kad, so ED2K additive behavior
    file.addSources(10);
    QCOMPARE(file.sourceCount(), uint32{10});
    file.addSources(5);
    QCOMPARE(file.sourceCount(), uint32{15});
}

void tst_SearchFile::addSources_kad_max()
{
    uint8 hash[16] = {};
    QByteArray packet = buildSearchResultPacket(hash, 1, 1, QStringLiteral("f.txt"), 100);
    SafeMemFile data(packet);

    SearchFile file(data, true, 0, 0, {}, true); // kadResult = true
    QVERIFY(file.isKadResult());

    file.addSources(10);
    QCOMPARE(file.sourceCount(), uint32{10});
    file.addSources(5);
    QCOMPARE(file.sourceCount(), uint32{10}); // max, not additive
    file.addSources(20);
    QCOMPARE(file.sourceCount(), uint32{20});
}

void tst_SearchFile::addCompleteSources_ed2k_additive()
{
    SearchFile file;
    file.addCompleteSources(3);
    QCOMPARE(file.completeSourceCount(), uint32{3});
    file.addCompleteSources(2);
    QCOMPARE(file.completeSourceCount(), uint32{5});
}

void tst_SearchFile::addCompleteSources_kad_max()
{
    uint8 hash[16] = {};
    QByteArray packet = buildSearchResultPacket(hash, 1, 1, QStringLiteral("f.txt"), 100);
    SafeMemFile data(packet);

    SearchFile file(data, true, 0, 0, {}, true);
    file.addCompleteSources(10);
    QCOMPARE(file.completeSourceCount(), uint32{10});
    file.addCompleteSources(5);
    QCOMPARE(file.completeSourceCount(), uint32{10});
}

void tst_SearchFile::isComplete_ed2k()
{
    SearchFile file;
    QCOMPARE(file.isComplete(), -1); // no sources

    file.addSources(10);
    QCOMPARE(file.isComplete(), 0); // has sources, no complete ones

    file.addCompleteSources(3);
    QCOMPARE(file.isComplete(), 1); // has complete sources
}

void tst_SearchFile::isComplete_kad()
{
    uint8 hash[16] = {};
    QByteArray packet = buildSearchResultPacket(hash, 1, 1, QStringLiteral("f.txt"), 100);
    SafeMemFile data(packet);

    SearchFile file(data, true, 0, 0, {}, true);
    file.addSources(10);
    file.addCompleteSources(5);
    QCOMPARE(file.isComplete(), -1); // Kad always returns -1
}

void tst_SearchFile::isConsideredSpam_threshold()
{
    SearchFile file;
    file.setSpamRating(SEARCH_SPAM_THRESHOLD - 1);
    QVERIFY(!file.isConsideredSpam());

    file.setSpamRating(SEARCH_SPAM_THRESHOLD);
    QVERIFY(file.isConsideredSpam());

    file.setSpamRating(SEARCH_SPAM_THRESHOLD + 1);
    QVERIFY(file.isConsideredSpam());
}

void tst_SearchFile::storeAndLoad_roundTrip()
{
    uint8 hash[16];
    std::memset(hash, 0xEF, 16);

    QByteArray packet = buildSearchResultPacket(hash, 0x0A010203, 4662,
                                                QStringLiteral("roundtrip.txt"), 54321, 7);
    SafeMemFile data(packet);
    SearchFile original(data, true, 0xC0A80001, 4661);

    // Store
    SafeMemFile stored;
    original.storeToFile(stored);

    // Load
    stored.seek(0, 0);
    SearchFile loaded(stored, true);

    QCOMPARE(loaded.fileName(), QStringLiteral("roundtrip.txt"));
    QCOMPARE(static_cast<uint64>(loaded.fileSize()), uint64{54321});
    QVERIFY(md4equ(loaded.fileHash(), hash));
    QCOMPARE(loaded.sourceCount(), uint32{7});
}

void tst_SearchFile::convertED2KTag_mediaLength()
{
    // Build a packet with an old-style "length" string tag containing "1:30"
    SafeMemFile mem;

    uint8 hash[16] = {};
    mem.write(hash, 16);
    mem.writeUInt32(1); // clientID
    mem.writeUInt16(1); // clientPort
    mem.writeUInt32(2); // tag count

    Tag(FT_FILENAME, QStringLiteral("song.mp3")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    // Old-style string-named tag: "length" = "1:30"
    Tag(QByteArray(FT_ED2K_MEDIA_LENGTH), QStringLiteral("1:30")).writeTagToFile(mem, UTF8Mode::Raw);

    QByteArray buf = mem.takeBuffer();
    SafeMemFile readMem(buf);

    SearchFile file(readMem, true);

    // The "1:30" should have been converted to 90 seconds
    uint32 length = file.getIntTagValue(FT_MEDIA_LENGTH);
    QCOMPARE(length, uint32{90});
}

void tst_SearchFile::isValidSearchResultClientIPPort_valid()
{
    QVERIFY(isValidSearchResultClientIPPort(0x0A010203, 4662));
    QVERIFY(isValidSearchResultClientIPPort(1, 1)); // low ID is valid
}

void tst_SearchFile::isValidSearchResultClientIPPort_invalid()
{
    QVERIFY(!isValidSearchResultClientIPPort(0, 4662));    // zero IP
    QVERIFY(!isValidSearchResultClientIPPort(0x0A010203, 0)); // zero port
    QVERIFY(!isValidSearchResultClientIPPort(0, 0));       // both zero
}

void tst_SearchFile::serverEquality()
{
    SearchFile::SServer a{0x0A000001, 4661, 100, false};
    SearchFile::SServer b{0x0A000001, 4661, 200, true};
    SearchFile::SServer c{0x0A000002, 4661, 100, false};

    QVERIFY(a == b);   // same IP+port, different avail/udp
    QVERIFY(!(a == c)); // different IP
}

void tst_SearchFile::clientManagement()
{
    SearchFile file;

    SearchFile::SClient c1{0x0A000001, 4662, 0xC0A80001, 4661};
    SearchFile::SClient c2{0x0A000002, 4663, 0xC0A80001, 4661};

    file.addClient(c1);
    QCOMPARE(file.clients().size(), std::size_t{1});

    // Adding same client again should not duplicate
    file.addClient(c1);
    QCOMPARE(file.clients().size(), std::size_t{1});

    // Adding different client should work
    file.addClient(c2);
    QCOMPARE(file.clients().size(), std::size_t{2});
}

QTEST_MAIN(tst_SearchFile)
#include "tst_SearchFile.moc"
