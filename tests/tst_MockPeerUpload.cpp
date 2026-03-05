/// @file tst_MockPeerUpload.cpp
/// @brief Mock peer upload test — exercises our upload pipeline with RC4 obfuscation.
///
/// Creates a mock TCP client (using EMSocket with RC4 obfuscation) that connects
/// to our ListenSocket, drives the ED2K protocol handshake, requests file blocks,
/// and verifies that the data our upload pipeline sends back matches the original
/// file bytes on disk.
///
/// Test file: data/incoming/qt-online-installer-macOS-x64-4.10.0.dmg

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <zlib.h>

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// MockDownloader — EMSocket subclass that records received packets
// ---------------------------------------------------------------------------

class MockDownloader : public EMSocket {
    Q_OBJECT

public:
    using EMSocket::EMSocket;

    struct ReceivedPacket {
        uint8 opcode;
        uint8 prot;
        std::vector<char> data;
    };

    struct DataBlock {
        uint64 start;
        uint64 end;
        QByteArray data;
    };

    std::vector<ReceivedPacket> receivedPackets;
    int lastErrorCode = 0;

    bool hasOpcode(uint8 op) const
    {
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op)
                return true;
        }
        return false;
    }

    bool hasOpcode(uint8 op, uint8 proto) const
    {
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op && rp.prot == proto)
                return true;
        }
        return false;
    }

    /// Count of data packets received (OP_SENDINGPART* or OP_COMPRESSEDPART*).
    int dataPacketCount() const
    {
        int count = 0;
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == OP_SENDINGPART || rp.opcode == OP_SENDINGPART_I64
                || rp.opcode == OP_COMPRESSEDPART || rp.opcode == OP_COMPRESSEDPART_I64)
                ++count;
        }
        return count;
    }

    /// Extract received data blocks from OP_SENDINGPART* and OP_COMPRESSEDPART* packets.
    /// Compressed blocks are decompressed with zlib before returning.
    std::vector<DataBlock> receivedDataBlocks() const
    {
        std::vector<DataBlock> blocks;

        // Collect compressed packet chunks keyed by start offset.
        // Each OP_COMPRESSEDPART has: hash(16) + start(4) + totalCompressedLen(4) + chunk.
        // Multiple packets with the same start offset form one block.
        struct CompressedEntry {
            uint64 start;
            uint32 totalCompressedLen;
            QByteArray compressedData;
        };
        std::map<uint64, CompressedEntry> compressedMap;

        for (const auto& rp : receivedPackets) {
            // --- Uncompressed ---
            if (rp.opcode == OP_SENDINGPART && rp.prot == OP_EDONKEYPROT && rp.data.size() > 24) {
                uint32 start = 0, end = 0;
                std::memcpy(&start, rp.data.data() + 16, 4);
                std::memcpy(&end, rp.data.data() + 20, 4);
                DataBlock block;
                block.start = start;
                block.end = end;
                block.data = QByteArray(rp.data.data() + 24,
                                        static_cast<qsizetype>(rp.data.size() - 24));
                blocks.push_back(std::move(block));
            } else if (rp.opcode == OP_SENDINGPART_I64 && rp.prot == OP_EMULEPROT && rp.data.size() > 32) {
                uint64 start = 0, end = 0;
                std::memcpy(&start, rp.data.data() + 16, 8);
                std::memcpy(&end, rp.data.data() + 24, 8);
                DataBlock block;
                block.start = start;
                block.end = end;
                block.data = QByteArray(rp.data.data() + 32,
                                        static_cast<qsizetype>(rp.data.size() - 32));
                blocks.push_back(std::move(block));
            }
            // --- Compressed (standard 32-bit offsets) ---
            else if (rp.opcode == OP_COMPRESSEDPART && rp.prot == OP_EMULEPROT && rp.data.size() > 24) {
                uint32 start32 = 0, compLen = 0;
                std::memcpy(&start32, rp.data.data() + 16, 4);
                std::memcpy(&compLen, rp.data.data() + 20, 4);
                auto& entry = compressedMap[start32];
                entry.start = start32;
                entry.totalCompressedLen = compLen;
                entry.compressedData.append(rp.data.data() + 24,
                                            static_cast<qsizetype>(rp.data.size() - 24));
            }
            // --- Compressed (64-bit offsets) ---
            else if (rp.opcode == OP_COMPRESSEDPART_I64 && rp.prot == OP_EMULEPROT && rp.data.size() > 28) {
                uint64 start64 = 0;
                uint32 compLen = 0;
                std::memcpy(&start64, rp.data.data() + 16, 8);
                std::memcpy(&compLen, rp.data.data() + 24, 4);
                auto& entry = compressedMap[start64];
                entry.start = start64;
                entry.totalCompressedLen = compLen;
                entry.compressedData.append(rp.data.data() + 28,
                                            static_cast<qsizetype>(rp.data.size() - 28));
            }
        }

        // Decompress collected compressed blocks
        for (const auto& [offset, entry] : compressedMap) {
            // Allocate generous output buffer (original block ≤ PARTSIZE)
            uLongf destLen = entry.totalCompressedLen * 10 + 300;
            if (destLen < 65536)
                destLen = 65536;
            std::vector<uint8> decompressed(destLen);

            int zResult = uncompress(decompressed.data(), &destLen,
                                     reinterpret_cast<const uint8*>(entry.compressedData.constData()),
                                     static_cast<uLong>(entry.compressedData.size()));
            if (zResult == Z_OK) {
                DataBlock block;
                block.start = entry.start;
                block.end = entry.start + destLen;
                block.data = QByteArray(reinterpret_cast<const char*>(decompressed.data()),
                                        static_cast<qsizetype>(destLen));
                blocks.push_back(std::move(block));
            } else {
                qWarning("zlib uncompress failed for block at offset %llu: error %d",
                         static_cast<unsigned long long>(entry.start), zResult);
            }
        }

        return blocks;
    }

protected:
    bool packetReceived(Packet* packet) override
    {
        ReceivedPacket rp;
        rp.opcode = packet->opcode;
        rp.prot = packet->prot;
        if (packet->pBuffer && packet->size > 0)
            rp.data.assign(packet->pBuffer, packet->pBuffer + packet->size);
        receivedPackets.push_back(std::move(rp));
        return true;
    }

    void onError(int errorCode) override
    {
        lastErrorCode = errorCode;
    }
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_MockPeerUpload : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void uploadFlow_sendingPartMatchesFile();
    void cleanupTestCase();

private:
    // Packet builders
    std::unique_ptr<Packet> buildHelloPacket();
    std::unique_ptr<Packet> buildEmuleInfo();
    std::unique_ptr<Packet> buildSetReqFileId(const uint8* hash);
    std::unique_ptr<Packet> buildRequestFileName(const uint8* hash);
    std::unique_ptr<Packet> buildStartUploadReq(const uint8* hash);
    std::unique_ptr<Packet> buildRequestParts(const uint8* hash,
                                               uint64 s0, uint64 e0,
                                               uint64 s1, uint64 e1,
                                               uint64 s2, uint64 e2);

    // Infrastructure
    TempDir* m_tmpDir = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    ClientList* m_clientList = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    UploadQueue* m_uploadQueue = nullptr;
    UploadDiskIOThread* m_diskIO = nullptr;
    QTimer m_processTimer;

    // File under test
    QString m_testFilePath;
    std::array<uint8, 16> m_fileHash{};
    uint64 m_fileSize = 0;
    std::array<uint8, 16> m_fakeUserHash{};
};

// ---------------------------------------------------------------------------
// initTestCase — set up upload infrastructure
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::initTestCase()
{
    m_tmpDir = new TempDir();

    // 1. Preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());
    thePrefs.setCryptLayerSupported(true);
    thePrefs.setCryptLayerRequested(true);
    thePrefs.setCryptLayerRequired(false);

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    QDir().mkpath(incomingDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({m_tmpDir->filePath(QStringLiteral("temp"))});

    // 2. Client credits (generates RSA keys → user hash)
    auto* creditsList = new ClientCreditsList();
    theApp.clientCredits = creditsList;

    // 3. Client list
    m_clientList = new ClientList(this);
    theApp.clientList = m_clientList;

    // 4. Listen socket (port 0 = random)
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(0), "Failed to start TCP listener");
    theApp.listenSocket = m_listenSocket;
    thePrefs.setPort(m_listenSocket->connectedPort());

    // Wire incoming connections
    connect(m_listenSocket, &ListenSocket::newClientConnection,
            m_clientList, &ClientList::handleIncomingConnection);

    // 5. Upload bandwidth throttler — NOT started and NOT set on the UploadQueue.
    //    Qt 6 forbids QTcpSocket writes from non-owner threads, but the throttler
    //    thread calls sendFileAndControlData() from its own thread. Instead, we
    //    flush standard/file data packets from the main thread in the process timer.
    //    The throttler object is kept alive (but idle) so theApp pointer is non-null.
    m_throttler = new UploadBandwidthThrottler(this);

    // 6. Known file list
    m_knownFiles = new KnownFileList();
    theApp.knownFileList = m_knownFiles;

    // 7. Shared file list
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.sharedFileList = m_sharedFiles;

    // 8. Upload disk IO thread
    m_diskIO = new UploadDiskIOThread(this);
    m_diskIO->start();

    // 9. Upload queue — throttler intentionally NOT set (see note at step 5)
    m_uploadQueue = new UploadQueue(this);
    m_uploadQueue->setDiskIOThread(m_diskIO);
    m_uploadQueue->setSharedFileList(m_sharedFiles);
    theApp.uploadQueue = m_uploadQueue;

    // 10. Process timer — drives upload queue and listen socket.
    //     Also flushes file data packets from the main thread because
    //     Qt 6 sockets don't support writes from non-owner threads
    //     (the UploadBandwidthThrottler thread).
    connect(&m_processTimer, &QTimer::timeout, this, [this] {
        m_uploadQueue->process();
        m_listenSocket->process();
        // Flush any pending file data from the main thread
        m_uploadQueue->forEachUploading([](UpDownClient* client) {
            if (auto* sock = client->socket())
                sock->sendFileAndControlData(UINT32_MAX, 1);
        });
    });
    m_processTimer.start(100);

    // -----------------------------------------------------------------------
    // Hash the DMG file synchronously and register as shared
    // -----------------------------------------------------------------------

    m_testFilePath = projectDataDir() + QStringLiteral("/incoming/qt-online-installer-macOS-x64-4.10.0.dmg");
    QVERIFY2(QFile::exists(m_testFilePath),
             qPrintable(QStringLiteral("Test file not found: %1").arg(m_testFilePath)));

    QFile dmg(m_testFilePath);
    QVERIFY(dmg.open(QIODevice::ReadOnly));
    m_fileSize = static_cast<uint64>(dmg.size());
    QVERIFY(m_fileSize > 0);

    // Calculate MD4 part hashes
    const uint64 partSize = PARTSIZE;
    const uint64 numParts = (m_fileSize + partSize - 1) / partSize;
    std::vector<std::array<uint8, 16>> partHashes;

    for (uint64 p = 0; p < numParts; ++p) {
        const uint64 offset = p * partSize;
        const uint64 remaining = m_fileSize - offset;
        const auto chunkSize = static_cast<qint64>(std::min(remaining, partSize));

        QByteArray chunk = dmg.read(chunkSize);
        QCOMPARE(chunk.size(), chunkSize);

        MD4Hasher hasher;
        hasher.add(chunk.constData(), static_cast<std::size_t>(chunk.size()));
        hasher.finish();

        std::array<uint8, 16> h{};
        std::memcpy(h.data(), hasher.getHash(), 16);
        partHashes.push_back(h);
    }

    // Compute overall file hash
    if (numParts == 1) {
        // Single part: file hash = part hash
        std::memcpy(m_fileHash.data(), partHashes[0].data(), 16);
    } else {
        // Multi-part: file hash = MD4(concat of all part hashes)
        MD4Hasher fileHasher;
        for (const auto& ph : partHashes)
            fileHasher.add(ph.data(), 16);
        fileHasher.finish();
        std::memcpy(m_fileHash.data(), fileHasher.getHash(), 16);
    }

    // Create KnownFile
    auto* knownFile = new KnownFile();
    knownFile->setFileName(QFileInfo(m_testFilePath).fileName());
    knownFile->setFileSize(EMFileSize(m_fileSize));
    knownFile->setFileHash(m_fileHash.data());
    knownFile->setFilePath(m_testFilePath);
    knownFile->setPath(QFileInfo(m_testFilePath).absolutePath() + QDir::separator());
    knownFile->fileIdentifier().setMD4HashSet(partHashes);

    QVERIFY(m_knownFiles->safeAddKFile(knownFile));
    QVERIFY(m_sharedFiles->safeAddKFile(knownFile));

    // Verify file is findable by hash
    QVERIFY(m_sharedFiles->getFileByID(m_fileHash.data()) != nullptr);

    // Generate a random fake user hash for the mock peer
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : m_fakeUserHash)
        b = static_cast<uint8>(dist(rng));

}

// ---------------------------------------------------------------------------
// Test: Full upload flow with data verification
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::uploadFlow_sendingPartMatchesFile()
{
    // 1. Connect with encryption
    MockDownloader mock;
    mock.setObfuscationConfig(thePrefs.obfuscationConfig());
    mock.setConnectionEncryption(true, thePrefs.userHash().data(), false);
    mock.connectToHost(QHostAddress::LocalHost, m_listenSocket->serverPort());
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
    m_processTimer.stop();

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
// Packet builders
// ---------------------------------------------------------------------------

std::unique_ptr<Packet> tst_MockPeerUpload::buildHelloPacket()
{
    SafeMemFile data;

    // OP_HELLO has a 1-byte hash-size prefix (always 0x10 = 16)
    data.writeUInt8(0x10);

    // 16-byte user hash
    data.writeHash16(m_fakeUserHash.data());

    // Client ID (high ID: 127.0.0.1 in network byte order)
    data.writeUInt32(0x7F000001);

    // Port
    data.writeUInt16(4662);

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
// Cleanup
// ---------------------------------------------------------------------------

void tst_MockPeerUpload::cleanupTestCase()
{
    m_processTimer.stop();

    if (m_listenSocket)
        m_listenSocket->stopListening();

    if (m_diskIO) {
        m_diskIO->endThread();
        m_diskIO->wait(5000);
    }

    // Throttler was never started — no need to endThread/wait

    // Reset all globals
    theApp.uploadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_MockPeerUpload)
#include "tst_MockPeerUpload.moc"
