/// @file tst_MockPeerUpload.cpp
/// @brief Mock peer upload test — exercises our upload pipeline with RC4 obfuscation.
///
/// Creates a mock TCP client (using EMSocket with RC4 obfuscation) that connects
/// to our ListenSocket, drives the ED2K protocol handshake, requests file blocks,
/// and verifies that the data our upload pipeline sends back matches the original
/// file bytes on disk.
///
/// Test file: data/incoming/eMuleQt-testfile-20MB.bin

#include "MockPeerSocket.h"
#include "UploadPipelineFixture.h"
#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "net/Address.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "stats/Statistics.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

namespace {

/// A mock peer needs a run-unique user hash *and* TCP port, or ClientList reads it as the
/// previous peer reconnecting and displaces that one instead of adding a second identity.
constexpr uint16 kFriendPeerPort  = 4772;
constexpr uint16 kFriendPeerPort2 = 4782;
constexpr uint16 kSessionPeerPort = 4792;

/// UploadPipelineFixture publishes neither of these globals, and it is shared with
/// tst_HttpCacheMultiPeer, so the friend tests install them for their own duration only —
/// the other tests in this file keep running against theApp.statistics == nullptr.
///
/// RAII rather than plain assignment: a QVERIFY failing mid-test returns early, and a global
/// left pointing at a destroyed stack object is dereferenced by the next test that uploads.
struct ScopedStatistics {
    Statistics stats;
    ScopedStatistics()  { theApp.statistics = &stats; }
    ~ScopedStatistics() { theApp.statistics = nullptr; }
};

struct ScopedFriendList {
    FriendList list;
    ScopedFriendList()  { theApp.friendList = &list; }
    ~ScopedFriendList() { theApp.friendList = nullptr; }
};

/// CoreSession publishes the throttler on theApp in lockstep with handing it to the upload
/// queue (CoreSession.cpp:207-220), and ~EMSocket relies on that: it is the only place a
/// socket deregisters itself from m_standardOrder, and it looks the throttler up through this
/// global. UploadPipelineFixture leaves it null by design, so a test that drives the real
/// throttler *and* lets a peer disconnect has to restore the pairing, or the throttler thread
/// keeps sending to a freed socket. UploadQueue::removeFromUploadQueue() cannot cover it:
/// disconnected() nulls m_socket before calling it, so getFileUploadSocket() is already gone.
struct ScopedThrottlerGlobal {
    explicit ScopedThrottlerGlobal(UploadBandwidthThrottler* t)
        { theApp.uploadBandwidthThrottler = t; }
    ~ScopedThrottlerGlobal() { theApp.uploadBandwidthThrottler = nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_MockPeerUpload : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void uploadFlow_sendingPartMatchesFile();
    void uploadFlow_longDurationRateLimited();
    void uploadFlow_throttlerEnforcesSpeedLimit();
    void uploadFlow_unlimitedIsNotTrickled();
    void uploadFlow_friendSlotBytesAreCounted();
    void uploadFlow_friendDatarateSurvivesPeerLeaving();
    void uploadFlow_sessionUpAndCreditsAreCounted();
    void cleanupTestCase();

private:
    // Packet builders
    /// @p userHash nullptr keeps m_fakeUserHash, so the tests that predate the friend work
    /// are unaffected. The friend tests pass their own identity, because a friend is matched
    /// by user hash (FriendList::searchFriend).
    std::unique_ptr<Packet> buildHelloPacket(const uint8* userHash = nullptr,
                                             uint16 tcpPort = 4662);
    std::unique_ptr<Packet> buildEmuleInfo();
    std::unique_ptr<Packet> buildSetReqFileId(const uint8* hash);
    std::unique_ptr<Packet> buildRequestFileName(const uint8* hash);
    std::unique_ptr<Packet> buildStartUploadReq(const uint8* hash);
    std::unique_ptr<Packet> buildRequestParts(const uint8* hash,
                                               uint64 s0, uint64 e0,
                                               uint64 s1, uint64 e1,
                                               uint64 s2, uint64 e2);

    /// Connect, encrypt, HELLO/EMULEINFO, ask for @p fileHash and wait for the upload slot.
    /// @p outClient receives the server-side client, or stays null. An out-parameter rather
    /// than a return value because the QVERIFY family expands to a bare `return`, which a
    /// function with a return type cannot use.
    void handshakeMockPeer(MockPeerSocket& mock, const uint8* userHash, uint16 tcpPort,
                           const uint8* fileHash, UpDownClient*& outClient);

    // Infrastructure — listen socket, upload queue, disk IO, shared files
    UploadPipelineFixture m_pipe;

    // File under test (compressible)
    QString m_testFilePath;
    std::array<uint8, 16> m_fileHash{};
    uint64 m_fileSize = 0;

    // Random (incompressible) file for throttler test
    QString m_randomFilePath;
    std::array<uint8, 16> m_randomFileHash{};
    uint64 m_randomFileSize = 0;

    std::array<uint8, 16> m_fakeUserHash{};

    // Separate identities for the two friend-slot tests — see kFriendPeerPort above.
    std::array<uint8, 16> m_friendUserHash{};
    std::array<uint8, 16> m_friendUserHash2{};
    std::array<uint8, 16> m_sessionUserHash{};
};

// ---------------------------------------------------------------------------
// initTestCase — set up upload infrastructure
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::initTestCase()
{
    m_pipe.setup(this);

    m_testFilePath = projectDataDir() + QStringLiteral("/incoming/eMuleQt-testfile-20MB.bin");
    m_pipe.registerSharedFile(m_testFilePath, m_fileHash, m_fileSize);

    m_randomFilePath = projectDataDir() + QStringLiteral("/incoming/eMuleQt-testfile-20MB-random.bin");
    m_pipe.registerSharedFile(m_randomFilePath, m_randomFileHash, m_randomFileSize);

    // Generate a random fake user hash for the mock peer
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : m_fakeUserHash)
        b = static_cast<uint8>(dist(rng));
    for (auto& b : m_friendUserHash)
        b = static_cast<uint8>(dist(rng));
    for (auto& b : m_friendUserHash2)
        b = static_cast<uint8>(dist(rng));
    for (auto& b : m_sessionUserHash)
        b = static_cast<uint8>(dist(rng));
}

// ---------------------------------------------------------------------------
// Test: Full upload flow with data verification
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_sendingPartMatchesFile()
{
    // 1. Connect with encryption
    MockPeerSocket mock;
    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(mock.waitForConnected(5000));

    // 2. Wait for encryption handshake to complete
    QTRY_VERIFY_WITH_TIMEOUT(mock.isEncryptionLayerReady(), 5000);

    // 3. Send OP_HELLO
    mock.sendPacket(buildHelloPacket());

    // 4. Wait for OP_HELLOANSWER
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_HELLOANSWER), 5000);

    // 5. Send OP_EMULEINFO (initiator sends info after receiving HELLOANSWER)
    mock.sendPacket(buildEmuleInfo());
    // Wait for OP_EMULEINFOANSWER from server
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    // 6. Send OP_SETREQFILEID + OP_REQUESTFILENAME
    mock.sendPacket(buildSetReqFileId(m_fileHash.data()));
    mock.sendPacket(buildRequestFileName(m_fileHash.data()));

    // 7. Wait for OP_REQFILENAMEANSWER + OP_FILESTATUS
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_FILESTATUS), 5000);

    // 8. Send OP_STARTUPLOADREQ
    mock.sendPacket(buildStartUploadReq(m_fileHash.data()));

    // 9. Wait for OP_ACCEPTUPLOADREQ (UploadQueue grants slot)
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_ACCEPTUPLOADREQ), 10000);

    // 10. Send OP_REQUESTPARTS — request 3 blocks from known offsets
    constexpr uint64 blockSize = 10240;
    mock.sendPacket(buildRequestParts(m_fileHash.data(),
        0, blockSize,                   // block 0
        blockSize, blockSize * 2,       // block 1
        blockSize * 2, blockSize * 3)); // block 2

    // 11. Wait for at least 3 data packets (compressed or uncompressed)
    QTRY_VERIFY_WITH_TIMEOUT(mock.dataPacketCount() >= 3, 15000);

    // Stop the process timer before heavy verification to avoid interference
    m_pipe.processTimer.stop();

    // 12. Decompress and verify received blocks match the original file
    auto blocks = mock.receivedDataBlocks();
    QVERIFY2(!blocks.empty(), "No data blocks received");

    std::sort(blocks.begin(), blocks.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });

    constexpr uint64 expectedBytes = blockSize * 3;

    QFile dmg(m_testFilePath);
    QVERIFY(dmg.open(QIODevice::ReadOnly));

    uint64 totalVerified = 0;
    for (const auto& block : blocks) {
        QVERIFY(dmg.seek(static_cast<qint64>(block.start)));
        const auto len = static_cast<qint64>(block.end - block.start);
        QByteArray expected = dmg.read(len);
        QCOMPARE(expected.size(), len);
        QCOMPARE(block.data.size(), expected.size());
        QVERIFY2(block.data == expected,
                 qPrintable(QStringLiteral("Data mismatch at offset %1-%2")
                                .arg(block.start).arg(block.end)));
        totalVerified += static_cast<uint64>(block.data.size());
    }

    QVERIFY2(totalVerified >= expectedBytes,
             qPrintable(QStringLiteral("Expected %1 bytes, got %2")
                            .arg(expectedBytes).arg(totalVerified)));
}

// ---------------------------------------------------------------------------
// Test: Rate-limited upload must survive >60 seconds (regression for 40s timeout bug)
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_longDurationRateLimited()
{
    // Stop the default unlimited-bandwidth process timer
    m_pipe.processTimer.stop();

    // Rate-limited process timer: ~1 KB per 100ms tick ≈ 10 KB/s.
    // We continuously request small blocks (10KB each like the existing test).
    // At 10 KB/s, the upload runs for well over 60 seconds to transfer ~700KB,
    // proving the old 40-second CONNECTION_TIMEOUT no longer kills uploads.
    constexpr uint32 bytesPerTick = 1024;
    constexpr int requiredSeconds = 60;

    QTimer rateLimitedTimer;
    connect(&rateLimitedTimer, &QTimer::timeout, this, [this] {
        m_pipe.uploadQueue->process();
        m_pipe.listenSocket->process();
        m_pipe.uploadQueue->forEachUploading([](UpDownClient* client) {
            if (auto* sock = client->socket())
                sock->sendFileAndControlData(bytesPerTick, 1);
        });
    });

    // 1. Connect with encryption
    MockPeerSocket mock;
    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(mock.waitForConnected(5000));
    QTRY_VERIFY_WITH_TIMEOUT(mock.isEncryptionLayerReady(), 5000);

    // 2. ED2K handshake
    mock.sendPacket(buildHelloPacket());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_HELLOANSWER), 5000);

    mock.sendPacket(buildEmuleInfo());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    // 3. File request + upload negotiation
    mock.sendPacket(buildSetReqFileId(m_fileHash.data()));
    mock.sendPacket(buildRequestFileName(m_fileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_FILESTATUS), 5000);

    mock.sendPacket(buildStartUploadReq(m_fileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_ACCEPTUPLOADREQ), 10000);

    // 4. Start rate-limited timer and record start time
    rateLimitedTimer.start(100);
    QElapsedTimer elapsed;
    elapsed.start();

    // 5. Continuously request 10KB blocks to keep the upload busy.
    //    Each OP_REQUESTPARTS carries 3 blocks = 30KB.
    //    At ~10 KB/s this takes ~3 seconds per round.
    constexpr uint64 blockSize = 10240;     // 10 KB per block

    uint64 nextOffset = 0;
    int roundsSent = 0;
    int prevDataPackets = 0;

    // Send initial round
    mock.sendPacket(buildRequestParts(m_fileHash.data(),
        nextOffset, nextOffset + blockSize,
        nextOffset + blockSize, nextOffset + blockSize * 2,
        nextOffset + blockSize * 2, nextOffset + blockSize * 3));
    nextOffset += blockSize * 3;
    ++roundsSent;

    // 6. Poll loop: keep requesting blocks until >60s has elapsed
    while (elapsed.elapsed() < (requiredSeconds + 5) * 1000) {
        QTest::qWait(500);

        int curDataPackets = mock.dataPacketCount();

        // When new data packets arrive, send another block request round
        // to keep the upload pipeline fed (stay ahead by 1-2 rounds)
        if (curDataPackets > prevDataPackets + 1
            && nextOffset + blockSize * 3 <= m_fileSize)
        {
            mock.sendPacket(buildRequestParts(m_fileHash.data(),
                nextOffset, nextOffset + blockSize,
                nextOffset + blockSize, nextOffset + blockSize * 2,
                nextOffset + blockSize * 2, nextOffset + blockSize * 3));
            nextOffset += blockSize * 3;
            prevDataPackets = curDataPackets;
            ++roundsSent;
        }

        // Once we've passed the required time and have data, we're done
        if (elapsed.elapsed() >= requiredSeconds * 1000 && mock.totalDataBytes() > 0)
            break;
    }

    rateLimitedTimer.stop();

    // 7. Verify: upload survived well past the old 40-second timeout
    const qint64 elapsedMs = elapsed.elapsed();
    const uint64 totalReceived = mock.totalDataBytes();
    QVERIFY2(elapsedMs >= requiredSeconds * 1000,
             qPrintable(QStringLiteral("Upload lasted only %1 ms, expected >%2 ms")
                            .arg(elapsedMs).arg(requiredSeconds * 1000)));

    // Must have received meaningful data (at least a few blocks worth)
    QVERIFY2(totalReceived >= blockSize * 3,
             qPrintable(QStringLiteral("Only %1 bytes received, expected >%2")
                            .arg(totalReceived).arg(blockSize * 3)));

    // 8. Spot-check uncompressed blocks against the original file.
    //    Compressed blocks may be incomplete at the tail, so we only
    //    verify blocks that successfully decompressed.
    auto blocks = mock.receivedDataBlocks();
    QVERIFY2(!blocks.empty(), "No decompressible data blocks received");

    QFile src(m_testFilePath);
    QVERIFY(src.open(QIODevice::ReadOnly));

    uint64 totalVerified = 0;
    for (const auto& block : blocks) {
        QVERIFY(src.seek(static_cast<qint64>(block.start)));
        const auto len = static_cast<qint64>(block.end - block.start);
        QByteArray expected = src.read(len);
        QCOMPARE(expected.size(), len);
        QCOMPARE(block.data.size(), expected.size());
        QVERIFY2(block.data == expected,
                 qPrintable(QStringLiteral("Data mismatch at offset %1-%2")
                                .arg(block.start).arg(block.end)));
        totalVerified += static_cast<uint64>(block.data.size());
    }

    qDebug("Rate-limited upload test: %lld ms, %llu bytes received, "
           "%llu bytes verified, %d rounds sent",
           elapsedMs, static_cast<unsigned long long>(totalReceived),
           static_cast<unsigned long long>(totalVerified), roundsSent);

    // Restart the default process timer for cleanup
    m_pipe.processTimer.start(100);
}

// ---------------------------------------------------------------------------
// Test: Real throttler enforces upload speed limit with EMBLOCKSIZE blocks
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_throttlerEnforcesSpeedLimit()
{
    // Stop the default process timer (it manually flushes data — we want the
    // real UploadBandwidthThrottler to drive sends instead).
    m_pipe.processTimer.stop();

    // Configure upload speed limit.
    // 500 KB/s × 30s = 15 MB = 73% of 20MB file, comfortably under 100%.
    constexpr uint32 uploadLimitKBs = 500;
    constexpr uint32 expectedBytesPerSec = uploadLimitKBs * 1024;   // 512000
    constexpr int testDurationSec = 30;
    constexpr int warmupSec = 5;
    constexpr double tolerance = 0.30;  // ±30% allowed

    thePrefs.setMaxUpload(uploadLimitKBs);

    // Wire up and start the REAL upload bandwidth throttler.
    // EMSocket::send() detects calls from the throttler's background thread
    // and uses native ::send() instead of Qt's write(), making this thread-safe.
    m_pipe.uploadQueue->setThrottler(m_pipe.throttler);
    m_pipe.throttler->setUploadQueue(m_pipe.uploadQueue);
    m_pipe.throttler->setDiskIOThread(m_pipe.diskIO);
    m_pipe.throttler->start();

    // 1. Connect with encryption
    MockPeerSocket mock;
    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(mock.waitForConnected(5000));

    // Increase mock receive buffer to prevent TCP backpressure stalling
    {
        int rcvBuf = 4 * 1024 * 1024;
        ::setsockopt(static_cast<int>(mock.socketDescriptor()), SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    }

    QTRY_VERIFY_WITH_TIMEOUT(mock.isEncryptionLayerReady(), 5000);

    // 2. ED2K handshake
    mock.sendPacket(buildHelloPacket());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_HELLOANSWER), 5000);

    mock.sendPacket(buildEmuleInfo());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    // 3. File request + upload negotiation (using random incompressible file)
    mock.sendPacket(buildSetReqFileId(m_randomFileHash.data()));
    mock.sendPacket(buildRequestFileName(m_randomFileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_FILESTATUS), 5000);

    mock.sendPacket(buildStartUploadReq(m_randomFileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_ACCEPTUPLOADREQ), 10000);

    // 4. Process timer — drives UploadQueue::process() and ListenSocket::process().
    //    The throttler handles sending; this timer only handles queue management
    //    and requesting new blocks from the mock peer.
    constexpr uint64 blockSize = EMBLOCKSIZE;
    const uint64 fileSize = m_randomFileSize;
    uint64 nextOffset = 0;
    uint64 prevMockBytes = 0;

    QTimer processTimer;
    connect(&processTimer, &QTimer::timeout, this, [this, &mock, &nextOffset, &prevMockBytes, fileSize] {
        m_pipe.uploadQueue->process();
        m_pipe.listenSocket->process();

        // Request new blocks when the mock has received data from the previous round.
        // Keep 2 rounds (6 blocks) ahead to avoid pipeline stalls.
        uint64 curBytes = mock.totalDataBytes();
        if (curBytes > prevMockBytes || nextOffset == 0) {
            prevMockBytes = curBytes;
            // Send up to 2 rounds of EMBLOCKSIZE block requests
            for (int r = 0; r < 2 && nextOffset + blockSize * 3 <= fileSize; ++r) {
                mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
                    nextOffset, nextOffset + blockSize,
                    nextOffset + blockSize, nextOffset + blockSize * 2,
                    nextOffset + blockSize * 2, nextOffset + blockSize * 3));
                nextOffset += blockSize * 3;
            }
        }
    });

    // Send initial block request and start timers
    mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
        0, blockSize, blockSize, blockSize * 2, blockSize * 2, blockSize * 3));
    nextOffset = blockSize * 3;
    QTest::qWait(100);

    processTimer.start(100);
    QElapsedTimer elapsed;
    elapsed.start();

    // 5. Run for testDurationSec, sampling datarate every second
    std::vector<uint32> datarateSamples;
    uint64 prevReceived = 0;

    for (int sec = 0; sec < testDurationSec + 5; ++sec) {
        QTest::qWait(1000);

        // Compute rate from mock-side received bytes (ground truth)
        uint64 curReceived = mock.totalDataBytes();
        uint32 rate = static_cast<uint32>(curReceived - prevReceived);
        prevReceived = curReceived;

        // Verify the rate round-trips through CBOR (same encoding as
        // IpcClientHandler::handleGetStats → "upDatarate" field).
        QCborMap stats;
        stats[QStringLiteral("upDatarate")] = static_cast<qint64>(rate);
        uint32 cborRate = static_cast<uint32>(
            stats[QStringLiteral("upDatarate")].toInteger());
        QCOMPARE(cborRate, rate);

        datarateSamples.push_back(rate);

        if (elapsed.elapsed() >= testDurationSec * 1000)
            break;
    }

    processTimer.stop();
    const qint64 elapsedMs = elapsed.elapsed();
    const uint64 totalReceived = mock.totalDataBytes();

    // 6. Verify test ran for the expected duration
    QVERIFY2(elapsedMs >= testDurationSec * 1000,
             qPrintable(QStringLiteral("Test ran only %1 ms").arg(elapsedMs)));

    // 7. Verify total transferred < 90% of file size
    const uint64 maxAllowed = static_cast<uint64>(fileSize * 90 / 100);
    QVERIFY2(totalReceived < maxAllowed,
             qPrintable(QStringLiteral("Transferred %1 bytes >= 90%% limit %2")
                            .arg(totalReceived).arg(maxAllowed)));

    // 8. Verify meaningful data was transferred (> 50% of expected)
    const uint64 expectedTotal = static_cast<uint64>(expectedBytesPerSec) * testDurationSec;
    QVERIFY2(totalReceived > expectedTotal / 2,
             qPrintable(QStringLiteral("Only %1 bytes transferred, expected >%2")
                            .arg(totalReceived).arg(expectedTotal / 2)));

    // 9. Verify datarate samples match the configured limit (after warmup)
    int samplesInRange = 0;
    int samplesChecked = 0;
    const uint32 lowerBound = static_cast<uint32>(expectedBytesPerSec * (1.0 - tolerance));
    const uint32 upperBound = static_cast<uint32>(expectedBytesPerSec * (1.0 + tolerance));

    for (int i = warmupSec; i < static_cast<int>(datarateSamples.size()); ++i) {
        ++samplesChecked;
        if (datarateSamples[static_cast<size_t>(i)] >= lowerBound
            && datarateSamples[static_cast<size_t>(i)] <= upperBound)
            ++samplesInRange;
    }

    QVERIFY2(samplesChecked > 0, "No post-warmup samples collected");

    // At least 50% of post-warmup samples must be within tolerance
    double inRangeRatio = static_cast<double>(samplesInRange) / samplesChecked;
    QVERIFY2(inRangeRatio >= 0.50,
             qPrintable(QStringLiteral(
                 "Only %1/%2 samples (%3%%) within [%4, %5] bytes/s (expected %6)")
                 .arg(samplesInRange).arg(samplesChecked)
                 .arg(inRangeRatio * 100, 0, 'f', 0)
                 .arg(lowerBound).arg(upperBound).arg(expectedBytesPerSec)));

    // 10. Verify upload speed never exceeds the limit by more than 10%
    const uint32 hardCap = static_cast<uint32>(expectedBytesPerSec * 1.10);
    for (size_t i = static_cast<size_t>(warmupSec); i < datarateSamples.size(); ++i) {
        QVERIFY2(datarateSamples[i] <= hardCap,
                 qPrintable(QStringLiteral("Sample %1 (%2 bytes/s) exceeds limit+10%% (%3)")
                     .arg(i).arg(datarateSamples[i]).arg(hardCap)));
    }

    qDebug("Throttler speed test: %lld ms, %llu bytes, %d/%d samples in range "
           "[%u-%u], target %u bytes/s",
           elapsedMs, static_cast<unsigned long long>(totalReceived),
           samplesInRange, samplesChecked, lowerBound, upperBound, expectedBytesPerSec);

    // Cleanup: stop throttler and disconnect from queue
    m_pipe.throttler->endThread();
    m_pipe.uploadQueue->setThrottler(nullptr);

    // Recreate throttler for potential future tests
    m_pipe.throttler = new UploadBandwidthThrottler(this);

    // Restart default process timer for cleanup
    m_pipe.processTimer.start(100);
}

// ---------------------------------------------------------------------------
// Unlimited upload must actually run at full speed.
//
// eMuleQt stores "no upload limit" as maxUpload() == 0, but the throttler is a port of
// MFC code that spells the same thing UNLIMITED. Reading the raw pref gave
// allowedDataRate = 0 * 1024 = 0, so bytesToSpend stayed 0 and the equal-bandwidth and
// full-priority send loops were skipped entirely — only the once-per-second trickle loop
// and a 500-byte control budget ran. thePrefs.maxUploadLimit() is what closes that gap.
//
// The trickle loop paces each socket via getNeededBytes(), which spreads the current send
// buffer over a 3-5 second deadline: roughly one EMBLOCKSIZE buffer per 3 s on a single
// socket, i.e. a ceiling around 60 KB/s. The assertion below sits well above that and far
// below what loopback delivers unthrottled, so it separates the two regimes cleanly
// without depending on the host's actual loopback speed.
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_unlimitedIsNotTrickled()
{
    m_pipe.processTimer.stop();

    constexpr int measureSec = 8;
    // ~190 KB/s averaged over the window: >3x the trickle ceiling, and a small fraction
    // of unthrottled loopback throughput.
    constexpr uint64 minTotalBytes = 1500u * 1024u;

    thePrefs.setMaxUpload(0);   // the GUI's "Unlimited"
    QCOMPARE(thePrefs.maxUploadLimit(), UNLIMITED);

    m_pipe.uploadQueue->setThrottler(m_pipe.throttler);
    m_pipe.throttler->setUploadQueue(m_pipe.uploadQueue);
    m_pipe.throttler->setDiskIOThread(m_pipe.diskIO);
    m_pipe.throttler->start();

    // 1. Connect with encryption
    MockPeerSocket mock;
    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(mock.waitForConnected(5000));

    {
        int rcvBuf = 4 * 1024 * 1024;
        ::setsockopt(static_cast<int>(mock.socketDescriptor()), SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    }

    QTRY_VERIFY_WITH_TIMEOUT(mock.isEncryptionLayerReady(), 5000);

    // 2. ED2K handshake
    mock.sendPacket(buildHelloPacket());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_HELLOANSWER), 5000);

    mock.sendPacket(buildEmuleInfo());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    // 3. File request + upload negotiation
    mock.sendPacket(buildSetReqFileId(m_randomFileHash.data()));
    mock.sendPacket(buildRequestFileName(m_randomFileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_FILESTATUS), 5000);

    mock.sendPacket(buildStartUploadReq(m_randomFileHash.data()));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_ACCEPTUPLOADREQ), 10000);

    // 4. Same block pump as the speed-limit test — the throttler does the sending.
    constexpr uint64 blockSize = EMBLOCKSIZE;
    const uint64 fileSize = m_randomFileSize;
    uint64 nextOffset = 0;
    uint64 prevMockBytes = 0;

    QTimer processTimer;
    connect(&processTimer, &QTimer::timeout, this, [this, &mock, &nextOffset, &prevMockBytes, fileSize] {
        m_pipe.uploadQueue->process();
        m_pipe.listenSocket->process();

        uint64 curBytes = mock.totalDataBytes();
        if (curBytes > prevMockBytes || nextOffset == 0) {
            prevMockBytes = curBytes;
            for (int r = 0; r < 2 && nextOffset + blockSize * 3 <= fileSize; ++r) {
                mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
                    nextOffset, nextOffset + blockSize,
                    nextOffset + blockSize, nextOffset + blockSize * 2,
                    nextOffset + blockSize * 2, nextOffset + blockSize * 3));
                nextOffset += blockSize * 3;
            }
        }
    });

    mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
        0, blockSize, blockSize, blockSize * 2, blockSize * 2, blockSize * 3));
    nextOffset = blockSize * 3;
    QTest::qWait(100);

    processTimer.start(100);
    QElapsedTimer elapsed;
    elapsed.start();

    // 5. Run the window, stopping early once the bar is cleared — on a fast host the
    //    whole file drains in a couple of seconds and there is nothing left to measure.
    uint32 peakSampleRate = 0;
    uint64 prevReceived = 0;
    for (int sec = 0; sec < measureSec; ++sec) {
        QTest::qWait(1000);
        const uint64 curReceived = mock.totalDataBytes();
        peakSampleRate = std::max(peakSampleRate,
                                  static_cast<uint32>(curReceived - prevReceived));
        prevReceived = curReceived;
        if (curReceived >= minTotalBytes)
            break;
    }

    processTimer.stop();
    const qint64 elapsedMs = elapsed.elapsed();
    const uint64 totalReceived = mock.totalDataBytes();

    qDebug("Unlimited upload test: %lld ms, %llu bytes, peak sample %u bytes/s",
           elapsedMs, static_cast<unsigned long long>(totalReceived), peakSampleRate);

    QVERIFY2(totalReceived >= minTotalBytes,
             qPrintable(QStringLiteral(
                 "Unlimited upload moved only %1 bytes in %2 ms (need >=%3). "
                 "A figure near the ~60 KB/s trickle ceiling means the throttler read the "
                 "raw maxUpload() 0 as a zero byte budget instead of UNLIMITED.")
                 .arg(totalReceived).arg(elapsedMs).arg(minTotalBytes)));

    // Cleanup: stop throttler and disconnect from queue
    m_pipe.throttler->endThread();
    m_pipe.uploadQueue->setThrottler(nullptr);
    m_pipe.throttler = new UploadBandwidthThrottler(this);

    m_pipe.processTimer.start(100);
}

// ---------------------------------------------------------------------------
// Bytes sent to a friend are counted only while that friend holds the friend slot.
//
// Statistics::addSessionSentBytesToFriend() had no production caller at all, so
// sessionSentBytesToFriend — the Statistics panel figure, and the cumTotalUploadedToFriend it
// is rolled into — were permanently zero. Its one writer is updateUploadingStatisticsData(),
// gated on friendPtr() && friendSlot(); MFC spells the same thing as the last argument of
// Add2SessionTransferData (srchybrid/UploadClient.cpp:438-439).
//
// Both phases run over a single connection, so the assertion pins the *gate* rather than just
// "the counter moved": the same peer uploads first without the slot and then with it.
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_friendSlotBytesAreCounted()
{
    ScopedStatistics stats;
    ScopedFriendList friends;

    // Unlimited, flushed from this thread by the fixture's own timer — no throttler thread is
    // involved, so what the counter sees is exactly what this test asked for.
    thePrefs.setMaxUpload(0);
    m_pipe.uploadQueue->setThrottler(nullptr);
    m_pipe.processTimer.start(100);

    // The friend exists before the peer connects, as it would after a restart. Linking is left
    // to the production hello path (UpDownClient::processHelloTags → Friend::setLinkedClient),
    // not done here — that path is half of what the counter depends on.
    Friend* f = friends.list.addFriend(m_friendUserHash.data(),
                                       Address::fromString(QStringLiteral("127.0.0.1")),
                                       kFriendPeerPort, QStringLiteral("MockFriend"));
    QVERIFY(f);
    QVERIFY(!f->friendSlot());

    MockPeerSocket mock;
    UpDownClient* client = nullptr;
    handshakeMockPeer(mock, m_friendUserHash.data(), kFriendPeerPort,
                      m_randomFileHash.data(), client);
    QVERIFY2(client, "No server-side client carries the mock peer's user hash");

    // Both directions of the link the byte counter rides on.
    QCOMPARE(f->linkedClient(), client);
    QCOMPARE(client->friendPtr(), f);
    QVERIFY(!client->friendSlot());

    // The incompressible file: MockPeerSocket::totalDataBytes() only counts OP_SENDINGPART,
    // so a compressible one would report no payload at all.
    constexpr uint64 blockSize = 10240;
    uint64 nextOffset = 0;

    // -- Phase 1: a friend, but with no slot. Nothing may be attributed. --------------------
    mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
        nextOffset, nextOffset + blockSize,
        nextOffset + blockSize, nextOffset + blockSize * 2,
        nextOffset + blockSize * 2, nextOffset + blockSize * 3));
    nextOffset += blockSize * 3;

    QTRY_VERIFY_WITH_TIMEOUT(mock.totalDataBytes() >= blockSize * 3, 20000);
    QTest::qWait(400);   // two process ticks, so the socket counters have been drained

    QCOMPARE(theApp.statistics->sessionSentBytesToFriend(), static_cast<uint64>(0));

    // -- Phase 2: the GUI's "Friend slot" toggle, mid-connection. ---------------------------
    f->setFriendSlot(true);
    QVERIFY2(client->friendSlot(),
             "Friend::setFriendSlot did not reach the linked client");

    const uint64 receivedBefore = mock.totalDataBytes();

    mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
        nextOffset, nextOffset + blockSize,
        nextOffset + blockSize, nextOffset + blockSize * 2,
        nextOffset + blockSize * 2, nextOffset + blockSize * 3));
    nextOffset += blockSize * 3;

    QTRY_VERIFY_WITH_TIMEOUT(mock.totalDataBytes() >= receivedBefore + blockSize * 3, 20000);
    QTest::qWait(400);

    const uint64 payload = mock.totalDataBytes() - receivedBefore;
    const uint64 counted = theApp.statistics->sessionSentBytesToFriend();

    // The counter is wire bytes — payload plus each data packet's 6-byte ed2k header and
    // 24-byte hash+offsets header — so it can never come in under the payload.
    QVERIFY2(counted >= payload,
             qPrintable(QStringLiteral("Only %1 bytes attributed to the friend slot for a "
                                       "%2 byte transfer").arg(counted).arg(payload)));

    // ...and phase 1's bytes must not have been swept in retroactively. Header overhead is
    // well under 1% at this block size, so half again is loose while still excluding ~2x.
    QVERIFY2(counted <= payload * 3 / 2,
             qPrintable(QStringLiteral("%1 bytes attributed for a %2 byte slotted transfer — "
                                       "bytes sent before the slot was granted were counted too")
                            .arg(counted).arg(payload)));
}

// ---------------------------------------------------------------------------
// The friend datarate stays sane when the friend-slot peer leaves the uploading list.
//
// UploadQueue::process() feeds m_averageFriendDRList and updateDatarates() derives the rate as
// back() - front(). That subtraction is only valid on a monotonic series. Pushing a snapshot
// sum over the current uploading list — which is what this did — is not monotonic: the sum
// drops to zero the moment the friend-slot client leaves, the uint64 subtraction underflows,
// and the reported friend datarate becomes a garbage multi-GB/s figure. It now pushes the
// running total from the test above, which is what makes MFC's identical subtraction valid.
//
// The series is only fed when a throttler is set, so unlike the test above this one wires the
// real UploadBandwidthThrottler, exactly as uploadFlow_throttlerEnforcesSpeedLimit does.
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_friendDatarateSurvivesPeerLeaving()
{
    ScopedStatistics stats;
    ScopedFriendList friends;

    m_pipe.processTimer.stop();

    // A steady rate, so the 30-second averaging window fills with real samples instead of the
    // whole file draining in a couple of ticks.
    thePrefs.setMaxUpload(500);

    Friend* f = friends.list.addFriend(m_friendUserHash2.data(),
                                       Address::fromString(QStringLiteral("127.0.0.1")),
                                       kFriendPeerPort2, QStringLiteral("MockFriend2"));
    QVERIFY(f);
    f->setFriendSlot(true);   // before the peer connects; setLinkedClient carries it across

    ScopedThrottlerGlobal throttlerGlobal(m_pipe.throttler);
    m_pipe.uploadQueue->setThrottler(m_pipe.throttler);
    m_pipe.throttler->setUploadQueue(m_pipe.uploadQueue);
    m_pipe.throttler->setDiskIOThread(m_pipe.diskIO);
    m_pipe.throttler->start();

    MockPeerSocket mock;
    UpDownClient* client = nullptr;
    handshakeMockPeer(mock, m_friendUserHash2.data(), kFriendPeerPort2,
                      m_randomFileHash.data(), client);
    QVERIFY(client);
    QVERIFY2(client->friendSlot(), "The friend slot did not reach the client at hello time");

    constexpr uint64 blockSize = EMBLOCKSIZE;
    const uint64 fileSize = m_randomFileSize;
    uint64 nextOffset = 0;
    uint64 prevMockBytes = 0;
    bool peerGone = false;

    QTimer processTimer;
    connect(&processTimer, &QTimer::timeout, this,
            [this, &mock, &nextOffset, &prevMockBytes, &peerGone, fileSize] {
        m_pipe.uploadQueue->process();
        m_pipe.listenSocket->process();

        // process() has to keep running after the peer leaves — that is when the friend series
        // is under test — but nothing may touch the socket once it is going away.
        if (peerGone)
            return;

        const uint64 curBytes = mock.totalDataBytes();
        if (curBytes > prevMockBytes || nextOffset == 0) {
            prevMockBytes = curBytes;
            for (int r = 0; r < 2 && nextOffset + blockSize * 3 <= fileSize; ++r) {
                mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
                    nextOffset, nextOffset + blockSize,
                    nextOffset + blockSize, nextOffset + blockSize * 2,
                    nextOffset + blockSize * 2, nextOffset + blockSize * 3));
                nextOffset += blockSize * 3;
            }
        }
    });

    mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
        0, blockSize, blockSize, blockSize * 2, blockSize * 2, blockSize * 3));
    nextOffset = blockSize * 3;
    QTest::qWait(100);

    processTimer.start(100);

    // Long enough for the friend series to hold several seconds of its own samples.
    QTest::qWait(8000);

    const uint32 rateWhileUploading = m_pipe.uploadQueue->friendDatarate();
    const uint64 totalToFriend = theApp.statistics->sessionSentBytesToFriend();

    QVERIFY2(totalToFriend > 0, "No bytes were attributed to the friend slot");
    QVERIFY2(rateWhileUploading > 0,
             "friendDatarate() stayed at zero throughout a friend-slot upload");

    // The peer leaves. disconnected() calls removeFromUploadQueue() straight away, which is
    // precisely where a snapshot-sum series drops and the subtraction underflows.
    peerGone = true;
    mock.disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!m_pipe.uploadQueue->hasActiveUploads(), 10000);

    // Over an averaging window of seconds a rate can never exceed everything ever sent to
    // friends; the 1 MiB floor covers a short transfer. An underflowed uint64 truncated to
    // uint32 is effectively uniform over [0, 2^32), so it clears this bound on a fraction of
    // a percent of samples — not on ten of them. Six seconds of polling also keeps the whole
    // loop inside the 30-second window, so it still straddles the departure.
    const uint64 cap = std::max<uint64>(totalToFriend, 1u << 20);
    for (int i = 0; i < 10; ++i) {
        QTest::qWait(600);   // updateDatarates() recomputes at most every 500 ms
        const uint64 sample = m_pipe.uploadQueue->friendDatarate();
        QVERIFY2(sample <= cap,
                 qPrintable(QStringLiteral("friendDatarate() = %1 B/s after the friend-slot "
                                           "peer left, over the %2 B/s ceiling — the friend "
                                           "byte series is not monotonic").arg(sample).arg(cap)));
    }

    processTimer.stop();

    // Cleanup: stop the throttler and hand the fixture back the way the other throttler tests
    // leave it, so cleanupTestCase and any later test start from a known state.
    m_pipe.throttler->endThread();
    m_pipe.uploadQueue->setThrottler(nullptr);
    m_pipe.throttler = new UploadBandwidthThrottler(this);

    m_pipe.processTimer.start(100);
}

// ---------------------------------------------------------------------------
// The per-slot session counters, the credit system, and the queue's success tally.
//
// All four of these were dead in ways that only real bytes on a real socket can expose:
//
//  - m_curSessionUp had no writer at all, because the port read MFC's baseline marker back
//    as if it were a counter (MFC: GetSessionUp() = m_nTransferredUp - m_nCurSessionUp,
//    srchybrid/UpdownClient.h:254). sessionUp() was therefore permanently zero.
//  - ClientCredits::addUploaded() / addDownloaded() were fully implemented and never called,
//    so scoreRatio() always returned 1.0 and the upload queue ignored credits entirely.
//  - m_curQueueSessionPayloadUp was fed wire bytes rather than the socket's payload figure,
//    so the SESSIONMAXTRANS slot rotation counted framing overhead against the peer.
//  - UploadQueue::removeFromUploadQueue() gates its successful/failed tally on
//    sessionUp() > 0, so every completed upload was booked as a failure and averageUpTime()
//    could never leave zero.
//
// Deliberately the deterministic shape of uploadFlow_friendSlotBytesAreCounted: no throttler,
// flushed from this thread by the fixture's own timer.
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_sessionUpAndCreditsAreCounted()
{
    ScopedStatistics stats;

    thePrefs.setMaxUpload(0);
    m_pipe.uploadQueue->setThrottler(nullptr);
    m_pipe.processTimer.start(100);

    // Every earlier test in this binary leaves its peer in the tracked-client map on the way
    // out (removeFromUploadQueue → addTrackClient), and addClientToQueue refuses a fourth
    // distinct TCP port from one address. Three ports are already spoken for by the time this
    // test runs, so clear the map rather than have the handshake rejected.
    QVERIFY(theApp.clientList);
    theApp.clientList->removeAllTrackedClients();

    // The queue's tallies are cumulative across this binary's tests, so compare deltas.
    const uint32 successBefore = m_pipe.uploadQueue->successfulUploadCount();
    const uint32 failedBefore  = m_pipe.uploadQueue->failedUploadCount();

    MockPeerSocket mock;
    UpDownClient* client = nullptr;
    handshakeMockPeer(mock, m_sessionUserHash.data(), kSessionPeerPort,
                      m_randomFileHash.data(), client);
    QVERIFY2(client, "No server-side client carries the mock peer's user hash");

    // The hello path assigns credits from theApp.clientCredits, which the fixture publishes.
    QVERIFY2(client->credits(), "The handshake did not attach a credit record");
    QCOMPARE(client->credits()->uploadedTotal(), static_cast<uint64>(0));
    QCOMPARE(client->sessionUp(), static_cast<uint64>(0));

    // The incompressible file: MockPeerSocket::totalDataBytes() only counts OP_SENDINGPART,
    // so a compressible one would report no payload at all.
    constexpr uint64 blockSize = 10240;
    uint64 nextOffset = 0;
    for (int round = 0; round < 3; ++round) {
        const uint64 before = mock.totalDataBytes();
        mock.sendPacket(buildRequestParts(m_randomFileHash.data(),
            nextOffset, nextOffset + blockSize,
            nextOffset + blockSize, nextOffset + blockSize * 2,
            nextOffset + blockSize * 2, nextOffset + blockSize * 3));
        nextOffset += blockSize * 3;
        QTRY_VERIFY_WITH_TIMEOUT(mock.totalDataBytes() >= before + blockSize * 3, 20000);
    }
    QTest::qWait(400);   // two process ticks, so the socket counters have been drained

    const uint64 payload     = mock.totalDataBytes();
    const uint64 sessionUp   = client->sessionUp();
    const uint64 payloadUp   = client->queueSessionPayloadUp();
    const uint64 credited    = client->credits()->uploadedTotal();

    QCOMPARE(payload, nextOffset);

    // sessionUp() is transferredUp() minus the mark taken when the slot opened, so it is a
    // suffix of the lifetime total, never more. Not equality: this binary's earlier tests can
    // leave a client object behind that ClientList re-identifies for a later peer, carrying
    // its accumulated transferredUp across.
    QVERIFY2(sessionUp > 0,
             qPrintable(QStringLiteral("sessionUp() stayed at zero after %1 payload bytes")
                            .arg(payload)));
    QVERIFY2(sessionUp <= client->transferredUp(),
             qPrintable(QStringLiteral("sessionUp() %1 exceeds the %2 lifetime total it is "
                                       "supposed to be a suffix of")
                            .arg(sessionUp).arg(client->transferredUp())));

    // Wire bytes: payload plus each data packet's 6-byte ed2k header and 24-byte
    // hash+offsets header. Never under the payload, and the overhead is well under 1%.
    QVERIFY2(sessionUp >= payload,
             qPrintable(QStringLiteral("sessionUp() %1 is under the %2 payload bytes the peer "
                                       "actually received").arg(sessionUp).arg(payload)));
    QVERIFY(sessionUp <= payload * 3 / 2);

    // ...whereas the per-slot payload counter is the file data alone, so it must be strictly
    // smaller. Equality would mean it is still being fed the wire figure.
    QVERIFY2(payloadUp > 0, "queueSessionPayloadUp() stayed at zero");
    QVERIFY2(payloadUp < sessionUp,
             qPrintable(QStringLiteral("queueSessionPayloadUp() %1 is not below the %2 wire "
                                       "bytes — it is still being fed wire bytes, which makes "
                                       "the SESSIONMAXTRANS slot rotation count framing "
                                       "overhead against the peer")
                            .arg(payloadUp).arg(sessionUp)));
    QCOMPARE(payloadUp, payload);

    // Credit for what we gave this peer. Without it scoreRatio() is stuck at 1.0 for everyone.
    QVERIFY2(credited > 0,
             "ClientCredits::addUploaded() was never called — the credit system is inert");
    QCOMPARE(credited, sessionUp);

    // -- The peer leaves: the slot must be booked as a success, not a failure. --------------
    //
    // m_totalUploadTime accumulates getUpStartTimeDelay() in whole seconds, so hold the slot
    // open past the first one — otherwise averageUpTime() would read zero from the integer
    // division alone and the assertion below would prove nothing.
    QTRY_VERIFY_WITH_TIMEOUT(client->getUpStartTimeDelay() >= 1500, 5000);

    mock.disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!m_pipe.uploadQueue->hasActiveUploads(), 10000);

    QCOMPARE(m_pipe.uploadQueue->successfulUploadCount(), successBefore + 1);
    QCOMPARE(m_pipe.uploadQueue->failedUploadCount(), failedBefore);
    QVERIFY2(m_pipe.uploadQueue->averageUpTime() > 0,
             "averageUpTime() stayed at zero — m_totalUploadTime never accumulated");
}

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

std::unique_ptr<Packet> tst_MockPeerUpload::buildHelloPacket(const uint8* userHash,
                                                            uint16 tcpPort)
{
    SafeMemFile data;

    // OP_HELLO has a 1-byte hash-size prefix (always 0x10 = 16)
    data.writeUInt8(0x10);

    // 16-byte user hash
    data.writeHash16(userHash ? userHash : m_fakeUserHash.data());

    // Client ID (high ID: 127.0.0.1 in network byte order)
    data.writeUInt32(0x7F000001);

    // Port
    data.writeUInt16(tcpPort);

    // Tag count
    data.writeUInt32(6);

    // Tags
    Tag(CT_NAME, QStringLiteral("MockPeer")).writeTagToFile(data);
    Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

    // CT_EMULE_UDPPORTS — (kadPort << 16) | udpPort
    const uint32 udpPorts = (static_cast<uint32>(4672) << 16) | 4672;
    Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS1 — capability bits
    const uint32 miscOpts1 =
        (static_cast<uint32>(1) << 29) | // AICH version = 1
        (static_cast<uint32>(1) << 28) | // Unicode
        (static_cast<uint32>(4) << 24) | // UDP version
        (static_cast<uint32>(1) << 20) | // Data compression
        (static_cast<uint32>(0) << 16) | // Secure ident (0 = none)
        (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) | // Source exchange
        (static_cast<uint32>(2) <<  8) | // Extended requests
        (static_cast<uint32>(1) <<  4) | // Comments
        (static_cast<uint32>(0) <<  3) | // Peer cache
        (static_cast<uint32>(1) <<  2) | // No view shared
        (static_cast<uint32>(1) <<  1) | // Multi packet
        (static_cast<uint32>(0) <<  0);  // Preview
    Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS2 — more capability bits
    const uint32 miscOpts2 =
        (static_cast<uint32>(KADEMLIA_VERSION) << 0) | // Kad version
        (static_cast<uint32>(1) << 4) |  // Large files
        (static_cast<uint32>(1) << 5) |  // Ext multi packet
        (static_cast<uint32>(1) << 7) |  // Crypt layer supported
        (static_cast<uint32>(1) << 8) |  // Crypt layer requested
        (static_cast<uint32>(0) << 9) |  // Crypt layer required
        (static_cast<uint32>(1) << 10) | // Source exchange 2
        (static_cast<uint32>(0) << 11) | // Captcha
        (static_cast<uint32>(0) << 12) | // Direct UDP callback
        (static_cast<uint32>(1) << 13);  // File identifiers
    Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(data);

    // CT_EMULE_VERSION — (compatClient << 24) | (majVer << 17) | (minVer << 10) | (upVer << 7)
    const uint32 emuleVer =
        (static_cast<uint32>(0) << 24) |
        (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
        (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
        (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
    Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

    // Server IP + port (0 = not connected to server)
    data.writeUInt32(0);
    data.writeUInt16(0);

    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO);
}

std::unique_ptr<Packet> tst_MockPeerUpload::buildEmuleInfo()
{
    SafeMemFile data;

    // eMule version byte: (majorVer << 4) | (minVer / 10)
    data.writeUInt8((SEND_EMULE_VERSION_MJR << 4) | (SEND_EMULE_VERSION_MIN / 10));

    // Protocol version
    data.writeUInt8(EMULE_PROTOCOL);

    // Tag count
    data.writeUInt32(6);

    // Tags
    Tag(static_cast<uint8>(ET_COMPRESSION), static_cast<uint32>(1)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_UDPVER), static_cast<uint32>(4)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_UDPPORT), static_cast<uint32>(0)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_SOURCEEXCHANGE), static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_COMMENTS), static_cast<uint32>(1)).writeTagToFile(data);
    Tag(static_cast<uint8>(ET_EXTENDEDREQUEST), static_cast<uint32>(2)).writeTagToFile(data);

    // OP_EMULEINFO (not answer — we're the initiator sending info first)
    return std::make_unique<Packet>(data, OP_EMULEPROT, OP_EMULEINFO);
}

std::unique_ptr<Packet> tst_MockPeerUpload::buildSetReqFileId(const uint8* hash)
{
    SafeMemFile data;
    data.writeHash16(hash);
    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_SETREQFILEID);
}

std::unique_ptr<Packet> tst_MockPeerUpload::buildRequestFileName(const uint8* hash)
{
    SafeMemFile data;
    data.writeHash16(hash);
    // No extended info — we're a simple downloader
    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQUESTFILENAME);
}

std::unique_ptr<Packet> tst_MockPeerUpload::buildStartUploadReq(const uint8* hash)
{
    SafeMemFile data;
    data.writeHash16(hash);
    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_STARTUPLOADREQ);
}

std::unique_ptr<Packet> tst_MockPeerUpload::buildRequestParts(
    const uint8* hash,
    uint64 s0, uint64 e0,
    uint64 s1, uint64 e1,
    uint64 s2, uint64 e2)
{
    // Use OP_REQUESTPARTS (uint32 offsets) — our file/offsets fit in 32 bits
    SafeMemFile data;
    data.writeHash16(hash);

    // 3 start offsets
    data.writeUInt32(static_cast<uint32>(s0));
    data.writeUInt32(static_cast<uint32>(s1));
    data.writeUInt32(static_cast<uint32>(s2));

    // 3 end offsets
    data.writeUInt32(static_cast<uint32>(e0));
    data.writeUInt32(static_cast<uint32>(e1));
    data.writeUInt32(static_cast<uint32>(e2));

    return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQUESTPARTS);
}

// ---------------------------------------------------------------------------
// Handshake helper
//
// Only the friend tests use it. The four tests above each vary the handshake — a different
// file, an SO_RCVBUF bump, a different driving timer — so folding them in would be a rewrite
// of passing tests for no coverage gain.
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::handshakeMockPeer(MockPeerSocket& mock, const uint8* userHash,
                                           uint16 tcpPort, const uint8* fileHash,
                                           UpDownClient*& outClient)
{
    outClient = nullptr;

    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_pipe.listenSocket->serverPort());
    QVERIFY(mock.waitForConnected(5000));
    QTRY_VERIFY_WITH_TIMEOUT(mock.isEncryptionLayerReady(), 5000);

    mock.sendPacket(buildHelloPacket(userHash, tcpPort));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_HELLOANSWER), 5000);

    mock.sendPacket(buildEmuleInfo());
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_EMULEINFOANSWER, OP_EMULEPROT), 5000);

    mock.sendPacket(buildSetReqFileId(fileHash));
    mock.sendPacket(buildRequestFileName(fileHash));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_FILESTATUS), 5000);

    mock.sendPacket(buildStartUploadReq(fileHash));
    QTRY_VERIFY_WITH_TIMEOUT(mock.hasOpcode(OP_ACCEPTUPLOADREQ), 15000);

    QVERIFY(theApp.clientList);
    outClient = theApp.clientList->findByUserHash(userHash);
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::cleanupTestCase()
{
    m_pipe.teardown();
}

QTEST_MAIN(tst_MockPeerUpload)
#include "tst_MockPeerUpload.moc"
