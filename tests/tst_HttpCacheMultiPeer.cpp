/// @file tst_HttpCacheMultiPeer.cpp
/// @brief HTTP Cache with three real peers on the wire.
///
/// Everything else in the HTTP Cache suite tests a piece in isolation: the codec
/// without a socket, the publisher without an upload queue, the client without a
/// peer. What decides whether any of it ever runs is HttpCacheManager, and its
/// inputs are the upload queue and the capabilities peers advertised during the
/// handshake — neither of which can be faked convincingly.
///
/// So this stands up the real uploader — ListenSocket, UploadQueue, disk IO —
/// and connects three mock ed2k peers to it over loopback, each advertising
/// MODMISC_HTTPCACHE in its hello. A fake cache server takes the POST and hands
/// the ciphertext back on GET, so a peer can act on the offer it was given and
/// prove the bytes are the part.
///
/// Three is the smallest interesting number: "Upload Saved" only becomes non-zero
/// at the *second* peer served from one upload, which is the whole economic
/// argument for the feature.
///
/// Note the ceiling — UploadQueue allows at most three clients per IP address
/// (UploadQueue.cpp:521 and the tracked-client gate below it), and every mock
/// here is 127.0.0.1. A fourth peer would be refused the queue, so the cases that
/// need a fresh offer target instead have a peer re-declare its part status, the
/// way a real client does when what it holds changes.

#include "FakeCacheServer.h"
#include "MockPeerSocket.h"
#include "TestHelpers.h"
#include "UploadPipelineFixture.h"

#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "crypto/AesCbc.h"
#include "files/KnownFile.h"
#include "httpcache/HttpCacheManager.h"
#include "httpcache/HttpCacheOffer.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>

#include <array>
#include <functional>
#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

namespace {

/// Small enough to be instant, large enough to look like a real block request.
constexpr uint64 kBlock = 10240;

/// The last part of the 20 MB test file is a 1,515,520 byte tail — deliberately
/// not a whole part, which is what the publisher refuses to touch.
constexpr uint32 kTailPart = 2;

} // namespace

class tst_HttpCacheMultiPeer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void onePublishServesThreePeers();
    void offeredChunkDecryptsToThePart();
    void belowMinClientsNothingIsPublished();
    void peerHoldingThePartIsNotCounted();
    void peerWithoutTheCapabilityIsIgnored();
    void twoPartsPublishOneTickAtATime();
    void shortTailPartIsNeverPublished();
    void repeatedCorruptReportsStopTheOffer();
    void serverErrorPausesFurtherPublishesButNotOffers();

    void cleanupTestCase();

private:
    // Private helpers after the test slots, per the house style.

    /// Bring one mock peer all the way to "granted an upload slot" and keep it.
    /// Uses QVERIFY, so a handshake that stalls fails the calling test.
    void joinPeer(bool httpCacheCapable = true, const std::vector<uint32>& partsHeld = {});

    /// Re-send the peer's part availability. A real client does this whenever what
    /// it holds changes; here it is also the only way to produce a peer that has
    /// not yet been told about a chunk without exceeding the per-IP queue limit.
    void redeclareParts(int index, const std::vector<uint32>& partsHeld);

    /// Ask for three blocks inside one part — the request is what seeds the whole
    /// candidate scan, so nothing is ever published speculatively.
    void seedPart(int index, uint32 part);

    /// One block in @p firstPart, two in @p secondPart: a single OP_REQUESTPARTS
    /// carries three ranges, so one packet can put two parts in play at once.
    void seedParts(int index, uint32 firstPart, uint32 secondPart);

    [[nodiscard]] MockPeerSocket& peer(int index) const { return *m_peers.at(static_cast<std::size_t>(index)); }

    /// Every offer this peer was sent, decoded.
    [[nodiscard]] std::vector<HttpCacheOffer> offersOf(int index) const;

    void reportResult(int index, const HttpCacheOffer& offer, HttpCacheResult result);

    /// One scan of the upload queue.
    void tick() const { QMetaObject::invokeMethod(m_cache, "process"); }

    /// Scan repeatedly until the predicate holds. Ticking in a loop rather than a
    /// fixed number of times keeps the test off the clock: a publish is a real
    /// HTTP exchange, and the queue re-promotes a client at its own pace.
    [[nodiscard]] bool tickUntil(const std::function<bool()>& done, int timeoutMs = 30000) const;

    /// Keep scanning for a while, for the cases whose point is that nothing happens.
    void tickQuietly(int rounds = 4) const;

    [[nodiscard]] static std::unique_ptr<Packet> buildHello(const std::array<uint8, 16>& userHash,
                                                            uint16 port, bool httpCacheCapable);
    [[nodiscard]] static std::unique_ptr<Packet> buildEmuleInfo();
    [[nodiscard]] static std::unique_ptr<Packet> buildSetReqFileId(const uint8* hash);
    [[nodiscard]] std::unique_ptr<Packet> buildRequestFileName(
        const uint8* hash, const std::vector<uint32>& partsHeld) const;
    [[nodiscard]] static std::unique_ptr<Packet> buildStartUploadReq(const uint8* hash);
    [[nodiscard]] static std::unique_ptr<Packet> buildRequestParts(
        const uint8* hash, uint64 s0, uint64 e0, uint64 s1, uint64 e1, uint64 s2, uint64 e2);

    UploadPipelineFixture m_pipe;

    FakeCacheServer* m_server = nullptr;
    HttpCacheManager* m_cache = nullptr;

    std::vector<std::unique_ptr<MockPeerSocket>> m_peers;

    /// Counts every peer the whole run has produced, not just this case's. The
    /// uploader remembers a client by user hash and by ip:port, so identities have
    /// to stay unique across cases or a new peer reads as an old one reconnecting.
    int m_peerSerial = 0;

    QString m_filePath;
    std::array<uint8, 16> m_fileHash{};
    uint64 m_fileSize = 0;
    uint16 m_partCount = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::initTestCase()
{
    m_pipe.setup(this);

    m_filePath = projectDataDir() + QStringLiteral("/incoming/eMuleQt-testfile-20MB.bin");
    m_pipe.registerSharedFile(m_filePath, m_fileHash, m_fileSize);

    m_partCount = static_cast<uint16>((m_fileSize + PARTSIZE - 1) / PARTSIZE);

    // Two whole parts and a tail: the multi-part cases need the first, the
    // tail-part case needs the second.
    QCOMPARE(m_partCount, 3);
    QVERIFY(m_fileSize > 2 * PARTSIZE);
    QVERIFY(m_fileSize < 3 * PARTSIZE);

    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowUpload(true);
    thePrefs.setHttpCacheApiKey(QStringLiteral("multi-peer-test-key"));
    thePrefs.setHttpCacheChunkTtlSeconds(21600);

    // The publish is deliberately rate limited in production — the point of the
    // feature is to spend less upstream, not more. At the default derived rate a
    // 9.28 MB part would take ~38 s, so take the throttle out of the measurement.
    thePrefs.setHttpCachePublishRateKBs(200000);
}

void tst_HttpCacheMultiPeer::init()
{
    m_server = new FakeCacheServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost));
    thePrefs.setHttpCacheBaseUrl(m_server->baseUrl());

    thePrefs.setHttpCacheMinClients(2);
    thePrefs.setHttpCacheMaxConcurrentPublishes(1);

    // A fresh manager per case, so entries, counters and any standing backoff all
    // start empty. Never start()ed: its 5 s timer would race every assertion here,
    // and process() is driven a tick at a time instead.
    m_cache = new HttpCacheManager(this);
    theApp.httpCache = m_cache;
}

void tst_HttpCacheMultiPeer::cleanup()
{
    for (auto& p : m_peers) {
        if (p)
            p->abort();
    }
    m_peers.clear();

    // Let the uploader notice the peers are gone before the next case counts them.
    QElapsedTimer clock;
    clock.start();
    while ((m_pipe.uploadQueue->uploadQueueLength() > 0
            || m_pipe.uploadQueue->waitingUserCount() > 0)
           && clock.elapsed() < 10000) {
        QTest::qWait(100);
    }

    // Every client that has held an upload slot stays on the per-address tracking
    // list for two hours, and three of them from one address is the limit. Without
    // this the second case's peers would be refused the queue outright.
    m_pipe.clientList->removeAllTrackedClients();

    theApp.httpCache = nullptr;
    delete m_cache;
    m_cache = nullptr;

    m_server->close();
    delete m_server;
    m_server = nullptr;
}

void tst_HttpCacheMultiPeer::cleanupTestCase()
{
    m_pipe.teardown();
}

// ---------------------------------------------------------------------------
// One chunk, three peers
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::onePublishServesThreePeers()
{
    joinPeer();
    joinPeer();
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());

    for (int i = 0; i < 3; ++i)
        seedPart(i, 0);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));

    // One upload, three peers served. That is the entire point of the feature.
    QCOMPARE(m_server->uploadCount(), 1);

    for (int i = 0; i < 3; ++i) {
        QTRY_VERIFY_WITH_TIMEOUT(offersOf(i).size() == 1, 5000);

        const HttpCacheOffer offer = offersOf(i).front();
        QCOMPARE(offer.partIndex, 0u);
        QCOMPARE(offer.plainLength, PARTSIZE);
        QCOMPARE(offer.cipherLength, AesCbcEncryptor::cipherLengthFor(PARTSIZE));
        QCOMPARE(offer.key.size(), qsizetype{kAesKeySize});
        QCOMPARE(offer.iv.size(), qsizetype{kAesIvSize});
        QCOMPARE(offer.cipherSha256.size(), qsizetype{32});
        QVERIFY(std::memcmp(offer.fileHash.data(), m_fileHash.data(), 16) == 0);

        // Every peer is pointed at the same blob under the same key — a publish
        // per peer would be the feature doing nothing at all.
        QCOMPARE(offer.url, offersOf(0).front().url);
        QCOMPARE(offer.key, offersOf(0).front().key);
        QCOMPARE(offer.iv, offersOf(0).front().iv);

        // The offer replaces the ed2k slot: the peer is told we have nothing more
        // of what it asked for, so the freed upstream goes to somebody else.
        QVERIFY2(peer(i).hasOpcode(OP_OUTOFPARTREQS, OP_EDONKEYPROT),
                 qPrintable(QStringLiteral("peer %1 kept its slot").arg(i)));
    }

    QCOMPARE(m_cache->sessionChunksPublished(), 1u);
    QCOMPARE(m_cache->sessionBytesPublished(), AesCbcEncryptor::cipherLengthFor(PARTSIZE));

    // The first peer offered a chunk is the one whose demand paid for the upload;
    // the two after it are bytes we never had to send again.
    QCOMPARE(m_cache->sessionBytesSaved(), 2 * PARTSIZE);

    // offerToQueue() runs every tick for the entry's whole life. Without the
    // offeredTo set this would turn into a packet flood and inflate the saving.
    tickQuietly();
    QCOMPARE(m_server->uploadCount(), 1);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(peer(i).countOpcode(OP_HTTPCACHE, OP_EMULEPROT), 1);
}

void tst_HttpCacheMultiPeer::offeredChunkDecryptsToThePart()
{
    joinPeer();
    joinPeer();
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());

    for (int i = 0; i < 3; ++i)
        seedPart(i, 0);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(0).size() == 1, 5000);

    const HttpCacheOffer offer = offersOf(0).front();

    // Act on the offer exactly as a downloader would: fetch the url the peer was
    // given, off the server it was pointed at.
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(QNetworkRequest(QUrl(offer.url)));
    QSignalSpy done(reply, &QNetworkReply::finished);
    QVERIFY(done.wait(20000));
    QCOMPARE(reply->error(), QNetworkReply::NoError);

    const QByteArray cipher = reply->readAll();
    reply->deleteLater();

    QCOMPARE(static_cast<uint64>(cipher.size()), offer.cipherLength);
    QCOMPARE(QCryptographicHash::hash(cipher, QCryptographicHash::Sha256), offer.cipherSha256);

    bool ok = false;
    const QByteArray plain = aesDecrypt(cipher, offer.key, offer.iv, &ok);
    QVERIFY2(ok, "the offered key and IV did not decrypt the chunk");
    QCOMPARE(static_cast<uint64>(plain.size()), PARTSIZE);

    QFile source(m_filePath);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QVERIFY(source.seek(static_cast<qint64>(offer.partIndex) * static_cast<qint64>(PARTSIZE)));
    const QByteArray expected = source.read(static_cast<qint64>(PARTSIZE));
    QCOMPARE(expected.size(), qsizetype{PARTSIZE});

    // Comparing digests rather than the buffers keeps a mismatch readable.
    QCOMPARE(QCryptographicHash::hash(plain, QCryptographicHash::Sha256),
             QCryptographicHash::hash(expected, QCryptographicHash::Sha256));

    // A good outcome must not retire the entry.
    reportResult(0, offer, HttpCacheResult::Ok);
    tickQuietly(2);
    QCOMPARE(m_server->uploadCount(), 1);
}

// ---------------------------------------------------------------------------
// Who counts towards a publish
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::belowMinClientsNothingIsPublished()
{
    thePrefs.setHttpCacheMinClients(3);

    joinPeer();
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());

    seedPart(0, 0);
    seedPart(1, 0);

    tickQuietly();
    QCOMPARE(m_server->uploadCount(), 0);
    QVERIFY(offersOf(0).empty());
    QVERIFY(offersOf(1).empty());

    // The group is recomputed on every tick, so the third peer arriving is enough
    // to make the same part worth publishing.
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());
    seedPart(2, 0);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));
    QCOMPARE(m_server->uploadCount(), 1);

    for (int i = 0; i < 3; ++i)
        QTRY_VERIFY_WITH_TIMEOUT(offersOf(i).size() == 1, 5000);
}

void tst_HttpCacheMultiPeer::peerHoldingThePartIsNotCounted()
{
    thePrefs.setHttpCacheMinClients(3);

    joinPeer();
    joinPeer();
    joinPeer(true, {0});   // this one already has part 0
    QVERIFY(!QTest::currentTestFailed());

    seedPart(0, 0);
    seedPart(1, 0);

    // Three capable peers in the queue, but only two of them are missing the part.
    tickQuietly();
    QCOMPARE(m_server->uploadCount(), 0);

    thePrefs.setHttpCacheMinClients(2);
    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));

    QTRY_VERIFY_WITH_TIMEOUT(offersOf(0).size() == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(1).size() == 1, 5000);

    // Offering a peer a part it already has would be pure noise.
    tickQuietly(2);
    QVERIFY(offersOf(2).empty());
    QCOMPARE(m_cache->sessionBytesSaved(), PARTSIZE);
}

void tst_HttpCacheMultiPeer::peerWithoutTheCapabilityIsIgnored()
{
    thePrefs.setHttpCacheMinClients(3);

    joinPeer();
    joinPeer();
    joinPeer(/*httpCacheCapable*/ false);
    QVERIFY(!QTest::currentTestFailed());

    for (int i = 0; i < 3; ++i)
        seedPart(i, 0);

    // A peer that never advertised MODMISC_HTTPCACHE cannot be served this way, so
    // it cannot make up the numbers either.
    tickQuietly();
    QCOMPARE(m_server->uploadCount(), 0);

    thePrefs.setHttpCacheMinClients(2);
    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));

    QTRY_VERIFY_WITH_TIMEOUT(offersOf(0).size() == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(1).size() == 1, 5000);

    tickQuietly(2);
    QCOMPARE(peer(2).countOpcode(OP_HTTPCACHE, OP_EMULEPROT), 0);
}

// ---------------------------------------------------------------------------
// More than one part
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::twoPartsPublishOneTickAtATime()
{
    joinPeer();
    joinPeer();
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());

    // One packet puts both whole parts of the file in play.
    for (int i = 0; i < 3; ++i)
        seedParts(i, 0, 1);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));

    // Two candidates, one publish slot: the second part waits its turn rather than
    // doubling the upstream we just set out to save.
    QCOMPARE(m_server->uploadCount(), 1);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 2; }));
    QCOMPARE(m_server->uploadCount(), 2);

    for (int i = 0; i < 3; ++i) {
        QTRY_VERIFY_WITH_TIMEOUT(offersOf(i).size() == 2, 5000);

        auto offers = offersOf(i);
        std::sort(offers.begin(), offers.end(),
                  [](const auto& a, const auto& b) { return a.partIndex < b.partIndex; });

        QCOMPARE(offers[0].partIndex, 0u);
        QCOMPARE(offers[1].partIndex, 1u);
        QVERIFY2(offers[0].url != offers[1].url, "two parts were published as one chunk");
        QVERIFY2(offers[0].key != offers[1].key,
                 "a key was reused across chunks — the server could correlate them");
    }

    QCOMPARE(m_cache->sessionChunksPublished(), 2u);
    QCOMPARE(m_cache->sessionBytesSaved(), 4 * PARTSIZE);
}

void tst_HttpCacheMultiPeer::shortTailPartIsNeverPublished()
{
    joinPeer();
    joinPeer();
    joinPeer();
    QVERIFY(!QTest::currentTestFailed());

    for (int i = 0; i < 3; ++i)
        seedPart(i, kTailPart);

    // The file's last part is short, and it is the least-shared part there is.
    // Carving a special case for it would buy nothing, so it is skipped outright.
    tickQuietly(6);
    QCOMPARE(m_server->uploadCount(), 0);
    QCOMPARE(m_cache->sessionChunksPublished(), 0u);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(peer(i).countOpcode(OP_HTTPCACHE, OP_EMULEPROT), 0);
}

// ---------------------------------------------------------------------------
// What the peers report back
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::repeatedCorruptReportsStopTheOffer()
{
    joinPeer();
    joinPeer();
    joinPeer(true, {0});   // holds part 0, so it is not offered it
    QVERIFY(!QTest::currentTestFailed());

    seedPart(0, 0);
    seedPart(1, 0);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(0).size() == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(1).size() == 1, 5000);

    const HttpCacheOffer offer = offersOf(0).front();

    // Three verdicts against the blob itself. Peer 0 tried twice — a report is per
    // fetch, not per peer.
    reportResult(0, offer, HttpCacheResult::Corrupt);
    reportResult(0, offer, HttpCacheResult::Corrupt);
    reportResult(1, offer, HttpCacheResult::Corrupt);
    QTest::qWait(1000);

    // Peer 2 no longer holds part 0, so it is now a peer that has never been told
    // about that chunk — and must never be, because the chunk has been retired.
    redeclareParts(2, {});
    for (int i = 0; i < 3; ++i)
        seedParts(i, 0, 1);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 2; }));
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(2).size() == 1, 10000);
    tickQuietly(2);

    // The healthy part still reaches it; the retired one never does.
    QCOMPARE(offersOf(2).size(), std::size_t{1});
    QCOMPARE(offersOf(2).front().partIndex, 1u);

    // Retiring an entry must not delete it: the chunk may be serving other peers
    // perfectly well, and it is left to lapse at its TTL.
    QCOMPARE(m_server->uploadCount(), 2);
}

void tst_HttpCacheMultiPeer::serverErrorPausesFurtherPublishesButNotOffers()
{
    joinPeer();
    joinPeer();
    joinPeer(true, {1});   // holds part 1, so it is not offered it
    QVERIFY(!QTest::currentTestFailed());

    seedPart(0, 1);
    seedPart(1, 1);

    QVERIFY(tickUntil([this] { return m_cache->sessionChunksPublished() >= 1; }));
    QCOMPARE(m_server->uploadCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(offersOf(0).size() == 1, 5000);

    // The next upload is refused. A 500 will greet the chunk after it just the
    // same, so publishing has to stand down rather than re-POST 9.28 MB every tick.
    m_server->queueReply({.status = 500, .body = R"({"error":"cannot commit chunk"})"});

    for (int i = 0; i < 3; ++i)
        seedParts(i, 0, 1);

    QVERIFY(tickUntil([this] {
        return m_server->uploadCount() >= 2 && m_cache->activePublishCount() == 0;
    }));
    QCOMPARE(m_cache->sessionChunksPublished(), 1u);

    // A refusing POST endpoint says nothing about the blobs already sitting on
    // that server, so peers keep being pointed at them while publishing is paused.
    // Ticking rather than just waiting: the re-offer loop lives in the same scan
    // that the backoff is about to cut short, which is exactly the point.
    redeclareParts(2, {});
    QVERIFY(tickUntil([this] { return offersOf(2).size() == 1; }));
    QCOMPARE(offersOf(2).front().partIndex, 1u);

    tickQuietly(4);
    QCOMPARE(m_server->uploadCount(), 2);
    QCOMPARE(m_cache->sessionChunksPublished(), 1u);
}

// ---------------------------------------------------------------------------
// Peers
// ---------------------------------------------------------------------------

void tst_HttpCacheMultiPeer::joinPeer(bool httpCacheCapable, const std::vector<uint32>& partsHeld)
{
    const int serial = m_peerSerial++;

    std::array<uint8, 16> userHash{};
    for (std::size_t i = 0; i < userHash.size(); ++i)
        userHash[i] = static_cast<uint8>(0xA0 + i);
    userHash[0] = static_cast<uint8>(serial + 1);
    userHash[15] = static_cast<uint8>(0xFF - serial);

    auto socket = std::make_unique<MockPeerSocket>();
    socket->setObfuscationConfig(thePrefs.obfuscationConfig());
    socket->setConnectionEncryption(true, thePrefs.userHash().data(), false);
    socket->connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(socket->waitForConnected(5000));
    QTRY_VERIFY_WITH_TIMEOUT(socket->isEncryptionLayerReady(), 5000);

    socket->sendPacket(buildHello(userHash, static_cast<uint16>(4662 + serial),
                                  httpCacheCapable));
    QTRY_VERIFY_WITH_TIMEOUT(socket->hasOpcode(OP_HELLOANSWER), 5000);

    socket->sendPacket(buildEmuleInfo());
    QTRY_VERIFY_WITH_TIMEOUT(socket->hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    // SETREQFILEID first: it resets the part status, and the extended info that
    // follows on REQUESTFILENAME is what fills it in again.
    socket->sendPacket(buildSetReqFileId(m_fileHash.data()));
    socket->sendPacket(buildRequestFileName(m_fileHash.data(), partsHeld));
    QTRY_VERIFY_WITH_TIMEOUT(socket->hasOpcode(OP_FILESTATUS), 5000);
    QVERIFY2(!socket->hasOpcode(OP_FILEREQANSNOFIL),
             "the uploader could not match the extended part info");

    socket->sendPacket(buildStartUploadReq(m_fileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(socket->hasOpcode(OP_ACCEPTUPLOADREQ), 15000);

    m_peers.push_back(std::move(socket));
}

void tst_HttpCacheMultiPeer::redeclareParts(int index, const std::vector<uint32>& partsHeld)
{
    MockPeerSocket& target = peer(index);
    const int before = target.countOpcode(OP_REQFILENAMEANSWER, OP_EDONKEYPROT);

    // No SETREQFILEID this time: the file has not changed, so the new availability
    // simply replaces the old one.
    target.sendPacket(buildRequestFileName(m_fileHash.data(), partsHeld));
    QTRY_VERIFY_WITH_TIMEOUT(
        target.countOpcode(OP_REQFILENAMEANSWER, OP_EDONKEYPROT) > before, 5000);
}

void tst_HttpCacheMultiPeer::seedPart(int index, uint32 part)
{
    const uint64 base = static_cast<uint64>(part) * PARTSIZE;
    peer(index).sendPacket(buildRequestParts(m_fileHash.data(),
                                             base, base + kBlock,
                                             base + kBlock, base + 2 * kBlock,
                                             base + 2 * kBlock, base + 3 * kBlock));
}

void tst_HttpCacheMultiPeer::seedParts(int index, uint32 firstPart, uint32 secondPart)
{
    const uint64 a = static_cast<uint64>(firstPart) * PARTSIZE;
    const uint64 b = static_cast<uint64>(secondPart) * PARTSIZE;
    peer(index).sendPacket(buildRequestParts(m_fileHash.data(),
                                             a, a + kBlock,
                                             b, b + kBlock,
                                             b + kBlock, b + 2 * kBlock));
}

std::vector<HttpCacheOffer> tst_HttpCacheMultiPeer::offersOf(int index) const
{
    std::vector<HttpCacheOffer> offers;

    for (const QByteArray& payload : peer(index).payloadsFor(OP_HTTPCACHE, OP_EMULEPROT)) {
        const auto parsed = HttpCacheCodec::parse(
            reinterpret_cast<const uint8*>(payload.constData()),
            static_cast<uint32>(payload.size()));
        if (parsed.kind == HttpCacheCodec::Kind::Offer)
            offers.push_back(parsed.offer);
    }

    return offers;
}

void tst_HttpCacheMultiPeer::reportResult(int index, const HttpCacheOffer& offer,
                                          HttpCacheResult result)
{
    HttpCacheReport report;
    report.fileHash = offer.fileHash;
    report.partIndex = offer.partIndex;
    report.result = result;
    report.bytesFetched = result == HttpCacheResult::Ok ? offer.plainLength : 0;

    peer(index).sendPacket(HttpCacheCodec::buildReport(report, false));
}

bool tst_HttpCacheMultiPeer::tickUntil(const std::function<bool()>& done, int timeoutMs) const
{
    QElapsedTimer clock;
    clock.start();

    while (true) {
        tick();
        QTest::qWait(200);

        if (done())
            return true;
        if (clock.elapsed() > timeoutMs)
            return false;
    }
}

void tst_HttpCacheMultiPeer::tickQuietly(int rounds) const
{
    for (int i = 0; i < rounds; ++i) {
        tick();
        QTest::qWait(300);
    }
}

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildHello(const std::array<uint8, 16>& userHash,
                                                           uint16 port, bool httpCacheCapable)
{
    SafeMemFile data;

    // OP_HELLO has a 1-byte hash-size prefix (always 0x10 = 16)
    data.writeUInt8(0x10);
    data.writeHash16(userHash.data());

    // Client ID (high ID: 127.0.0.1 in network byte order) and port.
    //
    // Every peer needs its own port: they all share one loopback address, and a
    // second client claiming the same ip:port is the same client reconnecting as
    // far as the uploader is concerned — it would drop the first one's socket.
    data.writeUInt32(0x7F000001);
    data.writeUInt16(port);

    data.writeUInt32(7);

    Tag(CT_NAME, QStringLiteral("MockPeer")).writeTagToFile(data);
    Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

    const uint32 kadPort = static_cast<uint32>(port) + 10;
    const uint32 udpPorts = (kadPort << 16) | kadPort;
    Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

    const uint32 miscOpts1 =
        (static_cast<uint32>(1) << 29) | // AICH version = 1
        (static_cast<uint32>(1) << 28) | // Unicode
        (static_cast<uint32>(4) << 24) | // UDP version
        (static_cast<uint32>(1) << 20) | // Data compression
        (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) |
        (static_cast<uint32>(2) <<  8) | // Extended requests
        (static_cast<uint32>(1) <<  4) | // Comments
        (static_cast<uint32>(1) <<  2) | // No view shared
        (static_cast<uint32>(1) <<  1);  // Multi packet
    Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

    const uint32 miscOpts2 =
        (static_cast<uint32>(KADEMLIA_VERSION) << 0) |
        (static_cast<uint32>(1) << 4) |  // Large files
        (static_cast<uint32>(1) << 5) |  // Ext multi packet
        (static_cast<uint32>(1) << 7) |  // Crypt layer supported
        (static_cast<uint32>(1) << 8) |  // Crypt layer requested
        (static_cast<uint32>(1) << 10) | // Source exchange 2
        (static_cast<uint32>(1) << 13);  // File identifiers
    Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(data);

    // The capability that decides everything here. It rides in the *hello* tag —
    // UpDownClient reads MODMISC_HTTPCACHE from CT_MOD_MISCOPTIONS, not from
    // OP_EMULEINFO, so advertising it there would advertise nothing.
    Tag(CT_MOD_MISCOPTIONS, httpCacheCapable ? MODMISC_HTTPCACHE : 0u).writeTagToFile(data);

    const uint32 emuleVer =
        (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
        (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
        (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
    Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

    // Server IP + port (0 = not connected to server)
    data.writeUInt32(0);
    data.writeUInt16(0);

    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO);
}

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildEmuleInfo()
{
    SafeMemFile data;

    data.writeUInt8((SEND_EMULE_VERSION_MJR << 4) | (SEND_EMULE_VERSION_MIN / 10));
    data.writeUInt8(EMULE_PROTOCOL);
    data.writeUInt32(6);

    Tag(static_cast<uint8>(ET_COMPRESSION), static_cast<uint32>(1)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_UDPVER), static_cast<uint32>(4)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_UDPPORT), static_cast<uint32>(0)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_SOURCEEXCHANGE),
        static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_COMMENTS), static_cast<uint32>(1)).writeTagToFile(data);

    // Extended requests v2 is what makes the uploader read the part availability
    // out of OP_REQUESTFILENAME at all.
    Tag(static_cast<uint8>(ET_EXTENDEDREQUEST), static_cast<uint32>(2)).writeTagToFile(data);

    return std::make_unique<Packet>(data, OP_EMULEPROT, OP_EMULEINFO);
}

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildSetReqFileId(const uint8* hash)
{
    SafeMemFile data;
    data.writeHash16(hash);
    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_SETREQFILEID);
}

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildRequestFileName(
    const uint8* hash, const std::vector<uint32>& partsHeld) const
{
    SafeMemFile data;
    data.writeHash16(hash);

    // Extended info: the part count has to agree with ours exactly, or the
    // uploader answers FILEREQANSNOFIL instead.
    data.writeUInt16(m_partCount);

    const uint16 byteCount = (m_partCount + 7) / 8;
    std::vector<uint8> bitmap(byteCount, 0);
    for (const uint32 part : partsHeld) {
        if (part < m_partCount)
            bitmap[part / 8] |= static_cast<uint8>(1u << (part % 8));
    }
    data.write(bitmap.data(), byteCount);

    // Complete source count, read for extended requests v2 and above.
    data.writeUInt16(0);

    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQUESTFILENAME);
}

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildStartUploadReq(const uint8* hash)
{
    SafeMemFile data;
    data.writeHash16(hash);
    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_STARTUPLOADREQ);
}

std::unique_ptr<Packet> tst_HttpCacheMultiPeer::buildRequestParts(
    const uint8* hash, uint64 s0, uint64 e0, uint64 s1, uint64 e1, uint64 s2, uint64 e2)
{
    // OP_REQUESTPARTS (32-bit offsets): the test file is 20 MB, so everything fits.
    SafeMemFile data;
    data.writeHash16(hash);

    data.writeUInt32(static_cast<uint32>(s0));
    data.writeUInt32(static_cast<uint32>(s1));
    data.writeUInt32(static_cast<uint32>(s2));

    data.writeUInt32(static_cast<uint32>(e0));
    data.writeUInt32(static_cast<uint32>(e1));
    data.writeUInt32(static_cast<uint32>(e2));

    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQUESTPARTS);
}

QTEST_MAIN(tst_HttpCacheMultiPeer)
#include "tst_HttpCacheMultiPeer.moc"
