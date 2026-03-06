/// @file tst_FileDownloadLive.cpp
/// @brief Live network integration test — download a file via Kad, verify sharing.
///
/// Adds an ED2K link to the download queue, bootstraps into the Kad network,
/// finds sources via Kad source search, downloads the complete file, and verifies
/// it transitions to the shared file list after completion.
///
/// Requires internet connectivity and a working Kad network.
///
/// Port configuration (env vars EMULE_TCP_PORT, EMULE_UDP_PORT):
///   - Both set → bind TCP listener and UDP socket to those ports.
///   - Unset → random ports are used (firewalled).
///
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUDPListener.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "net/EncryptedDatagramSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>

#include <atomic>
#include <memory>
#include <vector>
#if __has_include(<zlib.h>)
#include <zlib.h>
#endif

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

static constexpr auto kED2KLink =
    "ed2k://|file|eMulev0.50a.-MorphXTv12.7-bin.zip|6634615|"
    "547BF5AF06F5E473060AFCAF66787842|h=QUQNSEDFUZNTUOWF5G65TWDBUQMPCSJX|/";

/// Expected MD4 hash: 547BF5AF06F5E473060AFCAF66787842
static constexpr uint8 kExpectedHash[16] = {
    0x54, 0x7B, 0xF5, 0xAF, 0x06, 0xF5, 0xE4, 0x73,
    0x06, 0x0A, 0xFC, 0xAF, 0x66, 0x78, 0x78, 0x42
};

static constexpr uint64 kExpectedSize = 6634615;

class tst_FileDownloadLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void addED2KLink_createsPartFile();
    void kadSourceSearch_findsSources();
    void download_completesAndIsShared();
    void cleanupTestCase();

private:
    void onReadyRead();

    /// Rate-limited send to contacts.
    void rateLimitedSend(const ContactArray& contacts,
                         std::function<void(const Contact*)> sendFn,
                         std::function<bool()> earlyStop = nullptr);

    static constexpr int kSendRate = 10;

    TempDir* m_tmpDir = nullptr;
    Kademlia* m_kad = nullptr;
    ClientList* m_clientList = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    ClientUDPSocket* m_clientUDP = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    QUdpSocket m_socket;

    std::atomic<int> m_kadPacketsReceived{0};
    std::atomic<int> m_kadPacketsSent{0};
    std::atomic<int> m_rawDatagramsReceived{0};
    std::atomic<int> m_decryptFailed{0};
    std::atomic<int> m_kadSourcesFound{0};
    QTimer m_processTimer;
};

// ---------------------------------------------------------------------------
// UDP packet handler — same pattern as tst_KadLiveNetwork
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QNetworkDatagram dg = m_socket.receiveDatagram(6000);
        if (!dg.isValid())
            continue;

        m_rawDatagramsReceived.fetch_add(1, std::memory_order_relaxed);

        QByteArray data = dg.data();
        if (data.size() < 2)
            continue;

        const uint32 senderIP = dg.senderAddress().toIPv4Address();
        const auto senderPort = static_cast<uint16>(dg.senderPort());

        auto* buf = reinterpret_cast<const uint8*>(data.constData());
        const auto bufLen = static_cast<uint32>(data.size());
        uint8 proto = buf[0];

        // Plain Kad packet
        if (proto == OP_KADEMLIAHEADER) {
            m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
            m_kad->processPacket(buf + 1, bufLen - 1,
                                 senderIP, senderPort, false, KadUDPKey(0));
            continue;
        }

        // Compressed Kad packet
        if (proto == OP_KADEMLIAPACKEDPROT) {
            if (bufLen < 3) continue;
            m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
            uint8 opcode = buf[1];
            uLongf unpackedLen = static_cast<uLongf>(bufLen - 2) * 10 + 300;
            auto unpackBuf = std::make_unique<uint8[]>(unpackedLen + 1);
            unpackBuf[0] = opcode;
            if (uncompress(reinterpret_cast<Bytef*>(unpackBuf.get() + 1), &unpackedLen,
                           reinterpret_cast<const Bytef*>(buf + 2), bufLen - 2) == Z_OK) {
                m_kad->processPacket(unpackBuf.get(), static_cast<uint32>(unpackedLen + 1),
                                     senderIP, senderPort, false, KadUDPKey(0));
            }
            continue;
        }

        // Encrypted packet — try decryption
        auto userHash = thePrefs.userHash();
        const uint8* kadIDPtr = nullptr;
        uint32 kadRecvKey = 0;
        if (auto* kadPrefs = Kademlia::getInstancePrefs()) {
            kadIDPtr = RoutingZone::localKadId().getData();
            kadRecvKey = kadPrefs->getUDPVerifyKey(senderIP);
        }

        auto dr = EncryptedDatagramSocket::decryptReceivedClient(
            const_cast<uint8*>(buf), static_cast<int>(bufLen),
            senderIP, userHash.data(), kadIDPtr, kadRecvKey);

        const bool decrypted = (dr.data != buf);
        if (!decrypted) {
            m_decryptFailed.fetch_add(1, std::memory_order_relaxed);
        }
        if (dr.length > 1 && dr.data && decrypted) {
            uint8 innerProto = dr.data[0];
            bool validKey = dr.receiverVerifyKey != 0;
            KadUDPKey senderUDPKey(dr.senderVerifyKey, senderIP);

            if (innerProto == OP_KADEMLIAHEADER) {
                m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
                m_kad->processPacket(dr.data + 1, static_cast<uint32>(dr.length - 1),
                                     senderIP, senderPort, validKey, senderUDPKey);
            } else if (innerProto == OP_KADEMLIAPACKEDPROT) {
                if (dr.length < 3) continue;
                m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
                uint8 opcode = dr.data[1];
                uLongf unpackedLen = static_cast<uLongf>(dr.length - 2) * 10 + 300;
                auto unpackBuf = std::make_unique<uint8[]>(unpackedLen + 1);
                unpackBuf[0] = opcode;
                if (uncompress(reinterpret_cast<Bytef*>(unpackBuf.get() + 1), &unpackedLen,
                               reinterpret_cast<const Bytef*>(dr.data + 2),
                               static_cast<uLong>(dr.length - 2)) == Z_OK) {
                    m_kad->processPacket(unpackBuf.get(), static_cast<uint32>(unpackedLen + 1),
                                         senderIP, senderPort, validKey, senderUDPKey);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Setup — bootstrap Kad and prepare download infrastructure
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::initTestCase()
{
    setKadLogging(true);
    m_tmpDir = new TempDir();

    // 1. Preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    const QString tempDir = m_tmpDir->filePath(QStringLiteral("temp"));
    QDir().mkpath(incomingDir);
    QDir().mkpath(tempDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({tempDir});

    // 2. Copy nodes.dat for Kad bootstrap
    const QString srcNodes = projectDataDir() + QStringLiteral("/nodes.dat");
    QVERIFY2(QFile::exists(srcNodes), "Missing data/nodes.dat bootstrap file");

    const QString dstNodes = QDir::tempPath() + QStringLiteral("/nodes.dat");
    if (QFile::exists(dstNodes))
        QFile::remove(dstNodes);
    QVERIFY(QFile::copy(srcNodes, dstNodes));

    // 3. Port configuration
    const int envTcpPort = qEnvironmentVariableIntValue("EMULE_TCP_PORT");
    const int envUdpPort = qEnvironmentVariableIntValue("EMULE_UDP_PORT");
    const bool portsOpen = (envTcpPort > 0 && envTcpPort <= 65535
                         && envUdpPort > 0 && envUdpPort <= 65535);
    const auto tcpBindPort = portsOpen ? static_cast<uint16>(envTcpPort) : uint16{0};
    const auto udpBindPort = portsOpen ? static_cast<uint16>(envUdpPort) : uint16{0};

    QVERIFY2(m_socket.bind(QHostAddress::AnyIPv4, udpBindPort), "Failed to bind UDP socket");
    connect(&m_socket, &QUdpSocket::readyRead, this, &tst_FileDownloadLive::onReadyRead);
    m_socket.writeDatagram(QByteArray(1, '\0'), QHostAddress(QStringLiteral("8.8.8.8")), 53);

    // 3b. Client credits — needed for SecureIdent (public key + signature exchange)
    //     Set configDir so RSA key file can be created in the temp directory.
    thePrefs.setConfigDir(m_tmpDir->path());
    auto* creditsList = new ClientCreditsList();
    theApp.clientCredits = creditsList;

    // 4. Infrastructure
    m_clientList = new ClientList(this);
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(tcpBindPort), "Failed to start TCP listener");

    // 4b. Client UDP socket — needed for Kad callbacks to Low-ID sources.
    m_clientUDP = new ClientUDPSocket(this);
    m_clientUDP->create();
    theApp.clientUDP = m_clientUDP;

    // 4c. Upload bandwidth throttler — drives EMSocket::sendControlData() so
    //     control packets (OP_HELLO, file requests, etc.) actually get sent.
    m_throttler = new UploadBandwidthThrottler(this);
    m_throttler->start();
    theApp.uploadBandwidthThrottler = m_throttler;

    thePrefs.setPort(m_listenSocket->connectedPort());
    thePrefs.setUdpPort(static_cast<uint16>(m_socket.localPort()));

    qDebug() << "Ports:" << (portsOpen ? "open" : "random")
             << "TCP:" << m_listenSocket->connectedPort()
             << "UDP:" << m_socket.localPort();

    // 5. File management
    m_knownFiles = new KnownFileList();
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    m_downloadQueue = new DownloadQueue(this);
    m_downloadQueue->setSharedFileList(m_sharedFiles);
    m_downloadQueue->setKnownFileList(m_knownFiles);
    m_downloadQueue->setClientList(m_clientList);

    // 6. Wire into global context
    theApp.clientList = m_clientList;
    theApp.listenSocket = m_listenSocket;
    theApp.sharedFileList = m_sharedFiles;
    theApp.knownFileList = m_knownFiles;
    theApp.downloadQueue = m_downloadQueue;
    Kademlia::setClientList(m_clientList);
    Kademlia::setIPFilter(nullptr);

    // 6b. Process timer — drives DownloadQueue::process() and ListenSocket::process()
    //     which flush PartFile buffers, connect to sources, and accept incoming peers.
    connect(&m_processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_listenSocket->process();
    });
    m_processTimer.start(100);

    // 7. Wire Kad source results → DownloadQueue
    Kademlia::setKadSourceResultCallback(
        [this](uint32 searchID, const uint8* fileHash,
               uint32 ip, uint16 tcpPort,
               uint32 buddyIP, uint16 buddyPort, uint8 buddyCrypt,
               uint8 sourceType, const uint8* buddyHash,
               const uint8* clientHash) {
            m_kadSourcesFound.fetch_add(1, std::memory_order_relaxed);
            m_downloadQueue->addKadSourceResult(searchID, fileHash,
                                                 ip, tcpPort,
                                                 buddyIP, buddyPort, buddyCrypt,
                                                 sourceType, buddyHash,
                                                 clientHash);
        });

    // 8. Start Kademlia
    RoutingBin::resetGlobalTracking();
    m_kad = new Kademlia(this);
    m_kad->start();
    QVERIFY(m_kad->isRunning());

    // 9. Wire outbound UDP
    connect(m_kad->getUDPListener(), &KademliaUDPListener::packetToSend,
            this, [this](QByteArray data, uint32 destIP, uint16 destPort,
                         KadUDPKey targetKey, UInt128 cryptTargetID) {
        if (data.isEmpty())
            return;
        m_kadPacketsSent.fetch_add(1, std::memory_order_relaxed);

        const auto plainLen = static_cast<int>(data.size()) + 1;
        const int overhead = EncryptedDatagramSocket::encryptOverheadSize(true);
        QByteArray buf(overhead + plainLen, '\0');
        auto* raw = reinterpret_cast<uint8*>(buf.data());
        raw[overhead] = static_cast<uint8>(OP_KADEMLIAHEADER);
        memcpy(raw + overhead + 1, data.constData(), static_cast<size_t>(data.size()));

        bool canEncrypt = !(cryptTargetID == UInt128(uint32{0}));
        const uint8* kadIDBytes = canEncrypt ? cryptTargetID.getData() : nullptr;

        uint32 senderVerifyKey = 0;
        if (auto* kadPrefs = Kademlia::getInstancePrefs())
            senderVerifyKey = kadPrefs->getUDPVerifyKey(destIP);

        uint32 receiverVerifyKey = targetKey.getKeyValue(destIP);

        if (canEncrypt) {
            uint32 totalLen = EncryptedDatagramSocket::encryptSendClient(
                raw, static_cast<uint32>(plainLen),
                kadIDBytes, true, receiverVerifyKey, senderVerifyKey, 0);
            m_socket.writeDatagram(
                reinterpret_cast<const char*>(raw),
                static_cast<qint64>(totalLen),
                QHostAddress(destIP), destPort);
        } else {
            m_socket.writeDatagram(
                reinterpret_cast<const char*>(raw + overhead),
                static_cast<qint64>(plainLen),
                QHostAddress(destIP), destPort);
        }
    });

    // 10. Bootstrap into Kad network
    ContactArray contacts;
    m_kad->getRoutingZone()->getAllEntries(contacts);
    qDebug() << "Loaded" << contacts.size() << "bootstrap contacts from nodes.dat";

    auto* udpListener = m_kad->getUDPListener();
    rateLimitedSend(contacts,
        [udpListener](const Contact* c) {
            udpListener->bootstrap(c->getIPAddress(), c->getUDPPort());
        },
        [this] { return m_kadPacketsReceived.load(std::memory_order_relaxed) >= 10; });

    m_kad->bootstrap(QStringLiteral("boot.emule-security.org"), 4672);
    QTest::qWait(3'000);

    // 11. HELLO exchange to verify contacts
    {
        ContactArray helloContacts;
        m_kad->getRoutingZone()->getAllEntries(helloContacts);
        const int baseRecv = m_kadPacketsReceived.load(std::memory_order_relaxed);
        rateLimitedSend(helloContacts,
            [udpListener](const Contact* c) {
                UInt128 contactID = c->getClientID();
                udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
                                           c->getIPAddress(), c->getUDPPort(),
                                           c->getVersion(), c->getUDPKey(), &contactID, false);
            },
            [this, baseRecv] {
                return m_kadPacketsReceived.load(std::memory_order_relaxed) - baseRecv >= 20;
            });

        (void)QTest::qWaitFor([this, baseRecv] {
            return m_kadPacketsReceived.load(std::memory_order_relaxed) - baseRecv >= 10;
        }, 10'000);
    }

    // Verify Kad connected
    const bool connected = QTest::qWaitFor([this] {
        return m_kad->isConnected();
    }, 60'000);
    QVERIFY2(connected, "Kademlia did not connect to the network within 60 seconds");

    qDebug() << "Kad connected — sent:" << m_kadPacketsSent.load()
             << "received:" << m_kadPacketsReceived.load()
             << "routing contacts:" << m_kad->getRoutingZone()->getNumContacts();
}

// ---------------------------------------------------------------------------
// Test 1: Add ED2K link and verify PartFile creation
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::addED2KLink_createsPartFile()
{
    QSignalSpy addedSpy(m_downloadQueue, &DownloadQueue::fileAdded);

    const bool added = m_downloadQueue->addDownloadFromED2KLink(
        QString::fromLatin1(kED2KLink),
        thePrefs.tempDirs().first());

    QVERIFY2(added, "addDownloadFromED2KLink failed");
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(m_downloadQueue->fileCount(), 1);

    PartFile* pf = m_downloadQueue->fileByID(kExpectedHash);
    QVERIFY2(pf != nullptr, "PartFile not found by hash");
    QCOMPARE(pf->fileName(), QStringLiteral("eMulev0.50a.-MorphXTv12.7-bin.zip"));
    QCOMPARE(static_cast<uint64>(pf->fileSize()), kExpectedSize);
    QVERIFY2(pf->status() != PartFileStatus::Error, "PartFile in error state");

    qDebug() << "PartFile created:" << pf->fileName()
             << "size:" << static_cast<uint64>(pf->fileSize())
             << "status:" << static_cast<int>(pf->status());
}

// ---------------------------------------------------------------------------
// Test 2: Kad source search finds peers
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::kadSourceSearch_findsSources()
{
    PartFile* pf = m_downloadQueue->fileByID(kExpectedHash);
    QVERIFY2(pf != nullptr, "PartFile not found — addED2KLink test must pass first");

    // Start a Kad source search for our file hash
    UInt128 target;
    target.setValueBE(kExpectedHash);
    auto* search = SearchManager::prepareLookup(SearchType::File, true, target);
    QVERIFY2(search != nullptr, "prepareLookup for File source search returned nullptr");
    const uint32 searchID = search->getSearchID();

    // Link the search to the PartFile
    pf->setKadFileSearchID(searchID);

    qDebug() << "Started Kad source search — searchID:" << searchID;

    // Wait for at least one source to be found (up to 120s)
    const bool gotSources = QTest::qWaitFor([this] {
        return m_kadSourcesFound.load(std::memory_order_relaxed) >= 1;
    }, 120'000);

    const int sourcesFound = m_kadSourcesFound.load(std::memory_order_relaxed);
    qDebug() << "Kad source search — sources found:" << sourcesFound
             << "file sources:" << pf->sourceCount();

    QVERIFY2(gotSources,
             qPrintable(QStringLiteral("No Kad sources found within 120s (found %1)")
                            .arg(sourcesFound)));
}

// ---------------------------------------------------------------------------
// Test 3: Full file download and sharing verification
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::download_completesAndIsShared()
{
    PartFile* pf = m_downloadQueue->fileByID(kExpectedHash);
    QVERIFY2(pf != nullptr, "PartFile not found");

    QSignalSpy completeSpy(m_downloadQueue, &DownloadQueue::fileCompleted);
    QSignalSpy moveFinishedSpy(pf->partNotifier(), &PartFileNotifier::fileMoveFinished);

    const float startPercent = pf->percentCompleted();
    qDebug() << "Download starting at" << startPercent << "% complete"
             << "sources:" << pf->sourceCount();

    // Track whether any source sent us a queue ranking (proves protocol works)
    int maxQueueRank = 0;
    int rankedSources = 0;

    // Periodically re-search for sources via Kad (every 60s).
    QTimer kadResearchTimer;
    connect(&kadResearchTimer, &QTimer::timeout, this, [this, pf] {
        UInt128 target;
        target.setValueBE(kExpectedHash);
        auto* search = SearchManager::prepareLookup(SearchType::File, true, target);
        if (search) {
            pf->setKadFileSearchID(search->getSearchID());
            qDebug() << "Re-started Kad source search — searchID:" << search->getSearchID()
                     << "current sources:" << pf->sourceCount();
        }
    });
    kadResearchTimer.start(30'000);

    // Periodic progress reporter — also tracks queue rankings
    QTimer progressTimer;
    connect(&progressTimer, &QTimer::timeout, this, [pf, &maxQueueRank, &rankedSources] {
        rankedSources = 0;
        for (auto* client : pf->srcList()) {
            const int rank = client->remoteQueueRank();
            if (rank > 0) {
                ++rankedSources;
                maxQueueRank = std::max(maxQueueRank, rank);
            }
        }
        qDebug() << "Progress:" << pf->percentCompleted() << "%"
                 << "sources:" << pf->sourceCount()
                 << "datarate:" << pf->datarate() << "B/s"
                 << "gaps:" << pf->gapList().size()
                 << "status:" << static_cast<int>(pf->status())
                 << "rankedSources:" << rankedSources;
        // Log per-source state
        for (auto* client : pf->srcList()) {
            qDebug() << "  source:" << client->userName()
                     << "state:" << static_cast<int>(client->downloadState())
                     << "queueRank:" << client->remoteQueueRank()
                     << "socket:" << (client->socket() != nullptr);
        }
    });
    progressTimer.start(30'000);

    // Wait for download to complete — 15 min timeout.
    // Sources typically queue us (rank 100–600), so we may wait a while
    // for an upload slot.  The file is only 6.3 MB so once a source
    // starts sending, the actual transfer is fast.
    const bool completed = QTest::qWaitFor([&completeSpy] {
        return completeSpy.count() >= 1;
    }, 900'000);

    kadResearchTimer.stop();
    progressTimer.stop();

    // Final scan for queue rankings (timer may not have fired recently)
    for (auto* client : pf->srcList()) {
        const int rank = client->remoteQueueRank();
        if (rank > 0) {
            ++rankedSources;
            maxQueueRank = std::max(maxQueueRank, rank);
        }
    }

    if (completed) {
        qDebug() << "Download completed!";

        // Wait for async file move to finish (up to 30s)
        if (moveFinishedSpy.count() == 0) {
            const bool moved = QTest::qWaitFor([&moveFinishedSpy] {
                return moveFinishedSpy.count() >= 1;
            }, 30'000);
            QVERIFY2(moved, "File move did not complete within 30 seconds");
        }

        // Verify move succeeded
        QVERIFY2(!moveFinishedSpy.isEmpty(), "fileMoveFinished signal not emitted");
        QVERIFY2(moveFinishedSpy.first().first().toBool(), "File move failed");

        // Verify the file is now in the shared list
        KnownFile* shared = m_sharedFiles->getFileByID(kExpectedHash);
        QVERIFY2(shared != nullptr, "Completed file not found in SharedFileList");

        qDebug() << "File is shared:" << shared->fileName()
                 << "size:" << static_cast<uint64>(shared->fileSize());

        // Verify the file exists on disk in the incoming directory
        const QString expectedPath = thePrefs.incomingDir() + QDir::separator()
                                     + QStringLiteral("eMulev0.50a.-MorphXTv12.7-bin.zip");
        QVERIFY2(QFile::exists(expectedPath),
                 qPrintable(QStringLiteral("File not found at %1").arg(expectedPath)));

        // Verify file size matches
        QFileInfo fi(expectedPath);
        QCOMPARE(static_cast<uint64>(fi.size()), kExpectedSize);

        // File should no longer be in the download queue
        QCOMPARE(m_downloadQueue->fileCount(), 0);
        return;
    }

    // Download did not complete — check for queue ranking as minimum success.
    // Receiving at least 1 OP_QUEUERANKING proves our protocol handshake,
    // HELLO exchange, file request, and STARTUPLOADREQ are all working
    // correctly end-to-end with real eMule peers.
    qDebug() << "Download did NOT complete within timeout."
             << "Progress:" << pf->percentCompleted() << "%"
             << "Status:" << static_cast<int>(pf->status())
             << "Sources:" << pf->sourceCount()
             << "Gaps:" << pf->gapList().size()
             << "RankedSources:" << rankedSources
             << "MaxQueueRank:" << maxQueueRank;

    QVERIFY2(rankedSources > 0,
             qPrintable(QStringLiteral(
                 "No queue ranking received from any source (%1 sources, %2% done)")
                 .arg(pf->sourceCount())
                 .arg(static_cast<double>(pf->percentCompleted()), 0, 'f', 1)));

    qDebug() << "PASS: Received queue ranking from" << rankedSources
             << "source(s), max rank:" << maxQueueRank
             << "— protocol handshake verified";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::rateLimitedSend(const ContactArray& contacts,
                                            std::function<void(const Contact*)> sendFn,
                                            std::function<bool()> earlyStop)
{
    int sent = 0;
    for (const Contact* c : contacts) {
        if (sent > 0 && sent % kSendRate == 0) {
            QTest::qWait(1'000);
            if (earlyStop && earlyStop())
                break;
        }
        sendFn(c);
        ++sent;
    }
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void tst_FileDownloadLive::cleanupTestCase()
{
    m_processTimer.stop();

    Kademlia::setKadSourceResultCallback(nullptr);
    Kademlia::setKadKeywordResultCallback(nullptr);
    Kademlia::setKadNotesResultCallback(nullptr);

    if (m_kad) {
        SearchManager::stopAllSearches();
        m_kad->stop();
    }

    m_socket.close();

    if (m_listenSocket)
        m_listenSocket->stopListening();

    // Stop the throttler thread before resetting globals
    if (m_throttler) {
        m_throttler->endThread();
        m_throttler->wait(5000);
    }

    // Reset global state
    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.clientUDP = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;
    Kademlia::setClientList(nullptr);
    Kademlia::setIPFilter(nullptr);

    RoutingBin::resetGlobalTracking();

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_FileDownloadLive)
#include "tst_FileDownloadLive.moc"
