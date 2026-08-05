/// @file tst_DownloadQueue.cpp
/// @brief Tests for transfer/DownloadQueue — file management, lookup,
///        priority sorting, source management.

#include "TestFixtures.h"
#include "TestHelpers.h"
#include "app/AppContext.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "client/UpDownClient.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::testing;

namespace {

/// Build a server OP_FOUNDSOURCES-style body: fileHash[16] + count[1] +
/// per-source(userId[4] + port[2]). Non-obfuscated, so no crypt byte or hash.
/// Each id is written little-endian so the memcpy read in addServerSourceResult
/// recovers exactly the value passed here — pass high IDs in network order
/// (toNetworkUint32()) and low IDs as their raw ED2K value.
QByteArray makeServerSourceBody(const uint8* hash, const std::vector<uint32>& ids,
                                uint16 port = 4662)
{
    QByteArray buf(reinterpret_cast<const char*>(hash), 16);
    buf.append(static_cast<char>(ids.size()));   // source count (single byte)
    for (uint32 id : ids) {
        for (int i = 0; i < 4; ++i)
            buf.append(static_cast<char>((id >> (8 * i)) & 0xFF));
        buf.append(static_cast<char>(port & 0xFF));
        buf.append(static_cast<char>((port >> 8) & 0xFF));
    }
    return buf;
}

void feedServerSources(DownloadQueue& dq, const QByteArray& body)
{
    dq.addServerSourceResult(reinterpret_cast<const uint8*>(body.constData()),
                             static_cast<uint32>(body.size()), /*obfuscated*/ false);
}

/// Append the 2-byte OP_EDONKEYPROT, OP_GLOBFOUNDSOURCES separator that joins
/// per-file blocks in a single OP_GLOBFOUNDSOURCES datagram.
void appendGlobSeparator(QByteArray& buf)
{
    buf.append(static_cast<char>(OP_EDONKEYPROT));
    buf.append(static_cast<char>(OP_GLOBFOUNDSOURCES));
}

} // namespace

class tst_DownloadQueue : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void construction_empty();
    void addDownload_basic();
    void addDownload_paused();
    void removeFile_basic();
    void deleteAll_keepsCompletedFileOwnedByKnownList();
    void fileByID_found();
    void fileByID_notFound();
    void isFileExisting_basic();
    void sortByPriority_ordering();
    void startNextFile_resumesPaused();
    void init_scansDirectory();
    void checkAndAddSource_basic();
    void addServerSources_dropsLowIdWhenFirewalled();
    void addServerSources_dropsIpFilteredHighId();
    void addServerSources_dropsBannedHighId();
    void addServerSources_parsesIPv6Sentinel();
    void addServerSources_vetsIPv6LikeIPv4();

    // eD2K link sources
    void linkSources_ipv6LiteralAdded();
    void linkSources_ipv4AndIPv6BecomeOneClient();
    void linkSources_dropsIpFilteredV4();
    void linkSources_dropsBannedV6();
    void linkSources_respectsMaxSourcesPerFile();
    void linkSources_ipv6NotDedupedAgainstAddresslessClient();

    void udpGlobalSourcesSingleBlock();
    void udpGlobalSourcesMultiBlock();

    // #34 global-UDP-source rotation (SendNextUDPPacket port).
    void udpMaxFilesPerPacket_capsByServerCapability();
    void udpStopUDPRequests_resetsCursorAndStampsTime();
    void udpSourceRotation_terminatesOnePass();
    void udpSourceRotation_skipsDeadServers();
    void udpSourceRotation_batchesAndSplitsByPacketCap();
    void udpSourceRotation_bailsWhenCryptRequired();

    // Per-source walk in process(): the OP_CHANGE_CLIENT_IP flush and the re-ask clock.
    void process_flushesPendingIPChangeForSources();
    void process_udpReaskHonoursPerPeerReaskTime();

private:
    QTemporaryDir m_tempDir;

    PartFile* createTestPartFile(const uint8* hash, const QString& name,
                                  uint8 priority = kPrNormal);
};

namespace {

// Mirrors the production MAX_REQUESTS_PER_SERVER (DownloadQueue.cpp).
constexpr uint32 kMaxRequestsForTest = 35;

/// A ready-to-query download (status Empty ⇒ eligible for global getsources).
struct UdpTestServer {
    uint32 ip;
    uint16 port;
    uint32 udpFlags;
    uint32 failedCount;
};

/// Installs a local ServerList as the global one and a connected ServerConnect,
/// with crypt-required forced off (it would otherwise short-circuit the pass).
/// Restores every global on scope exit. The ServerConnect is flipped to
/// "connected" by the test methods themselves (they hold the friend access).
struct UdpSourceEnv {
    eMule::ServerList  list;
    eMule::ServerConnect sc{list};
    eMule::ServerList* savedSL;
    bool savedCryptReq;

    UdpSourceEnv()
        : savedSL(theApp.serverList)
        , savedCryptReq(thePrefs.cryptLayerRequired())
    {
        theApp.serverList = &list;
        thePrefs.setCryptLayerRequired(false);
    }
    ~UdpSourceEnv()
    {
        theApp.serverList = savedSL;
        thePrefs.setCryptLayerRequired(savedCryptReq);
    }
    UdpSourceEnv(const UdpSourceEnv&) = delete;
    UdpSourceEnv& operator=(const UdpSourceEnv&) = delete;

    eMule::Server* seed(const UdpTestServer& s)
    {
        auto srv = std::make_unique<eMule::Server>(s.ip, s.port);
        srv->setUDPFlags(s.udpFlags);
        srv->setFailedCount(s.failedCount);
        return list.addServer(std::move(srv));
    }
};

} // namespace

void tst_DownloadQueue::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    thePrefs.setIncomingDir(m_tempDir.path() + QStringLiteral("/incoming"));
    thePrefs.setTempDirs({m_tempDir.path() + QStringLiteral("/temp")});
    QDir().mkpath(thePrefs.incomingDir());
    QDir().mkpath(thePrefs.tempDirs().first());
}

PartFile* tst_DownloadQueue::createTestPartFile(const uint8* hash,
                                                  const QString& name,
                                                  uint8 priority)
{
    auto* pf = new PartFile;
    pf->setFileName(name);
    pf->setFileSize(PARTSIZE);
    pf->setFileHash(hash);
    pf->setAutoDownPriority(false);
    pf->setDownPriority(priority);
    return pf;
}

void tst_DownloadQueue::construction_empty()
{
    DownloadQueue dq;
    QCOMPARE(dq.fileCount(), 0);
    QCOMPARE(dq.datarate(), 0U);
    QVERIFY(dq.files().empty());
}

void tst_DownloadQueue::addDownload_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("test1.bin"));

    QSignalSpy spy(&dq, &DownloadQueue::fileAdded);

    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);
    QCOMPARE(spy.count(), 1);

    // Don't add duplicate
    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);

    dq.deleteAll();
}

void tst_DownloadQueue::addDownload_paused()
{
    DownloadQueue dq;

    uint8 hash[16] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("paused.bin"));

    dq.addDownload(pf, true);
    QCOMPARE(dq.fileCount(), 1);
    QVERIFY(pf->isPaused());

    dq.deleteAll();
}

void tst_DownloadQueue::removeFile_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("remove.bin"));

    dq.addDownload(pf);
    QCOMPARE(dq.fileCount(), 1);

    QSignalSpy spy(&dq, &DownloadQueue::fileRemoved);

    dq.removeFile(pf);
    QCOMPARE(dq.fileCount(), 0);
    QCOMPARE(spy.count(), 1);

    delete pf;
}

// Regression: on shutdown the download queue is torn down (~DownloadQueue →
// deleteAll) *before* KnownFileList::save(). A completed download is handed to
// KnownFileList (onDownloadCompleted → safeAddKFile), which owns it from then on.
// If deleteAll freed that PartFile, KnownFileList would keep a dangling pointer
// and the shutdown save would dereference it — the production SIGSEGV in
// KnownFile::writeToFile (KnownFile.cpp:393, "for (auto& tag : tags())"), plus a
// double-free in KnownFileList::clear(). deleteAll must leave any file owned by
// KnownFileList alive.
void tst_DownloadQueue::deleteAll_keepsCompletedFileOwnedByKnownList()
{
    KnownFileList kfl;
    const QString knownDir = m_tempDir.path() + QStringLiteral("/known_owner");
    QDir().mkpath(knownDir);
    kfl.init(knownDir);

    DownloadQueue dq;
    dq.setKnownFileList(&kfl);

    uint8 hash[16] = {0xC0, 0xFF, 0xEE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("completed.bin"));
    pf->setStatus(PartFileStatus::Complete);

    // Completion registers the file in BOTH the queue (kept for UI display) and
    // the known-file list (its new owner).
    dq.addDownload(pf);
    QVERIFY(kfl.safeAddKFile(pf));
    QCOMPARE(kfl.count(), size_t(1));
    QVERIFY(kfl.isFilePtrInList(pf));

    // Shutdown teardown order: queue first...
    dq.deleteAll();

    // ...then the known-file save. Before the fix, deleteAll had freed pf and this
    // save dereferenced a dangling pointer (SIGSEGV in KnownFile::writeToFile).
    QVERIFY(kfl.isFilePtrInList(pf));
    QCOMPARE(kfl.count(), size_t(1));
    kfl.save();
    QCOMPARE(pf->fileName(), QStringLiteral("completed.bin")); // pf still alive

    // KnownFileList owns pf now and frees it exactly once (no double-free).
    kfl.clear();
    QCOMPARE(kfl.count(), size_t(0));
}

void tst_DownloadQueue::fileByID_found()
{
    DownloadQueue dq;

    uint8 hash[16] = {4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* pf = createTestPartFile(hash, QStringLiteral("find.bin"));

    dq.addDownload(pf);

    PartFile* found = dq.fileByID(hash);
    QVERIFY(found != nullptr);
    QCOMPARE(found, pf);

    dq.deleteAll();
}

void tst_DownloadQueue::fileByID_notFound()
{
    DownloadQueue dq;

    uint8 hash1[16] = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    uint8 hash2[16] = {6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash1, QStringLiteral("find2.bin"));

    dq.addDownload(pf);

    PartFile* found = dq.fileByID(hash2);
    QVERIFY(found == nullptr);

    dq.deleteAll();
}

void tst_DownloadQueue::isFileExisting_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    auto* pf = createTestPartFile(hash, QStringLiteral("exists.bin"));

    dq.addDownload(pf);

    QVERIFY(dq.isFileExisting(hash));

    uint8 otherHash[16] = {8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    QVERIFY(!dq.isFileExisting(otherHash));

    dq.deleteAll();
}

void tst_DownloadQueue::sortByPriority_ordering()
{
    DownloadQueue dq;

    uint8 hash1[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    uint8 hash3[16] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};

    auto* low = createTestPartFile(hash1, QStringLiteral("low.bin"), kPrLow);
    auto* high = createTestPartFile(hash2, QStringLiteral("high.bin"), kPrHigh);
    auto* normal = createTestPartFile(hash3, QStringLiteral("normal.bin"), kPrNormal);

    // Add in wrong order
    dq.addDownload(low);
    dq.addDownload(normal);
    dq.addDownload(high);

    // After sorting, files should be ordered by priority
    // rightFileHasHigherPrio returns true when left < right priority
    const auto& files = dq.files();
    QCOMPARE(files.size(), 3U);

    // Verify high-priority file comes first (or at least higher-prio before lower)
    bool highBeforeLow = false;
    int highIdx = -1, lowIdx = -1;
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        if (files[static_cast<size_t>(i)] == high) highIdx = i;
        if (files[static_cast<size_t>(i)] == low) lowIdx = i;
    }
    highBeforeLow = (highIdx < lowIdx);
    QVERIFY(highBeforeLow);

    dq.deleteAll();
}

void tst_DownloadQueue::startNextFile_resumesPaused()
{
    DownloadQueue dq;

    uint8 hash1[16] = {20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};

    auto* pf1 = createTestPartFile(hash1, QStringLiteral("paused1.bin"), kPrLow);
    auto* pf2 = createTestPartFile(hash2, QStringLiteral("paused2.bin"), kPrHigh);

    dq.addDownload(pf1, true);
    dq.addDownload(pf2, true);

    QVERIFY(pf1->isPaused());
    QVERIFY(pf2->isPaused());

    // Start next should resume the highest priority paused file
    dq.startNextFile();

    // At least one should be resumed
    QVERIFY(!pf1->isPaused() || !pf2->isPaused());

    dq.deleteAll();
}

void tst_DownloadQueue::init_scansDirectory()
{
    // Create a temp dir with a .part.met file
    const QString tempDir = m_tempDir.path() + QStringLiteral("/scan_test");
    QDir().mkpath(tempDir);

    // Create a PartFile and save it
    PartFile pf;
    pf.setFileName(QStringLiteral("scan_test.bin"));
    pf.setFileSize(10000);

    uint8 hash[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pf.setFileHash(hash);

    QVERIFY(pf.createPartFile(tempDir));

    const QString metFileName = pf.partMetFileName();
    QVERIFY(QFile::exists(tempDir + QDir::separator() + metFileName));

    // Now create a DownloadQueue and init from the directory
    DownloadQueue dq;
    dq.init({tempDir});

    QCOMPARE(dq.fileCount(), 1);
    QVERIFY(dq.fileByID(hash) != nullptr);

    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_basic()
{
    DownloadQueue dq;

    uint8 hash[16] = {30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("source_test.bin"));

    dq.addDownload(pf);

    UpDownClient client;
    client.setUserAddress(Address::fromNetworkOrder(0x01020304));
    client.setUserPort(4662);

    bool added = dq.checkAndAddSource(pf, &client);
    QVERIFY(added);
    QCOMPARE(pf->sourceCount(), 1);

    // Adding same source again should fail
    bool duplicate = dq.checkAndAddSource(pf, &client);
    QVERIFY(!duplicate);
    QCOMPARE(pf->sourceCount(), 1);

    dq.deleteAll();
}

// A server OP_FOUNDSOURCES answer may include low-ID sources. While we are
// firewalled, two firewalled peers can never accept each other's connection, so
// those must be dropped — matching CPartFile::AddSources / CanAddSource. This
// mirrors tst_SourceExchange::parse_dropsLowIdSourcesWhenFirewalled for the
// server path.
void tst_DownloadQueue::addServerSources_dropsLowIdWhenFirewalled()
{
    // With no server connection and no Kad, theApp.isFirewalled() is true.
    QVERIFY(theApp.isFirewalled());

    DownloadQueue dq;

    uint8 hash[16] = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("server_source_test.bin"));
    dq.addDownload(pf);

    const uint32 highIdNet =
        Address::fromString(QStringLiteral("77.66.55.44")).toNetworkUint32();

    // One low-ID source (small raw ED2K id) and one high-ID source.
    feedServerSources(dq, makeServerSourceBody(hash, {0x00000123u, highIdNet}));

    // Only the high-ID source survives; the low-ID one is dropped while firewalled.
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("77.66.55.44")).toUint32());

    dq.deleteAll();
}

// S3a: a classic OP_FOUNDSOURCES block may carry an inline IPv6 source, marked by the
// ClientID sentinel 0xFFFFFFFF followed by 16 IPv6 bytes. The v6 source must be parsed
// (with openIPv6 set and NOT dropped for being firewalled), AND a following normal source
// must still parse — proving the 16 sentinel bytes were consumed and the list stays in sync.
void tst_DownloadQueue::addServerSources_parsesIPv6Sentinel()
{
    QVERIFY(theApp.isFirewalled());

    DownloadQueue dq;
    uint8 hash[16] = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("server_source_ipv6.bin"));
    dq.addDownload(pf);

    // 2a01:4f8::1122:3333 — genuine global unicast. Server sources are vetted with
    // isGoodIP like every other family, so the documentation prefix 2001:db8::/32 would
    // be dropped before the parse could be observed (same reason as the link tests below).
    const std::array<uint8, 16> v6bytes{
        0x2a, 0x01, 0x04, 0xf8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33};
    const uint32 highIdNet =
        Address::fromString(QStringLiteral("88.77.66.55")).toNetworkUint32();

    // Body: hash[16], count=2, [sentinel source: 0xFFFFFFFF + port + 16 IPv6 bytes],
    // then [normal high-ID source]. Sentinel FIRST so a mis-count desyncs the second.
    QByteArray body(reinterpret_cast<const char*>(hash), 16);
    body.append(static_cast<char>(2));                       // count
    const uint16 port = 4662;
    auto appendU32 = [&](uint32 v) { for (int i = 0; i < 4; ++i) body.append(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    auto appendU16 = [&](uint16 v) { body.append(static_cast<char>(v & 0xFF)); body.append(static_cast<char>((v >> 8) & 0xFF)); };
    appendU32(IPV6_SOURCE_SENTINEL);                         // sentinel ClientID
    appendU16(port);
    body.append(reinterpret_cast<const char*>(v6bytes.data()), 16);  // inline IPv6
    appendU32(highIdNet);                                    // normal high-ID source
    appendU16(port);

    feedServerSources(dq, body);

    // Both sources parsed: no desync from the sentinel's 16 bytes.
    QCOMPARE(pf->sourceCount(), 2);

    bool foundIPv6 = false, foundHighID = false;
    for (const UpDownClient* src : pf->srcList()) {
        if (src->openIPv6()) {
            foundIPv6 = true;
            QCOMPARE(src->userIPv6(), Address::fromIPv6Bytes(v6bytes.data()));
        } else if (src->userIDHybrid() == Address::fromString(QStringLiteral("88.77.66.55")).toUint32()) {
            foundHighID = true;
        }
    }
    QVERIFY2(foundIPv6, "IPv6 sentinel source not parsed");
    QVERIFY2(foundHighID, "high-ID source after the sentinel desynced");

    dq.deleteAll();
}

// A server-supplied IPv6 source gets the same vetting as its IPv4 twin. The IP filter
// itself runs downstream in checkAndAddSource (makeSourceClient points userAddress at
// the IPv6 when there is no usable IPv4), but isGoodIP and the ban list have no such
// coverage and must be applied inline — they used to be skipped outright.
void tst_DownloadQueue::addServerSources_vetsIPv6LikeIPv4()
{
    const std::array<uint8, 16> bogonV6{      // 2001:db8::1 — documentation space
        0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const std::array<uint8, 16> bannedV6{     // 2a01:4f8::dead
        0x2a, 0x01, 0x04, 0xf8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0xad};
    const std::array<uint8, 16> filteredV6{   // 2a02:26f0::beef
        0x2a, 0x02, 0x26, 0xf0, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xef};
    const std::array<uint8, 16> cleanV6{      // 2a03:2880::1
        0x2a, 0x03, 0x28, 0x80, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    DownloadQueue dq;

    ClientList cl;
    cl.addBannedClient(Address::fromIPv6Bytes(bannedV6.data()));
    dq.setClientList(&cl);

    IPFilter ipf;
    ipf.addIPRange6(filteredV6, filteredV6, 0, "test-block-v6");
    ipf.sortAndMerge();
    dq.setIPFilter(&ipf);

    uint8 hash[16] = {43, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    auto* pf = createTestPartFile(hash, QStringLiteral("server_source_ipv6_vet.bin"));
    dq.addDownload(pf);

    QByteArray body(reinterpret_cast<const char*>(hash), 16);
    body.append(static_cast<char>(4));                       // count
    for (const auto* v6 : {&bogonV6, &bannedV6, &filteredV6, &cleanV6}) {
        for (int i = 0; i < 4; ++i)
            body.append(static_cast<char>((IPV6_SOURCE_SENTINEL >> (8 * i)) & 0xFF));
        body.append(static_cast<char>(4662 & 0xFF));
        body.append(static_cast<char>((4662 >> 8) & 0xFF));
        body.append(reinterpret_cast<const char*>(v6->data()), 16);
    }

    feedServerSources(dq, body);

    // Only the clean global-unicast source survives all three gates.
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIPv6(), Address::fromIPv6Bytes(cleanV6.data()));

    dq.deleteAll();
}

// An ipfiltered high-ID server source must be dropped. checkAndAddSource's own
// ipfilter is skipped for server sources (they carry no userAddress), so the
// drop has to happen inline in addServerSourceResult — mirroring CPartFile::
// AddSources (MFC PartFile.cpp:2485).
void tst_DownloadQueue::addServerSources_dropsIpFilteredHighId()
{
    DownloadQueue dq;

    IPFilter ipf;
    // addIPRange takes host byte order; block just the one address.
    const uint32 filteredHost =
        Address::fromString(QStringLiteral("77.66.55.44")).toUint32();
    ipf.addIPRange(filteredHost, filteredHost, 0, "test-block");
    dq.setIPFilter(&ipf);

    uint8 hash[16] = {41, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("server_ipfilter_test.bin"));
    dq.addDownload(pf);

    const uint32 filteredNet =
        Address::fromString(QStringLiteral("77.66.55.44")).toNetworkUint32();
    const uint32 cleanNet =
        Address::fromString(QStringLiteral("88.77.66.55")).toNetworkUint32();

    // One filtered high-ID and one clean high-ID.
    feedServerSources(dq, makeServerSourceBody(hash, {filteredNet, cleanNet}));

    // Only the clean high-ID survives; the filtered one is dropped.
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("88.77.66.55")).toUint32());

    dq.deleteAll();
}

// A banned high-ID server source must be dropped. checkAndAddSource has no ban
// check, so this happens inline in addServerSourceResult — mirroring CPartFile::
// AddSources (MFC PartFile.cpp:2490).
void tst_DownloadQueue::addServerSources_dropsBannedHighId()
{
    DownloadQueue dq;

    ClientList cl;
    cl.addBannedClient(Address::fromString(QStringLiteral("77.66.55.44")));
    dq.setClientList(&cl);

    uint8 hash[16] = {42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* pf = createTestPartFile(hash, QStringLiteral("server_ban_test.bin"));
    dq.addDownload(pf);

    const uint32 bannedNet =
        Address::fromString(QStringLiteral("77.66.55.44")).toNetworkUint32();
    const uint32 cleanNet =
        Address::fromString(QStringLiteral("88.77.66.55")).toNetworkUint32();

    // One banned high-ID and one clean high-ID.
    feedServerSources(dq, makeServerSourceBody(hash, {bannedNet, cleanNet}));

    // Only the clean high-ID survives; the banned one is dropped.
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("88.77.66.55")).toUint32());

    dq.deleteAll();
}

// ---------------------------------------------------------------------------
// eD2K link sources
//
// Link text is untrusted, so addLinkSources() vets each family independently. Only
// literals are exercised here — a hostname would need DNS, which HostResolver covers.
// 2a01:4f8::1 is genuine global unicast (2001:db8::/32 fails isGoodIP as documentation
// space, and 88.77.66.55 is a routable IPv4).
// ---------------------------------------------------------------------------

void tst_DownloadQueue::linkSources_ipv6LiteralAdded()
{
    // We are firewalled here (the harness default), which must NOT stop an IPv6 source:
    // it is dialable over v6 no matter what our ED2K ID is.
    QVERIFY(theApp.isFirewalled());

    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_v6.bin"));
    dq.addDownload(pf);

    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    dq.addLinkSources(pf, {{QStringLiteral("2a01:4f8::1"), 4662, v6, {}}});

    QCOMPARE(pf->sourceCount(), 1);
    const UpDownClient* src = pf->srcList().front();
    QVERIFY(src->openIPv6());
    QCOMPARE(src->userIPv6(), v6);
    QCOMPARE(src->userAddress(), v6);       // dialed directly over IPv6
    QCOMPARE(src->sourceFrom(), SourceFrom::Link);

    dq.deleteAll();
}

void tst_DownloadQueue::linkSources_ipv4AndIPv6BecomeOneClient()
{
    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_dual.bin"));
    dq.addDownload(pf);

    const Address v4 = Address::fromString(QStringLiteral("88.77.66.55"));
    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    dq.addLinkPeerSource(pf, v4, v6, 4662);

    // One peer, one client — both families attached, so tryToConnect() can choose.
    QCOMPARE(pf->sourceCount(), 1);
    const UpDownClient* src = pf->srcList().front();
    QVERIFY(src->openIPv6());
    QCOMPARE(src->userIPv6(), v6);
    QCOMPARE(src->userIDHybrid(), v4.toUint32());   // HighID from the IPv4

    dq.deleteAll();
}

void tst_DownloadQueue::linkSources_dropsIpFilteredV4()
{
    DownloadQueue dq;

    IPFilter filter;
    const uint32 filteredHost = Address::fromString(QStringLiteral("88.77.66.55")).toUint32();
    filter.addIPRange(filteredHost, filteredHost, 0, "test-filter");
    dq.setIPFilter(&filter);

    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_filtered.bin"));
    dq.addDownload(pf);

    const Address v4 = Address::fromString(QStringLiteral("88.77.66.55"));
    dq.addLinkSources(pf, {{QStringLiteral("88.77.66.55"), 4662, v4, {}}});
    QCOMPARE(pf->sourceCount(), 0);

    // The same peer with a usable IPv6 alongside is still worth keeping.
    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    dq.addLinkPeerSource(pf, v4, v6, 4662);
    QCOMPARE(pf->sourceCount(), 1);
    QVERIFY(pf->srcList().front()->openIPv6());

    dq.deleteAll();
}

void tst_DownloadQueue::linkSources_dropsBannedV6()
{
    DownloadQueue dq;

    ClientList cl;
    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    cl.addBannedClient(v6);
    dq.setClientList(&cl);

    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_banned.bin"));
    dq.addDownload(pf);

    dq.addLinkSources(pf, {{QStringLiteral("2a01:4f8::1"), 4662, v6, {}}});
    QCOMPARE(pf->sourceCount(), 0);

    dq.deleteAll();
}

void tst_DownloadQueue::linkSources_respectsMaxSourcesPerFile()
{
    const uint32 savedMax = thePrefs.maxSourcesPerFile();
    thePrefs.setMaxSourcesPerFile(2);

    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_cap.bin"));
    dq.addDownload(pf);

    std::vector<ED2KLinkSource> sources;
    for (int i = 1; i <= 5; ++i) {
        const QString literal = QStringLiteral("2a01:4f8::%1").arg(i);
        sources.push_back({literal, uint16{4662}, Address::fromString(literal), {}});
    }
    dq.addLinkSources(pf, sources);

    QCOMPARE(pf->sourceCount(), 2);

    dq.deleteAll();
    thePrefs.setMaxSourcesPerFile(savedMax);
}

void tst_DownloadQueue::linkSources_ipv6NotDedupedAgainstAddresslessClient()
{
    // Regression: checkAndAddSource() looked the source up with
    // findByIP(userAddress().toNetworkUint32(), port), which is 0 for every IPv6
    // address — matching any client with no user address (every server-supplied source)
    // on the same port, so the IPv6 source was rejected as a bogus duplicate.
    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);

    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash, QStringLiteral("link_dedup.bin"));
    dq.addDownload(pf);

    // A LowID client with no user address on port 4662, as a server source would be.
    auto* addressless = new UpDownClient(4662, /*userId=*/5, 0, 0, pf, true);
    QVERIFY(addressless->userAddress().isNull());
    cl.addClient(addressless);

    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    dq.addLinkSources(pf, {{QStringLiteral("2a01:4f8::1"), 4662, v6, {}}});

    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIPv6(), v6);

    dq.deleteAll();
}

// A UDP OP_GLOBFOUNDSOURCES datagram carrying one file's block must land its
// sources on the matching download — the receive path that was previously dead
// (globalFoundSources connected to nothing).
void tst_DownloadQueue::udpGlobalSourcesSingleBlock()
{
    DownloadQueue dq;

    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_single.bin"));
    dq.addDownload(pf);

    const uint32 highIdNet =
        Address::fromString(QStringLiteral("88.77.66.55")).toNetworkUint32();

    const QByteArray body = makeServerSourceBody(hash, {highIdNet});

    // The answering server is unknown to this fixture, so attribution falls back
    // to the sender endpoint — irrelevant for a high-ID source.
    const Endpoint from(Address::fromString(QStringLiteral("1.2.3.4")), 4665);
    dq.addUDPGlobalSources(reinterpret_cast<const uint8*>(body.constData()),
                           static_cast<uint32>(body.size()), from);

    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("88.77.66.55")).toUint32());

    dq.deleteAll();
}

// A datagram may pack several files' blocks separated by OP_EDONKEYPROT,
// OP_GLOBFOUNDSOURCES. A block for a file we don't have must be skipped by its
// source count without corrupting the block that follows it.
void tst_DownloadQueue::udpGlobalSourcesMultiBlock()
{
    DownloadQueue dq;

    uint8 hash1[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    uint8 unknown[16] = {0xEE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9};

    auto* pf1 = createTestPartFile(hash1, QStringLiteral("udp_multi1.bin"));
    auto* pf2 = createTestPartFile(hash2, QStringLiteral("udp_multi2.bin"));
    dq.addDownload(pf1);
    dq.addDownload(pf2);

    const uint32 ip1 = Address::fromString(QStringLiteral("11.11.11.11")).toNetworkUint32();
    const uint32 ipX = Address::fromString(QStringLiteral("22.22.22.22")).toNetworkUint32();
    const uint32 ip2 = Address::fromString(QStringLiteral("33.33.33.33")).toNetworkUint32();

    // block(hash1) | sep | block(UNKNOWN, 2 sources) | sep | block(hash2)
    QByteArray body = makeServerSourceBody(hash1, {ip1});
    appendGlobSeparator(body);
    body.append(makeServerSourceBody(unknown, {ipX, ipX}));
    appendGlobSeparator(body);
    body.append(makeServerSourceBody(hash2, {ip2}));

    const Endpoint from(Address::fromString(QStringLiteral("1.2.3.4")), 4665);
    dq.addUDPGlobalSources(reinterpret_cast<const uint8*>(body.constData()),
                           static_cast<uint32>(body.size()), from);

    // Both known files got their source; the unknown middle block was skipped
    // cleanly (had it mis-parsed, hash2's block would have been lost).
    QCOMPARE(pf1->sourceCount(), 1);
    QCOMPARE(pf2->sourceCount(), 1);
    QCOMPARE(pf1->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("11.11.11.11")).toUint32());
    QCOMPARE(pf2->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("33.33.33.33")).toUint32());

    dq.deleteAll();
}

// ---------------------------------------------------------------------------
// #34 — global UDP source rotation (port of CDownloadQueue::SendNextUDPPacket)
// ---------------------------------------------------------------------------

// The per-packet batch cap: old servers take one hash per datagram; extended
// servers batch until 35 files or MAX_UDP_PACKET_DATA (510) bytes, sized by the
// GETSOURCES1 (16B/file) vs GETSOURCES2 (20B/file) layout.
void tst_DownloadQueue::udpMaxFilesPerPacket_capsByServerCapability()
{
    UdpSourceEnv env;
    DownloadQueue dq;

    // No extended-getsources support ⇒ exactly one file per datagram.
    Server* plain = env.seed({0x0A000001u, 4661, /*udpFlags*/ 0u, 0u});
    dq.m_curUdpServer = plain;
    dq.m_requestsSentToServer = 0;
    QVERIFY(!dq.isMaxFilesPerUDPServerPacketReached(0, 0));   // room for the first
    QVERIFY(dq.isMaxFilesPerUDPServerPacketReached(1, 0));    // ...then full

    // GETSOURCES1 server (16 bytes/file): fills at 32 files (512 ≥ 510).
    Server* g1 = env.seed({0x0A000002u, 4661, SrvUdpFlag::ExtGetSources, 0u});
    dq.m_curUdpServer = g1;
    dq.m_requestsSentToServer = 0;
    QVERIFY(!dq.isMaxFilesPerUDPServerPacketReached(31, 0));  // 496 < 510
    QVERIFY(dq.isMaxFilesPerUDPServerPacketReached(32, 0));   // 512 ≥ 510
    // ...or at 35 requests regardless of byte count.
    dq.m_requestsSentToServer = kMaxRequestsForTest;
    QVERIFY(dq.isMaxFilesPerUDPServerPacketReached(1, 0));

    // GETSOURCES2 server (20 bytes/file): fills at 26 files (520 ≥ 510).
    Server* g2 = env.seed({0x0A000003u, 4661,
                           SrvUdpFlag::ExtGetSources | SrvUdpFlag::ExtGetSources2, 0u});
    dq.m_curUdpServer = g2;
    dq.m_requestsSentToServer = 0;
    QVERIFY(!dq.isMaxFilesPerUDPServerPacketReached(25, 0));  // 500 < 510
    QVERIFY(dq.isMaxFilesPerUDPServerPacketReached(26, 0));   // 520 ≥ 510
}

void tst_DownloadQueue::udpStopUDPRequests_resetsCursorAndStampsTime()
{
    UdpSourceEnv env;
    DownloadQueue dq;
    dq.m_curUdpServer = env.seed({0x0A000001u, 4661, SrvUdpFlag::ExtGetSources, 0u});
    dq.m_searchedServers = 5;
    dq.m_requestsSentToServer = 7;
    dq.m_lastUdpSearchTime = 0;

    dq.stopUDPRequests();

    QCOMPARE(dq.m_curUdpServer, nullptr);
    QCOMPARE(dq.m_lastUdpFile, nullptr);
    QCOMPARE(dq.m_searchedServers, 0u);
    QCOMPARE(dq.m_requestsSentToServer, 0u);
    QVERIFY(dq.m_lastUdpSearchTime != 0);   // stamped with getTickCount()
}

// One pass walks the whole server list ONCE and stops — the core #34 fix. Each
// call queries exactly one server; the final call both queries the last server
// and terminates (returns false). A wrapping rotation would never terminate —
// the loop guard turns that regression into a failed count assertion.
void tst_DownloadQueue::udpSourceRotation_terminatesOnePass()
{
    UdpSourceEnv env;
    env.seed({0x0A000001u, 4661, SrvUdpFlag::ExtGetSources, 0u});
    env.seed({0x0A000002u, 4661, SrvUdpFlag::ExtGetSources, 0u});
    env.seed({0x0A000003u, 4661, SrvUdpFlag::ExtGetSources, 0u});

    DownloadQueue dq;
    dq.setServerConnect(&env.sc);
    env.sc.m_connected = true;

    uint8 hash[16] = {60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_rot.bin"));
    pf->setStatus(PartFileStatus::Empty);
    dq.addDownload(pf);

    int calls = 0;
    bool more = true;
    while (more && calls <= 32) { more = dq.sendNextUDPPacket(); ++calls; }

    QCOMPARE(calls, 3);                       // one pass over 3 servers, no wrap
    QCOMPARE(dq.m_curUdpServer, nullptr);     // cursor cleared at the tail
    QVERIFY(dq.m_lastUdpSearchTime != 0);     // idle timer stamped (process() gates on it)

    dq.deleteAll();
}

// The connected server is skipped (queried over TCP) and dead servers
// (failedCount ≥ deadServerRetries) are skipped — exercised here via the dead
// path, which shares the exact skip loop.
void tst_DownloadQueue::udpSourceRotation_skipsDeadServers()
{
    UdpSourceEnv env;
    const uint32 dead = thePrefs.deadServerRetries();               // 20 by default
    env.seed({0x0A000001u, 4661, SrvUdpFlag::ExtGetSources, 0u});    // alive
    env.seed({0x0A000002u, 4661, SrvUdpFlag::ExtGetSources, dead});  // dead → skipped
    env.seed({0x0A000003u, 4661, SrvUdpFlag::ExtGetSources, 0u});    // alive

    DownloadQueue dq;
    dq.setServerConnect(&env.sc);
    env.sc.m_connected = true;

    uint8 hash[16] = {61, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_dead.bin"));
    pf->setStatus(PartFileStatus::Empty);
    dq.addDownload(pf);

    int calls = 0;
    bool more = true;
    while (more && calls <= 32) { more = dq.sendNextUDPPacket(); ++calls; }

    QCOMPARE(calls, 2);                        // only the two live servers queried
    QCOMPARE(dq.m_curUdpServer, nullptr);

    dq.deleteAll();
}

// Several files to one extended server go out batched, and the 510-byte packet
// cap splits them across datagrams — the old code sent one datagram PER FILE.
void tst_DownloadQueue::udpSourceRotation_batchesAndSplitsByPacketCap()
{
    UdpSourceEnv env;
    env.seed({0x0A000001u, 4661, SrvUdpFlag::ExtGetSources, 0u});   // GETSOURCES1, 16B/file

    DownloadQueue dq;
    dq.setServerConnect(&env.sc);
    env.sc.m_connected = true;

    // 40 files ⇒ two datagrams to the one server: 32 (cap at 512 ≥ 510) + 8.
    for (int i = 0; i < 40; ++i) {
        uint8 hash[16] = {62, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                          static_cast<uint8>(i)};
        auto* pf = createTestPartFile(hash, QStringLiteral("udp_batch_%1.bin").arg(i));
        pf->setStatus(PartFileStatus::Empty);
        dq.addDownload(pf);
    }

    int calls = 0;
    bool more = true;
    while (more && calls <= 100) { more = dq.sendNextUDPPacket(); ++calls; }

    QCOMPARE(calls, 2);                        // 40 files batched into 2 datagrams
    QCOMPARE(dq.m_curUdpServer, nullptr);

    dq.deleteAll();
}

// Global getsources returns sources without a user hash, unusable when the crypt
// layer is required — the pass must not start at all.
void tst_DownloadQueue::udpSourceRotation_bailsWhenCryptRequired()
{
    UdpSourceEnv env;
    env.seed({0x0A000001u, 4661, SrvUdpFlag::ExtGetSources, 0u});

    DownloadQueue dq;
    dq.setServerConnect(&env.sc);
    env.sc.m_connected = true;

    uint8 hash[16] = {63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_cryptreq.bin"));
    pf->setStatus(PartFileStatus::Empty);
    dq.addDownload(pf);

    thePrefs.setCryptLayerRequired(true);      // env restores it on scope exit
    QVERIFY(!dq.sendNextUDPPacket());
    QCOMPARE(dq.m_curUdpServer, nullptr);      // never started a pass

    dq.deleteAll();
}

// ---------------------------------------------------------------------------
// The per-source walk in process()
//
// MFC has no queue-wide re-ask window: CDownloadQueue::Process() stamps only the two
// *server* UDP timers, and the file re-ask clock is per (client, file) —
// SetLastAskedTime() / GetTimeUntilReask(), DownloadClient.cpp:1882. These two pin the
// walk that replaced it: it flushes a queued IP change for every source, and it asks a
// source for a re-ask only once that source's own clock has run out.
// ---------------------------------------------------------------------------

/// Run enough process() passes to reach the m_udCounter == 0 sub-tick (~1 s of ticks).
static void runOneSecondOfTicks(DownloadQueue& dq)
{
    for (int i = 0; i < 10; ++i)
        dq.process();
}

void tst_DownloadQueue::process_flushesPendingIPChangeForSources()
{
    IPv6AdvertiseGuard guard;

    DownloadQueue dq;
    uint8 hash[16];
    std::memset(hash, 0x51, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("ipchange_flush.bin"));
    dq.addDownload(pf);

    QTcpServer server;
    UpDownClient source;
    QTcpSocket* peer = wireLoopbackSocket(server, source);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(source, 0x31);
    // Deliberately UDP-incapable: the flush sits *above* the supportsUDP() gate, so a
    // refactor that folded it inside would fail here.
    QVERIFY(!source.supportsUDP());
    // OnQueue with a live socket, so PartFile::process() does not also try to dial it and
    // put unrelated frames on the wire.
    source.setDownloadState(DownloadState::OnQueue);
    pf->addSource(&source);

    QCoreApplication::processEvents();
    peer->readAll();                          // drain anything the setup produced
    source.markSendIPPending();

    runOneSecondOfTicks(dq);

    QVERIFY2(waitForBytes(peer, 22), "a source on a Ready/Empty file must be flushed");
    const QByteArray raw = peer->readAll();
    QCOMPARE(static_cast<uint8>(raw[0]), static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(static_cast<uint8>(raw[5]), static_cast<uint8>(OP_CHANGE_CLIENT_IP));
    QVERIFY(!source.sendIPPending());

    pf->srcList().clear();
    source.setSocket(nullptr);
    peer->close();
    QCoreApplication::processEvents();
    dq.deleteAll();
}

void tst_DownloadQueue::process_udpReaskHonoursPerPeerReaskTime()
{
    DownloadQueue dq;
    uint8 hash[16];
    std::memset(hash, 0x52, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("reask_clock.bin"));
    dq.addDownload(pf);

    UpDownClient source;
    source.setUserIDHybrid(0x0A141E28u);      // HighID → the direct re-ask branch
    source.setUserAddress(Address::fromString(QStringLiteral("10.20.30.40")));
    source.setUserPort(4662);
    uint8 userHash[16];
    std::memset(userHash, 0x32, sizeof(userHash));
    source.setUserHash(userHash);
    feedMuleInfoUDP(source, 4672);
    QVERIFY(source.supportsUDP());
    source.setReqFile(pf);
    pf->addSource(&source);

    // Never asked before, so its clock reads zero and the first pass re-asks it. That is
    // MFC's GetTimeUntilReask() contract for an unknown (client, file) pair.
    QCOMPARE(source.timeUntilReask(pf), 0u);
    runOneSecondOfTicks(dq);
    QVERIFY2(source.reaskPending(), "a source that has never been asked must be re-asked");

    // The answer stamps the clock — MFC UDPReaskACK(), DownloadClient.cpp:1310 — and the
    // source then goes quiet for FILEREASKTIME instead of being re-asked every pass.
    source.udpReaskACK(0);
    QVERIFY(!source.reaskPending());
    QVERIFY(source.timeUntilReask(pf) > 0);

    runOneSecondOfTicks(dq);
    QVERIFY2(!source.reaskPending(),
             "an answered source must not be re-asked again inside FILEREASKTIME");

    pf->srcList().clear();
    dq.deleteAll();
}

QTEST_GUILESS_MAIN(tst_DownloadQueue)
#include "tst_DownloadQueue.moc"
