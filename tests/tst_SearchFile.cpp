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
    void construct_ratingIsScaledToFive();
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
    void metaRow_recognizedByHash();
    void metaRow_tagsDisagree_invalid();
    void metaRow_unknownVersion_invalid();
    void metaRow_hashWithoutTags_stillMeta();
    void metaRow_ed2kUnaffected();
    void kadOrigin_recognizedByTag();
    void kadOrigin_prefixStrippedOnlyWithTag();
    void kadOrigin_survivesStoreAndLoad();
    void kadOrigin_ignoredOnMetaRow();
};

/// Helper: an eNode meta row — hash + name + size + FT_META_* tags.
struct MetaTags {
    int kind = 1;
    int version = 1;
    uint32 fileIndex = 0xFFFFFFFF;
    bool withTags = true;
};

static QByteArray buildMetaPacket(const uint8* hash, const QString& name, uint32 size, const MetaTags& t)
{
    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0);
    mem.writeUInt16(0);
    mem.writeUInt32(t.withTags ? 7 : 2);
    Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, size).writeNewEd2kTag(mem);
    if (t.withTags) {
        Tag(FT_META_KIND, static_cast<uint32>(t.kind)).writeNewEd2kTag(mem);
        Tag(FT_META_VERSION, static_cast<uint32>(t.version)).writeNewEd2kTag(mem);
        Tag(FT_META_FILEINDEX, t.fileIndex).writeNewEd2kTag(mem);
        Tag(FT_META_ID, QStringLiteral("bt:v1:0CEC613B424DC858488612F7571BEFAF67C52616")).writeNewEd2kTag(mem, UTF8Mode::Raw);
        Tag(FT_META_MAGNET, QStringLiteral("magnet:?xt=urn:btih:0cec613b424dc858488612f7571befaf67c52616"))
            .writeNewEd2kTag(mem, UTF8Mode::Raw);
    }
    return mem.takeBuffer();
}

/// Helper: a file eNode found on Kad — a real MD4, the classic tags and
/// FT_META_NETWORK as the only meta tag. network 0 leaves the tag out.
static QByteArray buildKadOriginPacket(const uint8* hash, const QString& name, uint32 size,
                                       uint32 sources, uint32 network = FT_META_NETWORK_KAD)
{
    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0);
    mem.writeUInt16(0);
    mem.writeUInt32(network != 0 ? 4 : 3);
    Tag(FT_FILENAME, name).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, size).writeNewEd2kTag(mem);
    Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);
    if (network != 0)
        Tag(FT_META_NETWORK, network).writeNewEd2kTag(mem);
    return mem.takeBuffer();
}

/// Whole-release bt-v1 meta hash from the shared vectors (flags 0, index 0xFFFF).
static const uint8 kBtV1WholeHash[16] = {0xED, 0x2B, 0x01, 0x10, 0xFF, 0xFF,
                                         0x1E, 0x1B, 0x36, 0x20, 0xAD, 0xE2, 0xAF, 0x9D, 0x6E, 0x90};

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

// A server reports the average rating on a 0-255 scale in the low byte.
void tst_SearchFile::construct_ratingIsScaledToFive()
{
    uint8 hash[16];
    std::memset(hash, 0xAC, 16);

    SafeMemFile mem;
    mem.write(hash, 16);
    mem.writeUInt32(0x0A010203);
    mem.writeUInt16(4662);
    mem.writeUInt32(3);
    Tag(FT_FILENAME, QStringLiteral("rated.avi")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, uint32{1000}).writeNewEd2kTag(mem);
    Tag(FT_FILERATING, uint32{0x4000 | 204}).writeNewEd2kTag(mem);   // 204 / 51 = 4
    SafeMemFile data(mem.takeBuffer());

    SearchFile file(data, true, 0xC0A80001, 4661);
    QCOMPARE(file.userRating(), uint32{4});
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

void tst_SearchFile::metaRow_recognizedByHash()
{
    QByteArray packet = buildMetaPacket(kBtV1WholeHash, QStringLiteral("[torrent] Some.Release"), 5000, {});
    SafeMemFile data(packet);
    SearchFile file(data, true, 0x0A000001, 4661);

    QVERIFY(file.isMetaResult());
    QVERIFY(!file.isInvalidMetaResult());
    QVERIFY(file.meta().isTorrent());
    QCOMPARE(file.fileName(), QStringLiteral("Some.Release"));   // legacy prefix stripped
    QVERIFY(file.meta().magnet.startsWith(QStringLiteral("magnet:?xt=urn:btih:")));
    QVERIFY(file.meta().catalogId.startsWith(QStringLiteral("bt:v1:")));

    // an operator-configured prefix goes too, a release group's bracket stays
    for (const auto& [in, out] : {std::pair{QStringLiteral("[torrent example.org] A.B"), QStringLiteral("A.B")},
                                  std::pair{QStringLiteral("[USENET] C"), QStringLiteral("C")},
                                  std::pair{QStringLiteral("[Group] D"), QStringLiteral("[Group] D")}}) {
        QByteArray p = buildMetaPacket(kBtV1WholeHash, in, 5000, {});
        SafeMemFile d(p);
        QCOMPARE(SearchFile(d, true).fileName(), out);
    }

    SearchFile copy(&file);
    QVERIFY(copy.meta().isTorrent());
}

void tst_SearchFile::metaRow_tagsDisagree_invalid()
{
    MetaTags t;
    t.kind = 3;   // tag says nzb, hash says bt-v1
    QByteArray packet = buildMetaPacket(kBtV1WholeHash, QStringLiteral("x"), 5000, t);
    SafeMemFile data(packet);
    SearchFile file(data, true);
    QVERIFY(file.isInvalidMetaResult());
    QVERIFY(!file.isMetaResult());
}

void tst_SearchFile::metaRow_unknownVersion_invalid()
{
    MetaTags t;
    t.version = 2;
    QByteArray packet = buildMetaPacket(kBtV1WholeHash, QStringLiteral("x"), 5000, t);
    SafeMemFile data(packet);
    SearchFile file(data, true);
    QVERIFY(file.isInvalidMetaResult());
}

void tst_SearchFile::metaRow_hashWithoutTags_stillMeta()
{
    // the network comes from the hash; a name prefix alone decides nothing
    MetaTags t;
    t.withTags = false;
    QByteArray packet = buildMetaPacket(kBtV1WholeHash, QStringLiteral("Plain.Name"), 5000, t);
    SafeMemFile data(packet);
    SearchFile file(data, true);
    QVERIFY(file.isMetaResult());
    QVERIFY(file.meta().isTorrent());

    uint8 md4[16];
    std::memset(md4, 0x11, 16);
    QByteArray packet2 = buildSearchResultPacket(md4, 1, 1, QStringLiteral("[usenet] not.really"), 100);
    SafeMemFile data2(packet2);
    SearchFile ed2k(data2, true);
    QVERIFY(!ed2k.isMetaResult());
    QCOMPARE(ed2k.fileName(), QStringLiteral("[usenet] not.really"));
}

void tst_SearchFile::metaRow_ed2kUnaffected()
{
    uint8 hash[16];
    std::memset(hash, 0xAB, 16);
    QByteArray packet = buildSearchResultPacket(hash, 0x0A010203, 4662, QStringLiteral("test.mp3"), 12345, 10);
    SafeMemFile data(packet);
    SearchFile file(data, true);
    QVERIFY(!file.isMetaResult());
    QVERIFY(!file.isInvalidMetaResult());
    QCOMPARE(file.sourceCount(), uint32{10});
}

void tst_SearchFile::kadOrigin_recognizedByTag()
{
    uint8 hash[16];
    std::memset(hash, 0x4B, 16);
    QByteArray packet = buildKadOriginPacket(hash, QStringLiteral("[kad] Night.Of.The.Living.Dead.avi"), 7000, 17);
    SafeMemFile data(packet);
    SearchFile file(data, true, 0x0A000001, 4661);

    // an ordinary eD2K file: real hash, not a meta row, not dropped, downloadable
    QVERIFY(file.isKadOrigin());
    QVERIFY(!file.isMetaResult());
    QVERIFY(!file.isInvalidMetaResult());
    QVERIFY(md4equ(file.fileHash(), hash));
    QCOMPARE(file.fileName(), QStringLiteral("Night.Of.The.Living.Dead.avi"));
    QCOMPARE(file.sourceCount(), uint32{17});
    // found by the server, not by our own Kad search: sources still add up per server
    QVERIFY(!file.isKadResult());

    SearchFile copy(&file);
    QVERIFY(copy.isKadOrigin());

    // a network we do not know is not Kad
    QByteArray other = buildKadOriginPacket(hash, QStringLiteral("[kad] x.avi"), 7000, 1, 9);
    SafeMemFile otherData(other);
    SearchFile unknown(otherData, true);
    QVERIFY(!unknown.isKadOrigin());
    QCOMPARE(unknown.fileName(), QStringLiteral("[kad] x.avi"));
}

void tst_SearchFile::kadOrigin_prefixStrippedOnlyWithTag()
{
    uint8 hash[16];
    std::memset(hash, 0x4C, 16);

    // with the tag: the network's bracket goes, whatever the operator put in it
    for (const auto& [in, out] : {std::pair{QStringLiteral("[kad emule-qt.org] A.B.avi"), QStringLiteral("A.B.avi")},
                                  std::pair{QStringLiteral("[KAD] C.avi"), QStringLiteral("C.avi")},
                                  std::pair{QStringLiteral("D.avi"), QStringLiteral("D.avi")},
                                  std::pair{QStringLiteral("[Group] E.avi"), QStringLiteral("[Group] E.avi")},
                                  std::pair{QStringLiteral("[kadabra] F.avi"), QStringLiteral("[kadabra] F.avi")},
                                  std::pair{QStringLiteral("[kad]"), QStringLiteral("[kad]")}}) {
        QByteArray p = buildKadOriginPacket(hash, in, 7000, 3);
        SafeMemFile d(p);
        SearchFile f(d, true);
        QVERIFY(f.isKadOrigin());
        QCOMPARE(f.fileName(), out);
    }

    // without the tag the name decides nothing: a file merely called "[kad …]" keeps it
    QByteArray plain = buildKadOriginPacket(hash, QStringLiteral("[kad emule-qt.org] A.B.avi"), 7000, 3, 0);
    SafeMemFile plainData(plain);
    SearchFile file(plainData, true);
    QVERIFY(!file.isKadOrigin());
    QCOMPARE(file.fileName(), QStringLiteral("[kad emule-qt.org] A.B.avi"));
}

void tst_SearchFile::kadOrigin_survivesStoreAndLoad()
{
    uint8 hash[16];
    std::memset(hash, 0x4D, 16);
    QByteArray packet = buildKadOriginPacket(hash, QStringLiteral("[kad emule-qt.org] stored.avi"), 7000, 5);
    SafeMemFile data(packet);
    SearchFile file(data, true);

    SafeMemFile stored;
    file.storeToFile(stored);
    stored.seek(0, 0);
    SearchFile loaded(stored, true);

    QVERIFY(loaded.isKadOrigin());
    QCOMPARE(loaded.fileName(), QStringLiteral("stored.avi"));
    QCOMPARE(loaded.sourceCount(), uint32{5});
}

void tst_SearchFile::kadOrigin_ignoredOnMetaRow()
{
    // a pseudo-hash row is a torrent or an NZB and never also a Kad file
    SafeMemFile mem;
    mem.write(kBtV1WholeHash, 16);
    mem.writeUInt32(0);
    mem.writeUInt16(0);
    mem.writeUInt32(3);
    Tag(FT_FILENAME, QStringLiteral("[torrent] Some.Release")).writeNewEd2kTag(mem, UTF8Mode::Raw);
    Tag(FT_FILESIZE, uint32{5000}).writeNewEd2kTag(mem);
    Tag(FT_META_NETWORK, uint32{FT_META_NETWORK_KAD}).writeNewEd2kTag(mem);
    QByteArray packet = mem.takeBuffer();
    SafeMemFile data(packet);
    SearchFile file(data, true);

    QVERIFY(file.isMetaResult());
    QVERIFY(!file.isKadOrigin());
}

QTEST_MAIN(tst_SearchFile)
#include "tst_SearchFile.moc"
