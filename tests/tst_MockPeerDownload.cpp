/// @file tst_MockPeerDownload.cpp
/// @brief Mock peer download test — exercises our download pipeline with RC4 obfuscation.
///
/// Creates a mock TCP uploader (using EMSocket with RC4 auto-detection) that
/// accepts an incoming connection from our UpDownClient, drives the ED2K protocol
/// handshake, serves file blocks with a mix of compressed and uncompressed packets,
/// and verifies that the download pipeline (PartFile gap management, block requests,
/// data reception, hash verification, file completion) works correctly.
///
/// Test file: data/incoming/qt-online-installer-macOS-x64-4.10.0.dmg

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/FileIdentifier.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/EncryptedStreamSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTest>
#include <QTimer>

#include <zlib.h>

#include <array>
#include <cstring>
#include <random>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// MockUploaderSocket — EMSocket that auto-responds to download protocol
// ---------------------------------------------------------------------------

class MockUploaderSocket : public EMSocket {
    Q_OBJECT

public:
    explicit MockUploaderSocket(const std::array<uint8, 16>& fakeUserHash,
                                const std::array<uint8, 16>& fileHash,
                                const std::vector<std::array<uint8, 16>>& partHashes,
                                uint64 fileSize,
                                const QString& sourceFilePath,
                                QObject* parent = nullptr)
        : EMSocket(parent)
        , m_fakeUserHash(fakeUserHash)
        , m_fileHash(fileHash)
        , m_partHashes(partHashes)
        , m_fileSize(fileSize)
        , m_sourceFilePath(sourceFilePath)
    {
    }

    int compressedBlocksSent() const { return m_compressedBlocksSent; }
    int uncompressedBlocksSent() const { return m_uncompressedBlocksSent; }
    bool corruptionDelivered() const { return m_corruptionDelivered; }
    int aichRequestsReceived() const { return m_aichRequestsReceived; }

    void setCorruptionConfig(uint32 partNumber, uint32 blockIndex)
    {
        m_corruptionEnabled = true;
        m_corruptPartNumber = partNumber;
        m_corruptBlockIndex = blockIndex;
    }

    void setAICHHashSet(AICHRecoveryHashSet* hashSet) { m_aichHashSet = hashSet; }

protected:
    bool packetReceived(Packet* packet) override
    {
        // OP_HELLO and OP_EMULEINFO both have opcode 0x01 — dispatch by protocol
        if (packet->opcode == 0x01) {
            if (packet->prot == OP_EDONKEYPROT)
                handleHello();
            else if (packet->prot == OP_EMULEPROT)
                handleEmuleInfo();
            return true;
        }

        switch (packet->opcode) {
        case OP_SETREQFILEID:
            // Just acknowledge — file context is set
            break;
        case OP_REQUESTFILENAME:
            handleRequestFileName(packet);
            break;
        case OP_HASHSETREQUEST:
            handleHashSetRequest();
            break;
        case OP_STARTUPLOADREQ:
            handleStartUploadReq();
            break;
        case OP_REQUESTPARTS:
            handleRequestParts(packet->pBuffer, packet->size, false);
            break;
        case OP_REQUESTPARTS_I64:
            handleRequestParts(packet->pBuffer, packet->size, true);
            break;
        case OP_CANCELTRANSFER:
            // Download finished or cancelled — no-op
            break;
        case OP_AICHREQUEST:
            handleAICHRequest(packet->pBuffer, packet->size);
            break;
        default:
            break;
        }
        return true;
    }

    void onError(int errorCode) override
    {
        Q_UNUSED(errorCode);
    }

private:
    void handleHello()
    {
        // Respond with OP_HELLOANSWER (no 0x10 prefix — only OP_HELLO has it)
        SafeMemFile data;

        // User hash
        data.writeHash16(m_fakeUserHash.data());

        // Client ID (high ID: 127.0.0.1)
        data.writeUInt32(0x7F000001);

        // Port
        data.writeUInt16(4662);

        // Tag count
        data.writeUInt32(6);

        // Tags
        Tag(CT_NAME, QStringLiteral("MockUploader")).writeTagToFile(data);
        Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

        const uint32 udpPorts = (static_cast<uint32>(4672) << 16) | 4672;
        Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

        const uint32 miscOpts1 =
            (static_cast<uint32>(1) << 29) | // AICH version = 1
            (static_cast<uint32>(1) << 28) | // Unicode
            (static_cast<uint32>(4) << 24) | // UDP version
            (static_cast<uint32>(1) << 20) | // Data compression
            (static_cast<uint32>(0) << 16) | // Secure ident (0 = none)
            (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) |
            (static_cast<uint32>(2) <<  8) | // Extended requests
            (static_cast<uint32>(1) <<  4) | // Comments
            (static_cast<uint32>(0) <<  3) | // Peer cache
            (static_cast<uint32>(1) <<  2) | // No view shared
            (static_cast<uint32>(1) <<  1) | // Multi packet
            (static_cast<uint32>(0) <<  0);  // Preview
        Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

        const uint32 miscOpts2 =
            (static_cast<uint32>(KADEMLIA_VERSION) << 0) |
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

        const uint32 emuleVer =
            (static_cast<uint32>(0) << 24) |
            (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
            (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
            (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
        Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

        // Server IP + port (0 = not connected)
        data.writeUInt32(0);
        data.writeUInt16(0);

        auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLOANSWER);
        sendPacket(std::move(packet));
    }

    void handleEmuleInfo()
    {
        // Respond with OP_EMULEINFOANSWER
        SafeMemFile data;

        data.writeUInt8((SEND_EMULE_VERSION_MJR << 4) | (SEND_EMULE_VERSION_MIN / 10));
        data.writeUInt8(EMULE_PROTOCOL);

        data.writeUInt32(6);

        Tag(static_cast<uint8>(ET_COMPRESSION), static_cast<uint32>(1)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_UDPVER), static_cast<uint32>(4)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_UDPPORT), static_cast<uint32>(0)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_SOURCEEXCHANGE), static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_COMMENTS), static_cast<uint32>(1)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_EXTENDEDREQUEST), static_cast<uint32>(2)).writeTagToFile(data);

        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_EMULEINFOANSWER);
        sendPacket(std::move(packet));
    }

    void handleRequestFileName(Packet* reqPacket)
    {
        // Extract hash from request (first 16 bytes); ignore extra extended info bytes
        if (!reqPacket->pBuffer || reqPacket->size < 16)
            return;

        const uint8* reqHash = reinterpret_cast<const uint8*>(reqPacket->pBuffer);

        // Send OP_REQFILENAMEANSWER: hash(16) + filename string
        {
            SafeMemFile data;
            data.writeHash16(reqHash);
            data.writeString(QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg"), UTF8Mode::OptBOM);
            auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQFILENAMEANSWER);
            sendPacket(std::move(packet));
        }

        // Send OP_FILESTATUS: hash(16) + partCount(uint16=0) → 0 means complete source
        {
            SafeMemFile data;
            data.writeHash16(reqHash);
            data.writeUInt16(0); // 0 = complete source
            auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_FILESTATUS);
            sendPacket(std::move(packet));
        }
    }

    void handleHashSetRequest()
    {
        // OP_HASHSETANSWER: hash(16) + count(uint16) + N×hash(16)
        SafeMemFile data;
        data.writeHash16(m_fileHash.data());
        data.writeUInt16(static_cast<uint16>(m_partHashes.size()));
        for (const auto& ph : m_partHashes)
            data.writeHash16(ph.data());

        auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HASHSETANSWER);
        sendPacket(std::move(packet));
    }

    void handleStartUploadReq()
    {
        // OP_ACCEPTUPLOADREQ: empty payload
        auto packet = std::make_unique<Packet>(OP_ACCEPTUPLOADREQ, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
    }

    void handleRequestParts(const char* rawData, uint32 size, bool i64Offsets)
    {
        if (!rawData)
            return;

        const uint32 headerSize = i64Offsets ? (16 + 6 * 8) : (16 + 6 * 4);
        if (size < headerSize)
            return;

        // Parse: hash(16) + 3×start + 3×end
        const uint8* data = reinterpret_cast<const uint8*>(rawData);

        uint64 starts[3] = {};
        uint64 ends[3] = {};

        if (i64Offsets) {
            for (int i = 0; i < 3; ++i)
                std::memcpy(&starts[i], data + 16 + i * 8, 8);
            for (int i = 0; i < 3; ++i)
                std::memcpy(&ends[i], data + 16 + 24 + i * 8, 8);
        } else {
            for (int i = 0; i < 3; ++i) {
                uint32 s = 0;
                std::memcpy(&s, data + 16 + i * 4, 4);
                starts[i] = s;
            }
            for (int i = 0; i < 3; ++i) {
                uint32 e = 0;
                std::memcpy(&e, data + 16 + 12 + i * 4, 4);
                ends[i] = e;
            }
        }

        for (int i = 0; i < 3; ++i) {
            if (starts[i] == 0 && ends[i] == 0)
                continue; // unused slot

            // ends[i] is exclusive on the wire
            const uint64 blockStart = starts[i];
            const uint64 blockEnd = ends[i]; // exclusive

            if (blockEnd <= blockStart || blockEnd > m_fileSize)
                continue;

            const uint32 blockSize = static_cast<uint32>(blockEnd - blockStart);

            // Read source data from file
            QFile source(m_sourceFilePath);
            if (!source.open(QIODevice::ReadOnly))
                continue;
            source.seek(static_cast<qint64>(blockStart));
            QByteArray blockData = source.read(static_cast<qint64>(blockSize));
            source.close();
            if (static_cast<uint32>(blockData.size()) != blockSize)
                continue;

            // Corrupt data if configured and this block overlaps the target
            if (m_corruptionEnabled && !m_corruptionDelivered) {
                const uint32 partNum = static_cast<uint32>(blockStart / PARTSIZE);
                if (partNum == m_corruptPartNumber) {
                    const uint64 partOffset = blockStart - (static_cast<uint64>(partNum) * PARTSIZE);
                    const uint64 corruptBlockStart = static_cast<uint64>(m_corruptBlockIndex) * EMBLOCKSIZE;
                    const uint64 corruptBlockEnd = corruptBlockStart + EMBLOCKSIZE;
                    if (partOffset < corruptBlockEnd && (partOffset + blockSize) > corruptBlockStart) {
                        const uint64 flipStart = std::max(partOffset, corruptBlockStart) - partOffset;
                        for (uint32 j = 0; j < 16 && (flipStart + j) < blockSize; ++j)
                            blockData[static_cast<int>(flipStart + j)] ^= 0xFF;
                        m_corruptionDelivered = true;
                    }
                }
            }

            // Alternate compressed/uncompressed based on block counter
            const bool useCompression = (m_blockCounter % 2 == 0);
            ++m_blockCounter;

            if (useCompression) {
                sendCompressedBlock(blockStart, blockData);
            } else {
                sendUncompressedBlock(blockStart, blockEnd, blockData);
            }
        }
    }

    void sendUncompressedBlock(uint64 /*blockStart*/, uint64 blockEnd, const QByteArray& blockData)
    {
        // Split into 10240-byte chunks, matching UploadDiskIOThread::createStandardPackets
        uint32 togo = static_cast<uint32>(blockData.size());
        int readPos = 0;

        while (togo > 0) {
            uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
            togo -= nPacketSize;

            uint64 curEnd = blockEnd - togo;
            uint64 curStart = curEnd - nPacketSize;

            // OP_SENDINGPART: hash(16) + start(4) + end(4, exclusive) + data
            auto packet = std::make_unique<Packet>(OP_SENDINGPART,
                                                    nPacketSize + 24, OP_EDONKEYPROT, false);
            md4cpy(&packet->pBuffer[0], m_fileHash.data());
            uint32 s32 = static_cast<uint32>(curStart);
            uint32 e32 = static_cast<uint32>(curEnd);
            std::memcpy(&packet->pBuffer[16], &s32, 4);
            std::memcpy(&packet->pBuffer[20], &e32, 4);
            std::memcpy(&packet->pBuffer[24], blockData.constData() + readPos, nPacketSize);

            sendPacket(std::move(packet));
            readPos += static_cast<int>(nPacketSize);
        }

        ++m_uncompressedBlocksSent;
    }

    void sendCompressedBlock(uint64 blockStart, const QByteArray& blockData)
    {
        const uint32 originalSize = static_cast<uint32>(blockData.size());
        uLongf compressedSize = originalSize + 300;
        std::vector<uint8> compressed(compressedSize);

        int zResult = compress2(compressed.data(), &compressedSize,
                                reinterpret_cast<const Bytef*>(blockData.constData()),
                                originalSize, 1);

        if (zResult != Z_OK || originalSize <= compressedSize) {
            // Compression didn't help — fall back to uncompressed
            sendUncompressedBlock(blockStart, blockStart + originalSize, blockData);
            --m_blockCounter; // Don't count as compressed since we fell back
            ++m_blockCounter;
            return;
        }

        // Split compressed data into 10240-byte chunks
        // Each chunk has the same header: hash(16) + start(4) + totalCompressedSize(4)
        uint32 togo = static_cast<uint32>(compressedSize);
        int readPos = 0;

        while (togo > 0) {
            uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
            togo -= nPacketSize;

            auto packet = std::make_unique<Packet>(OP_COMPRESSEDPART,
                                                    nPacketSize + 24, OP_EMULEPROT, false);
            md4cpy(&packet->pBuffer[0], m_fileHash.data());
            uint32 s32 = static_cast<uint32>(blockStart);
            std::memcpy(&packet->pBuffer[16], &s32, 4);
            uint32 compLen = static_cast<uint32>(compressedSize);
            std::memcpy(&packet->pBuffer[20], &compLen, 4);
            std::memcpy(&packet->pBuffer[24], compressed.data() + readPos, nPacketSize);

            sendPacket(std::move(packet));
            readPos += static_cast<int>(nPacketSize);
        }

        ++m_compressedBlocksSent;
    }

    void handleAICHRequest(const char* rawData, uint32 size)
    {
        if (!rawData || size < 16 + 2 + kAICHHashSize || !m_aichHashSet)
            return;

        ++m_aichRequestsReceived;

        SafeMemFile file(reinterpret_cast<const uint8*>(rawData), size);
        uint8 fileHash[16];
        file.readHash16(fileHash);
        uint16 partNumber = file.readUInt16();
        AICHHash masterHash(file);

        // Build response: hash(16) + part(2) + masterHash(20) + recovery data
        SafeMemFile response;
        response.writeHash16(fileHash);
        response.writeUInt16(partNumber);
        masterHash.write(response);

        if (!m_aichHashSet->createPartRecoveryData(
                static_cast<uint64>(partNumber) * PARTSIZE, response, true))
            return;

        auto packet = std::make_unique<Packet>(response, OP_EMULEPROT, OP_AICHANSWER);
        sendPacket(std::move(packet));
    }

    std::array<uint8, 16> m_fakeUserHash;
    std::array<uint8, 16> m_fileHash;
    std::vector<std::array<uint8, 16>> m_partHashes;
    uint64 m_fileSize = 0;
    QString m_sourceFilePath;
    uint32 m_blockCounter = 0;
    int m_compressedBlocksSent = 0;
    int m_uncompressedBlocksSent = 0;

    // Corruption support
    bool m_corruptionEnabled = false;
    uint32 m_corruptPartNumber = 0;
    uint32 m_corruptBlockIndex = 2;
    bool m_corruptionDelivered = false;
    int m_aichRequestsReceived = 0;
    AICHRecoveryHashSet* m_aichHashSet = nullptr;
};

// ---------------------------------------------------------------------------
// MockUploader — custom QTcpServer that creates MockUploaderSocket on accept
// ---------------------------------------------------------------------------

class MockUploader : public QTcpServer {
    Q_OBJECT

public:
    explicit MockUploader(const std::array<uint8, 16>& fakeUserHash,
                          const std::array<uint8, 16>& fileHash,
                          const std::vector<std::array<uint8, 16>>& partHashes,
                          uint64 fileSize,
                          const QString& sourceFilePath,
                          QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_fakeUserHash(fakeUserHash)
        , m_fileHash(fileHash)
        , m_partHashes(partHashes)
        , m_fileSize(fileSize)
        , m_sourceFilePath(sourceFilePath)
    {
    }

    bool startListening()
    {
        return listen(QHostAddress::LocalHost, 0);
    }

    bool hasConnection() const { return m_socket != nullptr; }
    MockUploaderSocket* socket() const { return m_socket; }

    int compressedBlocksSent() const { return m_socket ? m_socket->compressedBlocksSent() : 0; }
    int uncompressedBlocksSent() const { return m_socket ? m_socket->uncompressedBlocksSent() : 0; }

    void setCorruptionConfig(uint32 partNumber, uint32 blockIndex)
    {
        m_corruptPartNumber = partNumber;
        m_corruptBlockIndex = blockIndex;
        m_corruptionEnabled = true;
    }

    void setAICHHashSet(AICHRecoveryHashSet* hashSet) { m_aichHashSet = hashSet; }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        m_socket = new MockUploaderSocket(m_fakeUserHash, m_fileHash, m_partHashes,
                                           m_fileSize, m_sourceFilePath, this);
        m_socket->setSocketDescriptor(socketDescriptor);

        // Pass corruption config and AICH hash set to socket
        if (m_corruptionEnabled)
            m_socket->setCorruptionConfig(m_corruptPartNumber, m_corruptBlockIndex);
        if (m_aichHashSet)
            m_socket->setAICHHashSet(m_aichHashSet);

        // Set obfuscation config with the MOCK's user hash — the client derives
        // its RC4 key from the mock's hash, so the server must use the same hash.
        ObfuscationConfig config;
        config.cryptLayerEnabled = true;
        config.cryptLayerRequired = false;
        config.cryptLayerRequiredStrict = false;
        config.userHash = m_fakeUserHash;
        m_socket->setObfuscationConfig(config);
    }

private:
    std::array<uint8, 16> m_fakeUserHash;
    std::array<uint8, 16> m_fileHash;
    std::vector<std::array<uint8, 16>> m_partHashes;
    uint64 m_fileSize = 0;
    QString m_sourceFilePath;
    MockUploaderSocket* m_socket = nullptr;

    // Corruption config — forwarded to socket on accept
    bool m_corruptionEnabled = false;
    uint32 m_corruptPartNumber = 0;
    uint32 m_corruptBlockIndex = 2;
    AICHRecoveryHashSet* m_aichHashSet = nullptr;
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_MockPeerDownload : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void downloadFlow_partFileReachesCompletion();
    void downloadFlow_corruptionDetectedAndRecovered();
    void cleanupTestCase();

private:
    // Infrastructure
    TempDir* m_tmpDir = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    ClientList* m_clientList = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    MockUploader* m_mockUploader = nullptr;
    QTimer m_processTimer;

    // File under test
    QString m_testFilePath;
    std::array<uint8, 16> m_fileHash{};
    std::vector<std::array<uint8, 16>> m_partHashes;
    uint64 m_fileSize = 0;
    std::array<uint8, 16> m_mockUserHash{};

    // AICH hash data
    AICHRecoveryHashSet m_aichHashSet;
    AICHHash m_aichMasterHash;

    // Download objects
    PartFile* m_partFile = nullptr;
    UpDownClient* m_client = nullptr;
};

// ---------------------------------------------------------------------------
// initTestCase — set up download infrastructure
// ---------------------------------------------------------------------------

void tst_MockPeerDownload::initTestCase()
{
    m_tmpDir = new TempDir();

    // 1. Preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());
    thePrefs.setCryptLayerSupported(true);
    thePrefs.setCryptLayerRequested(true);
    thePrefs.setCryptLayerRequired(false);

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    const QString tempDir = m_tmpDir->filePath(QStringLiteral("temp"));
    QDir().mkpath(incomingDir);
    QDir().mkpath(tempDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({tempDir});

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

    connect(m_listenSocket, &ListenSocket::newClientConnection,
            m_clientList, &ClientList::handleIncomingConnection);

    // 5. Known file list
    m_knownFiles = new KnownFileList();
    theApp.knownFileList = m_knownFiles;

    // 6. Shared file list
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.sharedFileList = m_sharedFiles;

    // 7. Download queue
    m_downloadQueue = new DownloadQueue(this);
    m_downloadQueue->setClientList(m_clientList);
    m_downloadQueue->setSharedFileList(m_sharedFiles);
    m_downloadQueue->setKnownFileList(m_knownFiles);
    theApp.downloadQueue = m_downloadQueue;

    // -----------------------------------------------------------------------
    // Hash the DMG file synchronously
    // -----------------------------------------------------------------------

    m_testFilePath = projectDataDir() + QStringLiteral("/incoming/qt-online-installer-macOS-x64-4.10.0.dmg");
    QVERIFY2(QFile::exists(m_testFilePath),
             qPrintable(QStringLiteral("Test file not found: %1").arg(m_testFilePath)));

    QFile dmg(m_testFilePath);
    QVERIFY(dmg.open(QIODevice::ReadOnly));
    m_fileSize = static_cast<uint64>(dmg.size());
    QVERIFY(m_fileSize > 0);

    const uint64 partSize = PARTSIZE;
    const uint64 numParts = (m_fileSize + partSize - 1) / partSize;

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
        m_partHashes.push_back(h);
    }
    dmg.close();

    // Compute overall file hash
    if (numParts == 1) {
        std::memcpy(m_fileHash.data(), m_partHashes[0].data(), 16);
    } else {
        MD4Hasher fileHasher;
        for (const auto& ph : m_partHashes)
            fileHasher.add(ph.data(), 16);
        fileHasher.finish();
        std::memcpy(m_fileHash.data(), fileHasher.getHash(), 16);
    }

    // -----------------------------------------------------------------------
    // Build AICH hash tree for the entire file
    // -----------------------------------------------------------------------
    m_aichHashSet = AICHRecoveryHashSet(EMFileSize(m_fileSize));
    {
        QFile aichFile(m_testFilePath);
        QVERIFY(aichFile.open(QIODevice::ReadOnly));
        uint64 remaining = m_fileSize;
        for (uint64 part = 0; part < numParts; ++part) {
            const uint64 partLength = std::min(remaining, partSize);
            AICHHashTree* partTree = m_aichHashSet.m_hashTree.findHash(
                part * PARTSIZE, partLength);
            QVERIFY(partTree);
            uint8 dummyMd4[16]{};
            KnownFile::createHash(aichFile, partLength, dummyMd4, partTree);
            remaining -= partLength;
        }
        aichFile.close();
    }
    QVERIFY(m_aichHashSet.reCalculateHash(false));
    m_aichHashSet.setStatus(EAICHStatus::HashSetComplete);
    m_aichMasterHash = m_aichHashSet.getMasterHash();

    // -----------------------------------------------------------------------
    // Generate a random fake user hash for the mock uploader
    // -----------------------------------------------------------------------
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : m_mockUserHash)
        b = static_cast<uint8>(dist(rng));

    // -----------------------------------------------------------------------
    // Start mock uploader server
    // -----------------------------------------------------------------------
    m_mockUploader = new MockUploader(m_mockUserHash, m_fileHash, m_partHashes,
                                       m_fileSize, m_testFilePath, this);
    QVERIFY(m_mockUploader->startListening());

    // -----------------------------------------------------------------------
    // Create PartFile for download
    // -----------------------------------------------------------------------
    m_partFile = new PartFile();
    m_partFile->setFileName(QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg"));
    m_partFile->setFileSize(EMFileSize(m_fileSize));
    m_partFile->setFileHash(m_fileHash.data());
    m_partFile->fileIdentifier().setMD4HashSet(m_partHashes);
    m_partFile->aichRecoveryHashSet().setMasterHash(m_aichMasterHash, EAICHStatus::Trusted);

    QVERIFY(m_partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));

    // Add to download queue
    m_downloadQueue->addDownload(m_partFile);

    // -----------------------------------------------------------------------
    // Create UpDownClient targeting the mock uploader
    // -----------------------------------------------------------------------
    m_client = new UpDownClient(
        m_mockUploader->serverPort(),   // port = mock server's listening port
        htonl(0x7F000001),              // userId = 127.0.0.1 as ED2K ID (network order)
        0, 0,                           // serverIP, serverPort = 0 (no server)
        m_partFile,                     // reqFile = our PartFile
        true);                          // ed2kID = true

    // Pre-set mock's identity for RC4 encryption key derivation
    m_client->setUserHash(m_mockUserHash.data());
    m_client->setConnectOptions(0x03, true, false); // supportsCrypt + requestsCrypt

    // Add client as a source for the PartFile
    m_partFile->addSource(m_client);

    // -----------------------------------------------------------------------
    // Process timer — drives download queue and flushes socket data
    // -----------------------------------------------------------------------
    connect(&m_processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_listenSocket->process();
        // Flush control packets from main thread
        if (m_client && m_client->socket())
            m_client->socket()->sendFileAndControlData(UINT32_MAX, 1);
        // Also flush mock uploader socket
        if (m_mockUploader && m_mockUploader->socket())
            m_mockUploader->socket()->sendFileAndControlData(UINT32_MAX, 1);
    });
    m_processTimer.start(100);
}

// ---------------------------------------------------------------------------
// Test: Full download flow with data verification
// ---------------------------------------------------------------------------

void tst_MockPeerDownload::downloadFlow_partFileReachesCompletion()
{
    // 1. Initiate download connection
    QVERIFY(m_client->askForDownload());

    // 2. Wait for mock to accept the connection
    QTRY_VERIFY_WITH_TIMEOUT(m_mockUploader->hasConnection(), 5000);

    // 3. Wait for encryption layer ready on mock's socket
    QTRY_VERIFY_WITH_TIMEOUT(m_mockUploader->socket()->isEncryptionLayerReady(), 5000);

    // 4. Wait for all gaps filled (download complete, hashes verified)
    QTRY_VERIFY_WITH_TIMEOUT(m_partFile->gapList().empty(), 120000);

    // 5. Wait for completion status
    QTRY_VERIFY_WITH_TIMEOUT(
        m_partFile->status() == PartFileStatus::Completing ||
        m_partFile->status() == PartFileStatus::Complete, 30000);

    // 6. Verify completed size
    QCOMPARE(static_cast<uint64>(m_partFile->completedSize()), m_fileSize);

    // 7. Verify both compressed and uncompressed blocks were served
    QVERIFY(m_mockUploader->compressedBlocksSent() > 0);
    QVERIFY(m_mockUploader->uncompressedBlocksSent() > 0);

    // 8. Byte-level spot check: compare first 64KB of completed file with original.
    //    After completion, the file is moved from temp/ to incoming/.
    QFile original(m_testFilePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QByteArray expectedBytes = original.read(65536);
    original.close();

    // Look for the completed file in incoming dir first, then temp dir
    QString completedPath;
    QDir incomingDir(m_tmpDir->filePath(QStringLiteral("incoming")));
    QStringList incomingFiles = incomingDir.entryList(
        {QStringLiteral("*.dmg")}, QDir::Files);
    if (!incomingFiles.isEmpty()) {
        completedPath = incomingDir.filePath(incomingFiles.first());
    } else {
        // Still in temp dir as .part (file move may be async)
        QDir tempDir(m_tmpDir->filePath(QStringLiteral("temp")));
        QStringList partFiles = tempDir.entryList({QStringLiteral("*.part")}, QDir::Files);
        QVERIFY2(!partFiles.isEmpty(), "No completed file found in incoming or temp dir");
        completedPath = tempDir.filePath(partFiles.first());
    }

    QFile completedFile(completedPath);
    QVERIFY(completedFile.open(QIODevice::ReadOnly));
    QByteArray actualBytes = completedFile.read(65536);
    completedFile.close();

    QCOMPARE(actualBytes.size(), expectedBytes.size());
    QVERIFY2(actualBytes == expectedBytes,
             "First 64KB of downloaded file doesn't match original");
}

// ---------------------------------------------------------------------------
// Test: Corruption detected via MD4 mismatch, AICH recovers good blocks
// ---------------------------------------------------------------------------

void tst_MockPeerDownload::downloadFlow_corruptionDetectedAndRecovered()
{
    // 1. Stop previous mock uploader and process timer
    m_processTimer.stop();
    if (m_mockUploader) {
        m_mockUploader->close();
        delete m_mockUploader;
        m_mockUploader = nullptr;
    }

    // 2. Create new MockUploader with corruption enabled
    auto* corruptMock = new MockUploader(m_mockUserHash, m_fileHash, m_partHashes,
                                          m_fileSize, m_testFilePath, this);
    corruptMock->setCorruptionConfig(0 /* part 0 */, 2 /* block 2 within part */);
    corruptMock->setAICHHashSet(&m_aichHashSet);
    QVERIFY(corruptMock->startListening());
    m_mockUploader = corruptMock;

    // 3. Create fresh PartFile in isolated temp dirs
    const QString tempDir2 = m_tmpDir->filePath(QStringLiteral("temp2"));
    const QString incoming2 = m_tmpDir->filePath(QStringLiteral("incoming2"));
    QDir().mkpath(tempDir2);
    QDir().mkpath(incoming2);
    thePrefs.setIncomingDir(incoming2);
    thePrefs.setTempDirs({tempDir2});

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg"));
    partFile->setFileSize(EMFileSize(m_fileSize));
    partFile->setFileHash(m_fileHash.data());
    partFile->fileIdentifier().setMD4HashSet(m_partHashes);
    partFile->aichRecoveryHashSet().setMasterHash(m_aichMasterHash, EAICHStatus::Trusted);

    QVERIFY(partFile->createPartFile(tempDir2));
    m_downloadQueue->addDownload(partFile);
    m_partFile = partFile;

    // 4. Create fresh UpDownClient targeting the new mock
    auto* client = new UpDownClient(
        corruptMock->serverPort(), htonl(0x7F000001),
        0, 0, partFile, true);
    client->setUserHash(m_mockUserHash.data());
    client->setConnectOptions(0x03, true, false);
    partFile->addSource(client);
    m_client = client;

    // 5. Restart process timer
    m_processTimer.start(100);

    // 6. Initiate download
    QVERIFY(client->askForDownload());

    // 7. Wait for connection and encryption handshake
    QTRY_VERIFY_WITH_TIMEOUT(corruptMock->hasConnection(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(corruptMock->socket()->isEncryptionLayerReady(), 5000);

    // 8. Wait for corruption to be delivered
    QTRY_VERIFY_WITH_TIMEOUT(corruptMock->socket()->corruptionDelivered(), 30000);

    // 9. Wait for AICH recovery request (proves corruption was detected)
    QTRY_VERIFY_WITH_TIMEOUT(corruptMock->socket()->aichRequestsReceived() > 0, 60000);

    // 10. Wait for all gaps filled (AICH recovered good blocks, bad block re-downloaded)
    QTRY_VERIFY_WITH_TIMEOUT(partFile->gapList().empty(), 120000);

    // 11. Wait for completion status
    QTRY_VERIFY_WITH_TIMEOUT(
        partFile->status() == PartFileStatus::Completing ||
        partFile->status() == PartFileStatus::Complete, 30000);

    // 12. Verify completed size
    QCOMPARE(static_cast<uint64>(partFile->completedSize()), m_fileSize);

    // 13. Verify AICH recovery was invoked
    QVERIFY(corruptMock->socket()->aichRequestsReceived() > 0);

    // 14. Wait for Complete status (not just Completing) so file is moved
    QTRY_VERIFY_WITH_TIMEOUT(
        partFile->status() == PartFileStatus::Complete, 30000);

    // 15. Spot check first 64KB against original
    QFile original(m_testFilePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QByteArray expectedBytes = original.read(65536);
    original.close();

    // Look for completed file in incoming dir, then temp dir
    QString completedPath;
    QDir incomingDir(incoming2);
    QStringList files = incomingDir.entryList(QDir::Files);
    if (!files.isEmpty()) {
        completedPath = incomingDir.filePath(files.first());
    } else {
        QDir tempDirObj(tempDir2);
        QStringList partFiles = tempDirObj.entryList({QStringLiteral("*.part")}, QDir::Files);
        QVERIFY2(!partFiles.isEmpty(),
                 qPrintable(QStringLiteral("No file in %1 or %2").arg(incoming2, tempDir2)));
        completedPath = tempDirObj.filePath(partFiles.first());
    }

    QFile completedFile(completedPath);
    QVERIFY(completedFile.open(QIODevice::ReadOnly));
    QCOMPARE(completedFile.read(65536), expectedBytes);
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void tst_MockPeerDownload::cleanupTestCase()
{
    m_processTimer.stop();

    if (m_listenSocket)
        m_listenSocket->stopListening();

    // Reset all globals
    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_MockPeerDownload)
#include "tst_MockPeerDownload.moc"
