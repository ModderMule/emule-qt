/// @file tst_UploadQueueStore.cpp
/// @brief Tests for transfer/UploadQueueStore — Upload Queue Storage (UQS).

#include "TestFixtures.h"
#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "transfer/UploadQueue.h"
#include "transfer/UploadQueueStore.h"
#include "utils/Opcodes.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

class tst_UploadQueueStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // -- codec ---------------------------------------------------------------
    void codec_roundTripsEveryField();
    void codec_missingFileReadsEmpty();
    void codec_truncatedFileYieldsNothing();
    void codec_garbageVersionRejected();
    void codec_emptyRecordListRoundTrips();

    // -- record eligibility --------------------------------------------------
    void record_needsHashFileAndPort();
    void record_lowIdNeedsServerOrKadRoute();

    // -- expiry --------------------------------------------------------------
    void expiry_wholeFileDroppedPastOneHour();
    void expiry_perRecordUsesOfflineGap();
    void expiry_clockGoingBackwardsDoesNotRevive();

    // -- save policy ---------------------------------------------------------
    void save_refusedBeforeLoad();
    void save_cappedAtOneHundredBestScores();
    void save_skipsClientsWithoutAHash();

    // -- load / injection ----------------------------------------------------
    void load_injectsOntoWaitingListWithoutDialling();
    void load_preservesRelativeQueueOrder();
    void load_skipsUnsharedFile();
    void load_skipsFilteredAddress();
    void load_skipsClientAlreadyKnown();
    void load_marksLoadedEvenWhenFileMissing();

    // -- obfuscation ---------------------------------------------------------
    void obfuscation_cryptFlagsAndKadVersionSurvive();

    // -- connectivity gate ---------------------------------------------------
    void process_doesNotLoadOrSaveWhileOffline();

private:
    QString storePath() const { return QDir(m_dir.path()).filePath(QStringLiteral("uploadqueue.met")); }

    QTemporaryDir m_dir;

    // Live scaffolding — score() and the purge both reach through theApp.
    std::unique_ptr<KnownFileList>     m_knownFiles;
    std::unique_ptr<SharedFileList>    m_sharedFiles;
    std::unique_ptr<ClientList>        m_clientList;
    std::unique_ptr<ClientCreditsList> m_credits;
    KnownFile* m_file = nullptr;
    std::array<uint8, 16> m_fileHash{};

    SharedFileList* m_savedShared  = nullptr;
    ClientList*        m_savedClients = nullptr;
    IPFilter*          m_savedFilter  = nullptr;
    ClientCreditsList* m_savedCredits = nullptr;
    bool               m_savedPref    = true;
    bool               m_savedSecIdent = true;
};

namespace {

/// A record that passes isRestorable(), so a test only has to override what it cares about.
QueuedClientRecord makeRecord(uint8 seed, const std::array<uint8, 16>& fileHash)
{
    QueuedClientRecord rec;
    std::memset(rec.userHash.data(), seed, rec.userHash.size());
    rec.reqUpFileId  = fileHash;
    rec.userIPv4     = Address::fromString(QStringLiteral("77.13.%1.9").arg(seed));
    rec.userPort     = static_cast<uint16>(4662 + seed);
    rec.userIDHybrid = 0x4D0D0009u + seed;
    rec.serverIP     = 0x0100007Fu;
    rec.serverPort   = 4661;
    rec.kadPort      = static_cast<uint16>(4672 + seed);
    rec.udpPort      = static_cast<uint16>(4672 + seed);
    rec.userName     = QStringLiteral("peer-%1").arg(seed);
    return rec;
}

} // namespace

void tst_UploadQueueStore::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
}

void tst_UploadQueueStore::init()
{
    m_savedShared   = theApp.sharedFileList;
    m_savedClients  = theApp.clientList;
    m_savedFilter   = theApp.ipFilter;
    m_savedCredits  = theApp.clientCredits;
    m_savedPref     = thePrefs.rememberUploadQueue();
    m_savedSecIdent = thePrefs.useSecureIdent();
    thePrefs.setRememberUploadQueue(true);
    // Keeps ClientCreditsList's ctor off the RSA path: it would generate a cryptkey.dat in
    // the user's real config dir. Nothing here needs secure ident, and with keySize 0
    // secureWaitStartTime() returns the unsecure value restoreWaitStartTime() writes.
    thePrefs.setUseSecureIdent(false);

    m_knownFiles  = std::make_unique<KnownFileList>();
    m_sharedFiles = std::make_unique<SharedFileList>(m_knownFiles.get());
    m_clientList  = std::make_unique<ClientList>();
    // Without a credits list the restored clients get no ClientCredits, their wait start
    // stays 0 and every score() is 0 — i.e. exactly the "queued but never promoted" state
    // this feature exists to avoid. Production always has one by the time we load.
    m_credits     = std::make_unique<ClientCreditsList>();

    std::memset(m_fileHash.data(), 0x5A, m_fileHash.size());
    m_file = new KnownFile();
    m_file->setFileHash(m_fileHash.data());
    m_file->setFileName(QStringLiteral("queued.bin"));
    QVERIFY(m_sharedFiles->safeAddKFile(m_file));

    theApp.sharedFileList = m_sharedFiles.get();
    theApp.clientList     = m_clientList.get();
    theApp.clientCredits  = m_credits.get();
    theApp.ipFilter       = nullptr;

    QFile::remove(storePath());
}

void tst_UploadQueueStore::cleanup()
{
    theApp.sharedFileList = m_savedShared;
    theApp.clientList     = m_savedClients;
    theApp.clientCredits  = m_savedCredits;
    theApp.ipFilter       = m_savedFilter;
    thePrefs.setRememberUploadQueue(m_savedPref);
    thePrefs.setUseSecureIdent(m_savedSecIdent);

    m_credits.reset();
    m_clientList.reset();
    m_sharedFiles.reset();
    m_knownFiles.reset();
    m_file = nullptr;
}

// ===========================================================================
// Codec
// ===========================================================================

void tst_UploadQueueStore::codec_roundTripsEveryField()
{
    QueuedClientRecord rec = makeRecord(3, m_fileHash);
    rec.userIPv6                = Address::fromString(QStringLiteral("2606:4700::1234"));
    rec.connectOptions          = 0x0F;
    rec.kadVersion              = KADEMLIA_VERSION8_49b;
    rec.udpVer                  = 4;
    rec.waitedSeconds           = 3412;
    rec.sinceLastRequestSeconds = 91;
    rec.askedCount              = 7;
    rec.clientVersion           = 0x3C;
    rec.emuleVersion            = 0x44;
    rec.compatibleClient        = 0;

    QVERIFY(UploadQueueFile::write(storePath(), {rec}, 1'700'000'000u));

    const auto contents = UploadQueueFile::read(storePath());
    QCOMPARE(contents.savedAtUnix, 1'700'000'000u);
    QCOMPARE(contents.records.size(), size_t{1});

    const QueuedClientRecord& got = contents.records.front();
    QCOMPARE(std::memcmp(got.userHash.data(), rec.userHash.data(), 16), 0);
    QCOMPARE(std::memcmp(got.reqUpFileId.data(), rec.reqUpFileId.data(), 16), 0);
    QCOMPARE(got.userIDHybrid, rec.userIDHybrid);
    QCOMPARE(got.userIPv4, rec.userIPv4);
    QCOMPARE(got.userIPv6, rec.userIPv6);
    QCOMPARE(got.userPort, rec.userPort);
    QCOMPARE(got.serverIP, rec.serverIP);
    QCOMPARE(got.serverPort, rec.serverPort);
    QCOMPARE(got.kadPort, rec.kadPort);
    QCOMPARE(got.udpPort, rec.udpPort);
    QCOMPARE(got.connectOptions, rec.connectOptions);
    QCOMPARE(got.kadVersion, rec.kadVersion);
    QCOMPARE(got.udpVer, rec.udpVer);
    QCOMPARE(got.waitedSeconds, rec.waitedSeconds);
    QCOMPARE(got.sinceLastRequestSeconds, rec.sinceLastRequestSeconds);
    QCOMPARE(got.askedCount, rec.askedCount);
    QCOMPARE(got.userName, rec.userName);
    QCOMPARE(got.clientVersion, rec.clientVersion);
    QCOMPARE(got.emuleVersion, rec.emuleVersion);
    QCOMPARE(got.compatibleClient, rec.compatibleClient);
}

void tst_UploadQueueStore::codec_missingFileReadsEmpty()
{
    const auto contents = UploadQueueFile::read(
        QDir(m_dir.path()).filePath(QStringLiteral("nope.met")));
    QVERIFY(contents.records.empty());
    QCOMPARE(contents.savedAtUnix, 0u);
}

// A half-written file must yield nothing at all rather than the records that happened to
// land before the cut — a partially decoded queue is worse than no queue.
void tst_UploadQueueStore::codec_truncatedFileYieldsNothing()
{
    std::vector<QueuedClientRecord> records;
    for (uint8 i = 0; i < 5; ++i)
        records.push_back(makeRecord(static_cast<uint8>(i + 1), m_fileHash));
    QVERIFY(UploadQueueFile::write(storePath(), records, 1'700'000'000u));

    QFile f(storePath());
    QVERIFY(f.open(QIODevice::ReadWrite));
    const qint64 full = f.size();
    QVERIFY(full > 40);
    QVERIFY(f.resize(full / 2));      // cut mid-record
    f.close();

    QVERIFY(UploadQueueFile::read(storePath()).records.empty());
}

void tst_UploadQueueStore::codec_garbageVersionRejected()
{
    QVERIFY(UploadQueueFile::write(storePath(), {makeRecord(1, m_fileHash)}, 1'700'000'000u));

    QFile f(storePath());
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(0));
    const char bogus = 0x7F;
    QCOMPARE(f.write(&bogus, 1), qint64(1));
    f.close();

    QVERIFY(UploadQueueFile::read(storePath()).records.empty());
}

void tst_UploadQueueStore::codec_emptyRecordListRoundTrips()
{
    QVERIFY(UploadQueueFile::write(storePath(), {}, 1'700'000'000u));
    const auto contents = UploadQueueFile::read(storePath());
    QVERIFY(contents.records.empty());
    QCOMPARE(contents.savedAtUnix, 1'700'000'000u);
}

// ===========================================================================
// Record eligibility
// ===========================================================================

void tst_UploadQueueStore::record_needsHashFileAndPort()
{
    QVERIFY(makeRecord(1, m_fileHash).isRestorable());

    QueuedClientRecord noHash = makeRecord(1, m_fileHash);
    noHash.userHash = {};
    QVERIFY2(!noHash.isRestorable(),
             "without a hash there is no credits key, no dedup key and no obfuscation key");

    QueuedClientRecord noFile = makeRecord(1, m_fileHash);
    noFile.reqUpFileId = {};
    QVERIFY(!noFile.isRestorable());

    QueuedClientRecord noPort = makeRecord(1, m_fileHash);
    noPort.userPort = 0;
    QVERIFY(!noPort.isRestorable());
}

void tst_UploadQueueStore::record_lowIdNeedsServerOrKadRoute()
{
    QueuedClientRecord lowId = makeRecord(1, m_fileHash);
    lowId.userIPv4     = Address();
    lowId.userIPv6     = Address();
    lowId.userIDHybrid = 0x00FFFFFEu;      // LowID

    lowId.serverIP = 0; lowId.serverPort = 0; lowId.kadPort = 0;
    QVERIFY2(!lowId.isRestorable(), "a LowID with no callback route is unreachable");

    lowId.kadPort = 4672;
    QVERIFY(lowId.isRestorable());

    lowId.kadPort = 0; lowId.serverIP = 0x0100007Fu; lowId.serverPort = 4661;
    QVERIFY(lowId.isRestorable());
}

// ===========================================================================
// Expiry
// ===========================================================================

void tst_UploadQueueStore::expiry_wholeFileDroppedPastOneHour()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QVERIFY(UploadQueueFile::write(storePath(), {makeRecord(1, m_fileHash)},
                                   now - (MAX_PURGEQUEUETIME / 1000) - 60));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 0);
    QCOMPARE(queue.waitingUserCount(), 0);
}

// The offline gap counts towards "time since we last heard from them": a record that was
// already 50 minutes stale and then sat on disk for 20 more is past the 1 h purge line.
void tst_UploadQueueStore::expiry_perRecordUsesOfflineGap()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    QueuedClientRecord fresh = makeRecord(1, m_fileHash);
    fresh.sinceLastRequestSeconds = 60;
    QueuedClientRecord stale = makeRecord(2, m_fileHash);
    stale.sinceLastRequestSeconds = 50 * 60;

    QVERIFY(UploadQueueFile::write(storePath(), {fresh, stale}, now - 20 * 60));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 1);
    QCOMPARE(queue.waitingUserCount(), 1);
}

void tst_UploadQueueStore::expiry_clockGoingBackwardsDoesNotRevive()
{
    // Stamped in the future — the offline gap must clamp to 0, not wrap to something huge.
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QVERIFY(UploadQueueFile::write(storePath(), {makeRecord(1, m_fileHash)}, now + 3600));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 1);
}

// ===========================================================================
// Save policy
// ===========================================================================

// The trap this pins: saving before the one-shot load would write our still-empty queue
// over the stored one, destroying exactly the data we were about to restore.
void tst_UploadQueueStore::save_refusedBeforeLoad()
{
    QVERIFY(UploadQueueFile::write(storePath(), {makeRecord(1, m_fileHash)}, 
                                   static_cast<uint32>(QDateTime::currentSecsSinceEpoch())));
    const qint64 sizeBefore = QFileInfo(storePath()).size();

    UploadQueue queue;
    UploadQueueStore store;
    QVERIFY(!store.hasLoaded());
    QVERIFY2(!store.saveNow(&queue, storePath()), "saveNow must refuse before the load");
    QCOMPARE(QFileInfo(storePath()).size(), sizeBefore);
    QCOMPARE(UploadQueueFile::read(storePath()).records.size(), size_t{1});

    // After the load it is allowed, even though the queue is empty.
    QCOMPARE(store.loadAndInject(&queue, storePath()), 1);
    QVERIFY(store.hasLoaded());
    QVERIFY(store.saveNow(&queue, storePath()));
}

void tst_UploadQueueStore::save_cappedAtOneHundredBestScores()
{
    UploadQueue queue;
    UploadQueueStore store;

    // 150 waiters with strictly increasing wait times, so score order is known.
    std::vector<std::unique_ptr<UpDownClient>>  clients;
    std::vector<std::unique_ptr<ClientCredits>> credits;
    for (int i = 0; i < 150; ++i) {
        auto c = std::make_unique<UpDownClient>();
        std::array<uint8, 16> hash{};
        hash[0] = static_cast<uint8>(i & 0xFF);
        hash[1] = static_cast<uint8>(i >> 8);
        hash[2] = 0xAB;
        c->setUserHash(hash.data());
        c->setUserAddress(Address::fromNetworkOrder(0x0A000001u + static_cast<uint32>(i)));
        c->setUserPort(static_cast<uint16>(4662 + i));
        c->setUserIDHybrid(0x0A141E28u + static_cast<uint32>(i));

        auto cr = std::make_unique<ClientCredits>(hash.data());
        c->setCredits(cr.get());
        c->setUploadFileID(m_file);
        c->setReqUpFileId(m_fileHash.data());
        // Higher i => waited longer => higher score.
        c->restoreWaitStartTime(static_cast<uint32>(1000 * (i + 1)));

        QVERIFY(queue.addRestoredClient(c.get()));
        credits.push_back(std::move(cr));
        clients.push_back(std::move(c));
    }
    QCOMPARE(queue.waitingUserCount(), 150);

    QCOMPARE(store.loadAndInject(&queue, storePath()), 0);   // unblocks saving
    QVERIFY(store.saveNow(&queue, storePath()));

    const auto contents = UploadQueueFile::read(storePath());
    QCOMPARE(contents.records.size(), static_cast<size_t>(kMaxSavedQueueClients));

    // Best first, and the 100 kept are the 100 longest waits (i = 50..149).
    for (size_t i = 1; i < contents.records.size(); ++i) {
        QVERIFY2(contents.records[i - 1].waitedSeconds >= contents.records[i].waitedSeconds,
                 "records must be written best-score first");
    }
    QVERIFY(contents.records.front().waitedSeconds >= 149);
}

void tst_UploadQueueStore::save_skipsClientsWithoutAHash()
{
    UploadQueue queue;
    UploadQueueStore store;

    UpDownClient hashless;
    hashless.setUserAddress(Address::fromString(QStringLiteral("77.13.1.9")));
    hashless.setUserPort(4662);
    hashless.setUploadFileID(m_file);
    hashless.setReqUpFileId(m_fileHash.data());
    QVERIFY(queue.addRestoredClient(&hashless));
    QCOMPARE(queue.waitingUserCount(), 1);

    QCOMPARE(store.loadAndInject(&queue, storePath()), 0);
    QVERIFY(store.saveNow(&queue, storePath()));
    QVERIFY2(UploadQueueFile::read(storePath()).records.empty(),
             "a hashless waiter cannot be restored, so it must not be stored");
}

// ===========================================================================
// Load / injection
// ===========================================================================

// The core requirement: restored peers wait their turn, they are not dialled on startup.
void tst_UploadQueueStore::load_injectsOntoWaitingListWithoutDialling()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    std::vector<QueuedClientRecord> records;
    for (uint8 i = 1; i <= 5; ++i) {
        QueuedClientRecord rec = makeRecord(i, m_fileHash);
        rec.waitedSeconds = 100u * i;
        records.push_back(rec);
    }
    QVERIFY(UploadQueueFile::write(storePath(), records, now));

    UploadQueue queue;
    QSignalSpy addedSpy(&queue, &UploadQueue::clientAddedToQueue);
    QSignalSpy startedSpy(&queue, &UploadQueue::uploadStarted);

    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 5);

    QCOMPARE(queue.waitingUserCount(), 5);
    QCOMPARE(queue.uploadQueueLength(), 0);
    QCOMPARE(addedSpy.count(), 5);
    QVERIFY2(startedSpy.count() == 0,
             "restoring must not promote anyone to an upload slot");

    // And they are registered with ClientList, or the 1 s reaper would delete them.
    QCOMPARE(m_clientList->clientCount(), 5);
}

void tst_UploadQueueStore::load_preservesRelativeQueueOrder()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    std::vector<QueuedClientRecord> records;
    for (uint8 i = 1; i <= 4; ++i) {
        QueuedClientRecord rec = makeRecord(i, m_fileHash);
        rec.waitedSeconds = 4000u - 500u * i;    // written best-first
        records.push_back(rec);
    }
    QVERIFY(UploadQueueFile::write(storePath(), records, now));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 4);

    std::vector<uint64> scores;
    queue.forEachWaiting([&](UpDownClient* c) { scores.push_back(c->score(false)); });
    QCOMPARE(scores.size(), size_t{4});
    for (size_t i = 1; i < scores.size(); ++i) {
        QVERIFY2(scores[i - 1] > scores[i],
                 "the peer that had waited longest must still outrank the others");
    }
}

void tst_UploadQueueStore::load_skipsUnsharedFile()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QueuedClientRecord rec = makeRecord(1, m_fileHash);
    std::memset(rec.reqUpFileId.data(), 0x11, rec.reqUpFileId.size());   // not shared
    QVERIFY(UploadQueueFile::write(storePath(), {rec}, now));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 0);
    QCOMPARE(queue.waitingUserCount(), 0);
}

void tst_UploadQueueStore::load_skipsFilteredAddress()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QueuedClientRecord rec = makeRecord(1, m_fileHash);
    rec.userIDHybrid = rec.userIPv4.toUint32();      // HighID: no callback route
    QVERIFY(UploadQueueFile::write(storePath(), {rec}, now));

    IPFilter filter;
    const uint32 host = rec.userIPv4.toUint32();   // addIPRange takes host byte order
    filter.addIPRange(host, host, 0, "uqs-test");
    theApp.ipFilter = &filter;
    QVERIFY2(filter.isFiltered(rec.userIPv4), "fixture check: the address must be filtered");

    UploadQueue queue;
    UploadQueueStore store;
    QVERIFY2(store.loadAndInject(&queue, storePath()) == 0,
             "the stored file is untrusted input and must be re-vetted on load");

    theApp.ipFilter = nullptr;
}

void tst_UploadQueueStore::load_skipsClientAlreadyKnown()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QueuedClientRecord rec = makeRecord(1, m_fileHash);
    QVERIFY(UploadQueueFile::write(storePath(), {rec}, now));

    // The same peer is already live this session.
    UpDownClient existing;
    existing.setUserHash(rec.userHash.data());
    existing.setUserAddress(rec.userIPv4);
    existing.setUserPort(rec.userPort);
    m_clientList->addClient(&existing);

    UploadQueue queue;
    UploadQueueStore store;
    QVERIFY2(store.loadAndInject(&queue, storePath()) == 0,
             "ClientList::addClient only checks pointer identity, so the dedup must be explicit");
    QCOMPARE(queue.waitingUserCount(), 0);

    m_clientList->removeClient(&existing);
}

void tst_UploadQueueStore::load_marksLoadedEvenWhenFileMissing()
{
    UploadQueue queue;
    UploadQueueStore store;
    QVERIFY(!store.hasLoaded());
    QCOMPARE(store.loadAndInject(&queue, storePath()), 0);   // no file at all
    QVERIFY2(store.hasLoaded(), "a fresh install still counts as loaded, which unblocks saving");
}

// ===========================================================================
// Obfuscation
// ===========================================================================

// Regression pin for the two traps that would silently disable obfuscation for restored
// peers: setConnectOptions() ANDs each bit with its bool arguments, and kadVersion is what
// shouldReceiveCryptUDPPackets() tests before obfuscating outgoing UDP.
void tst_UploadQueueStore::obfuscation_cryptFlagsAndKadVersionSurvive()
{
    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QueuedClientRecord rec = makeRecord(1, m_fileHash);
    rec.connectOptions = 0x01 | 0x02 | 0x04 | 0x08;   // supports+requests+requires+callback
    rec.kadVersion     = KADEMLIA_VERSION8_49b;
    rec.udpVer         = 4;
    QVERIFY(UploadQueueFile::write(storePath(), {rec}, now));

    UploadQueue queue;
    UploadQueueStore store;
    QCOMPARE(store.loadAndInject(&queue, storePath()), 1);

    UpDownClient* restored = nullptr;
    queue.forEachWaiting([&](UpDownClient* c) { restored = c; });
    QVERIFY(restored);

    QVERIFY2(restored->supportsCryptLayer(), "crypt bits must survive setConnectOptions");
    QVERIFY(restored->requestsCryptLayer());
    QVERIFY(restored->requiresCryptLayer());
    QVERIFY2(restored->supportsDirectUDPCallback(),
             "callback=true must be passed, or firewalled peers become unreachable");
    QCOMPARE(restored->kadVersion(), uint8(KADEMLIA_VERSION8_49b));
    QCOMPARE(restored->udpVer(), uint8(4));
    QVERIFY2(restored->shouldReceiveCryptUDPPackets(),
             "without kadVersion we would send plaintext UDP to a peer expecting obfuscation");
    QVERIFY2(restored->hasValidHash(),
             "the user hash is the RC4 key seed for an obfuscated outgoing connect");
}

// ===========================================================================
// Connectivity gate
// ===========================================================================

// theApp.isConnected() is false here (no ServerConnect, no Kad), which is the whole point:
// while offline we must neither restore nor overwrite the stored queue.
void tst_UploadQueueStore::process_doesNotLoadOrSaveWhileOffline()
{
    QVERIFY(!theApp.isConnected());

    const uint32 now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    QVERIFY(UploadQueueFile::write(storePath(), {makeRecord(1, m_fileHash)}, now));
    const qint64 sizeBefore = QFileInfo(storePath()).size();

    UploadQueue queue;
    UploadQueueStore store;
    for (int i = 0; i < 5; ++i)
        store.process(&queue, storePath());

    QVERIFY(!store.hasLoaded());
    QCOMPARE(queue.waitingUserCount(), 0);
    QCOMPARE(QFileInfo(storePath()).size(), sizeBefore);
    QCOMPARE(UploadQueueFile::read(storePath()).records.size(), size_t{1});
}

QTEST_MAIN(tst_UploadQueueStore)
#include "tst_UploadQueueStore.moc"
