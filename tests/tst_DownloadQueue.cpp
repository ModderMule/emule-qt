/// @file tst_DownloadQueue.cpp
/// @brief Tests for transfer/DownloadQueue — file management, lookup,
///        priority sorting, source management.

#include "TestFixtures.h"
#include "TestHelpers.h"
#include "utils/TimeUtils.h"
#include "app/AppContext.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "client/UpDownClient.h"
#include "enodemeta/MetaHash.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "search/SearchFile.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>

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
    void addDownloadFromED2KLink_refusesMetaHash();
    void addDownloadFromED2KLink_emptyTempDirUsesDefault();
    void removeFile_basic();
    void deleteAll_keepsCompletedFileOwnedByKnownList();
    void autoClear_removesACompletedFileWhenEnabled();
    void fileByID_found();
    void fileByID_notFound();
    void fileByKadFileSearchID_found();
    void fileByKadFileSearchID_ignoresZero();
    void isFileExisting_basic();
    void sortByPriority_ordering();
    void startNextFile_resumesPaused();
    void startNextFile_prefersTheSameCategory();
    void autoCategory_assignsByPattern();
    void remapCategories_afterRemoval();
    void remapCategories_afterReorder();
    void remapCategories_afterCombinedEdit();
    void init_scansDirectory();
    void checkAndAddSource_basic();
    void checkAndAddSource_rejectsUnusableHighIdOnly();
    void checkAndAddSource_rejectsOwnUserHash();
    void checkAndAddSource_rejectsCryptIncompatible();
    void checkAndAddSource_rejectsSourceForAStoppedDownload();
    void checkAndAddSource_dedupsPreHelloHighIdByUserId();
    void checkAndAddSource_dedupsAfterHelloAgainstHashlessServerSource();
    void checkAndAddSource_a4afForSourceOfAnotherFile();
    void a4af_destroyedClientLeavesNoDanglingEntry();
    void a4af_removeSourceUnlinksBothSides();
    void disconnect_failedSourceLeavesTheFile_data();
    void disconnect_failedSourceLeavesTheFile();
    void disconnect_removedSourceIsReaped();
    void tryToConnect_precheckExitDropsTheSource_data();
    void tryToConnect_precheckExitDropsTheSource();
    void disconnect_proxyFailureKeepsTheSource();
    void fileNotFound_removesAndDeadListsForThatFileOnly();
    void fileNotFound_swapsToAnotherWantedFile();
    void fileNotFound_forAFileWeDoNotDownloadChangesNothing();
    void udpFileNotFound_ignoredWhileDownloading();
    void deadSource_ipv6SourcesAreToldApartByAddress();
    void checkAndAddSource_adoptsKnownClient();
    void checkAndAddSource_ipv6Dedup();
    void checkAndAddKnownSource_addsAPassiveSource();
    void checkAndAddKnownSource_a4afWhenItAlreadySourcesAnotherFile();
    void addServerSources_dropsLowIdWhenFirewalled();
    void addServerSources_dropsIpFilteredHighId();
    void addServerSources_dropsBannedHighId();
    void seedFromSearchResult_seedsParentAndChildClients();
    void seedFromSearchResult_seedsAICH();
    void addServerSources_parsesIPv6Sentinel();
    void addServerSources_vetsIPv6LikeIPv4();
    void addKadSources_type6KeepsDirectCallback();
    void addKadSources_type6DroppedWithoutTheBit();
    void addKadSources_buddyIpIsNetworkOrder();

    // eD2K link sources
    void linkSources_ipv6LiteralAdded();
    void linkSources_ipv4AndIPv6BecomeOneClient();
    void linkSources_dropsIpFilteredV4();
    void linkSources_dropsBannedV6();
    void linkSources_respectsMaxSourcesPerFile();
    void linkSources_ipv6NotDedupedAgainstAddresslessClient();

    void udpGlobalSourcesSingleBlock();
    void udpGlobalSourcesMultiBlock();
    void udpGlobalSources_onlyFromAServerWeAsked();

    // #34 global-UDP-source rotation (SendNextUDPPacket port).
    void udpMaxFilesPerPacket_capsByServerCapability();
    void udpStopUDPRequests_resetsCursorAndStampsTime();
    void udpSourceRotation_terminatesOnePass();
    void udpSourceRotation_skipsDeadServers();
    void udpSourceRotation_batchesAndSplitsByPacketCap();
    void udpSourceRotation_bailsWhenCryptRequired();

    // Per-source walk in process(): the OP_CHANGE_CLIENT_IP flush and the re-ask clock.
    void process_flushesPendingIPChangeForSources();
    void process_udpReaskWindowIsDisjointFromTheTcpReask();

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

namespace {

QString fileLinkFor(const uint8* hash, const QString& name)
{
    return QStringLiteral("ed2k://|file|%1|1000|%2|/").arg(name, md4str(hash));
}

int partMetCount(const QString& dir)
{
    return static_cast<int>(QDir(dir).entryList({QStringLiteral("*.part.met")}, QDir::Files).size());
}

} // namespace

void tst_DownloadQueue::addDownloadFromED2KLink_refusesMetaHash()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DownloadQueue dq;

    // a torrent row and a Usenet row, as eNode mints them
    const auto torrent = enodemeta::build(enodemeta::Kind::BtV1, 0, 0, QByteArray(20, '\x11'));
    const auto nzb = enodemeta::build(enodemeta::Kind::Nzb, 0, 0, QByteArray(32, '\x22'));
    QVERIFY(torrent && nzb);

    QVERIFY(!dq.addDownloadFromED2KLink(fileLinkFor(torrent->data(), QStringLiteral("t.mkv")), temp.path()));
    QVERIFY(!dq.addDownloadFromED2KLink(fileLinkFor(nzb->data(), QStringLiteral("u.mkv")), temp.path()));
    QCOMPARE(dq.fileCount(), 0);
    QCOMPARE(partMetCount(temp.path()), 0);

    // control: an ordinary MD4 link through the same door is queued
    const uint8 md4[16] = {0x4D, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    QVERIFY(dq.addDownloadFromED2KLink(fileLinkFor(md4, QStringLiteral("plain.bin")), temp.path()));
    QCOMPARE(dq.fileCount(), 1);
    QCOMPARE(partMetCount(temp.path()), 1);

    dq.deleteAll();
}

void tst_DownloadQueue::addDownloadFromED2KLink_emptyTempDirUsesDefault()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QStringList saved = thePrefs.tempDirs();
    const auto restore = qScopeGuard([&saved] { thePrefs.setTempDirs(saved); });
    thePrefs.setTempDirs({temp.path()});
    QCOMPARE(DownloadQueue::defaultTempDir(), temp.path());

    DownloadQueue dq;
    const uint8 md4[16] = {0x4E, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    QVERIFY(dq.addDownloadFromED2KLink(fileLinkFor(md4, QStringLiteral("default.bin")), QString()));
    QCOMPARE(dq.fileCount(), 1);
    QCOMPARE(partMetCount(temp.path()), 1);

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

// "Auto clear completed downloads" (MFC PartFile.cpp:3000): the finished file
// leaves the list, deferred past the completion listeners, and stays known.
void tst_DownloadQueue::autoClear_removesACompletedFileWhenEnabled()
{
    const auto restore = qScopeGuard([] { thePrefs.setAutoRemoveFinishedDownloads(false); });

    for (const bool autoClear : {false, true}) {
        thePrefs.setAutoRemoveFinishedDownloads(autoClear);

        KnownFileList kfl;
        const QString knownDir = m_tempDir.path() + QStringLiteral("/known_autoclear%1").arg(autoClear);
        QDir().mkpath(knownDir);
        kfl.init(knownDir);

        DownloadQueue dq;
        dq.setKnownFileList(&kfl);

        uint8 hash[16] = {0xAC, 0x1E, 0xA2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8(autoClear)};
        auto* pf = createTestPartFile(hash, QStringLiteral("autoclear.bin"));
        pf->setStatus(PartFileStatus::Complete);
        dq.addDownload(pf);

        bool listedAtCompletion = false;
        connect(&dq, &DownloadQueue::fileCompleted, this,
                [&](PartFile* f) { listedAtCompletion = dq.fileCount() == 1 && f == pf; });
        QSignalSpy removed(&dq, &DownloadQueue::fileRemoved);
        emit pf->partNotifier()->downloadCompleted();

        QTRY_VERIFY(kfl.isFilePtrInList(pf));
        QVERIFY(listedAtCompletion);
        if (autoClear) {
            QTRY_COMPARE(dq.fileCount(), 0);
            QCOMPARE(removed.size(), 1);
        } else {
            QTest::qWait(50);
            QCOMPARE(dq.fileCount(), 1);
            QCOMPARE(removed.size(), 0);
            dq.removeFile(pf);
        }
        QVERIFY(kfl.isFilePtrInList(pf));   // still alive, owned by the known list
        kfl.clear();
    }
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

void tst_DownloadQueue::fileByKadFileSearchID_found()
{
    DownloadQueue dq;

    uint8 hashA[16] = {0x4A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x4A};
    uint8 hashB[16] = {0x4B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x4B};
    auto* a = createTestPartFile(hashA, QStringLiteral("kadA.bin"));
    auto* b = createTestPartFile(hashB, QStringLiteral("kadB.bin"));
    dq.addDownload(a);
    dq.addDownload(b);

    a->setKadFileSearchID(77);
    b->setKadFileSearchID(78);

    QCOMPARE(dq.fileByKadFileSearchID(77), a);
    QCOMPARE(dq.fileByKadFileSearchID(78), b);
    QVERIFY(dq.fileByKadFileSearchID(79) == nullptr);

    dq.deleteAll();
}

void tst_DownloadQueue::fileByKadFileSearchID_ignoresZero()
{
    DownloadQueue dq;

    uint8 hash[16] = {0x4C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x4C};
    auto* pf = createTestPartFile(hash, QStringLiteral("kadZero.bin"));
    dq.addDownload(pf);

    // 0 means "no search". Matching on it would hand a file to the first
    // expiring search that happens to have no owner.
    QCOMPARE(pf->kadFileSearchID(), 0U);
    QVERIFY(dq.fileByKadFileSearchID(0) == nullptr);

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

void tst_DownloadQueue::startNextFile_prefersTheSameCategory()
{
    DownloadCategory all;
    all.title = QStringLiteral("All");
    DownloadCategory movies;
    movies.title = QStringLiteral("Movies");
    thePrefs.setCategories({all, movies});

    thePrefs.setStartNextPausedFile(true);
    thePrefs.setStartNextPausedFileSameCat(true);
    thePrefs.setStartNextPausedFileOnlySameCat(false);

    DownloadQueue dq;

    uint8 hash1[16] = {21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hash2[16] = {21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};

    // The uncategorised file has the higher download priority, so "any
    // category" would pick it. Preferring the same category must not.
    auto* uncategorised = createTestPartFile(hash1, QStringLiteral("other.bin"), kPrHigh);
    auto* inCategory = createTestPartFile(hash2, QStringLiteral("film.bin"), kPrLow);
    inCategory->setCategory(1);

    dq.addDownload(uncategorised, true);
    dq.addDownload(inCategory, true);

    dq.startNextFileIfPrefs(1);
    QVERIFY2(!inCategory->isPaused(), "the same category wins over a higher priority");
    QVERIFY(uncategorised->isPaused());

    // Nothing paused is left in category 1, so the fallback takes over.
    dq.startNextFileIfPrefs(1);
    QVERIFY(!uncategorised->isPaused());

    dq.deleteAll();
    thePrefs.setStartNextPausedFile(false);
    thePrefs.setCategories({all});
}

void tst_DownloadQueue::autoCategory_assignsByPattern()
{
    DownloadCategory all;
    all.title = QStringLiteral("All");
    DownloadCategory movies;
    movies.title = QStringLiteral("Movies");
    movies.autocat = QStringLiteral("mkv|avi");
    DownloadCategory shows;
    shows.title = QStringLiteral("Shows");
    shows.autocat = QStringLiteral("^S[0-9]+E[0-9]+");
    shows.autocatIsRegexp = true;
    DownloadCategory images;
    images.title = QStringLiteral("Images");
    images.autocat = QStringLiteral("*.iso");
    thePrefs.setCategories({all, movies, shows, images});

    DownloadQueue dq;

    struct Case { uint8 tag; const char* name; uint32 expected; };
    const Case cases[] = {
        {1, "holiday.mkv",       1},  // one of a '|' list
        {2, "S01E02 pilot.mp4",  2},  // regular expression
        {3, "ubuntu-24.04.iso",  3},  // wildcard term
        {4, "notes.txt",         0},  // nothing matches
    };

    std::vector<PartFile*> files;
    for (const auto& c : cases) {
        uint8 hash[16] = {22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, c.tag};
        auto* pf = createTestPartFile(hash, QString::fromLatin1(c.name));
        dq.addDownload(pf, true);
        QCOMPARE(pf->category(), c.expected);
        files.push_back(pf);
    }

    // An explicit category is never overridden — MFC bails on the same test
    // (srchybrid/DownloadQueue.cpp:1242).
    uint8 hash5[16] = {22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    auto* explicitCat = createTestPartFile(hash5, QStringLiteral("holiday.mkv"));
    explicitCat->setCategory(3);
    dq.addDownload(explicitCat, true);
    QCOMPARE(explicitCat->category(), 3U);

    dq.deleteAll();
    thePrefs.setCategories({all});
}

void tst_DownloadQueue::remapCategories_afterRemoval()
{
    DownloadQueue dq;

    std::vector<PartFile*> files;
    for (uint8 cat = 0; cat < 4; ++cat) {
        uint8 hash[16] = {23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, cat};
        auto* pf = createTestPartFile(hash, QStringLiteral("cat%1.bin").arg(cat));
        pf->setCategory(cat);
        dq.addDownload(pf, true);
        files.push_back(pf);
    }

    // Category 2 was deleted, so it is simply absent from the map: 1 stays put,
    // 3 slides into the gap, and 2's downloads fall back to All.
    dq.remapCategories({{1, 1}, {3, 2}});

    QCOMPARE(files[0]->category(), 0U);
    QCOMPARE(files[1]->category(), 1U);
    QCOMPARE(files[2]->category(), 0U);
    QCOMPARE(files[3]->category(), 2U);

    dq.deleteAll();
}

void tst_DownloadQueue::remapCategories_afterReorder()
{
    DownloadQueue dq;

    std::vector<PartFile*> files;
    for (uint8 cat = 0; cat < 4; ++cat) {
        uint8 hash[16] = {24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, cat};
        auto* pf = createTestPartFile(hash, QStringLiteral("move%1.bin").arg(cat));
        pf->setCategory(cat);
        dq.addDownload(pf, true);
        files.push_back(pf);
    }

    // The user dragged category 3 in front of category 1.
    dq.remapCategories({{3, 1}, {1, 2}, {2, 3}});

    QCOMPARE(files[0]->category(), 0U);
    QCOMPARE(files[1]->category(), 2U);
    QCOMPARE(files[2]->category(), 3U);
    QCOMPARE(files[3]->category(), 1U);

    dq.deleteAll();
}

void tst_DownloadQueue::remapCategories_afterCombinedEdit()
{
    DownloadQueue dq;

    std::vector<PartFile*> files;
    for (uint8 cat = 0; cat < 4; ++cat) {
        uint8 hash[16] = {25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, cat};
        auto* pf = createTestPartFile(hash, QStringLiteral("both%1.bin").arg(cat));
        pf->setCategory(cat);
        dq.addDownload(pf, true);
        files.push_back(pf);
    }

    // Remove 1 *and* reorder in one edit — the case a length-based guess gets
    // wrong: the list shrinks by one, so "the last category was removed" would
    // send category 3's downloads to All and leave 2 pointing at 3's files.
    dq.remapCategories({{3, 1}, {2, 2}});

    QCOMPARE(files[0]->category(), 0U);
    QCOMPARE(files[1]->category(), 0U);  // its category is gone
    QCOMPARE(files[2]->category(), 2U);  // stayed where it was
    QCOMPARE(files[3]->category(), 1U);  // moved to the front

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

void tst_DownloadQueue::checkAndAddSource_rejectsOwnUserHash()
{
    // A source exchange can hand us our own identity behind an address we do not
    // recognise as ours; the user hash is what gives it away. MFC DownloadQueue.cpp:461.
    DownloadQueue dq;

    uint8 hash[16] = {31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("self_source.bin"));
    dq.addDownload(pf);

    // A zero user hash reads as "no hash", and MFC only compares when the source has one,
    // so the preference needs a real hash for this to mean anything.
    struct UserHashGuard {
        std::array<uint8, 16> prev = thePrefs.userHash();
        ~UserHashGuard() { thePrefs.setUserHash(prev); }
    } hashGuard;
    std::array<uint8, 16> ourHash{};
    ourHash.fill(0x9F);
    thePrefs.setUserHash(ourHash);

    UpDownClient self;
    self.setUserAddress(Address::fromNetworkOrder(0x05060708));
    self.setUserIDHybrid(0x08070605);
    self.setUserPort(4662);
    self.setUserHash(ourHash.data());
    QVERIFY(self.hasValidHash());

    QVERIFY2(!dq.checkAndAddSource(pf, &self), "our own user hash is not a source");
    QCOMPARE(pf->sourceCount(), 0);

    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_rejectsCryptIncompatible()
{
    // A peer demanding obfuscation we have switched off can never be connected to, so it
    // is a wasted source slot rather than a source. MFC DownloadQueue.cpp:478-485.
    struct PrefGuard {
        bool supported = thePrefs.cryptLayerSupported();
        ~PrefGuard() { thePrefs.setCryptLayerSupported(supported); }
    } prefGuard;

    DownloadQueue dq;

    uint8 hash[16] = {32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("crypt_source.bin"));
    dq.addDownload(pf);

    UpDownClient peer;
    peer.setUserAddress(Address::fromNetworkOrder(0x0A0B0C0D));
    peer.setUserIDHybrid(0x0D0C0B0A);
    peer.setUserPort(4662);
    uint8 peerHash[16];
    std::memset(peerHash, 0x7B, sizeof(peerHash));
    peer.setUserHash(peerHash);
    peer.setConnectOptions(0x07, true, false);   // supports + requests + requires
    QVERIFY(peer.requiresCryptLayer());

    thePrefs.setCryptLayerSupported(false);
    QVERIFY2(!dq.checkAndAddSource(pf, &peer), "peer requires obfuscation we cannot do");
    QCOMPARE(pf->sourceCount(), 0);

    thePrefs.setCryptLayerSupported(true);
    QVERIFY2(dq.checkAndAddSource(pf, &peer), "with obfuscation on it is a fine source");
    QCOMPARE(pf->sourceCount(), 1);

    pf->removeSource(&peer);
    dq.deleteAll();
}

// The High-ID-only address check. MFC applies IsGoodIP to a source only when it has a
// High ID (srchybrid/DownloadQueue.cpp:568-575), because a Low ID's user ID is an ID and
// not an address — testing it would reject every firewalled source, and the IPv6-only
// marker kNoIPv4SourceId is deliberately a Low ID. Without the guard the Kad and
// source-exchange ingresses accepted multicast, broadcast, loopback and LAN peers as
// download sources.
void tst_DownloadQueue::checkAndAddSource_rejectsUnusableHighIdOnly()
{
    DownloadQueue dq;

    uint8 hash[16] = {31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("goodip_guard.bin"));
    dq.addDownload(pf);

    // Each source gets its own address and port so the file's own dup checks stay out of
    // the way and only the new guard decides.
    auto makeHighId = [](const char* ip, uint16 port) {
        auto c = std::make_unique<UpDownClient>();
        const Address addr = Address::fromString(QString::fromLatin1(ip));
        c->setUserAddress(addr);
        c->setUserIDHybrid(addr.toUint32());   // High ID: the hybrid ID *is* the IPv4
        c->setUserPort(port);
        return c;
    };

    auto multicast = makeHighId("224.0.0.1", 4662);
    QVERIFY(!multicast->hasLowID());
    QVERIFY2(!dq.checkAndAddSource(pf, multicast.get()), "multicast is not a peer address");

    auto broadcast = makeHighId("255.255.255.255", 4663);
    QVERIFY(!broadcast->hasLowID());
    QVERIFY2(!dq.checkAndAddSource(pf, broadcast.get()), "broadcast is not a peer address");

    auto loopback = makeHighId("127.0.0.1", 4664);
    QVERIFY(!loopback->hasLowID());
    QVERIFY2(!dq.checkAndAddSource(pf, loopback.get()), "loopback is not a peer address");

    // A Low ID carries an ID in that field, not an address — the guard must not look at
    // it. 0x00010203 is what a peer behind a server hands us; it is also what the raw
    // bytes of 0.1.2.3 would read as, which is precisely why MFC gates on HasLowID().
    auto lowId = std::make_unique<UpDownClient>();
    lowId->setUserIDHybrid(0x00010203u);
    lowId->setUserAddress(Address::fromString(QStringLiteral("85.1.2.3")));
    lowId->setUserPort(4665);
    QVERIFY(lowId->hasLowID());
    QVERIFY2(dq.checkAndAddSource(pf, lowId.get()), "a Low ID must not be address-checked");

    // A LAN peer is rejected only because filterLANIPs is on; the lab-rig switch that
    // makes an interop network usable must keep working through this guard too.
    {
        auto lan = makeHighId("192.168.7.9", 4666);
        QVERIFY(!lan->hasLowID());
        QVERIFY(!dq.checkAndAddSource(pf, lan.get()));
    }
    {
        LabModeGuard lab(true);
        auto lan = makeHighId("192.168.7.9", 4667);
        QVERIFY2(dq.checkAndAddSource(pf, lan.get()),
                 "lab mode (filterLANIPs=false) must still admit LAN sources");
        pf->srcList().clear();
    }

    // And an ordinary public High ID is untouched.
    auto publicPeer = makeHighId("81.2.3.4", 4668);
    QVERIFY(dq.checkAndAddSource(pf, publicPeer.get()));

    pf->srcList().clear();
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

// A download started from a search result takes the clients that answered for it. A
// merged answer keeps its clients on the child row, so both levels are read; the vetting
// is the server-source one.
void tst_DownloadQueue::seedFromSearchResult_seedsParentAndChildClients()
{
    QVERIFY(theApp.isFirewalled());

    DownloadQueue dq;

    uint8 hash[16] = {43, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    auto* pf = createTestPartFile(hash, QStringLiteral("search_seed_test.bin"));
    dq.addDownload(pf);

    const uint32 server = Address::fromString(QStringLiteral("99.88.77.66")).toNetworkUint32();
    const uint32 onParent = Address::fromString(QStringLiteral("77.66.55.44")).toNetworkUint32();
    const uint32 onChild = Address::fromString(QStringLiteral("88.77.66.55")).toNetworkUint32();

    SearchFile parent;
    parent.setFileHash(hash);
    parent.addClient({onParent, 4662, server, 4661});
    parent.addClient({0x00000123u, 4663, server, 4661});   // LowID while firewalled: dropped

    SearchFile child(&parent);
    child.setListParent(&parent);
    parent.addListChild(&child);
    child.addClient({onChild, 4664, server, 4661});
    // the copied parent client must not be added twice

    dq.seedFromSearchResult(pf, parent);

    QCOMPARE(pf->sourceCount(), 2);
    std::vector<uint32> ids;
    for (const UpDownClient* client : pf->srcList())
        ids.push_back(client->userIDHybrid());
    std::ranges::sort(ids);
    std::vector<uint32> expected{Address::fromString(QStringLiteral("77.66.55.44")).toUint32(),
                                 Address::fromString(QStringLiteral("88.77.66.55")).toUint32()};
    std::ranges::sort(expected);
    QCOMPARE(ids, expected);

    dq.deleteAll();
}

// The result's AICH hash seeds the recovery set like a link's does — unless the answers
// disagree, or the download already has one.
void tst_DownloadQueue::seedFromSearchResult_seedsAICH()
{
    DownloadQueue dq;

    const uint8 rawA[20] = {0xA1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    const uint8 rawB[20] = {0xB2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    const AICHHash hashA(rawA);
    const AICHHash hashB(rawB);

    // Agreed hash: taken, recovery set usable at once
    {
        uint8 hash[16] = {44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
        auto* pf = createTestPartFile(hash, QStringLiteral("search_aich_ok.bin"));
        dq.addDownload(pf);

        SearchFile parent;
        parent.setFileHash(hash);
        parent.fileIdentifier().setAICHHash(hashA);
        SearchFile child(&parent);
        child.setListParent(&parent);
        parent.addListChild(&child);

        dq.seedFromSearchResult(pf, parent);

        QVERIFY(pf->fileIdentifier().hasAICHHash());
        QVERIFY(pf->fileIdentifier().getAICHHash() == hashA);
        QVERIFY(pf->aichRecoveryHashSet().hasValidMasterHash());
        QCOMPARE(pf->aichRecoveryHashSet().getStatus(), EAICHStatus::Verified);
    }

    // Two answers, two hashes: none
    {
        uint8 hash[16] = {45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
        auto* pf = createTestPartFile(hash, QStringLiteral("search_aich_conflict.bin"));
        dq.addDownload(pf);

        SearchFile parent;
        parent.setFileHash(hash);
        parent.fileIdentifier().setAICHHash(hashA);
        SearchFile child(&parent);
        child.setListParent(&parent);
        parent.addListChild(&child);
        child.fileIdentifier().setAICHHash(hashB);

        dq.seedFromSearchResult(pf, parent);

        QVERIFY(!pf->fileIdentifier().hasAICHHash());
        QVERIFY(!pf->aichRecoveryHashSet().hasValidMasterHash());
    }

    // The download's own hash stays
    {
        uint8 hash[16] = {46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
        auto* pf = createTestPartFile(hash, QStringLiteral("search_aich_keep.bin"));
        pf->fileIdentifier().setAICHHash(hashB);
        dq.addDownload(pf);

        SearchFile parent;
        parent.setFileHash(hash);
        parent.fileIdentifier().setAICHHash(hashA);

        dq.seedFromSearchResult(pf, parent);

        QVERIFY(pf->fileIdentifier().getAICHHash() == hashB);
    }

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

// ---------------------------------------------------------------------------
// Kad source results — addKadSourceResult's per-type switch
//
// Type 6 is "firewalled, but reachable by a direct UDP callback". Unlike types 3
// and 5 it names no buddy, so bit 3 of the crypt byte is the *entire* difference
// between a usable record and an unreachable one — which is why it is checked on
// the way in, and why it must survive being checked.
// ---------------------------------------------------------------------------

namespace {

/// A Kad open enough that theApp.isFirewalled() is false — types 3, 5 and 6 are all
/// refused outright while we are the firewalled party, since two firewalled peers can
/// never reach each other.
void makeReachable(eMule::testing::KadFixture& fx)
{
    fx.kadPrefs().setLastContact();   // isConnected() also wants a contact on record
}

kad::Kademlia::KadSourceResult makeKadResult(const uint8* fileHash, uint8 type,
                                             const uint8* clientHash, uint32 ip,
                                             uint16 tcpPort, uint16 udpPort, uint8 crypt)
{
    kad::Kademlia::KadSourceResult r;
    r.fileHash   = fileHash;
    r.sourceType = type;
    r.clientHash = clientHash;
    r.ip         = ip;
    r.tcpPort    = tcpPort;
    r.udpPort    = udpPort;
    r.buddyCrypt = crypt;
    return r;
}

} // namespace

void tst_DownloadQueue::addKadSources_type6KeepsDirectCallback()
{
    // Regression, and it took two bugs to break: the publisher never set bit 3, and the
    // consumer passed callback=false to setConnectOptions — which ANDs that very bit
    // away, on the source it had just admitted *because* the bit was set. The result
    // reported no callback route at all and was dropped on the first tryToConnect().
    //
    // supportsDirectUDPCallback() wants the flag, a valid user hash and a non-zero Kad
    // port together, so asserting it pins all three assignments at once.
    eMule::testing::KadFixture fx{eMule::testing::KadMode::Open};
    makeReachable(fx);
    QVERIFY(!theApp.isFirewalled());

    DownloadQueue dq;
    uint8 hash[16] = {60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash, QStringLiteral("kad_type6.bin"));
    dq.addDownload(pf);

    uint8 clientHash[16];
    std::memset(clientHash, 0x5A, sizeof(clientHash));

    const uint32 sourceIP = Address::fromString(QStringLiteral("77.66.55.44")).toUint32();
    // 0x09 = crypt-layer supported | direct UDP callback.
    dq.addKadSourceResult(makeKadResult(hash, 6, clientHash, sourceIP, 4662, 4672, 0x09));

    QCOMPARE(pf->sourceCount(), 1);
    auto* src = pf->srcList().front();
    QVERIFY(src->supportsDirectUDPCallback());
    QCOMPARE(src->kadPort(), uint16{4672});
    // The IP is for UDP, not TCP: a type-6 source is LowID and cannot be dialled.
    QCOMPARE(src->connectAddress(), Address::fromString(QStringLiteral("77.66.55.44")));

    dq.deleteAll();
}

void tst_DownloadQueue::addKadSources_type6DroppedWithoutTheBit()
{
    // The other half of the contract: type 6 without the callback bit is a source
    // nobody can reach, so it is refused rather than added and retried forever.
    eMule::testing::KadFixture fx{eMule::testing::KadMode::Open};
    makeReachable(fx);
    QVERIFY(!theApp.isFirewalled());

    DownloadQueue dq;
    uint8 hash[16] = {61, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash, QStringLiteral("kad_type6_nobit.bin"));
    dq.addDownload(pf);

    uint8 clientHash[16];
    std::memset(clientHash, 0x5B, sizeof(clientHash));

    const uint32 sourceIP = Address::fromString(QStringLiteral("77.66.55.43")).toUint32();
    dq.addKadSourceResult(makeKadResult(hash, 6, clientHash, sourceIP, 4662, 4672, 0x01));

    QCOMPARE(pf->sourceCount(), 0);

    dq.deleteAll();
}

void tst_DownloadQueue::addKadSources_buddyIpIsNetworkOrder()
{
    // FT_SERVERIP arrives in network order — the one IP tag in a Kad source record that
    // is not host order. Reading it the other way sent every buddy callback to a
    // byte-reversed address, which is invisible unless the test address is asymmetric.
    eMule::testing::KadFixture fx{eMule::testing::KadMode::Open};
    makeReachable(fx);
    QVERIFY(!theApp.isFirewalled());

    DownloadQueue dq;
    uint8 hash[16] = {62, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    auto* pf = createTestPartFile(hash, QStringLiteral("kad_type3_buddy.bin"));
    dq.addDownload(pf);

    uint8 clientHash[16];
    std::memset(clientHash, 0x5C, sizeof(clientHash));
    uint8 buddyHash[16];
    std::memset(buddyHash, 0x6C, sizeof(buddyHash));

    const Address buddy = Address::fromString(QStringLiteral("11.22.33.44"));
    QVERIFY(buddy.toNetworkUint32() != buddy.toUint32());

    auto result = makeKadResult(hash, 3, clientHash,
                                Address::fromString(QStringLiteral("77.66.55.42")).toUint32(),
                                4662, 4672, 0x01);
    result.buddyIP   = buddy.toNetworkUint32();
    result.buddyPort = 5555;
    result.buddyHash = buddyHash;
    dq.addKadSourceResult(result);

    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->buddyAddress(), buddy);

    dq.deleteAll();
}

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
    dq.noteUdpSourceRequest(from.address(), getTickCount());
    dq.addUDPGlobalSources(reinterpret_cast<const uint8*>(body.constData()),
                           static_cast<uint32>(body.size()), from);

    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(pf->srcList().front()->userIDHybrid(),
             Address::fromString(QStringLiteral("88.77.66.55")).toUint32());

    dq.deleteAll();
}

// Stock takes OP_GLOBFOUNDSOURCES from anybody; the hash being public, that is a way
// to hand us sources of the sender's choosing.
void tst_DownloadQueue::udpGlobalSources_onlyFromAServerWeAsked()
{
    DownloadQueue dq;

    uint8 hash[16] = {52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_asked.bin"));
    dq.addDownload(pf);

    const uint32 src = Address::fromString(QStringLiteral("88.77.66.54")).toNetworkUint32();
    const QByteArray body = makeServerSourceBody(hash, {src});
    const Endpoint from(Address::fromString(QStringLiteral("1.2.3.5")), 4665);
    const auto deliver = [&] {
        dq.addUDPGlobalSources(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()), from);
    };

    deliver();                                   // never asked
    QCOMPARE(pf->sourceCount(), 0);

    const uint64 now = getTickCount();
    if (now > 200'000) {
        dq.noteUdpSourceRequest(from.address(), now - 180'000);   // asked long ago
        deliver();
        QCOMPARE(pf->sourceCount(), 0);
    }

    dq.noteUdpSourceRequest(Address::fromString(QStringLiteral("1.2.3.6")), now);
    deliver();                                   // somebody else was asked
    QCOMPARE(pf->sourceCount(), 0);

    dq.noteUdpSourceRequest(from.address(), now);
    deliver();
    QCOMPARE(pf->sourceCount(), 1);

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
    dq.noteUdpSourceRequest(from.address(), getTickCount());
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

void tst_DownloadQueue::process_udpReaskWindowIsDisjointFromTheTcpReask()
{
    // MFC will not UDP re-ask while our own UDP is off or we are firewalled.
    UdpReaskReadyGuard udpReady;

    // ...nor with nowhere to send the datagram. The flag under test is only raised once
    // the packet is actually handed to the socket, so the socket has to exist.
    ClientUDPSocket clientUDP;
    QVERIFY(clientUDP.rebind(0));
    theApp.clientUDP = &clientUDP;
    const auto restoreUDP = qScopeGuard([] { theApp.clientUDP = nullptr; });

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
    source.setDownloadState(DownloadState::OnQueue);   // MFC's DS_ONQUEUE gate
    pf->addSource(&source);

    // Never asked, so the clock reads zero — MFC's GetTimeUntilReask() contract for an
    // unknown (client, file) pair. That instant belongs to the *TCP* re-ask in
    // PartFile::process(); the UDP window is the two minutes before it
    // (MFC PartFile.cpp:2338-2339). The two used to share this instant, so the same pass
    // sent a datagram and then charged it as failed at the head of askForDownload() —
    // driving m_failedUDPPackets past the 30% abort and killing UDP re-asks for good.
    QCOMPARE(source.timeUntilReask(pf), 0u);
    runOneSecondOfTicks(dq);
    QVERIFY2(!source.udpPacketPending(),
             "a fully expired clock is the TCP branch's turn, not the UDP one's");

    // The source itself is perfectly eligible — only the schedule withheld it.
    source.udpReaskForDownload();
    QVERIFY(source.udpPacketPending());

    // The answer stamps the clock — MFC UDPReaskACK(), DownloadClient.cpp:1310 — so the
    // source goes quiet for FILEREASKTIME instead of being re-asked every pass. That puts
    // it far outside the two-minute window as well.
    source.udpReaskACK(0);
    QVERIFY(!source.udpPacketPending());
    QVERIFY(source.timeUntilReask(pf) > MIN2MS(2));

    runOneSecondOfTicks(dq);
    QVERIFY2(!source.udpPacketPending(),
             "an answered source must not be re-asked again inside FILEREASKTIME");

    pf->srcList().clear();
    dq.deleteAll();
}

// MFC refuses a source for a stopped download outright (DownloadQueue.cpp:456): we are
// not going to talk to it, so keeping it only ages the entry.
void tst_DownloadQueue::checkAndAddSource_rejectsSourceForAStoppedDownload()
{
    DownloadQueue dq;

    uint8 hash[16] = {36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("stopped.bin"));
    dq.addDownload(pf);
    pf->stopFile();

    UpDownClient client;
    const Address addr = Address::fromString(QStringLiteral("81.2.69.160"));
    client.setUserAddress(addr);
    client.setUserIDHybrid(addr.toUint32());
    client.setUserPort(4662);

    QVERIFY(!dq.checkAndAddSource(pf, &client));
    QCOMPARE(pf->sourceCount(), 0);

    dq.deleteAll();
}

// A peer that asks us for a file we happen to be downloading is a source we never had to
// look for. It stays in the ClientList either way — it is already connected to us — which
// is the whole reason this path is separate from checkAndAddSource().
void tst_DownloadQueue::checkAndAddKnownSource_addsAPassiveSource()
{
    DownloadQueue dq;

    uint8 hash[16] = {37, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("passive.bin"));
    dq.addDownload(pf);

    UpDownClient client;
    const Address addr = Address::fromString(QStringLiteral("81.2.69.161"));
    client.setUserAddress(addr);
    client.setUserIDHybrid(addr.toUint32());
    client.setUserPort(4662);
    QCOMPARE(client.sourceFrom(), SourceFrom::Server);

    QVERIFY(dq.checkAndAddKnownSource(pf, &client, /*ignoreGlobalDeadList*/ true));
    QCOMPARE(pf->sourceCount(), 1);
    QCOMPARE(client.reqFile(), pf);
    QCOMPARE(client.sourceFrom(), SourceFrom::Passive);

    // Asking twice does not source it twice.
    QVERIFY(!dq.checkAndAddKnownSource(pf, &client, true));
    QCOMPARE(pf->sourceCount(), 1);

    pf->removeSource(&client);
    dq.deleteAll();
}

// Already sourcing another file? Then this is an A4AF relationship, not a second source
// entry — MFC DownloadQueue.cpp:578-590.
void tst_DownloadQueue::checkAndAddKnownSource_a4afWhenItAlreadySourcesAnotherFile()
{
    DownloadQueue dq;

    uint8 hashA[16] = {38, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("a4af-a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("a4af-b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);

    UpDownClient client;
    const Address addr = Address::fromString(QStringLiteral("81.2.69.162"));
    client.setUserAddress(addr);
    client.setUserIDHybrid(addr.toUint32());
    client.setUserPort(4662);

    QVERIFY(dq.checkAndAddKnownSource(fileA, &client, true));
    QCOMPARE(fileA->sourceCount(), 1);

    // Connected, so MFC records the A4AF and leaves the client where it is; an idle one
    // would be swapped to whichever file wins (srchybrid/DownloadQueue.cpp:584-586).
    client.setDownloadState(DownloadState::Connected);

    // The same client now asks about the other file.
    QVERIFY2(!dq.checkAndAddKnownSource(fileB, &client, true),
             "a client already sourcing another file is an A4AF candidate, not a source");
    QCOMPARE(fileB->sourceCount(), 0);
    QCOMPARE(fileB->a4afSourceCount(), 1);

    client.removeFileFromOtherLists(fileB);
    fileA->removeSource(&client);
    dq.deleteAll();
}

// ---------------------------------------------------------------------------
// checkAndAddSource duplicate detection — MFC DownloadQueue.cpp:488-526
// ---------------------------------------------------------------------------

namespace {

/// A source as a server / SLS / Kad ingress builds it: ED2K ID only, no userAddress, no hash.
std::unique_ptr<UpDownClient> makePreHelloSource(PartFile* pf, const char* ip, uint16 port)
{
    const Address addr = Address::fromString(QString::fromLatin1(ip));
    return std::make_unique<UpDownClient>(port, addr.toNetworkUint32(), 0, 0, pf, true);
}

/// A v6-only source as makeSourceClient() builds it.
std::unique_ptr<UpDownClient> makeV6OnlySource(PartFile* pf, const char* ip, uint16 port)
{
    const Address v6 = Address::fromString(QString::fromLatin1(ip));
    auto c = std::make_unique<UpDownClient>(port, kNoIPv4SourceId, 0, 0, pf, true);
    c->setUserIPv6(v6);
    c->setOpenIPv6(true);
    c->setUserAddress(v6);
    return c;
}

} // namespace

// The live bug: an SLS source and a server source for one HighID peer both got in,
// because neither has a userAddress or hash before the hello. compare() falls back to
// userIDHybrid + port for exactly this case.
void tst_DownloadQueue::checkAndAddSource_dedupsPreHelloHighIdByUserId()
{
    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* pf = createTestPartFile(hash, QStringLiteral("dedup_prehello.bin"));
    dq.addDownload(pf);
    {
        auto sls = makePreHelloSource(pf, "81.2.69.160", 28022);
        sls->setSourceFrom(SourceFrom::SLS);
        QVERIFY(!sls->hasLowID());
        QCOMPARE(dq.checkAndAddSource(pf, sls.get()), sls.get());

        auto srv = makePreHelloSource(pf, "81.2.69.160", 28022);
        QVERIFY2(!dq.checkAndAddSource(pf, srv.get()), "same ID + port is the same peer");
        QCOMPARE(pf->sourceCount(), 1);

        auto otherPort = makePreHelloSource(pf, "81.2.69.160", 28023);
        QCOMPARE(dq.checkAndAddSource(pf, otherPort.get()), otherPort.get());
        QCOMPARE(pf->sourceCount(), 2);

        pf->removeSource(sls.get());
        pf->removeSource(otherPort.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_dedupsAfterHelloAgainstHashlessServerSource()
{
    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* pf = createTestPartFile(hash, QStringLiteral("dedup_posthello.bin"));
    dq.addDownload(pf);
    {
        // Already said hello: verified address and a user hash.
        auto known = makePreHelloSource(pf, "81.2.69.161", 4662);
        const uint8 userHash[16] = {0xC2, 0x59, 0x75, 0x21, 7, 0x0E, 0xFD, 0x50,
                                    0, 0xE2, 0xC7, 0x58, 0xD8, 0x6F, 0x73, 0x01};
        known->setUserAddress(Address::fromString(QStringLiteral("81.2.69.161")));
        known->setUserHash(userHash);
        QCOMPARE(dq.checkAndAddSource(pf, known.get()), known.get());

        auto srv = makePreHelloSource(pf, "81.2.69.161", 4662);
        QVERIFY(!srv->hasValidHash());
        QVERIFY(!dq.checkAndAddSource(pf, srv.get()));
        QCOMPARE(pf->sourceCount(), 1);

        pf->removeSource(known.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_a4afForSourceOfAnotherFile()
{
    DownloadQueue dq;
    uint8 hashA[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    uint8 hashB[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("dedup_a4af_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("dedup_a4af_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);
    {
        auto onA = makePreHelloSource(fileA, "81.2.69.162", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, onA.get()), onA.get());
        // Connected: the A4AF is recorded but the client is not swapped away.
        onA->setDownloadState(DownloadState::Connected);

        auto forB = makePreHelloSource(fileB, "81.2.69.162", 4662);
        QVERIFY2(!dq.checkAndAddSource(fileB, forB.get()),
                 "a peer already sourcing another file becomes A4AF, not a second object");
        QCOMPARE(fileB->sourceCount(), 0);
        QCOMPARE(fileB->a4afSourceCount(), 1);

        onA->removeFileFromOtherLists(fileB);
        fileA->removeSource(onA.get());
    }
    dq.deleteAll();
}

// A client freed while it is an A4AF candidate used to stay in the other file's list,
// and deleteAll() then walked a freed pointer.
void tst_DownloadQueue::a4af_destroyedClientLeavesNoDanglingEntry()
{
    DownloadQueue dq;
    uint8 hashA[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    uint8 hashB[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("a4af_dtor_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("a4af_dtor_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.164", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        QVERIFY(client->addRequestForAnotherFile(fileB));
        QCOMPARE(fileB->a4afSrcList().size(), size_t(1));
        fileA->removeSource(client.get());
    }
    QVERIFY(fileB->a4afSrcList().empty());
    QCOMPARE(fileB->sourceCount(), 0);
    dq.deleteAll();
}

// MFC RemoveSource unlinks the A4AF on both sides (DownloadQueue.cpp:636-657).
void tst_DownloadQueue::a4af_removeSourceUnlinksBothSides()
{
    DownloadQueue dq;
    uint8 hashA[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9};
    uint8 hashB[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("a4af_rm_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("a4af_rm_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.165", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        QVERIFY(client->addRequestForAnotherFile(fileB));

        dq.removeSource(client.get());
        QCOMPARE(fileA->sourceCount(), 0);
        QVERIFY(fileB->a4afSrcList().empty());
        QCOMPARE(client->otherRequestCount(), size_t(0));
    }
    dq.deleteAll();
}

namespace {

/// Deliver OP_FILEREQANSNOFIL for `hash` the way the socket would.
void deliverFileNotFound(UpDownClient* client, const uint8* hash)
{
    QVERIFY(QMetaObject::invokeMethod(client, "onFileRequestReceived", Qt::DirectConnection,
                                      Q_ARG(const uint8*, hash), Q_ARG(uint32, 16),
                                      Q_ARG(uint8, OP_FILEREQANSNOFIL)));
}

struct AppQueueScope {
    explicit AppQueueScope(DownloadQueue* dq) { theApp.downloadQueue = dq; }
    ~AppQueueScope() { theApp.downloadQueue = nullptr; }
};

} // namespace

void tst_DownloadQueue::disconnect_failedSourceLeavesTheFile_data()
{
    QTest::addColumn<DownloadState>("state");
    QTest::addColumn<bool>("connecting");
    QTest::addColumn<bool>("removed");

    QTest::newRow("connect timed out") << DownloadState::Connecting << true << true;
    QTest::newRow("callback never came") << DownloadState::WaitCallback << true << true;
    QTest::newRow("failed before a route was picked") << DownloadState::Connecting << false << true;
    QTest::newRow("connected, never answered") << DownloadState::Connected << false << true;
    QTest::newRow("hashset request unanswered") << DownloadState::ReqHashSet << false << true;
    QTest::newRow("protocol error") << DownloadState::Error << false << true;
    QTest::newRow("queued source reconnect failed") << DownloadState::OnQueue << true << true;
    QTest::newRow("queued source drops") << DownloadState::OnQueue << false << false;
    QTest::newRow("no needed parts drops") << DownloadState::NoNeededParts << false << false;
    QTest::newRow("too many connections") << DownloadState::TooManyConns << false << false;
}

// MFC removes a source it could not reach or that never answered (BaseClient.cpp:1122-1192).
// Kept instead, they filled a long download up to maxSourcesPerFile with dead peers.
void tst_DownloadQueue::disconnect_failedSourceLeavesTheFile()
{
    QFETCH(DownloadState, state);
    QFETCH(bool, connecting);
    QFETCH(bool, removed);

    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);
    AppQueueScope scope(&dq);
    theApp.clientList = &cl;

    uint8 hashA[16] = {53, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("dead_src.bin"));
    dq.addDownload(fileA);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.180", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        cl.removeClient(client.get());   // owned by this scope, not by the list
        client->setDownloadState(state);
        if (connecting)
            client->setConnectingState(ConnectingState::DirectTCP);

        const bool unneeded = client->disconnected(QStringLiteral("test"));

        QCOMPARE(unneeded, removed);
        QCOMPARE(fileA->sourceCount(), removed ? 0 : 1);
        QCOMPARE(client->reqFile(), removed ? nullptr : fileA);
        QCOMPARE(client->connectingState(), ConnectingState::None);
        if (removed) {
            QCOMPARE(client->downloadState(), DownloadState::None);
            auto again = makePreHelloSource(fileA, "81.2.69.180", 4662);
            QVERIFY2(dq.checkAndAddSource(fileA, again.get()) == nullptr,
                     "a source we just failed to reach was taken straight back");
        } else {
            QCOMPARE(client->downloadState(), state);
            dq.removeSource(client.get());
        }
    }
    theApp.clientList = nullptr;
    dq.deleteAll();
}

void tst_DownloadQueue::tryToConnect_precheckExitDropsTheSource_data()
{
    QTest::addColumn<bool>("lowId");
    QTest::newRow("socket limit reached") << false;
    QTest::newRow("LowID with no callback route") << true;
}

// L7: a connect attempt that ends in tryToConnect's own checks is a failed attempt like
// any other (MFC srchybrid/BaseClient.cpp:1285-1372) — the source goes and is dead-listed.
void tst_DownloadQueue::tryToConnect_precheckExitDropsTheSource()
{
    QFETCH(bool, lowId);

    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);
    AppQueueScope scope(&dq);
    theApp.clientList = &cl;

    // Two sockets against a limit of one
    ListenSocket listener;
    ClientReqSocket busyA, busyB;
    const uint16 oldMax = thePrefs.maxConnections();
    if (!lowId) {
        thePrefs.setMaxConnections(1);
        listener.addSocket(&busyA);
        listener.addSocket(&busyB);
        theApp.listenSocket = &listener;
    }
    const auto restore = qScopeGuard([oldMax] {
        theApp.listenSocket = nullptr;
        theApp.clientList = nullptr;
        thePrefs.setMaxConnections(oldMax);
    });

    uint8 hashA[16] = {54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("precheck.bin"));
    dq.addDownload(fileA);
    {
        auto client = lowId
            ? std::make_unique<UpDownClient>(uint16(4662), uint32(4711), htonl(0x51024501), uint16(4661),
                                             fileA, true)
            : makePreHelloSource(fileA, "81.2.69.181", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        cl.removeClient(client.get());
        client->setDownloadState(DownloadState::OnQueue);
        QCOMPARE(client->hasLowID(), lowId);

        QVERIFY(!client->tryToConnect());

        QCOMPARE(fileA->sourceCount(), 0);
        QCOMPARE(client->reqFile(), nullptr);
        QCOMPARE(client->connectingState(), ConnectingState::None);
        QVERIFY(cl.globalDeadSourceList.isDeadSource(client->deadSourceKey()));
    }
    listener.removeSocket(&busyA);
    listener.removeSocket(&busyB);
    dq.deleteAll();
}

// A connect that died at our own proxy says nothing about the peer (MFC :1185).
void tst_DownloadQueue::disconnect_proxyFailureKeepsTheSource()
{
    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);
    AppQueueScope scope(&dq);
    theApp.clientList = &cl;
    const auto restore = qScopeGuard([] { theApp.clientList = nullptr; });

    uint8 hashA[16] = {55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("proxy.bin"));
    dq.addDownload(fileA);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.182", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        cl.removeClient(client.get());
        client->setDownloadState(DownloadState::OnQueue);
        client->setConnectingState(ConnectingState::DirectTCP);

        auto* sock = new ClientReqSocket(client.get());
        sock->setProxyConnectFailed(true);
        client->setSocket(sock);

        client->disconnected(QStringLiteral("Socket error: proxy"));

        QCOMPARE(fileA->sourceCount(), 1);
        QCOMPARE(client->reqFile(), fileA);
        QCOMPARE(client->downloadState(), DownloadState::OnQueue);
        QVERIFY(!cl.globalDeadSourceList.isDeadSource(client->deadSourceKey()));
        dq.removeSource(client.get());
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dq.deleteAll();
}

// removeSource() has to drop the client's file link too, or the reaper keeps it for ever.
void tst_DownloadQueue::disconnect_removedSourceIsReaped()
{
    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);
    AppQueueScope scope(&dq);
    theApp.clientList = &cl;

    uint8 hashA[16] = {53, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("reaped_src.bin"));
    dq.addDownload(fileA);

    auto* client = makePreHelloSource(fileA, "81.2.69.181", 4662).release();
    QCOMPARE(dq.checkAndAddSource(fileA, client), client);
    QVERIFY(cl.isValidClient(client));

    cl.process();
    QVERIFY2(cl.isValidClient(client), "a live source must not be reaped");

    client->setDownloadState(DownloadState::Connecting);
    client->disconnected(QStringLiteral("Connection try timeout"));
    cl.process();
    QVERIFY(!cl.isValidClient(client));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    theApp.clientList = nullptr;
    dq.deleteAll();
}

// "I don't have that file" is about one file. MFC dead-lists the peer on that file for
// 45 minutes (ListenSocket.cpp:431); without it the next source answer re-adds it.
void tst_DownloadQueue::fileNotFound_removesAndDeadListsForThatFileOnly()
{
    DownloadQueue dq;
    AppQueueScope scope(&dq);
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("fnf_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("fnf_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.170", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        client->setDownloadState(DownloadState::OnQueue);

        deliverFileNotFound(client.get(), hashA);
        QCOMPARE(fileA->sourceCount(), 0);

        auto again = makePreHelloSource(fileA, "81.2.69.170", 4662);
        QVERIFY2(dq.checkAndAddSource(fileA, again.get()) == nullptr,
                 "a source that said it lacks the file was taken back");

        auto other = makePreHelloSource(fileB, "81.2.69.170", 4662);
        QCOMPARE(dq.checkAndAddSource(fileB, other.get()), other.get());
        dq.removeSource(other.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::fileNotFound_swapsToAnotherWantedFile()
{
    DownloadQueue dq;
    AppQueueScope scope(&dq);
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
    uint8 hashB[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("fnf_swap_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("fnf_swap_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.171", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        QVERIFY(client->addRequestForAnotherFile(fileB));
        client->setDownloadState(DownloadState::OnQueue);

        deliverFileNotFound(client.get(), hashA);

        QCOMPARE(fileA->sourceCount(), 0);
        QCOMPARE(fileB->sourceCount(), 1);
        QCOMPARE(client->reqFile(), fileB);
        QVERIFY2(fileA->a4afSrcList().empty(), "it must not come back to the file it lacks");
        dq.removeSource(client.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::fileNotFound_forAFileWeDoNotDownloadChangesNothing()
{
    DownloadQueue dq;
    AppQueueScope scope(&dq);
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    uint8 stranger[16] = {52, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("fnf_other.bin"));
    dq.addDownload(fileA);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.172", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        client->setDownloadState(DownloadState::OnQueue);

        deliverFileNotFound(client.get(), stranger);

        QCOMPARE(fileA->sourceCount(), 1);
        QCOMPARE(client->downloadState(), DownloadState::OnQueue);
        dq.removeSource(client.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::udpFileNotFound_ignoredWhileDownloading()
{
    DownloadQueue dq;
    AppQueueScope scope(&dq);
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("fnf_udp.bin"));
    dq.addDownload(fileA);
    {
        auto client = makePreHelloSource(fileA, "81.2.69.173", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());

        client->setDownloadState(DownloadState::Downloading);
        client->udpReaskFNF();
        QCOMPARE(fileA->sourceCount(), 1);

        client->setDownloadState(DownloadState::OnQueue);
        client->udpReaskFNF();
        QCOMPARE(fileA->sourceCount(), 0);
        QVERIFY(fileA->deadSourceList().isDeadSource(client->deadSourceKey()));
    }
    dq.deleteAll();
}

// IPv6-only sources all carry the same placeholder ID; matching on ID + port would make
// one dead peer on port 4662 take every other one with it.
void tst_DownloadQueue::deadSource_ipv6SourcesAreToldApartByAddress()
{
    DownloadQueue dq;
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("dead_v6.bin"));
    dq.addDownload(fileA);
    {
        auto dead = makeV6OnlySource(fileA, "2a01:4f8::10", 4662);
        auto alive = makeV6OnlySource(fileA, "2a01:4f8::11", 4662);
        const Address server = Address::fromString(QStringLiteral("81.2.69.1"));
        dead->setServerAddress(server);
        alive->setServerAddress(server);

        fileA->deadSourceList().addDeadSource(dead->deadSourceKey(), dead->hasLowID());
        QVERIFY(fileA->deadSourceList().isDeadSource(dead->deadSourceKey()));
        QVERIFY(!fileA->deadSourceList().isDeadSource(alive->deadSourceKey()));
    }
    dq.deleteAll();
}

// A new source for a peer we already know (say, one on our upload queue) adopts that
// instance instead of adding a second one. MFC DownloadQueue.cpp:508-521.
void tst_DownloadQueue::checkAndAddSource_adoptsKnownClient()
{
    DownloadQueue dq;
    ClientList cl;
    dq.setClientList(&cl);
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};
    auto* pf = createTestPartFile(hash, QStringLiteral("dedup_adopt.bin"));
    dq.addDownload(pf);
    {
        UpDownClient known;
        const Address addr = Address::fromString(QStringLiteral("81.2.69.163"));
        known.setUserAddress(addr);
        known.setUserIDHybrid(addr.toUint32());
        known.setUserPort(4662);
        cl.addClient(&known);

        auto src = makePreHelloSource(pf, "81.2.69.163", 4662);
        QCOMPARE(dq.checkAndAddSource(pf, src.get()), &known);
        QCOMPARE(cl.clientCount(), 1);
        QCOMPARE(pf->sourceCount(), 1);
        QCOMPARE(known.reqFile(), pf);

        pf->removeSource(&known);
        cl.removeClient(&known);
    }
    dq.deleteAll();
}

void tst_DownloadQueue::checkAndAddSource_ipv6Dedup()
{
    DownloadQueue dq;
    uint8 hash[16] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6};
    auto* pf = createTestPartFile(hash, QStringLiteral("dedup_ipv6.bin"));
    dq.addDownload(pf);
    {
        // Two v6-only sources: same address + port is one peer, another address is not.
        auto a = makeV6OnlySource(pf, "2a03:2880:f10c:83:face:b00c:0:25de", 4662);
        QCOMPARE(dq.checkAndAddSource(pf, a.get()), a.get());
        auto aAgain = makeV6OnlySource(pf, "2a03:2880:f10c:83:face:b00c:0:25de", 4662);
        QVERIFY(!dq.checkAndAddSource(pf, aAgain.get()));
        auto c = makeV6OnlySource(pf, "2a03:2880:f10c:83:face:b00c:0:25df", 4662);
        QCOMPARE(dq.checkAndAddSource(pf, c.get()), c.get());
        QCOMPARE(pf->sourceCount(), 2);

        // Dual-stack HighID source carrying a's IPv6 as a hint: no shared IPv4 key, the
        // advertised v6 + port is what identifies it.
        auto dual = makePreHelloSource(pf, "81.2.69.170", 4662);
        dual->setUserIPv6(Address::fromString(QStringLiteral("2a03:2880:f10c:83:face:b00c:0:25de")));
        QVERIFY2(!dq.checkAndAddSource(pf, dual.get()), "same advertised v6 + port");
        QCOMPARE(pf->sourceCount(), 2);

        // HighID peer we reached over v6 keeps its v4 ID; a verified-v4 copy must not be
        // told apart just because the address families differ.
        auto overV6 = makePreHelloSource(pf, "81.2.69.180", 4700);
        overV6->setUserAddress(Address::fromString(QStringLiteral("2a03:2880:f10c:83:face:b00c:0:9")));
        QVERIFY(!overV6->hasLowID());
        QCOMPARE(dq.checkAndAddSource(pf, overV6.get()), overV6.get());
        auto overV4 = makePreHelloSource(pf, "81.2.69.180", 4700);
        overV4->setUserAddress(Address::fromString(QStringLiteral("81.2.69.180")));
        QVERIFY(!dq.checkAndAddSource(pf, overV4.get()));
        QCOMPARE(pf->sourceCount(), 3);

        pf->removeSource(a.get());
        pf->removeSource(c.get());
        pf->removeSource(overV6.get());
    }
    dq.deleteAll();
}

QTEST_GUILESS_MAIN(tst_DownloadQueue)
#include "tst_DownloadQueue.moc"
