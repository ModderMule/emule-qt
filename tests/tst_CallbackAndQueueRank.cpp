#include "pch.h"
/// @file tst_CallbackAndQueueRank.cpp
/// @brief Tests for callback mechanisms and queue rank reception.
///
/// Covers the 6 bugs fixed in the callback/queue rank system.
/// Uses real TCP (with and without encryption) and real UDP packets on
/// localhost.  Simulates 3+ nodes per test where appropriate.

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/ClientReqSocket.h"
#include "net/Address.h"
#include "net/ClientUDPSocket.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTest>
#include <QTimer>

using namespace eMule;
using namespace eMule::testing;

// ===========================================================================
// MockSourceSocket — speaks just enough ED2K to test queue rank reception.
// Sends OP_QUEUERANKING after OP_STARTUPLOADREQ.
// ===========================================================================

class MockSourceSocket : public EMSocket {
    Q_OBJECT

public:
    explicit MockSourceSocket(const std::array<uint8, 16>& userHash,
                              const std::array<uint8, 16>& fileHash,
                              uint16 queueRank,
                              QObject* parent = nullptr)
        : EMSocket(parent)
        , m_userHash(userHash)
        , m_fileHash(fileHash)
        , m_queueRank(queueRank)
    {}

    bool receivedStartUpload() const { return m_receivedStartUpload; }
    bool receivedReaskcallbackTcp() const { return m_receivedReaskcallbackTcp; }
    std::vector<uint8> lastReaskcallbackData() const { return m_lastReaskcallbackData; }

protected:
    void onError(int /*errorCode*/) override {}

    bool packetReceived(Packet* packet) override
    {
        if (packet->prot == OP_EDONKEYPROT) {
            switch (packet->opcode) {
            case OP_HELLO:           handleHello(); break;
            case OP_REQUESTFILENAME: handleRequestFileName(); break;
            case OP_SETREQFILEID:    break;
            case OP_STARTUPLOADREQ:  handleStartUploadReq(); break;
            case OP_REQUESTSOURCES:
            case OP_REQUESTSOURCES2: break;
            default: break;
            }
        } else if (packet->prot == OP_EMULEPROT) {
            switch (packet->opcode) {
            case OP_EMULEINFO:        handleEmuleInfo(); break;
            case OP_MULTIPACKET_EXT2: handleRequestFileName(); break;
            case OP_REASKCALLBACKTCP:
                m_receivedReaskcallbackTcp = true;
                if (packet->pBuffer && packet->size > 0)
                    m_lastReaskcallbackData.assign(
                        reinterpret_cast<uint8*>(packet->pBuffer),
                        reinterpret_cast<uint8*>(packet->pBuffer) + packet->size);
                break;
            default: break;
            }
        }
        return true;
    }

private:
    void handleHello()
    {
        // OP_HELLOANSWER has NO 0x10 hash-size prefix (unlike OP_HELLO)
        SafeMemFile data;
        data.writeHash16(m_userHash.data());
        data.writeUInt32(htonl(0x7F000001));
        data.writeUInt16(4662);

        data.writeUInt32(6);
        Tag(CT_NAME, QStringLiteral("MockSource")).writeTagToFile(data);
        Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

        const uint32 udpPorts = (static_cast<uint32>(4672) << 16) | 4672;
        Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

        const uint32 miscOpts1 =
            (1u << 29) | (1u << 28) | (4u << 24) | (1u << 20) |
            (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) |
            (2u << 8) | (1u << 4) | (1u << 2) | (1u << 1);
        Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

        const uint32 miscOpts2 =
            (static_cast<uint32>(KADEMLIA_VERSION) << 0) |
            (1u << 4) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 13);
        Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(data);

        const uint32 emuleVer =
            (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
            (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
            (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
        Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

        data.writeUInt32(0); // server IP
        data.writeUInt16(0); // server port

        auto pkt = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLOANSWER);
        sendPacket(std::move(pkt));
    }

    void handleEmuleInfo()
    {
        SafeMemFile data;
        data.writeUInt8((SEND_EMULE_VERSION_MJR << 4) | (SEND_EMULE_VERSION_MIN / 10));
        data.writeUInt8(EMULE_PROTOCOL);
        data.writeUInt32(4);
        Tag(static_cast<uint8>(ET_COMPRESSION), static_cast<uint32>(1)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_UDPVER), static_cast<uint32>(4)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_SOURCEEXCHANGE), static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_EXTENDEDREQUEST), static_cast<uint32>(2)).writeTagToFile(data);

        auto pkt = std::make_unique<Packet>(data, OP_EMULEPROT, OP_EMULEINFOANSWER);
        sendPacket(std::move(pkt));
    }

    void handleRequestFileName()
    {
        SafeMemFile data;
        data.writeHash16(m_fileHash.data());
        data.writeString(QStringLiteral("testfile.bin"), UTF8Mode::None);
        auto pkt = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQFILENAMEANSWER);
        sendPacket(std::move(pkt));

        SafeMemFile status;
        status.writeHash16(m_fileHash.data());
        status.writeUInt16(0); // 0 = complete source
        auto pkt2 = std::make_unique<Packet>(status, OP_EDONKEYPROT, OP_FILESTATUS);
        sendPacket(std::move(pkt2));
    }

    void handleStartUploadReq()
    {
        m_receivedStartUpload = true;
        // Send OP_QUEUERANKING first — key thing being tested
        auto qrPkt = std::make_unique<Packet>(OP_QUEUERANKING, 12, OP_EMULEPROT);
        pokeUInt16(reinterpret_cast<uint8*>(qrPkt->pBuffer), m_queueRank);
        std::memset(qrPkt->pBuffer + 2, 0, 10);
        sendPacket(std::move(qrPkt));
    }

    std::array<uint8, 16> m_userHash;
    std::array<uint8, 16> m_fileHash;
    uint16 m_queueRank;
    bool m_receivedStartUpload = false;
    bool m_receivedReaskcallbackTcp = false;
    std::vector<uint8> m_lastReaskcallbackData;
};

// ===========================================================================
// MockCallbackPeer — dials US and opens with OP_HELLO.
//
// This is the LowID-callback direction: we asked the peer (via server or Kad) to call
// back, so the peer is the one connecting and the one sending the first hello. It records
// whether we answer with a file request, which is the observable proof that the inbound
// socket was attached to the source that was waiting for it.
// ===========================================================================

class MockCallbackPeer : public EMSocket {
    Q_OBJECT

public:
    /// `muleTags` controls whether the opening hello carries CT_EMULE_VERSION and
    /// CT_EMULE_MISCOPTIONS1. Without them the hello is a plain eD2K one and
    /// processHelloTypePacket() reports isMule == false — which is the input the
    /// inbound OP_EMULEINFO branch keys off (MFC ListenSocket.cpp:274-275).
    /// `allowBrowse` clears the "no view shared files" bit of CT_EMULE_MISCOPTIONS1.
    /// It defaults to false because that is what the original mock advertised and the
    /// other tests in this class are written against it.
    MockCallbackPeer(const std::array<uint8, 16>& userHash, uint16 advertisedPort,
                     QObject* parent = nullptr, bool muleTags = true,
                     bool allowBrowse = false)
        : EMSocket(parent)
        , m_userHash(userHash)
        , m_advertisedPort(advertisedPort)
        , m_muleTags(muleTags)
        , m_allowBrowse(allowBrowse)
    {}

    /// Send the opening OP_HELLO, exactly as a callback source would.
    void sendOpeningHello()
    {
        SafeMemFile data;
        data.writeUInt8(16);                    // hash-size prefix — OP_HELLO only
        data.writeHash16(m_userHash.data());
        data.writeUInt32(htonl(0x7F000001));
        data.writeUInt16(m_advertisedPort);

        data.writeUInt32(m_muleTags ? 4 : 2);
        Tag(CT_NAME, QStringLiteral("MockCallbackPeer")).writeTagToFile(data);
        Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

        // Deliberately no CT_EMULE_UDPPORTS: a zero Kad port keeps UpDownClient::compare()
        // out of its kadPort branch, so this peer can only match the source under test and
        // never some leftover 127.0.0.1 client from an earlier test in this class.
        if (m_muleTags) {
            // Bit 2 is "no view shared files" — the peer refusing to be browsed.
            const uint32 miscOpts1 =
                (1u << 29) | (1u << 28) | (4u << 24) | (1u << 20) |
                (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) |
                (2u << 8) | (1u << 4) | (m_allowBrowse ? 0u : (1u << 2)) | (1u << 1);
            Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

            const uint32 emuleVer =
                (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
                (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
                (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
            Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);
        }

        data.writeUInt32(0); // server IP
        data.writeUInt16(0); // server port

        sendPacket(std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO));
    }

    bool receivedHelloAnswer() const { return m_receivedHelloAnswer; }
    bool receivedFileRequest() const { return m_receivedFileRequest; }
    bool receivedEmuleInfo() const { return m_receivedEmuleInfo; }
    bool receivedAskSharedFiles() const { return m_receivedAskSharedFiles; }
    bool receivedAskSharedDirs() const { return m_receivedAskSharedDirs; }
    void clearSharedFileListFlags()
    {
        m_receivedAskSharedFiles = false;
        m_receivedAskSharedDirs = false;
    }

protected:
    void onError(int /*errorCode*/) override {}

    bool packetReceived(Packet* packet) override
    {
        if (packet->prot == OP_EDONKEYPROT) {
            switch (packet->opcode) {
            case OP_HELLOANSWER:  m_receivedHelloAnswer = true; break;
            // Either shape counts — which one we send depends on the extended-request
            // version negotiated from the hello.
            case OP_REQUESTFILENAME:
            case OP_SETREQFILEID: m_receivedFileRequest = true; break;
            case OP_ASKSHAREDFILES: m_receivedAskSharedFiles = true; break;
            case OP_ASKSHAREDDIRS:  m_receivedAskSharedDirs = true; break;
            default: break;
            }
        } else if (packet->prot == OP_EMULEPROT) {
            switch (packet->opcode) {
            case OP_MULTIPACKET:
            case OP_MULTIPACKET_EXT:
            case OP_MULTIPACKET_EXT2: m_receivedFileRequest = true; break;
            case OP_EMULEINFO: m_receivedEmuleInfo = true; break;
            default: break;
            }
        }
        return true;
    }

private:
    std::array<uint8, 16> m_userHash;
    uint16 m_advertisedPort;
    bool m_muleTags = true;
    bool m_allowBrowse = false;
    bool m_receivedHelloAnswer = false;
    bool m_receivedFileRequest = false;
    bool m_receivedEmuleInfo = false;
    bool m_receivedAskSharedFiles = false;
    bool m_receivedAskSharedDirs = false;
};

// ===========================================================================
// MockSourceServer — QTcpServer wrapper, optionally with encryption
// ===========================================================================

class MockSourceServer : public QTcpServer {
    Q_OBJECT

public:
    MockSourceServer(const std::array<uint8, 16>& userHash,
                     const std::array<uint8, 16>& fileHash,
                     uint16 queueRank,
                     bool enableEncryption,
                     QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_userHash(userHash)
        , m_fileHash(fileHash)
        , m_queueRank(queueRank)
        , m_enableEncryption(enableEncryption)
    {}

    bool startListening() { return listen(QHostAddress::LocalHost, 0); }
    MockSourceSocket* socket() const { return m_socket; }
    bool hasConnection() const { return m_socket != nullptr; }

protected:
    void incomingConnection(qintptr desc) override
    {
        m_socket = new MockSourceSocket(m_userHash, m_fileHash, m_queueRank, this);
        m_socket->setSocketDescriptor(desc);
        if (m_enableEncryption) {
            ObfuscationConfig config;
            config.cryptLayerEnabled = true;
            config.cryptLayerRequired = false;
            config.cryptLayerRequiredStrict = false;
            config.userHash = m_userHash;
            m_socket->setObfuscationConfig(config);
        }
    }

private:
    std::array<uint8, 16> m_userHash;
    std::array<uint8, 16> m_fileHash;
    uint16 m_queueRank;
    bool m_enableEncryption;
    MockSourceSocket* m_socket = nullptr;
};

// ===========================================================================
// Helper: build mule info buffer to set m_udpVer/m_udpPort
// ===========================================================================

static QByteArray buildMinimalMuleInfo()
{
    std::vector<Tag> tags;
    tags.emplace_back(ET_COMPRESSION, static_cast<uint32>(1));
    tags.emplace_back(ET_UDPVER, static_cast<uint32>(4));
    tags.emplace_back(ET_UDPPORT, static_cast<uint32>(4672));

    SafeMemFile data;
    data.writeUInt8(0x01);
    data.writeUInt8(EMULE_PROTOCOL);
    data.writeUInt32(static_cast<uint32>(tags.size()));
    for (const auto& tag : tags)
        tag.writeTagToFile(data);
    return data.buffer();
}

// ===========================================================================
// Helper: flush a ClientUDPSocket without throttler
// ===========================================================================

static void flushUDPSocket(ClientUDPSocket* udp)
{
    // sendControlData processes m_controlQueue → m_sendReadyQueue
    udp->sendControlData(UINT32_MAX, 0);
    // flushSendQueue is a private slot invoked via QueuedConnection — process events to trigger it
    QCoreApplication::processEvents();
}

// ===========================================================================
// Test class
// ===========================================================================

class tst_CallbackAndQueueRank : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // TCP queue rank — encrypted and plaintext
    void queueRank_receivedViaTcp_encrypted();
    void queueRank_receivedViaTcp_plaintext();
    // UDP reask ACK — encrypted and plaintext
    void queueRank_updatedViaUdpReask_encrypted();
    void queueRank_updatedViaUdpReask_plaintext();
    // Kad callback relay (3 nodes, real TCP)
    void kadCallbackRelay_relaysToBuddy();
    // Connecting client timeout
    void connectingTimeout_cleansUpStuckClient();
    // Direct callback state
    void directCallback_setsCorrectState();
    // UDP reask via buddy — real UDP packet (3 nodes)
    void udpReaskViaCallback_sendsRealPacket();
    // A4AF swap between two files
    void swapToAnotherFile_swapsSourceAndTracksA4AF();
    // LowID callback completion — inbound socket attaches to the waiting source
    void lowIdCallback_attachesInboundSocketToWaitingSource();
    void incomingHello_unknownPeer_staysSeparateClient();
    // Inbound OP_EMULEINFO — MFC ListenSocket.cpp:274-275
    void incomingHello_legacyEmulePeer_receivesEmuleInfo();
    void incomingHello_muleHelloPeer_getsNoEmuleInfo();
    // Deferred shared-file-list request — MFC BaseClient.cpp:1567-1573, :1783-1791
    void requestSharedFileList_sendsAfterHandshakeAndRefusesDuplicate();
    // OP_REASKCALLBACKTCP — the IPv6 sentinel relay form
    void reaskCallbackTcp_parsesIPv6SentinelForm();

private:
    // Shared helper for TCP QR tests
    void doTcpQueueRankTest(bool encrypted);
    // Shared helper for UDP reask tests
    void doUdpReaskTest(bool encrypted);

    TempDir* m_tmpDir = nullptr;
    ClientList* m_clientList = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    ClientUDPSocket* m_senderUDP = nullptr;
    ClientUDPSocket* m_receiverUDP = nullptr;
    QTimer m_processTimer;

    std::array<uint8, 16> m_fileHash{};
};

// ===========================================================================
// Setup / Teardown
// ===========================================================================

void tst_CallbackAndQueueRank::initTestCase()
{
    m_tmpDir = new TempDir();

    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());
    thePrefs.setCryptLayerSupported(true);
    thePrefs.setCryptLayerRequested(true);
    thePrefs.setCryptLayerRequired(false);
    thePrefs.setUdpPort(4672);
    // publicIP must match localhost so UDP encryption key derivation works.
    // Sender encrypts with MD5(targetHash + publicIP + ...), receiver decrypts
    // with MD5(userHash + senderIP + ...).  Both must be 127.0.0.1 (host order).
    theApp.setPublicIP(0x7F000001);

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    const QString tempDir = m_tmpDir->filePath(QStringLiteral("temp"));
    QDir().mkpath(incomingDir);
    QDir().mkpath(tempDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({tempDir});

    auto* credits = new ClientCreditsList();
    theApp.clientCredits = credits;

    m_clientList = new ClientList(this);
    theApp.clientList = m_clientList;

    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(0), "Failed to start TCP listener");
    theApp.listenSocket = m_listenSocket;
    thePrefs.setPort(m_listenSocket->connectedPort());

    connect(m_listenSocket, &ListenSocket::newClientConnection,
            m_clientList, &ClientList::handleIncomingConnection);

    m_knownFiles = new KnownFileList();
    theApp.knownFileList = m_knownFiles;

    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.sharedFileList = m_sharedFiles;

    m_downloadQueue = new DownloadQueue(this);
    m_downloadQueue->setClientList(m_clientList);
    m_downloadQueue->setSharedFileList(m_sharedFiles);
    m_downloadQueue->setKnownFileList(m_knownFiles);
    theApp.downloadQueue = m_downloadQueue;

    // Two real UDP sockets for packet tests
    m_receiverUDP = new ClientUDPSocket(this);
    QVERIFY(m_receiverUDP->rebind(0));
    theApp.clientUDP = m_receiverUDP;

    m_senderUDP = new ClientUDPSocket(this);
    QVERIFY(m_senderUDP->rebind(0));

    // Wire the receiver's reaskAckReceived signal (same as CoreSession)
    connect(m_receiverUDP, &ClientUDPSocket::reaskAckReceived,
        this, [](const Endpoint& senderEP, const uint8* data, uint32 size) {
            if (!theApp.clientList)
                return;
            auto* sender = theApp.clientList->findByEndpoint_UDP(senderEP.address(),
                                                                 senderEP.port());
            if (!sender || !sender->reaskPending())
                return;
            SafeMemFile io(data, size);
            if (sender->udpVer() > 3 && sender->reqFile())
                sender->processFileStatus(true, io, sender->reqFile());
            uint16 rank = io.readUInt16();
            sender->setRemoteQueueFull(false);
            sender->udpReaskACK(rank);
            sender->incDownAskedCount();
        });

    std::memset(m_fileHash.data(), 0xAB, 16);

    connect(&m_processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_listenSocket->process();
        m_clientList->process();
    });
    m_processTimer.start(100);
}

void tst_CallbackAndQueueRank::cleanupTestCase()
{
    m_processTimer.stop();

    if (m_listenSocket)
        m_listenSocket->stopListening();

    if (m_downloadQueue && m_knownFiles) {
        for (auto* f : m_downloadQueue->files())
            m_knownFiles->remove(f);
    }

    delete m_downloadQueue;  m_downloadQueue = nullptr;
    delete m_sharedFiles;    m_sharedFiles = nullptr;
    delete m_clientList;     m_clientList = nullptr;
    delete m_listenSocket;   m_listenSocket = nullptr;

    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    theApp.clientUDP = nullptr;

    delete m_senderUDP;   m_senderUDP = nullptr;
    delete m_receiverUDP; m_receiverUDP = nullptr;

    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

// ===========================================================================
// TCP queue rank — shared implementation for encrypted/plaintext
// ===========================================================================

void tst_CallbackAndQueueRank::doTcpQueueRankTest(bool encrypted)
{
    std::array<uint8, 16> mockHash;
    std::memset(mockHash.data(), 0x11, 16);

    MockSourceServer mockServer(mockHash, m_fileHash, 42, encrypted, this);
    QVERIFY(mockServer.startListening());

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("testfile.bin"));
    partFile->setFileSize(EMFileSize(1024 * 1024));
    partFile->setFileHash(m_fileHash.data());
    QVERIFY(partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    m_downloadQueue->addDownload(partFile);

    auto* client = new UpDownClient(
        mockServer.serverPort(),
        htonl(0x7F000001), 0, 0,
        partFile, true);
    client->setUserHash(mockHash.data());
    if (encrypted)
        client->setConnectOptions(0x03, true, false); // supports + requests crypto
    else
        client->setConnectOptions(0x00, false, false);
    partFile->addSource(client);
    m_clientList->addClient(client);

    QVERIFY(client->askForDownload());

    QTRY_VERIFY_WITH_TIMEOUT(mockServer.hasConnection(), 5000);

    if (encrypted)
        QTRY_VERIFY_WITH_TIMEOUT(mockServer.socket()->isEncryptionLayerReady(), 5000);

    auto flushTimer = std::make_unique<QTimer>(this);
    connect(flushTimer.get(), &QTimer::timeout, this, [&mockServer, client] {
        if (mockServer.socket())
            mockServer.socket()->sendFileAndControlData(UINT32_MAX, 1);
        if (client->socket())
            client->socket()->sendFileAndControlData(UINT32_MAX, 1);
    });
    flushTimer->start(50);

    QTRY_VERIFY_WITH_TIMEOUT(mockServer.socket()->receivedStartUpload(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(client->remoteQueueRank(), 42u, 5000);
    QCOMPARE(client->downloadState(), DownloadState::OnQueue);

    flushTimer->stop();
    m_downloadQueue->removeSource(client);
    m_downloadQueue->removeFile(partFile);
}

void tst_CallbackAndQueueRank::queueRank_receivedViaTcp_encrypted()
{
    doTcpQueueRankTest(true);
}

void tst_CallbackAndQueueRank::queueRank_receivedViaTcp_plaintext()
{
    doTcpQueueRankTest(false);
}

// ===========================================================================
// UDP reask ACK — shared implementation for encrypted/plaintext
// Real UDP packets on localhost.
// ===========================================================================

void tst_CallbackAndQueueRank::doUdpReaskTest(bool encrypted)
{
    // Create a client that's already OnQueue.  The sender UDP socket pretends
    // to be this remote source sending OP_REASKACK back to our receiver.
    auto* client = new UpDownClient();
    // IP matches what the UDP datagram source will be. Build it from the string form:
    // feeding a host-order constant into fromNetworkOrder() used to cancel out against
    // the inverted byte order in the lookup, so this test passed for the wrong reason.
    const Address loopback = Address::fromString(QStringLiteral("127.0.0.1"));
    client->setUserAddress(loopback);
    client->setConnectAddress(loopback);
    client->setUserIDHybrid(loopback.toUint32()); // high ID (> 16M)
    client->setUserPort(4662);
    client->setDownloadState(DownloadState::OnQueue);

    // Set the receiver's user hash on the client (for encryption key derivation)
    auto ourHash = thePrefs.userHash();
    client->setUserHash(ourHash.data());

    // processMuleInfoPacket to set m_udpVer (needed for reaskPending checks)
    auto miBuf = buildMinimalMuleInfo();
    client->processMuleInfoPacket(reinterpret_cast<const uint8*>(miBuf.constData()),
                                  static_cast<uint32>(miBuf.size()));

    QCOMPARE(client->udpVer(), uint8{4});
    // UDP port must match the sender socket's port so findByIP_UDP can locate
    // the client when the OP_REASKACK response arrives.  Set AFTER
    // processMuleInfoPacket which overwrites m_udpPort from the ET_UDPPORT tag.
    QVERIFY(m_senderUDP->connectedPort() != 0);
    client->setUDPPort(m_senderUDP->connectedPort());
    QVERIFY(client->supportsUDP());

    m_clientList->addClient(client);

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("testfile.bin"));
    partFile->setFileSize(EMFileSize(1024 * 1024));
    partFile->setFileHash(m_fileHash.data());
    QVERIFY(partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    m_downloadQueue->addDownload(partFile);
    client->setReqFile(partFile);
    partFile->addSource(client);

    // udpReaskForDownload sends OP_REASKFILEPING (real UDP) and sets m_reaskPending
    client->udpReaskForDownload();
    QVERIFY(client->reaskPending());

    // Now build the OP_REASKACK response from the "remote source" and send it
    // back to our receiver via real UDP.
    // Format for UDPv4+: part status (uint16 partCount=0 for complete) + uint16 rank
    const uint16 expectedRank = encrypted ? 77 : 55;
    SafeMemFile ackData;
    ackData.writeUInt16(0); // partCount=0 means complete source (no bitmap)
    ackData.writeUInt16(expectedRank);
    auto ackPkt = std::make_unique<Packet>(ackData, OP_EMULEPROT, OP_REASKACK);

    // Send from m_senderUDP to m_receiverUDP on localhost
    m_senderUDP->sendPacket(std::move(ackPkt),
                            0x7F000001,                      // dest IP (host order)
                            m_receiverUDP->connectedPort(),  // dest port
                            encrypted,                       // encrypt?
                            encrypted ? ourHash.data() : nullptr, // target hash for key
                            false, 0);

    // Flush sender: sendControlData + processEvents triggers flushSendQueue + onReadyRead
    flushUDPSocket(m_senderUDP);
    // Give event loop time for receiver's onReadyRead to fire
    QTest::qWait(200);
    QCoreApplication::processEvents();

    // Verify the queue rank arrived via the real UDP path
    QTRY_COMPARE_WITH_TIMEOUT(client->remoteQueueRank(), static_cast<uint32>(expectedRank), 3000);
    QVERIFY(!client->reaskPending());

    // Cleanup
    m_downloadQueue->removeSource(client);
    m_clientList->removeClient(client);
    m_downloadQueue->removeFile(partFile);
}

void tst_CallbackAndQueueRank::queueRank_updatedViaUdpReask_encrypted()
{
    doUdpReaskTest(true);
}

void tst_CallbackAndQueueRank::queueRank_updatedViaUdpReask_plaintext()
{
    doUdpReaskTest(false);
}

// ===========================================================================
// Kad callback relay — 3 nodes, real TCP
// ===========================================================================

void tst_CallbackAndQueueRank::kadCallbackRelay_relaysToBuddy()
{
    std::array<uint8, 16> buddyHash;
    std::memset(buddyHash.data(), 0x33, 16);

    MockSourceServer buddyServer(buddyHash, m_fileHash, 0, false, this);
    QVERIFY(buddyServer.startListening());

    auto* buddyClient = new UpDownClient(
        buddyServer.serverPort(),
        htonl(0x7F000001),
        0, 0, nullptr, true);
    buddyClient->setUserHash(buddyHash.data());
    buddyClient->setBuddyID(buddyHash.data());
    buddyClient->setConnectOptions(0x00, false, false);
    m_clientList->addClient(buddyClient);

    QVERIFY(buddyClient->tryToConnect());
    QTRY_VERIFY_WITH_TIMEOUT(buddyServer.hasConnection(), 5000);

    auto flushTimer = std::make_unique<QTimer>(this);
    connect(flushTimer.get(), &QTimer::timeout, this, [&buddyServer, buddyClient] {
        if (buddyServer.socket())
            buddyServer.socket()->sendFileAndControlData(UINT32_MAX, 1);
        if (buddyClient->socket())
            buddyClient->socket()->sendFileAndControlData(UINT32_MAX, 1);
    });
    flushTimer->start(50);

    QTRY_VERIFY_WITH_TIMEOUT(buddyClient->socket() != nullptr, 5000);

    m_clientList->setBuddy(buddyClient, BuddyStatus::Connected);
    QCOMPARE(m_clientList->getBuddy(), buddyClient);

    // Build relay payload: buddyID(16) + fileHash(16) + padding
    std::vector<uint8> reaskcallbackData(36, 0);
    std::memcpy(reaskcallbackData.data(), buddyHash.data(), 16);
    std::memcpy(reaskcallbackData.data() + 16, m_fileHash.data(), 16);

    auto* buddy = m_clientList->getBuddy();
    QVERIFY(buddy && buddy->socket());
    QVERIFY(md4equ(reaskcallbackData.data(), buddy->buddyID()));

    const uint32 remoteIP = htonl(0x0A000001);
    const uint16 remotePort = 5555;

    pokeUInt32(reaskcallbackData.data() + 10, remoteIP);
    pokeUInt16(reaskcallbackData.data() + 14, remotePort);
    uint32 relaySize = static_cast<uint32>(reaskcallbackData.size()) - 10;
    auto packet = std::make_unique<Packet>(OP_REASKCALLBACKTCP, relaySize, OP_EMULEPROT);
    std::memcpy(packet->pBuffer, reaskcallbackData.data() + 10, relaySize);
    buddy->sendPacket(std::move(packet));

    QTRY_VERIFY_WITH_TIMEOUT(buddyServer.socket()->receivedReaskcallbackTcp(), 5000);

    const auto& relayedData = buddyServer.socket()->lastReaskcallbackData();
    QVERIFY(relayedData.size() >= 22);
    QCOMPARE(peekUInt32(relayedData.data()), remoteIP);
    QCOMPARE(peekUInt16(relayedData.data() + 4), remotePort);
    QVERIFY(md4equ(relayedData.data() + 6, m_fileHash.data()));

    flushTimer->stop();
    m_clientList->setBuddy(nullptr, BuddyStatus::None);
    m_clientList->removeClient(buddyClient);
    delete buddyClient;
}

// ===========================================================================
// Connecting client timeout
// ===========================================================================

void tst_CallbackAndQueueRank::connectingTimeout_cleansUpStuckClient()
{
    auto* client = new UpDownClient();
    client->setConnectingState(ConnectingState::KadCallback);
    client->setDownloadState(DownloadState::WaitCallbackKad);

    m_clientList->addClient(client);
    m_clientList->addConnectingClient(client);

    m_clientList->processConnectingClients();
    QCOMPARE(client->downloadState(), DownloadState::WaitCallbackKad);
    QCOMPARE(client->connectingState(), ConnectingState::KadCallback);

    m_clientList->removeConnectingClient(client);
    m_clientList->processConnectingClients();
    QCOMPARE(client->downloadState(), DownloadState::WaitCallbackKad);

    client->disconnected(QStringLiteral("Connection try timeout"));
    QCOMPARE(client->connectingState(), ConnectingState::None);
    QCOMPARE(client->downloadState(), DownloadState::None);

    client->setConnectingState(ConnectingState::KadCallback);
    client->setDownloadState(DownloadState::WaitCallbackKad);
    m_clientList->addConnectingClient(client);
    m_clientList->addConnectingClient(client); // duplicate — ignored
    m_clientList->removeConnectingClient(client);
    m_clientList->processConnectingClients();
    QCOMPARE(client->downloadState(), DownloadState::WaitCallbackKad);

    m_clientList->removeClient(client);
    delete client;
}

// ===========================================================================
// Direct callback state
// ===========================================================================

void tst_CallbackAndQueueRank::directCallback_setsCorrectState()
{
    auto* client = new UpDownClient();
    client->setUserIDHybrid(200);
    client->setConnectAddress(Address::fromNetworkOrder(htonl(0x7F000001)));
    client->setUserPort(4662);
    client->setKadPort(4672);

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("testfile.bin"));
    partFile->setFileSize(EMFileSize(1024 * 1024));
    partFile->setFileHash(m_fileHash.data());
    QVERIFY(partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    client->setReqFile(partFile);

    // Process HELLO to set supportsDirectUDPCallback (bit 12 of CT_EMULE_MISCOPTIONS2)
    SafeMemFile helloData;
    helloData.writeUInt8(0x10);
    uint8 fakeHash[16];
    std::memset(fakeHash, 0x44, 16);
    helloData.writeHash16(fakeHash);
    helloData.writeUInt32(200);
    helloData.writeUInt16(4662);
    helloData.writeUInt32(3);

    Tag(CT_NAME, QStringLiteral("FWClient")).writeTagToFile(helloData);
    const uint32 miscOpts2 =
        (static_cast<uint32>(KADEMLIA_VERSION) << 0) |
        (1u << 4) | (1u << 5) | (1u << 7) | (1u << 8) |
        (1u << 12) | (1u << 13);
    Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(helloData);
    Tag(CT_EMULE_VERSION,
        (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |
        (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |
        (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7)).writeTagToFile(helloData);
    helloData.writeUInt32(0);
    helloData.writeUInt16(0);

    const auto& buf = helloData.buffer();
    client->processHelloPacket(reinterpret_cast<const uint8*>(buf.constData()),
                               static_cast<uint32>(buf.size()));

    // A direct UDP callback is delivered to the peer's Kad endpoint, so the client
    // needs one before it can be considered reachable that way.
    client->setKadPort(4672);
    QVERIFY(client->supportsDirectUDPCallback());
    QVERIFY(client->hasLowID());

    m_clientList->addClient(client);

    bool connected = client->tryToConnect();
    QVERIFY(connected);
    QCOMPARE(client->connectingState(), ConnectingState::DirectCallback);

    m_clientList->removeClient(client);
    delete partFile;
    delete client;
}

// ===========================================================================
// UDP reask via buddy — real unencrypted UDP packet to QUdpSocket (3 nodes)
// ===========================================================================

void tst_CallbackAndQueueRank::udpReaskViaCallback_sendsRealPacket()
{
    // --- Node 3: buddy's UDP receiver ---
    QUdpSocket buddyReceiver;
    QVERIFY(buddyReceiver.bind(QHostAddress::LocalHost, 0));
    uint16 buddyPort = buddyReceiver.localPort();

    // --- Node 2: firewalled source with buddy ---
    auto* client = new UpDownClient();
    client->setUserIDHybrid(100);
    client->setConnectAddress(Address());
    client->setUserPort(4662);
    client->setUDPPort(4672);

    uint8 hash[16];
    std::memset(hash, 0x55, 16);
    client->setUserHash(hash);

    uint8 buddyId[16];
    std::memset(buddyId, 0x66, 16);
    client->setBuddyID(buddyId);
    client->setBuddyAddress(Address::fromHostOrder(0x7F000001)); // host byte order for 127.0.0.1
    client->setBuddyPort(buddyPort);

    auto miBuf = buildMinimalMuleInfo();
    client->processMuleInfoPacket(reinterpret_cast<const uint8*>(miBuf.constData()),
                                  static_cast<uint32>(miBuf.size()));
    QVERIFY(client->supportsUDP());
    QVERIFY(client->hasLowID());
    QVERIFY(client->hasValidBuddyID());

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("testfile.bin"));
    partFile->setFileSize(EMFileSize(1024 * 1024));
    partFile->setFileHash(m_fileHash.data());
    QVERIFY(partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    client->setReqFile(partFile);
    client->setDownloadState(DownloadState::OnQueue);

    m_clientList->addClient(client);

    // Call udpReaskForDownload — should queue OP_REASKCALLBACKUDP to buddy
    client->udpReaskForDownload();
    QVERIFY(client->reaskPending());

    // Flush the sender (theApp.clientUDP = m_receiverUDP in this test)
    flushUDPSocket(m_receiverUDP);

    // Read the datagram from the buddy receiver
    QTRY_VERIFY_WITH_TIMEOUT(buddyReceiver.hasPendingDatagrams(), 3000);

    QByteArray dgram = buddyReceiver.receiveDatagram().data();
    QVERIFY(dgram.size() >= 2);

    // OP_REASKCALLBACKUDP is sent UNENCRYPTED (matching MFC behavior).
    // Wire format: [OP_EMULEPROT(0xC5)][OP_REASKCALLBACKUDP(0x94)][payload]
    const auto* raw = reinterpret_cast<const uint8*>(dgram.constData());
    QCOMPARE(raw[0], static_cast<uint8>(OP_EMULEPROT));
    QCOMPARE(raw[1], static_cast<uint8>(OP_REASKCALLBACKUDP));

    // Payload: buddyID(16) + fileHash(16) [+ optional part status + sources count]
    QVERIFY(dgram.size() >= 2 + 32); // header + buddyID + fileHash
    QVERIFY(md4equ(raw + 2, buddyId));       // buddyID matches
    QVERIFY(md4equ(raw + 2 + 16, m_fileHash.data())); // fileHash matches

    // Cleanup
    client->setReqFile(nullptr);
    m_clientList->removeClient(client);
    delete partFile;
    delete client;
}

// ===========================================================================
// A4AF swap — swap source between two files, verify tracking
// ===========================================================================

void tst_CallbackAndQueueRank::swapToAnotherFile_swapsSourceAndTracksA4AF()
{
    // --- Two PartFiles ---
    std::array<uint8, 16> hashA{};
    std::memset(hashA.data(), 0xAA, 16);
    std::array<uint8, 16> hashB{};
    std::memset(hashB.data(), 0xBB, 16);

    auto* fileA = new PartFile();
    fileA->setFileName(QStringLiteral("fileA.bin"));
    fileA->setFileSize(EMFileSize(1024 * 1024));
    fileA->setFileHash(hashA.data());
    QVERIFY(fileA->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    m_downloadQueue->addDownload(fileA);

    auto* fileB = new PartFile();
    fileB->setFileName(QStringLiteral("fileB.bin"));
    fileB->setFileSize(EMFileSize(2 * 1024 * 1024));
    fileB->setFileHash(hashB.data());
    QVERIFY(fileB->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));
    m_downloadQueue->addDownload(fileB);

    // --- Source client assigned to fileA, with fileB as A4AF ---
    auto* client = new UpDownClient();
    client->setUserIDHybrid(htonl(0x7F000001));
    client->setConnectAddress(Address::fromNetworkOrder(0x7F000001));
    client->setUserPort(4662);
    client->setReqFile(fileA);
    fileA->addSource(client);
    client->addRequestForAnotherFile(fileB);
    fileB->a4afSrcList().push_back(client);
    client->setDownloadState(DownloadState::OnQueue);
    client->setRemoteQueueRank(42);
    m_clientList->addClient(client);

    // Pre-swap state
    QCOMPARE(fileA->sourceCount(), 1);
    QCOMPARE(fileB->sourceCount(), 0);
    QCOMPARE(fileB->a4afSourceCount(), 1);
    QCOMPARE(fileA->a4afSourceCount(), 0);
    QCOMPARE(client->reqFile(), fileA);

    // --- Swap fileA → fileB (directed) ---
    bool swapped = client->swapToAnotherFile(
        QStringLiteral("test swap"), false, false, false, fileB);
    QVERIFY(swapped);

    // Client now on fileB
    QCOMPARE(client->reqFile(), fileB);
    QCOMPARE(fileB->sourceCount(), 1);
    QCOMPARE(fileA->sourceCount(), 0);
    // fileA tracks client as A4AF
    QCOMPARE(fileA->a4afSourceCount(), 1);
    QCOMPARE(fileA->a4afSrcList()[0], client);
    // fileB no longer has client as A4AF
    QCOMPARE(fileB->a4afSourceCount(), 0);
    // Queue rank preserved across swap
    QCOMPARE(client->remoteQueueRank(), 42u);

    // --- Swap back fileB → fileA ---
    bool swappedBack = client->swapToAnotherFile(
        QStringLiteral("swap back"), false, false, false, fileA);
    QVERIFY(swappedBack);

    QCOMPARE(client->reqFile(), fileA);
    QCOMPARE(fileA->sourceCount(), 1);
    QCOMPARE(fileB->sourceCount(), 0);
    QCOMPARE(fileB->a4afSourceCount(), 1);
    QCOMPARE(fileB->a4afSrcList()[0], client);
    QCOMPARE(fileA->a4afSourceCount(), 0);
    QCOMPARE(client->remoteQueueRank(), 42u);

    // Cleanup
    m_downloadQueue->removeSource(client);
    m_clientList->removeClient(client);
    m_downloadQueue->removeFile(fileA);
    m_downloadQueue->removeFile(fileB);
}

// ===========================================================================
// LowID callback completion (real TCP, 2 nodes)
//
// The whole point of the callback dance: we cannot dial a firewalled source, so we ask it
// to dial us. Before the attach fix the inbound socket landed on a brand-new, empty client
// while the real source sat in WaitCallback until the 45 s watchdog filed it as dead.
// ===========================================================================

void tst_CallbackAndQueueRank::lowIdCallback_attachesInboundSocketToWaitingSource()
{
    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x5A);
    constexpr uint16 kPeerPort = 4999;   // unique in this class — see MockCallbackPeer

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("callbackfile.bin"));
    partFile->setFileSize(EMFileSize(1024 * 1024));
    std::array<uint8, 16> fileHash{};
    fileHash.fill(0x5B);
    partFile->setFileHash(fileHash.data());
    QVERIFY(partFile->createPartFile(m_tmpDir->filePath(QStringLiteral("temp"))));

    // The source we asked to call back: firewalled, so it has no socket of its own and
    // sits in WaitCallback with the file it owes us already attached.
    auto* source = new UpDownClient();
    source->setUserHash(peerHash.data());
    source->setUserAddress(Address::fromNetworkOrder(htonl(0x7F000001)));
    source->setUserPort(kPeerPort);
    source->setReqFile(partFile);
    source->setDownloadState(DownloadState::WaitCallback);
    m_clientList->addClient(source);

    const int clientsBefore = m_clientList->clientCount();
    QCOMPARE(source->socket(), nullptr);

    // The peer dials us and speaks first, exactly as a callback source does.
    auto* peer = new MockCallbackPeer(peerHash, kPeerPort, this);
    peer->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY2(peer->waitForConnected(5000), "callback peer could not reach the listen socket");
    peer->sendOpeningHello();

    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedHelloAnswer(), 5000);

    // The inbound socket must have been re-homed onto the waiting source...
    QTRY_VERIFY_WITH_TIMEOUT(source->socket() != nullptr, 5000);
    // ...which then leaves WaitCallback...
    QCOMPARE(source->downloadState(), DownloadState::Connected);
    // ...and asks for the file. This is what never happened before the fix: the deferred
    // request waited on an OP_HELLOANSWER that an inbound peer never sends.
    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedFileRequest(), 5000);

    // The throwaway client created for the accepted socket is gone again.
    QCOMPARE(m_clientList->clientCount(), clientsBefore);

    peer->close();
    peer->deleteLater();
    QCoreApplication::processEvents();

    source->setDownloadState(DownloadState::None);
    source->setReqFile(nullptr);
    m_clientList->removeClient(source);
    delete source;
    delete partFile;
}

// ===========================================================================
// Negative control — an unrelated peer must NOT be merged into anything
// ===========================================================================

void tst_CallbackAndQueueRank::incomingHello_unknownPeer_staysSeparateClient()
{
    std::array<uint8, 16> waitingHash{};
    waitingHash.fill(0x6A);
    std::array<uint8, 16> strangerHash{};
    strangerHash.fill(0x6B);
    constexpr uint16 kWaitingPort = 4998;
    constexpr uint16 kStrangerPort = 4997;

    auto* waiting = new UpDownClient();
    waiting->setUserHash(waitingHash.data());
    waiting->setUserAddress(Address::fromNetworkOrder(htonl(0x7F000001)));
    waiting->setUserPort(kWaitingPort);
    waiting->setDownloadState(DownloadState::WaitCallback);
    m_clientList->addClient(waiting);

    const int clientsBefore = m_clientList->clientCount();

    auto* peer = new MockCallbackPeer(strangerHash, kStrangerPort, this);
    peer->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY(peer->waitForConnected(5000));
    peer->sendOpeningHello();

    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedHelloAnswer(), 5000);

    // The stranger keeps its own client object and leaves the waiting source alone.
    QCOMPARE(m_clientList->clientCount(), clientsBefore + 1);
    QCOMPARE(waiting->socket(), nullptr);
    QCOMPARE(waiting->downloadState(), DownloadState::WaitCallback);

    peer->close();
    peer->deleteLater();
    QCoreApplication::processEvents();

    waiting->setDownloadState(DownloadState::None);
    m_clientList->removeClient(waiting);
    delete waiting;
}

void tst_CallbackAndQueueRank::incomingHello_legacyEmulePeer_receivesEmuleInfo()
{
    // A pre-0.42 eMule opens with a plain eD2K hello — its user hash is the only thing
    // that identifies it as an eMule. MFC answers such a peer with an unsolicited
    // OP_EMULEINFO so it learns our capabilities (ListenSocket.cpp:274-275); this port
    // never did, so those peers saw us as a bare eDonkey client.
    std::array<uint8, 16> legacyHash{};
    legacyHash.fill(0x71);
    legacyHash[5] = 14;         // the eMule user-hash marker that hashType() reads
    legacyHash[14] = 111;
    constexpr uint16 kLegacyPort = 4996;

    auto* peer = new MockCallbackPeer(legacyHash, kLegacyPort, this, /*muleTags*/ false);
    peer->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY(peer->waitForConnected(5000));
    peer->sendOpeningHello();

    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedHelloAnswer(), 5000);
    QVERIFY2(peer->receivedEmuleInfo(),
             "an eMule-hashed peer that sent a plain eD2K hello must get OP_EMULEINFO");

    auto* created = m_clientList->findByUserHash(legacyHash.data());
    QVERIFY(created != nullptr);

    peer->close();
    peer->deleteLater();
    QCoreApplication::processEvents();

    m_clientList->removeClient(created);
    delete created;
}

void tst_CallbackAndQueueRank::incomingHello_muleHelloPeer_getsNoEmuleInfo()
{
    // The negative control that makes the test above meaningful. A peer that already
    // sent CT_EMULE_VERSION told us everything in the hello; sending OP_EMULEINFO back
    // would overwrite its "version came from hello" marker with our legacy version byte,
    // which anti-leech modules read as a forged version and ban us for.
    std::array<uint8, 16> muleHash{};
    muleHash.fill(0x72);
    muleHash[5] = 14;           // eMule-shaped hash too, so ONLY isMule differs
    muleHash[14] = 111;
    constexpr uint16 kMulePort = 4995;

    auto* peer = new MockCallbackPeer(muleHash, kMulePort, this, /*muleTags*/ true);
    peer->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY(peer->waitForConnected(5000));
    peer->sendOpeningHello();

    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedHelloAnswer(), 5000);
    QVERIFY2(!peer->receivedEmuleInfo(),
             "a peer that sent an extended (mule) hello must NOT be sent OP_EMULEINFO");

    auto* created = m_clientList->findByUserHash(muleHash.data());
    QVERIFY(created != nullptr);

    peer->close();
    peer->deleteLater();
    QCoreApplication::processEvents();

    m_clientList->removeClient(created);
    delete created;
}

void tst_CallbackAndQueueRank::requestSharedFileList_sendsAfterHandshakeAndRefusesDuplicate()
{
    // requestSharedFileList() used to send inline and bail entirely when there was no
    // socket, so the counter could never be 1 at connect time — which is the value the
    // whole OP_ASKSHAREDDIRS answer chain keys off. It now arms the counter and lets
    // onHandshakeCompleted() do the sending.
    std::array<uint8, 16> browseHash{};
    browseHash.fill(0x73);
    constexpr uint16 kBrowsePort = 4994;

    auto* peer = new MockCallbackPeer(browseHash, kBrowsePort, this,
                                      /*muleTags*/ true, /*allowBrowse*/ true);
    peer->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY(peer->waitForConnected(5000));
    peer->sendOpeningHello();
    QTRY_VERIFY_WITH_TIMEOUT(peer->receivedHelloAnswer(), 5000);

    auto* client = m_clientList->findByUserHash(browseHash.data());
    QVERIFY(client != nullptr);
    QVERIFY(client->checkHandshakeFinished());

    // Already connected, so tryToConnect() reaches onHandshakeCompleted() synchronously.
    const bool expectDirs = client->supportsSharedDirectories();
    client->requestSharedFileList();
    QCOMPARE(client->fileListRequested(), 1);

    if (expectDirs)
        QTRY_VERIFY_WITH_TIMEOUT(peer->receivedAskSharedDirs(), 5000);
    else
        QTRY_VERIFY_WITH_TIMEOUT(peer->receivedAskSharedFiles(), 5000);

    // A second request while one is in flight is refused outright (MFC :1789-1790) —
    // nothing new goes on the wire and the counter is untouched.
    peer->clearSharedFileListFlags();
    client->requestSharedFileList();
    QCOMPARE(client->fileListRequested(), 1);
    QTest::qWait(300);
    QVERIFY2(!peer->receivedAskSharedDirs() && !peer->receivedAskSharedFiles(),
             "a duplicate browse request must not re-send");

    // ...and the counter is released on disconnect, or the refusal would latch forever.
    client->disconnected(QStringLiteral("test teardown"));
    QCOMPARE(client->fileListRequested(), 0);

    peer->close();
    peer->deleteLater();
    QCoreApplication::processEvents();

    m_clientList->removeClient(client);
    delete client;

    // A peer that advertised "no view shared files" is refused before the counter is
    // ever armed — otherwise it would latch at 1 with nothing on the wire to clear it.
    std::array<uint8, 16> privateHash{};
    privateHash.fill(0x74);
    constexpr uint16 kPrivatePort = 4993;

    auto* shy = new MockCallbackPeer(privateHash, kPrivatePort, this,
                                     /*muleTags*/ true, /*allowBrowse*/ false);
    shy->connectToHost(QHostAddress::LocalHost, m_listenSocket->connectedPort());
    QVERIFY(shy->waitForConnected(5000));
    shy->sendOpeningHello();
    QTRY_VERIFY_WITH_TIMEOUT(shy->receivedHelloAnswer(), 5000);

    auto* shyClient = m_clientList->findByUserHash(privateHash.data());
    QVERIFY(shyClient != nullptr);
    shyClient->requestSharedFileList();
    QCOMPARE(shyClient->fileListRequested(), 0);
    QTest::qWait(300);
    QVERIFY(!shy->receivedAskSharedDirs() && !shy->receivedAskSharedFiles());

    shy->close();
    shy->deleteLater();
    QCoreApplication::processEvents();

    m_clientList->removeClient(shyClient);
    delete shyClient;
}

void tst_CallbackAndQueueRank::reaskCallbackTcp_parsesIPv6SentinelForm()
{
    // A buddy relaying an IPv6 requester writes
    //     <0xFFFFFFFF 4><ipv6 16><port 2><filehash 16>[...]
    // instead of the classic <ip 4><port 2><filehash 16>. Before the sentinel branch
    // existed we read 0xFFFFFFFF as the address and the first 16 address bytes as the file
    // hash — silent corruption rather than a clean miss. Asking for a file we do not share
    // proves both halves: a correct parse answers OP_FILENOTFOUND *at the v6 endpoint*,
    // which a corrupt parse could not even address.
    QUdpSocket requester;
    if (!requester.bind(QHostAddress::LocalHostIPv6, 0))
        QSKIP("IPv6 loopback is not available on this host");
    const uint16 requesterPort = requester.localPort();

    auto* buddy = new UpDownClient();
    m_clientList->addClient(buddy);
    m_clientList->setBuddy(buddy, BuddyStatus::Connected);

    const Address requesterV6 = Address::fromString(QStringLiteral("::1"));

    std::array<uint8, 16> unknownHash{};
    unknownHash.fill(0xEE);   // deliberately not in m_sharedFiles

    std::vector<uint8> payload;
    payload.resize(4 + 16 + 2 + 16);
    pokeUInt32(payload.data(), IPV6_SOURCE_SENTINEL);
    std::memcpy(payload.data() + 4, requesterV6.ipv6Bytes().data(), 16);
    pokeUInt16(payload.data() + 20, requesterPort);
    std::memcpy(payload.data() + 22, unknownHash.data(), 16);

    buddy->processReaskCallbackTCP(payload.data(), static_cast<uint32>(payload.size()));

    // Flush the UDP send queue (theApp.clientUDP is m_receiverUDP here).
    flushUDPSocket(m_receiverUDP);
    QTRY_VERIFY_WITH_TIMEOUT(requester.hasPendingDatagrams(), 3000);

    const QByteArray data = requester.receiveDatagram().data();
    QVERIFY(data.size() >= 2);
    const auto* raw = reinterpret_cast<const uint8*>(data.constData());
    QCOMPARE(raw[0], static_cast<uint8>(OP_EMULEPROT));
    QCOMPARE(raw[1], static_cast<uint8>(OP_FILENOTFOUND));

    m_clientList->setBuddy(nullptr, BuddyStatus::None);
    m_clientList->removeClient(buddy);
    delete buddy;
}

QTEST_MAIN(tst_CallbackAndQueueRank)
#include "tst_CallbackAndQueueRank.moc"
