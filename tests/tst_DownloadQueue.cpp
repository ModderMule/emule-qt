/// @file tst_DownloadQueue.cpp
/// @brief Tests for transfer/DownloadQueue — file management, lookup,
///        priority sorting, source management.

#include "CborSerializers.h"
#include "app/CoreOps.h"
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
    void addDownloadFromSearch_followsThePausedOption();
    void clientDetails_carryWhatMfcsDialogNeeds();
    void addUserSource_vetsWhatTheUserTyped();
    void removeAutoPrioInCat_onlyThatCategory();
    void removeFile_basic();
    void deleteAll_keepsCompletedFileOwnedByKnownList();
    void autoClear_removesACompletedFileWhenEnabled();
    void fileByID_found();
    void fileByID_notFound();
    void fileByKadFileSearchID_found();
    void kadSearch_goesToTheFileWithFewestSources();
    void diskFloor_isJudgedPerVolumeAndResumesWithHeadroom();
    void diskFloor_noFloorStopsOnlyWhatCannotFit();
    void diskFloor_refusesTheWriteBeforeItHappens();
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
    void a4af_sourceRowsFollowTheAvailableOnes();
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
    void sourceIndex_followsAnIdentityThatChanges();
    void sourceIndex_forgetsARemovedSource();
    void sourceIndex_keepsADuplicateTestOffTheFullScan();
    void checkAndAddKnownSource_addsAPassiveSource();
    void checkAndAddKnownSource_a4afWhenItAlreadySourcesAnotherFile();
    void checkAndAddKnownSource_swapsOnlyAnIdleDueSource();
    void localSrcRequests_goOutFifteenToAFrame();
    void localSrcRequests_orderAndDropouts();
    void ratioLimitedDownload_followsTheUploadLimit();
    void remoteQueueFull_survivesTheRankAndEndsWithASlot();
    void remoteQueueFull_sourceIsPurgedNearTheCap();
    void localSrcRequests_resetOnNewServerSession();
    void addServerSources_dropsLowIdWhenFirewalled();
    void addServerSources_dropsIpFilteredHighId();
    void addServerSources_dropsBannedHighId();
    void seedFromSearchResult_seedsParentAndChildClients();
    void seedFromSearchResult_seedsAICH();
    void seedFromSearchResult_kadOnlyRootIsAVoteNotAFact();
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
    void process_reasksAQueuedSourceOverItsOpenConnection_data();
    void process_reasksAQueuedSourceOverItsOpenConnection();
    void downloadClientByIP_UDP_ignoresThePortOnAUniqueAddress();
    void activeTime_runsOnlyWhileConnectedAndNotPaused();
    void pauseFile_cancelsARunningTransfer();
    void stopFile_letsTheSourcesGo();
    void stopPausedFile_firesAfterAnIdleHour();
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
    // every indexed duplicate lookup in this suite is checked against the full scan
    DownloadQueue::setVerifySourceIndex(true);
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

void tst_DownloadQueue::addDownloadFromSearch_followsThePausedOption()
{
    // MFC AddSearchToDownload: "add new files paused" decides unless the caller says
    // (srchybrid/DownloadQueue.cpp:172). Nothing read the option.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QStringList savedDirs = thePrefs.tempDirs();
    const bool savedPaused = thePrefs.addNewFilesPaused();
    const auto restore = qScopeGuard([&] {
        thePrefs.setTempDirs(savedDirs);
        thePrefs.setAddNewFilesPaused(savedPaused);
        theApp.downloadQueue = nullptr;
    });
    thePrefs.setTempDirs({temp.path()});

    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    const auto add = [&dq](uint8 seed, std::optional<bool> paused) {
        uint8 md4[16] = {0x4F, seed, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        const auto out = ops::addDownloadFromSearch(md4str(md4), QStringLiteral("f%1.bin").arg(seed),
                                                    5000, {}, 0, 0, paused);
        const PartFile* file = out.added ? dq.fileByID(md4) : nullptr;
        return file ? std::optional<bool>(file->isPaused()) : std::nullopt;
    };

    thePrefs.setAddNewFilesPaused(true);
    QCOMPARE(add(1, std::nullopt), std::optional<bool>(true));
    QCOMPARE(add(2, false), std::optional<bool>(false));     // "Download" in advanced mode
    thePrefs.setAddNewFilesPaused(false);
    QCOMPARE(add(3, std::nullopt), std::optional<bool>(false));
    QCOMPARE(add(4, true), std::optional<bool>(true));       // "Download (Paused)"

    dq.deleteAll();
}

void tst_DownloadQueue::addUserSource_vetsWhatTheUserTyped()
{
    // MFC CAddSourceDlg (srchybrid/AddSourceDlg.cpp:112-170): a typed address is a
    // source like any other, and no more trusted.
    DownloadQueue dq;
    uint8 hash[16] = {52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* file = createTestPartFile(hash, QStringLiteral("usersrc.bin"));
    dq.addDownload(file);

    QVERIFY(!dq.addUserSource(file, QStringLiteral("not an address"), 4662));
    QVERIFY(!dq.addUserSource(file, QStringLiteral("81.2.69.170"), 0));        // no port
    QVERIFY(!dq.addUserSource(file, QStringLiteral("192.168.1.20"), 4662));    // LAN
    QCOMPARE(file->sourceCount(), 0);

    QVERIFY(dq.addUserSource(file, QStringLiteral(" 81.2.69.170 "), 4662));
    QCOMPARE(file->sourceCount(), 1);

    // Only HTTP can be a URL source
    QVERIFY(!dq.addUserUrlSource(file, QStringLiteral("ftp://example.org/file.bin")));
    QVERIFY(!dq.addUserUrlSource(file, QStringLiteral("no url")));
    QCOMPARE(file->sourceCount(), 1);

    dq.deleteAll();
}

void tst_DownloadQueue::removeAutoPrioInCat_onlyThatCategory()
{
    // MFC RemoveAutoPrioInCat (srchybrid/DownloadQueue.cpp:1136-1148), run when a
    // category is switched to downloading in alphabetical order.
    DownloadQueue dq;
    uint8 hashA[16] = {53, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {53, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    auto* inCat = createTestPartFile(hashA, QStringLiteral("incat.bin"));
    auto* other = createTestPartFile(hashB, QStringLiteral("other.bin"));
    dq.addDownload(inCat);
    dq.addDownload(other);
    inCat->setCategory(2);
    other->setCategory(1);
    inCat->setAutoDownPriority(true);
    other->setAutoDownPriority(true);

    dq.removeAutoPrioInCat(2, kPrNormal);
    QVERIFY(!inCat->isAutoDownPriority());
    QCOMPARE(inCat->downPriority(), uint8{kPrNormal});
    QVERIFY(other->isAutoDownPriority());

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

// The one Kad source search a second went to the first eligible file in priority
// order; a starved file behind well-supplied ones waited for all of them.
void tst_DownloadQueue::kadSearch_goesToTheFileWithFewestSources()
{
    DownloadQueue dq;
    uint8 h1[16]{}, h2[16]{}, h3[16]{};
    h1[0] = 0x71; h2[0] = 0x72; h3[0] = 0x73;
    auto* rich = createTestPartFile(h1, QStringLiteral("rich.bin"), kPrHigh);
    auto* some = createTestPartFile(h2, QStringLiteral("some.bin"), kPrNormal);
    auto* starved = createTestPartFile(h3, QStringLiteral("starved.bin"), kPrLow);
    dq.addDownload(rich);
    dq.addDownload(some);
    dq.addDownload(starved);

    std::vector<std::unique_ptr<UpDownClient>> clients;
    const auto give = [&clients](PartFile* file, int count) {
        for (int i = 0; i < count; ++i) {
            auto& c = clients.emplace_back(std::make_unique<UpDownClient>());
            c->setDownloadState(DownloadState::OnQueue);
            file->addSource(c.get());
        }
    };
    give(rich, 6);
    give(some, 3);

    const uint64 now = getTickCount();
    QCOMPARE(dq.files().front(), rich);                 // list order says "rich"
    QCOMPARE(dq.pickKadSearchFile(now), starved);

    // A file that has its search, or is not due yet, is out of the running.
    starved->setKadFileSearchID(77);
    QCOMPARE(dq.pickKadSearchFile(now), some);
    starved->setKadFileSearchID(0);

    starved->pauseFile();
    QCOMPARE(dq.pickKadSearchFile(now), some);
    starved->resumeFile();
    QCOMPARE(dq.pickKadSearchFile(now), starved);

    // Equal need: queue order decides.
    give(starved, 3);
    QCOMPARE(dq.pickKadSearchFile(now), some);

    for (auto* file : {rich, some, starved})
        file->forgetAllSources();
}

namespace {

/// Disk-check prefs for one test, and a queue whose free space the test dictates.
struct DiskFloorEnv {
    explicit DiskFloorEnv(DownloadQueue& dq, uint64 floor)
        : savedCheck(thePrefs.checkDiskspace()), savedFloor(thePrefs.minFreeDiskSpace())
    {
        thePrefs.setCheckDiskspace(true);
        thePrefs.setMinFreeDiskSpace(floor);
        dq.setFreeSpaceProbe([this](const QString& dir) -> std::optional<uint64> {
            ++probes;
            const auto it = free.find(QDir::cleanPath(dir));
            return it == free.end() ? std::nullopt : std::optional<uint64>(it.value());
        });
    }
    ~DiskFloorEnv()
    {
        thePrefs.setCheckDiskspace(savedCheck);
        thePrefs.setMinFreeDiskSpace(savedFloor);
    }
    QHash<QString, uint64> free;   // directory -> free bytes; absent = unknown
    int probes = 0;
    bool savedCheck;
    uint64 savedFloor;
};

constexpr uint64 kMiB = 1024ull * 1024;

} // namespace

// One volume was measured (the first temp directory) and its verdict applied to every
// download; a file came back the moment free space touched the floor, to be parked
// again by its next block.
void tst_DownloadQueue::diskFloor_isJudgedPerVolumeAndResumesWithHeadroom()
{
    DownloadQueue dq;
    DiskFloorEnv env(dq, 100 * kMiB);

    uint8 h1[16]{}, h2[16]{}, h3[16]{};
    h1[0] = 0x81; h2[0] = 0x82; h3[0] = 0x83;
    auto* onFull = createTestPartFile(h1, QStringLiteral("a.bin"));
    auto* onRoomy = createTestPartFile(h2, QStringLiteral("b.bin"));
    auto* onUnknown = createTestPartFile(h3, QStringLiteral("c.bin"));
    onFull->setTmpPath(QStringLiteral("/vol-full/temp"));
    onRoomy->setTmpPath(QStringLiteral("/vol-roomy/temp"));
    onUnknown->setTmpPath(QStringLiteral("/vol-unknown/temp"));
    dq.addDownload(onFull);
    dq.addDownload(onRoomy);
    dq.addDownload(onUnknown);

    env.free[QStringLiteral("/vol-full/temp")] = 99 * kMiB;
    env.free[QStringLiteral("/vol-roomy/temp")] = 500 * kMiB;

    dq.checkDiskspace();
    QVERIFY(onFull->isInsufficient());
    QVERIFY(!onRoomy->isInsufficient());
    QVERIFY2(!onUnknown->isInsufficient(), "unknown is not full");

    // At the floor, and a little above it: not yet. The file still needs a whole part.
    const uint64 need = onFull->neededSpace();
    QCOMPARE(need, uint64{PARTSIZE});
    env.free[QStringLiteral("/vol-full/temp")] = 100 * kMiB + need - 1;
    dq.checkDiskspace();
    QVERIFY2(onFull->isInsufficient(), "resumed without room to write into");

    env.free[QStringLiteral("/vol-full/temp")] = 100 * kMiB + need;
    dq.checkDiskspace();
    QVERIFY(!onFull->isInsufficient());

    // A pause of the user's own is neither made nor undone by the check.
    onRoomy->pauseFile();
    env.free[QStringLiteral("/vol-roomy/temp")] = 1;
    dq.checkDiskspace();
    QVERIFY(!onRoomy->isInsufficient());
    env.free[QStringLiteral("/vol-roomy/temp")] = 500 * kMiB;
    dq.checkDiskspace();
    QVERIFY(onRoomy->isPaused());

    // Switched off, what the check parked comes back.
    env.free[QStringLiteral("/vol-full/temp")] = 1;
    dq.checkDiskspace();
    QVERIFY(onFull->isInsufficient());
    thePrefs.setCheckDiskspace(false);
    dq.checkDiskspace();
    QVERIFY(!onFull->isInsufficient());
}

void tst_DownloadQueue::diskFloor_noFloorStopsOnlyWhatCannotFit()
{
    DownloadQueue dq;
    DiskFloorEnv env(dq, 0);

    uint8 h1[16]{};
    h1[0] = 0x84;
    auto* file = createTestPartFile(h1, QStringLiteral("a.bin"));
    file->setTmpPath(QStringLiteral("/vol/temp"));
    dq.addDownload(file);

    env.free[QStringLiteral("/vol/temp")] = uint64{PARTSIZE};
    dq.checkDiskspace();
    QVERIFY(!file->isInsufficient());

    env.free[QStringLiteral("/vol/temp")] = uint64{PARTSIZE} - 1;
    dq.checkDiskspace();
    QVERIFY(file->isInsufficient());

    env.free[QStringLiteral("/vol/temp")] = uint64{PARTSIZE};
    dq.checkDiskspace();
    QVERIFY(!file->isInsufficient());
}

// The write was tried first and the check ran after it failed. Two files on one volume
// must not both count the same free bytes either.
void tst_DownloadQueue::diskFloor_refusesTheWriteBeforeItHappens()
{
    DownloadQueue dq;
    DiskFloorEnv env(dq, 100 * kMiB);

    uint8 h1[16]{}, h2[16]{};
    h1[0] = 0x85; h2[0] = 0x86;
    auto* a = createTestPartFile(h1, QStringLiteral("a.bin"));
    auto* b = createTestPartFile(h2, QStringLiteral("b.bin"));
    a->setTmpPath(QStringLiteral("/vol/temp"));
    b->setTmpPath(QStringLiteral("/vol/temp"));
    dq.addDownload(a);
    dq.addDownload(b);

    env.free[QStringLiteral("/vol/temp")] = 100 * kMiB + 1000;
    QVERIFY(dq.reserveForWrite(a, 600));
    QVERIFY2(!dq.reserveForWrite(b, 600), "the first write already spent that room");
    QVERIFY(dq.reserveForWrite(b, 400));
    QCOMPARE(env.probes, 1);                 // measured once, then budgeted

    thePrefs.setCheckDiskspace(false);
    QVERIFY(dq.reserveForWrite(b, 1'000'000));
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

    dq.removeSource(&peer);   // also clears its reqFile, which outlives the file
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
        pf->forgetAllSources();
    }

    // And an ordinary public High ID is untouched.
    auto publicPeer = makeHighId("81.2.3.4", 4668);
    QVERIFY(dq.checkAndAddSource(pf, publicPeer.get()));

    pf->forgetAllSources();
    // Forgotten sources outlive pf; don't leave them a dangling reqFile
    lowId->setReqFile(nullptr);
    publicPeer->setReqFile(nullptr);
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
// A root that only Kad nodes reported was taken as Verified: one lying node planted it,
// and every honest source reporting the real one was then dropped as a liar.
void tst_DownloadQueue::seedFromSearchResult_kadOnlyRootIsAVoteNotAFact()
{
    DownloadQueue dq;
    const uint8 raw[20] = {0xC3, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    const AICHHash root(raw);

    uint8 hash[16] = {46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    auto* pf = createTestPartFile(hash, QStringLiteral("kad_root.bin"));
    pf->setTmpPath(m_tempDir.path());
    dq.addDownload(pf);

    SearchFile result;
    result.setKadResult(true);
    result.setFileHash(hash);
    result.fileIdentifier().setAICHHash(root);
    result.addAICHVoter(Address::fromString(QStringLiteral("88.1.0.1")));
    result.addAICHVoter(Address::fromString(QStringLiteral("89.1.0.1")));

    dq.seedFromSearchResult(pf, result);
    QVERIFY2(!pf->fileIdentifier().hasAICHHash(), "two Kad nodes are not proof");
    QCOMPARE(pf->aichRecoveryHashSet().getStatus(), EAICHStatus::Untrusted);
    QVERIFY(pf->aichRecoveryHashSet().getMasterHash() == root);

    // Sources agreeing with it carry it over the line; then it is kept like a link's.
    for (int i = 0; i < 8; ++i)
        pf->voteAICHRoot(root, Address::fromString(QStringLiteral("90.%1.0.1").arg(i * 16)));
    QCOMPARE(pf->aichRecoveryHashSet().getStatus(), EAICHStatus::Trusted);
    QVERIFY(pf->fileIdentifier().hasAICHHash());
    QVERIFY(pf->fileIdentifier().getAICHHash() == root);

    // A server answer carrying the same root makes it a fact at once.
    uint8 hash2[16] = {47, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9};
    auto* pf2 = createTestPartFile(hash2, QStringLiteral("server_root.bin"));
    dq.addDownload(pf2);
    SearchFile vouched;
    vouched.setKadResult(true);
    vouched.setFileHash(hash2);
    vouched.fileIdentifier().setAICHHash(root);
    vouched.setAICHVouchedDirectly();
    dq.seedFromSearchResult(pf2, vouched);
    QCOMPARE(pf2->aichRecoveryHashSet().getStatus(), EAICHStatus::Verified);

    dq.deleteAll();
}

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
    // OnQueue and just asked, so PartFile::process() neither dials nor re-asks it and
    // puts no unrelated frames on the wire.
    source.setDownloadState(DownloadState::OnQueue);
    pf->addSource(&source);
    source.setReqFile(pf);
    source.setLastAskedTime();

    QCoreApplication::processEvents();
    peer->readAll();                          // drain anything the setup produced
    source.markSendIPPending();

    runOneSecondOfTicks(dq);

    QVERIFY2(waitForBytes(peer, 22), "a source on a Ready/Empty file must be flushed");
    const QByteArray raw = peer->readAll();
    QCOMPARE(static_cast<uint8>(raw[0]), static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(static_cast<uint8>(raw[5]), static_cast<uint8>(OP_CHANGE_CLIENT_IP));
    QVERIFY(!source.sendIPPending());

    pf->forgetAllSources();
    source.setReqFile(nullptr);   // outlives pf
    source.setSocket(nullptr);
    peer->close();
    QCoreApplication::processEvents();
    dq.deleteAll();
}

// A queued source we hold a connection to (a peer we upload to, say) can be re-asked by
// neither UDP nor a dial. Left alone, the remote drops us from its queue after an hour.
// MFC SetActive / OnConnectionState: the download-time clock (FT_DL_ACTIVE_TIME) runs
// while the file is running and we are on a network. It had no writer at all.
void tst_DownloadQueue::activeTime_runsOnlyWhileConnectedAndNotPaused()
{
    UdpSourceEnv env;
    ServerConnect* const savedSC = theApp.serverConnect;
    theApp.serverConnect = &env.sc;
    const auto restore = qScopeGuard([&] { theApp.serverConnect = savedSC; });

    DownloadQueue dq;
    uint8 hash[16];
    std::memset(hash, 0x54, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("active_time.bin"));
    dq.addDownload(pf);

    // offline: the clock does not start
    pf->setActive(true);
    dq.process();
    QTest::qWait(1100);
    QCOMPARE(pf->dlActiveTime(), uint32{0});

    env.sc.m_connected = true;
    dq.process();                       // the connect edge starts it
    QTest::qWait(2100);
    QVERIFY(pf->dlActiveTime() >= 1);

    pf->pauseFile();
    const uint32 atPause = pf->dlActiveTime();
    QTest::qWait(1100);
    QCOMPARE(pf->dlActiveTime(), atPause);

    env.sc.m_connected = false;
}

// A NAT may answer a UDP reask from another port than the one advertised; MFC then
// takes the only source on that address (DownloadQueue.cpp:1072-1095).
void tst_DownloadQueue::downloadClientByIP_UDP_ignoresThePortOnAUniqueAddress()
{
    DownloadQueue dq;
    uint8 hash[16];
    std::memset(hash, 0x53, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("udp_lookup.bin"));
    dq.addDownload(pf);

    const Address addrA = Address::fromString(QStringLiteral("10.9.8.7"));
    const Address addrB = Address::fromString(QStringLiteral("10.9.8.6"));
    UpDownClient a, b1, b2;
    a.setUserAddress(addrA);
    a.setUDPPort(5000);
    b1.setUserAddress(addrB);
    b1.setUDPPort(6000);
    b2.setUserAddress(addrB);
    b2.setUDPPort(6001);
    for (UpDownClient* c : {&a, &b1, &b2})
        pf->addSource(c);

    QCOMPARE(dq.downloadClientByIP_UDP(addrA, 5000, false), &a);
    QCOMPARE(dq.downloadClientByIP_UDP(addrA, 5999, true), &a);        // remapped port
    QVERIFY(dq.downloadClientByIP_UDP(addrA, 5999, false) == nullptr);

    bool multiple = false;
    QCOMPARE(dq.downloadClientByIP_UDP(addrB, 6001, true, &multiple), &b2);
    QVERIFY(dq.downloadClientByIP_UDP(addrB, 6999, true, &multiple) == nullptr);
    QVERIFY(multiple);                                                 // ambiguous

    for (UpDownClient* c : {&a, &b1, &b2})
        pf->removeSource(c);
}

void tst_DownloadQueue::process_reasksAQueuedSourceOverItsOpenConnection()
{
    QFETCH(bool, due);
    IPv6AdvertiseGuard guard;

    DownloadQueue dq;
    uint8 hash[16];
    std::memset(hash, 0x52, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("reask_in_place.bin"));
    dq.addDownload(pf);

    QTcpServer server;
    UpDownClient source;
    QTcpSocket* peer = wireLoopbackSocket(server, source);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(source, 0x32);
    source.setDownloadState(DownloadState::OnQueue);
    pf->addSource(&source);
    source.setReqFile(pf);
    if (!due)
        source.setLastAskedTime();

    QCoreApplication::processEvents();
    peer->readAll();

    runOneSecondOfTicks(dq);

    if (due) {
        QCOMPARE(source.downloadState(), DownloadState::Connected);
        QVERIFY2(waitForBytes(peer, 22), "the file request goes out on the open socket");
    } else {
        QCOMPARE(source.downloadState(), DownloadState::OnQueue);
        QVERIFY(!waitForBytes(peer, 1));
    }

    source.setDownloadState(DownloadState::None);
    pf->forgetAllSources();
    source.setReqFile(nullptr);   // outlives pf
    source.setSocket(nullptr);
    peer->close();
    QCoreApplication::processEvents();
    dq.deleteAll();
}

void tst_DownloadQueue::process_reasksAQueuedSourceOverItsOpenConnection_data()
{
    QTest::addColumn<bool>("due");
    QTest::newRow("reask due") << true;
    QTest::newRow("just asked") << false;
}

// Without OP_CANCELTRANSFER the peer keeps sending into a file that no longer flushes.
void tst_DownloadQueue::pauseFile_cancelsARunningTransfer()
{
    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    const auto unhook = qScopeGuard([] { theApp.downloadQueue = nullptr; });

    uint8 hash[16];
    std::memset(hash, 0x53, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("pause_cancel.bin"));
    dq.addDownload(pf);

    QTcpServer server;
    UpDownClient source;
    QTcpSocket* peer = wireLoopbackSocket(server, source);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(source, 0x33);
    pf->addSource(&source);
    source.setReqFile(pf);
    source.setDownloadState(DownloadState::Downloading);
    QCoreApplication::processEvents();
    peer->readAll();

    pf->pauseFile();

    QCOMPARE(source.downloadState(), DownloadState::OnQueue);
    QVERIFY(pf->hasSource(&source));
    QVERIFY2(waitForBytes(peer, 6), "the peer is told to stop");
    const QByteArray raw = peer->readAll();
    QCOMPARE(static_cast<uint8>(raw[5]), static_cast<uint8>(OP_CANCELTRANSFER));

    source.setDownloadState(DownloadState::None);
    pf->forgetAllSources();
    source.setReqFile(nullptr);
    source.setSocket(nullptr);
    peer->close();
    QCoreApplication::processEvents();
    dq.deleteAll();
}

// A stopped file accepts no new sources; keeping the old ones strands them — never
// re-asked, never offered to the other files they are wanted for, never reaped.
void tst_DownloadQueue::stopFile_letsTheSourcesGo()
{
    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    const auto unhook = qScopeGuard([] { theApp.downloadQueue = nullptr; });

    uint8 hashA[16] = {60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {61, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("stop-a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("stop-b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);

    const auto makeSource = [&](UpDownClient& client, const char* ip, PartFile* file) {
        const Address addr = Address::fromString(QString::fromLatin1(ip));
        client.setUserAddress(addr);
        client.setUserIDHybrid(addr.toUint32());
        client.setUserPort(4662);
        QVERIFY(dq.checkAndAddKnownSource(file, &client, true));
    };

    UpDownClient onlyA;                      // nowhere else to go
    makeSource(onlyA, "81.2.69.180", fileA);
    onlyA.setDownloadState(DownloadState::OnQueue);

    UpDownClient alsoB;                      // source of A, wanted for B too
    makeSource(alsoB, "81.2.69.181", fileA);
    alsoB.setDownloadState(DownloadState::Downloading);
    QVERIFY(!dq.checkAndAddKnownSource(fileB, &alsoB, true));
    alsoB.setDownloadState(DownloadState::OnQueue);
    QCOMPARE(fileB->a4afSourceCount(), 1);

    UpDownClient ofB;                        // source of B, A is its alternative
    makeSource(ofB, "81.2.69.182", fileB);
    ofB.setDownloadState(DownloadState::Downloading);
    QVERIFY(!dq.checkAndAddKnownSource(fileA, &ofB, true));
    QCOMPARE(fileA->a4afSourceCount(), 1);

    fileA->stopFile();

    QVERIFY(fileA->isStopped());
    QCOMPARE(fileA->sourceCount(), 0);
    QCOMPARE(fileA->a4afSourceCount(), 0);
    QVERIFY(onlyA.reqFile() == nullptr);
    QCOMPARE(onlyA.downloadState(), DownloadState::None);
    QCOMPARE(alsoB.reqFile(), fileB);        // swapped, not dropped
    QVERIFY(fileB->hasSource(&alsoB));
    QCOMPARE(ofB.reqFile(), fileB);
    QCOMPARE(ofB.otherRequestCount(), std::size_t{0});

    for (UpDownClient* client : {&onlyA, &alsoB, &ofB}) {
        client->setDownloadState(DownloadState::None);
        client->removeFileFromOtherLists(fileA);
        client->removeFileFromOtherLists(fileB);
        dq.removeSource(client);
    }
    dq.deleteAll();
}

void tst_DownloadQueue::stopPausedFile_firesAfterAnIdleHour()
{
    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    const auto unhook = qScopeGuard([] { theApp.downloadQueue = nullptr; });

    uint8 hashA[16] = {62, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* paused = createTestPartFile(hashA, QStringLiteral("idle-paused.bin"));
    auto* noSpace = createTestPartFile(hashB, QStringLiteral("idle-nospace.bin"));
    dq.addDownload(paused);
    dq.addDownload(noSpace);

    UpDownClient a, b;
    a.setDownloadState(DownloadState::OnQueue);
    b.setDownloadState(DownloadState::OnQueue);
    paused->addSource(&a);
    a.setReqFile(paused);
    noSpace->addSource(&b);
    b.setReqFile(noSpace);

    paused->pauseFile();
    noSpace->pauseFile(true);
    const time_t now = std::time(nullptr);

    // Not yet an hour.
    paused->setLastPausePurge(now - HR2S(1) + 60);
    noSpace->setLastPausePurge(now - HR2S(1) + 60);
    paused->stopPausedFile();
    noSpace->stopPausedFile();
    QVERIFY(paused->hasSource(&a));
    QVERIFY(noSpace->hasSource(&b));
    QVERIFY(!paused->isStopped());

    paused->setLastPausePurge(now - HR2S(1) - 1);
    noSpace->setLastPausePurge(now - HR2S(1) - 1);
    paused->stopPausedFile();
    noSpace->stopPausedFile();
    QVERIFY(paused->isStopped());
    QCOMPARE(paused->sourceCount(), 0);
    QVERIFY(a.reqFile() == nullptr);
    // Out of space: sources go, the state the auto-resume waits on stays.
    QCOMPARE(noSpace->sourceCount(), 0);
    QVERIFY(noSpace->isInsufficient());
    QVERIFY(!noSpace->isStopped());

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

    pf->forgetAllSources();
    source.setReqFile(nullptr);   // outlives pf
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

    dq.removeSource(&client);   // also clears its reqFile, which outlives the file
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
    dq.removeSource(&client);   // also clears its reqFile, which outlives the file
    dq.deleteAll();
}

// ---------------------------------------------------------------------------
// Source requests to the connected server — MFC DownloadQueue.cpp:1279-1395
// ---------------------------------------------------------------------------

namespace {

/// The file hashes of a frame of back-to-back OP_GETSOURCES packets, in order.
/// Empty if the frame is not exactly that.
std::vector<QByteArray> hashesInSourceRequestFrame(const QByteArray& frame)
{
    std::vector<QByteArray> hashes;
    qsizetype pos = 0;
    while (pos + 6 <= frame.size()) {
        uint32 len = 0;   // opcode + payload
        std::memcpy(&len, frame.constData() + pos + 1, 4);
        if (uint8(frame[pos]) != OP_EDONKEYPROT || uint8(frame[pos + 5]) != OP_GETSOURCES
            || len != 1 + 20 || pos + 5 + len > frame.size())
            return {};
        hashes.push_back(frame.mid(pos + 6, 16));
        pos += 5 + len;
    }
    return pos == frame.size() ? hashes : std::vector<QByteArray>{};
}

} // namespace

// Each download used to send its own OP_GETSOURCES from PartFile::process(): N files,
// N packets in one tick after login, which is what a server's request credits are
// there to punish. Now they queue, 15 to a frame, the rest after the frame interval.
void tst_DownloadQueue::localSrcRequests_goOutFifteenToAFrame()
{
    DownloadQueue dq;

    std::vector<PartFile*> files;
    for (uint8 i = 0; i < 20; ++i) {
        const uint8 hash[16] = {70, i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        auto* pf = createTestPartFile(hash, QStringLiteral("src-req-%1.bin").arg(i));
        pf->setStatus(PartFileStatus::Empty);
        dq.addDownload(pf);
        pf->setLocalSrcReqQueued(true);
        dq.sendLocalSrcRequest(pf);
        dq.sendLocalSrcRequest(pf);   // queued once, however often it asks
        files.push_back(pf);
    }
    QCOMPARE(dq.m_localServerReqQueue.size(), std::size_t{20});

    const QByteArray first = dq.buildLocalRequestFrame(1000);
    QCOMPARE(hashesInSourceRequestFrame(first).size(), std::size_t{15});
    QCOMPARE(dq.m_localServerReqQueue.size(), std::size_t{5});

    const QByteArray second = dq.buildLocalRequestFrame(2000);
    QCOMPARE(hashesInSourceRequestFrame(second).size(), std::size_t{5});
    QVERIFY(dq.m_localServerReqQueue.empty());
    QVERIFY(dq.buildLocalRequestFrame(3000).isEmpty());

    // Every file was asked for exactly once, and knows when.
    int stampedFirst = 0;
    for (const PartFile* pf : files) {
        QVERIFY(!pf->isLocalSrcReqQueued());
        QVERIFY(pf->lastSearchTimeServer() == 1000 || pf->lastSearchTimeServer() == 2000);
        stampedFirst += pf->lastSearchTimeServer() == 1000;
    }
    QCOMPARE(stampedFirst, 15);

    dq.deleteAll();
}

// Longest-waiting first; among equals the higher download priority. A file that is no
// longer downloadable drops out without being asked for.
void tst_DownloadQueue::ratioLimitedDownload_followsTheUploadLimit()
{
    const auto kb = [](uint64 v) { return v * 1024; };
    const auto limit = [&](uint64 downKB, uint64 upKB) {
        return DownloadQueue::ratioLimitedDownload(kb(downKB), kb(upKB));
    };

    // 20 KB/s up or more, or no upload limit: the download limit stands.
    QCOMPARE(limit(500, 20), kb(500));
    QCOMPARE(limit(500, 0), kb(500));
    QCOMPARE(limit(0, 20), uint64{0});
    QCOMPARE(limit(0, 0), uint64{0});

    // Below that: 3x under 4, 4x under 10, 5x under 20.
    QCOMPARE(limit(500, 3), kb(9));
    QCOMPARE(limit(500, 4), kb(16));
    QCOMPARE(limit(500, 9), kb(36));
    QCOMPARE(limit(500, 10), kb(50));
    QCOMPARE(limit(500, 19), kb(95));

    // Never above what the user set, and "unlimited" down is capped all the same.
    QCOMPARE(limit(10, 9), kb(10));
    QCOMPARE(limit(0, 9), kb(36));
}

void tst_DownloadQueue::remoteQueueFull_survivesTheRankAndEndsWithASlot()
{
    // A full queue is reported as "full, rank 0" — the rank must not wipe the flag.
    UpDownClient client;
    client.setRemoteQueueFull(true);
    client.setRemoteQueueRank(0);
    QVERIFY(client.remoteQueueFull());
    client.udpReaskACK(0);
    QVERIFY(client.remoteQueueFull());

    // A slot ends it, for an eMule peer.
    client.setEmuleProtocol(true);
    client.setRemoteQueueFull(true);
    client.setRemoteQueueRank(7);
    client.setDownloadState(DownloadState::Downloading);
    QVERIFY(!client.remoteQueueFull());
    QCOMPARE(client.remoteQueueRank(), 0u);
    client.setDownloadState(DownloadState::None);
}

void tst_DownloadQueue::remoteQueueFull_sourceIsPurgedNearTheCap()
{
    DownloadQueue dq;
    theApp.downloadQueue = &dq;
    const uint16 oldMax = thePrefs.maxSourcesPerFile();
    const auto restore = qScopeGuard([oldMax] {
        thePrefs.setMaxSourcesPerFile(oldMax);
        theApp.downloadQueue = nullptr;
    });

    uint8 hash[16];
    std::memset(hash, 0x52, sizeof(hash));
    auto* pf = createTestPartFile(hash, QStringLiteral("queue_full_purge.bin"));
    dq.addDownload(pf);

    UpDownClient full;
    full.setDownloadState(DownloadState::OnQueue);
    full.setRemoteQueueFull(true);
    pf->addSource(&full);

    // Far from the cap: kept.
    thePrefs.setMaxSourcesPerFile(100);
    pf->process(0, 0);
    QVERIFY(pf->hasSource(&full));

    // At 4/5 of the cap: dropped.
    thePrefs.setMaxSourcesPerFile(1);
    full.setDownloadState(DownloadState::OnQueue);
    pf->process(0, 0);
    QVERIFY(!pf->hasSource(&full));

    pf->forgetAllSources();
    full.setReqFile(nullptr);
    dq.deleteAll();
}

void tst_DownloadQueue::localSrcRequests_orderAndDropouts()
{
    DownloadQueue dq;
    theApp.downloadQueue = &dq;   // pauseFile() unqueues through it
    const auto unhook = qScopeGuard([] { theApp.downloadQueue = nullptr; });

    const auto add = [&](uint8 id, uint8 priority, uint64 lastAsked) {
        const uint8 hash[16] = {71, id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        auto* pf = createTestPartFile(hash, QStringLiteral("src-order-%1.bin").arg(id), priority);
        pf->setStatus(PartFileStatus::Empty);
        dq.addDownload(pf);
        pf->setLastSearchTimeServer(lastAsked);
        pf->setLocalSrcReqQueued(true);
        dq.sendLocalSrcRequest(pf);
        return pf;
    };

    PartFile* recent     = add(1, kPrHigh, 5000);
    PartFile* oldLow     = add(2, kPrLow, 100);
    PartFile* oldHigh    = add(3, kPrHigh, 100);
    PartFile* neverAsked = add(4, kPrLow, 0);
    PartFile* paused     = add(5, kPrHigh, 0);
    PartFile* removed    = add(6, kPrHigh, 0);

    // Pausing takes the file off the queue; so does removing it (it is freed next).
    paused->pauseFile();
    QVERIFY(!paused->isLocalSrcReqQueued());
    dq.removeLocalServerRequest(removed);
    QVERIFY(!removed->isLocalSrcReqQueued());
    QCOMPARE(dq.m_localServerReqQueue.size(), std::size_t{4});

    const auto hashes = hashesInSourceRequestFrame(dq.buildLocalRequestFrame(9000));
    QCOMPARE(hashes.size(), std::size_t{4});
    const auto hashOf = [](const PartFile* pf) {
        return QByteArray(reinterpret_cast<const char*>(pf->fileHash()), 16);
    };
    QCOMPARE(hashes[0], hashOf(neverAsked));
    QCOMPARE(hashes[1], hashOf(oldHigh));
    QCOMPARE(hashes[2], hashOf(oldLow));
    QCOMPARE(hashes[3], hashOf(recent));

    // A file that stopped being downloadable while queued is dropped unasked.
    recent->setLocalSrcReqQueued(true);
    dq.sendLocalSrcRequest(recent);
    recent->setStatus(PartFileStatus::Completing);
    QVERIFY(dq.buildLocalRequestFrame(9500).isEmpty());
    QVERIFY(!recent->isLocalSrcReqQueued());
    QCOMPARE(recent->lastSearchTimeServer(), uint64{9000});
    recent->setStatus(PartFileStatus::Empty);

    dq.deleteAll();
}

// A new server session starts from scratch: whatever was queued for the old server is
// forgotten and every running download may ask at once — the old server's 15 minutes
// say nothing about this one (MFC ServerSocket.cpp:353).
void tst_DownloadQueue::localSrcRequests_resetOnNewServerSession()
{
    DownloadQueue dq;

    const uint8 hashA[16] = {72, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const uint8 hashB[16] = {72, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* queued = createTestPartFile(hashA, QStringLiteral("src-reset-a.bin"));
    auto* asked  = createTestPartFile(hashB, QStringLiteral("src-reset-b.bin"));
    for (auto* pf : {queued, asked}) {
        pf->setStatus(PartFileStatus::Empty);
        dq.addDownload(pf);
    }
    queued->setLocalSrcReqQueued(true);
    dq.sendLocalSrcRequest(queued);
    asked->setLastSearchTimeServer(123456);
    dq.m_nextTcpSrcReq = UINT64_MAX;

    dq.resetLocalServerRequests();

    QVERIFY(dq.m_localServerReqQueue.empty());
    QCOMPARE(dq.m_nextTcpSrcReq, uint64{0});
    QVERIFY(!queued->isLocalSrcReqQueued());
    QCOMPARE(asked->lastSearchTimeServer(), uint64{0});

    dq.deleteAll();
}

// A second file naming a source we already have swaps it over only when it is idle
// and due. The caller excludes Connected alone; the rest is SwapToAnotherFile's own
// gate (MFC DownloadClient.cpp:1552-1578). Without it a source in the middle of a
// transfer was moved to the other file, state still Downloading, no cancel sent.
void tst_DownloadQueue::checkAndAddKnownSource_swapsOnlyAnIdleDueSource()
{
    DownloadQueue dq;

    uint8 hashA[16] = {40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8 hashB[16] = {41, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("swap-a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("swap-b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);

    const auto makeSource = [&](UpDownClient& client, const char* ip) {
        const Address addr = Address::fromString(QString::fromLatin1(ip));
        client.setUserAddress(addr);
        client.setUserIDHybrid(addr.toUint32());
        client.setUserPort(4662);
        QVERIFY(dq.checkAndAddKnownSource(fileA, &client, true));
    };

    UpDownClient downloading;
    makeSource(downloading, "81.2.69.170");
    downloading.setDownloadState(DownloadState::Downloading);
    QVERIFY(!dq.checkAndAddKnownSource(fileB, &downloading, true));
    QCOMPARE(downloading.reqFile(), fileA);
    QCOMPARE(downloading.downloadState(), DownloadState::Downloading);

    UpDownClient justAsked;
    makeSource(justAsked, "81.2.69.171");
    justAsked.setDownloadState(DownloadState::OnQueue);
    justAsked.setLastAskedTime();
    QVERIFY(!dq.checkAndAddKnownSource(fileB, &justAsked, true));
    QCOMPARE(justAsked.reqFile(), fileA);

    // Queued and never asked: free to go, and it arrives stateless.
    UpDownClient idle;
    makeSource(idle, "81.2.69.172");
    idle.setDownloadState(DownloadState::OnQueue);
    QVERIFY(!dq.checkAndAddKnownSource(fileB, &idle, true));
    QCOMPARE(idle.reqFile(), fileB);
    QCOMPARE(idle.downloadState(), DownloadState::None);

    for (UpDownClient* client : {&downloading, &justAsked, &idle}) {
        client->setDownloadState(DownloadState::None);
        client->removeFileFromOtherLists(fileA);
        client->removeFileFromOtherLists(fileB);
        dq.removeSource(client);
    }
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
void tst_DownloadQueue::a4af_sourceRowsFollowTheAvailableOnes()
{
    // MFC lists a source that asks another file under this one too
    // (UNAVAILABLE_SOURCE, srchybrid/DownloadListCtrl.cpp:263). Only the count was sent.
    DownloadQueue dq;
    uint8 hashA[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    uint8 hashB[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    auto* fileA = createTestPartFile(hashA, QStringLiteral("a4af_rows_a.bin"));
    auto* fileB = createTestPartFile(hashB, QStringLiteral("a4af_rows_b.bin"));
    dq.addDownload(fileA);
    dq.addDownload(fileB);

    {
        auto client = makePreHelloSource(fileA, "81.2.69.165", 4662);
        QCOMPARE(dq.checkAndAddSource(fileA, client.get()), client.get());
        QVERIFY(client->addRequestForAnotherFile(fileB));

        const QCborArray own = Ipc::downloadSourcesToCbor(*fileA);
        QCOMPARE(own.size(), 1);
        QVERIFY(!own.at(0).toMap().contains(QStringLiteral("a4af")));
        QVERIFY(own.at(0).toMap().value(QStringLiteral("hasOtherRequests")).toBool());

        const QCborArray other = Ipc::downloadSourcesToCbor(*fileB);
        QCOMPARE(other.size(), 1);
        const QCborMap row = other.at(0).toMap();
        QVERIFY(row.value(QStringLiteral("a4af")).toBool());
        QCOMPARE(row.value(QStringLiteral("reqFileName")).toString(),
                 QStringLiteral("a4af_rows_a.bin"));
        QVERIFY(!row.value(QStringLiteral("noNeededHere")).toBool());

        fileA->removeSource(client.get());
    }
    dq.deleteAll();
}

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

namespace {

std::unique_ptr<UpDownClient> highIdSource(const QString& ip, uint16 port)
{
    auto c = std::make_unique<UpDownClient>();
    const Address addr = Address::fromString(ip);
    c->setUserAddress(addr);
    c->setUserIDHybrid(addr.toUint32());
    c->setUserPort(port);
    return c;
}

} // namespace

// A source is filed under what it was when it was added; the hello then changes all of it.
void tst_DownloadQueue::sourceIndex_followsAnIdentityThatChanges()
{
    DownloadQueue dq;
    uint8 hash[16] = {51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};
    auto* pf = createTestPartFile(hash, QStringLiteral("index_rekey.bin"));
    dq.addDownload(pf);
    {
        auto src = highIdSource(QStringLiteral("81.2.70.10"), 4662);
        QCOMPARE(dq.checkAndAddSource(pf, src.get()), src.get());
        QVERIFY(pf->hasSource(src.get()));

        // what a hello brings: a hash, another port, a Kad port, a server
        uint8 userHash[16] = {0x9A, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        src->setUserHash(userHash);
        src->setUserPort(5000);
        src->setKadPort(5001);

        auto byHash = highIdSource(QStringLiteral("81.2.70.99"), 4999);   // moved, same hash
        byHash->setUserHash(userHash);
        QCOMPARE(pf->findSourceLike(byHash.get()), src.get());
        QVERIFY(!dq.checkAndAddSource(pf, byHash.get()));

        auto byNewPort = highIdSource(QStringLiteral("81.2.70.10"), 5000);
        QVERIFY(!dq.checkAndAddSource(pf, byNewPort.get()));

        auto byKadPort = highIdSource(QStringLiteral("81.2.70.10"), 6000);
        byKadPort->setKadPort(5001);
        QVERIFY(!dq.checkAndAddSource(pf, byKadPort.get()));

        // the port it had before the hello is somebody else now — as the scan says too
        auto byOldPort = highIdSource(QStringLiteral("81.2.70.10"), 4662);
        QCOMPARE(pf->findSourceLikeByScan(byOldPort.get()), nullptr);
        QCOMPARE(dq.checkAndAddSource(pf, byOldPort.get()), byOldPort.get());
        QCOMPARE(pf->sourceCount(), 2);

        // an ID change: LowID on a server
        src->setUserIDHybrid(0x00001234u);
        src->setServerAddress(Address::fromString(QStringLiteral("90.1.2.3")));
        src->setServerPort(4661);
        auto byServer = std::make_unique<UpDownClient>();
        byServer->setUserIDHybrid(0x00001234u);
        byServer->setServerAddress(Address::fromString(QStringLiteral("90.1.2.3")));
        byServer->setServerPort(4661);
        byServer->setUserPort(7000);
        QCOMPARE(pf->findSourceLike(byServer.get()), src.get());
        QCOMPARE(pf->findSourceLikeByScan(byServer.get()), src.get());

        pf->removeSource(src.get());
        pf->removeSource(byOldPort.get());
    }
    dq.deleteAll();
}

void tst_DownloadQueue::sourceIndex_forgetsARemovedSource()
{
    DownloadQueue dq;
    uint8 hash[16] = {52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
    auto* pf = createTestPartFile(hash, QStringLiteral("index_remove.bin"));
    dq.addDownload(pf);
    {
        auto src = highIdSource(QStringLiteral("81.2.71.10"), 4662);
        src->setKadPort(4672);
        QCOMPARE(dq.checkAndAddSource(pf, src.get()), src.get());
        // changed after it was filed, then removed: no key of either state may linger
        src->setUserPort(4700);
        pf->removeSource(src.get());
        QVERIFY(!pf->hasSource(src.get()));
        QCOMPARE(pf->sourceCount(), 0);

        for (const uint16 port : {uint16{4662}, uint16{4700}}) {
            auto twin = highIdSource(QStringLiteral("81.2.71.10"), port);
            twin->setKadPort(4672);
            QCOMPARE(pf->findSourceLike(twin.get()), nullptr);
        }
        auto twin = highIdSource(QStringLiteral("81.2.71.10"), 4700);
        QCOMPARE(dq.checkAndAddSource(pf, twin.get()), twin.get());
        pf->removeSource(twin.get());
    }
    dq.deleteAll();
}

// It used to compare a candidate with every source of every file.
void tst_DownloadQueue::sourceIndex_keepsADuplicateTestOffTheFullScan()
{
    constexpr int kFiles = 20;
    constexpr int kSourcesPerFile = 300;
    constexpr int kCandidates = 200;

    DownloadQueue::setVerifySourceIndex(false);   // the check is the full scan
    const auto verifyAgain = qScopeGuard([] { DownloadQueue::setVerifySourceIndex(true); });
    const auto savedMax = thePrefs.maxSourcesPerFile();
    thePrefs.setMaxSourcesPerFile(10'000);
    const auto restoreMax = qScopeGuard([&] { thePrefs.setMaxSourcesPerFile(savedMax); });

    DownloadQueue dq;
    std::vector<PartFile*> files;
    std::vector<std::unique_ptr<UpDownClient>> owned;
    const auto ipOf = [](int file, int n) {
        return QStringLiteral("81.%1.%2.%3").arg(10 + file).arg(n / 250).arg(1 + n % 250);
    };
    for (int f = 0; f < kFiles; ++f) {
        uint8 hash[16] = {53, static_cast<uint8>(f), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9};
        auto* pf = createTestPartFile(hash, QStringLiteral("index_scale_%1.bin").arg(f));
        dq.addDownload(pf);
        files.push_back(pf);
        for (int n = 0; n < kSourcesPerFile; ++n) {
            owned.push_back(highIdSource(ipOf(f, n), 4662));
            owned.back()->setReqFile(pf);
            pf->addSource(owned.back().get());
        }
    }

    const uint64 before = PartFile::sourceCompareCount();
    int duplicates = 0;
    for (int i = 0; i < kCandidates; ++i) {
        // every other one is already a source of some file
        const bool known = i % 2 == 0;
        auto candidate = known ? highIdSource(ipOf(i % kFiles, i), 4662)
                               : highIdSource(QStringLiteral("82.1.%1.%2").arg(i / 250).arg(1 + i % 250), 4662);
        UpDownClient* added = dq.checkAndAddSource(files.back(), candidate.get());
        if (known) {
            QVERIFY(!added);
            ++duplicates;
        } else {
            QCOMPARE(added, candidate.get());
            owned.push_back(std::move(candidate));
        }
    }
    QCOMPARE(duplicates, kCandidates / 2);
    const uint64 compares = PartFile::sourceCompareCount() - before;
    QVERIFY2(compares <= 2 * kCandidates,
             qPrintable(QStringLiteral("%1 compares for %2 candidates over %3 sources")
                            .arg(compares).arg(kCandidates).arg(kFiles * kSourcesPerFile)));

    for (auto* pf : files)
        pf->forgetAllSources();
    for (auto& c : owned)
        c->setReqFile(nullptr);
    owned.clear();
    dq.deleteAll();
}

// MFC CClientDetailPage (srchybrid/ClientDetailDialog.cpp:86-175) shows "?" without
// credits and "-" for the queue score of an idle client; the GUI needs to be told.
void tst_DownloadQueue::clientDetails_carryWhatMfcsDialogNeeds()
{
    UpDownClient client(4662, /*userId=*/0x04030201, 0, 0, nullptr, true);
    client.setDownDatarate(1234);

    QCborMap m = Ipc::toCborDetailed(client, theApp);
    QCOMPARE(m.value(QStringLiteral("creditsKnown")).toBool(), false);
    QCOMPARE(m.value(QStringLiteral("uploadIdle")).toBool(), true);
    QCOMPARE(m.value(QStringLiteral("identification")).toString(), QStringLiteral("none"));
    QCOMPARE(m.value(QStringLiteral("obfuscation")).toString(), QStringLiteral("none"));
    // "datarate" is what the peer sends us — MFC's "Average Upload rate" row
    QCOMPARE(m.value(QStringLiteral("datarate")).toInteger(), 1234);
    QCOMPARE(m.value(QStringLiteral("upDatarate")).toInteger(), 0);

    const uint8 userHash[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    ClientCredits credits(userHash);
    client.setCredits(&credits);
    client.setUploadState(UploadState::OnUploadQueue);
    m = Ipc::toCborDetailed(client, theApp);
    QCOMPARE(m.value(QStringLiteral("creditsKnown")).toBool(), true);
    QCOMPARE(m.value(QStringLiteral("uploadIdle")).toBool(), false);
    // no crypto set up here: MFC reads that as "not supported"
    QCOMPARE(m.value(QStringLiteral("identification")).toString(), QStringLiteral("none"));
    client.setCredits(nullptr);
}

QTEST_GUILESS_MAIN(tst_DownloadQueue)
#include "tst_DownloadQueue.moc"
