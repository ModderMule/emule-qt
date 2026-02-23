/// @file tst_TcpConnect.cpp
/// @brief Offline TCP download connection test — exercises tryToConnect() →
///        TCP connection → connectionEstablished() → OP_HELLO flow against
///        a local ListenSocket, without requiring internet or Kad.
///        Also tests a full loopback file download via the ED2K protocol.

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/ClientReqSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_TcpConnect : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void tryToConnect_establishesTcpConnection();
    void connectionEstablished_sendsHelloToListener();
    void tryToConnect_alreadyConnecting_returnsTrue();
    void tryToConnect_noConnectIP_returnsFalse();
    void download_completesFile();
    void cleanupTestCase();

private:
    /// Create a minimal PartFile for download connection tests.
    std::unique_ptr<PartFile> makePartFile();

    /// Wire server-side UpDownClient to a freshly accepted ClientReqSocket.
    void wireServerClient(UpDownClient* client, ClientReqSocket* socket);

    TempDir* m_tmpDir = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    ClientList* m_clientList = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;

    // Kept alive across related tests (1 & 2)
    UpDownClient* m_connectedClient = nullptr;
    QSignalSpy* m_helloSpy = nullptr;

    // Download infrastructure (used by test 5)
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    UploadDiskIOThread* m_diskIO = nullptr;
    UploadQueue* m_uploadQueue = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    KnownFile* m_sharedFile = nullptr;
    QList<UpDownClient*> m_serverClients;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::unique_ptr<PartFile> tst_TcpConnect::makePartFile()
{
    auto pf = std::make_unique<PartFile>();
    pf->setFileName(QStringLiteral("test.bin"));
    pf->setFileSize(1'048'576);  // 1 MB
    uint8 hash[16];
    memset(hash, 0xAB, 16);
    pf->setFileHash(hash);
    return pf;
}

void tst_TcpConnect::wireServerClient(UpDownClient* client, ClientReqSocket* socket)
{
    client->setSocket(socket);
    socket->setClient(client);

    // Use string-based connect to access private slots on UpDownClient
    QObject::connect(socket, SIGNAL(helloReceived(const uint8*,uint32,uint8)),
                     client, SLOT(onHelloReceived(const uint8*,uint32,uint8)));

    QObject::connect(socket, SIGNAL(fileRequestReceived(const uint8*,uint32,uint8)),
                     client, SLOT(onFileRequestReceived(const uint8*,uint32,uint8)));

    QObject::connect(socket, SIGNAL(uploadRequestReceived(const uint8*,uint32)),
                     client, SLOT(onUploadRequestReceived(const uint8*,uint32)));

    QObject::connect(socket, SIGNAL(extPacketReceived(const uint8*,uint32,uint8)),
                     client, SLOT(onExtPacketReceived(const uint8*,uint32,uint8)));

    QObject::connect(socket, SIGNAL(packetForClient(const uint8*,uint32,uint8,uint8)),
                     client, SLOT(onPacketForClient(const uint8*,uint32,uint8,uint8)));
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void tst_TcpConnect::initTestCase()
{
    m_tmpDir = new TempDir();

    // 1. Preferences — load with configDir for RSA key generation
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    const QString tempDir = m_tmpDir->filePath(QStringLiteral("temp"));
    QDir().mkpath(incomingDir);
    QDir().mkpath(tempDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({tempDir});

    // 2. Disable encryption — avoids obfuscation handshake on loopback
    thePrefs.setCryptLayerSupported(false);
    thePrefs.setCryptLayerRequested(false);

    // 3. Set finite max upload so UploadQueue slot allocation works
    //    (with UNLIMITED speed and 0 data rate, forceNewClient() never
    //    grants a slot because m_datarate / upPerClient == 0)
    thePrefs.setMaxUpload(100);

    // 4. Client credits — needed by sendHelloPacket()
    auto* creditsList = new ClientCreditsList();
    theApp.clientCredits = creditsList;

    // 5. ClientList
    m_clientList = new ClientList(this);
    theApp.clientList = m_clientList;

    // 6. ListenSocket — bind to random port
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(0), "Failed to start TCP listener");
    theApp.listenSocket = m_listenSocket;

    // 7. UploadBandwidthThrottler — flushes control packets like OP_HELLO
    m_throttler = new UploadBandwidthThrottler(this);
    m_throttler->start();
    theApp.uploadBandwidthThrottler = m_throttler;

    // 8. Configure port from actual bound port
    thePrefs.setPort(m_listenSocket->connectedPort());

    // 9. No IP filter
    theApp.ipFilter = nullptr;

    // 10. KnownFileList + SharedFileList
    m_knownFiles = new KnownFileList();
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.knownFileList = m_knownFiles;
    theApp.sharedFileList = m_sharedFiles;

    // 11. UploadDiskIOThread (auto-starts worker thread in constructor)
    m_diskIO = new UploadDiskIOThread(this);

    // 12. UploadQueue
    m_uploadQueue = new UploadQueue(this);
    m_uploadQueue->setThrottler(m_throttler);
    m_uploadQueue->setDiskIOThread(m_diskIO);
    m_uploadQueue->setSharedFileList(m_sharedFiles);
    theApp.uploadQueue = m_uploadQueue;

    // 13. DownloadQueue
    m_downloadQueue = new DownloadQueue(this);
    m_downloadQueue->setSharedFileList(m_sharedFiles);
    m_downloadQueue->setKnownFileList(m_knownFiles);
    m_downloadQueue->setClientList(m_clientList);
    theApp.downloadQueue = m_downloadQueue;

    // 14. Create KnownFile from data/incoming/readme.txt and share it
    const QString dataIncoming = projectDataDir() + QStringLiteral("/incoming");
    QVERIFY2(QFile::exists(dataIncoming + QStringLiteral("/readme.txt")),
             "Missing data/incoming/readme.txt");

    m_sharedFile = new KnownFile();
    QVERIFY2(m_sharedFile->createFromFile(dataIncoming, QStringLiteral("readme.txt")),
             "Failed to create KnownFile from readme.txt");
    QVERIFY(m_sharedFiles->safeAddKFile(m_sharedFile));
}

// ---------------------------------------------------------------------------
// Test 1: tryToConnect → TCP → connectionEstablished (happy path)
// ---------------------------------------------------------------------------

void tst_TcpConnect::tryToConnect_establishesTcpConnection()
{
    auto partFile = makePartFile();

    // Create client targeting loopback on the listener port.
    // userId = htonl(0x7F000001) with ed2kID=true → m_connectIP = userId (NBO)
    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    partFile.get(), true, this);

    // Capture the server-side socket immediately when accepted and set up
    // the hello spy before event processing delivers the OP_HELLO packet.
    ClientReqSocket* serverSocket = nullptr;
    auto conn = QObject::connect(m_listenSocket, &ListenSocket::newClientConnection,
                                 this, [&](ClientReqSocket* socket) {
        serverSocket = socket;
        m_helloSpy = new QSignalSpy(socket, &ClientReqSocket::helloReceived);
    }, Qt::DirectConnection);

    // Initiate connection
    QVERIFY(client->tryToConnect());
    QCOMPARE(client->connectingState(), ConnectingState::DirectTCP);
    QCOMPARE(client->downloadState(), DownloadState::Connecting);

    // Wait for the listener to accept the connection
    QTRY_VERIFY_WITH_TIMEOUT(serverSocket != nullptr, 5000);
    QObject::disconnect(conn);
    QVERIFY(m_helloSpy != nullptr && m_helloSpy->isValid());

    // Wait for connectionEstablished() to reset connecting state
    QTRY_COMPARE_WITH_TIMEOUT(client->connectingState(), ConnectingState::None, 5000);

    // Download state should transition to Connected
    QCOMPARE(client->downloadState(), DownloadState::Connected);

    // Socket should be set
    QVERIFY(client->socket() != nullptr);

    // Keep client alive for test 2
    m_connectedClient = client;
}

// ---------------------------------------------------------------------------
// Test 2: connectionEstablished sends OP_HELLO to listener
// ---------------------------------------------------------------------------

void tst_TcpConnect::connectionEstablished_sendsHelloToListener()
{
    QVERIFY2(m_connectedClient != nullptr, "Test 1 must pass first");
    QVERIFY2(m_helloSpy != nullptr, "Hello spy not set up from test 1");

    // The throttler flushes the OP_HELLO packet — wait for it to arrive
    QTRY_VERIFY_WITH_TIMEOUT(m_helloSpy->count() >= 1, 10000);

    // Verify the opcode is OP_HELLO
    auto helloArgs = m_helloSpy->first();
    const auto opcode = helloArgs.at(2).value<uint8>();
    QCOMPARE(opcode, static_cast<uint8>(OP_HELLO));

    // Cleanup test 1/2 shared state
    delete m_helloSpy;
    m_helloSpy = nullptr;
    delete m_connectedClient;
    m_connectedClient = nullptr;
}

// ---------------------------------------------------------------------------
// Test 3: Double tryToConnect returns true (already connecting)
// ---------------------------------------------------------------------------

void tst_TcpConnect::tryToConnect_alreadyConnecting_returnsTrue()
{
    auto partFile = makePartFile();

    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    partFile.get(), true, this);

    QVERIFY(client->tryToConnect());
    QCOMPARE(client->connectingState(), ConnectingState::DirectTCP);

    // Second call while still connecting should return true
    QVERIFY(client->tryToConnect());

    // State unchanged
    QCOMPARE(client->connectingState(), ConnectingState::DirectTCP);

    // Let it settle, then clean up
    QTRY_COMPARE_WITH_TIMEOUT(client->connectingState(), ConnectingState::None, 5000);
    delete client;
}

// ---------------------------------------------------------------------------
// Test 4: Low-ID with no server → returns false
// ---------------------------------------------------------------------------

void tst_TcpConnect::tryToConnect_noConnectIP_returnsFalse()
{
    auto partFile = makePartFile();

    // Low ID (< 0x01000000), no server connection → no viable path
    auto* client = new UpDownClient(4662, 100, 0, 0,
                                    partFile.get(), true, this);

    QVERIFY(!client->tryToConnect());
    QCOMPARE(client->connectingState(), ConnectingState::None);

    delete client;
}

// ---------------------------------------------------------------------------
// Test 5: Full loopback file download
// ---------------------------------------------------------------------------

void tst_TcpConnect::download_completesFile()
{
    QVERIFY2(m_sharedFile != nullptr, "Shared KnownFile not set up from initTestCase");

    // -- Cleanup from earlier tests ------------------------------------------
    // Kill orphaned server-side sockets from tests 1–3 so they don't time out
    // and cause cascading disconnects during this test.
    m_listenSocket->killAllSockets();

    // Stop the throttler thread.  The throttler calls sendFileAndControlData()
    // from its own thread, which triggers QSocketNotifier cross-thread warnings
    // and corrupts Qt socket state (QTcpSocket is not thread-safe).
    // We manually drain server socket queues from the main thread instead.
    m_throttler->endThread();

    // -- Server-side client factory ------------------------------------------
    // Create an UpDownClient for each incoming connection and wire all signals.
    // This is only active during this test to avoid interfering with tests 1–4.
    auto factoryConn = QObject::connect(
        m_listenSocket, &ListenSocket::newClientConnection,
        this, [this](ClientReqSocket* socket) {
            auto* serverClient = new UpDownClient(this);
            wireServerClient(serverClient, socket);
            m_serverClients.append(serverClient);
        }, Qt::DirectConnection);

    // -- Process timer -------------------------------------------------------
    // Drives download/upload queues and manually sends queued data on server
    // sockets from the main thread (replacing the stopped throttler thread).
    // ListenSocket::process() is intentionally omitted to avoid timeout checks.
    QTimer processTimer;
    QObject::connect(&processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_uploadQueue->process();

        // Drain standard + control packet queues on server sockets from
        // the main thread.  Without the throttler thread, standard (data)
        // packets would otherwise sit in the queue forever.
        for (auto* serverClient : m_serverClients) {
            if (auto* sock = serverClient->socket())
                sock->sendFileAndControlData(1024 * 1024, 0);
        }
    });
    processTimer.start(100);

    // -- Setup: create PartFile for download ----------------------------------
    const uint8* fileHash = m_sharedFile->fileHash();
    const uint64 fileSize = static_cast<uint64>(m_sharedFile->fileSize());

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("readme.txt"));
    partFile->setFileSize(static_cast<EMFileSize>(fileSize));
    partFile->setFileHash(fileHash);

    const QString tempDir = thePrefs.tempDirs().first();
    QVERIFY2(partFile->createPartFile(tempDir), "Failed to create .part file");
    partFile->setStatus(PartFileStatus::Ready);

    m_downloadQueue->addDownload(partFile);

    // -- Spies ---------------------------------------------------------------
    QSignalSpy moveFinishedSpy(partFile->partNotifier(),
                               &PartFileNotifier::fileMoveFinished);

    // -- Initiate connection -------------------------------------------------
    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    partFile, true, this);

    QVERIFY(client->tryToConnect());

    // -- Wait for download to complete (up to 30s for a 12 KB file) ----------
    QTRY_VERIFY_WITH_TIMEOUT(moveFinishedSpy.count() >= 1, 30000);

    // Verify move succeeded
    QVERIFY2(moveFinishedSpy.first().first().toBool(), "File move failed");

    // Verify PartFile status is Complete
    QCOMPARE(partFile->status(), PartFileStatus::Complete);

    // -- Verify downloaded file ----------------------------------------------
    const QString downloadedPath = thePrefs.incomingDir() + QDir::separator()
                                   + QStringLiteral("readme.txt");
    QVERIFY2(QFile::exists(downloadedPath),
             qPrintable(QStringLiteral("Downloaded file not found at %1").arg(downloadedPath)));

    QFileInfo fi(downloadedPath);
    QCOMPARE(static_cast<uint64>(fi.size()), fileSize);

    // Byte-for-byte content comparison with original
    const QString originalPath = projectDataDir() + QStringLiteral("/incoming/readme.txt");
    QFile original(originalPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QFile downloaded(downloadedPath);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), original.readAll());

    // -- Cleanup: delete downloaded copy, verify original unchanged -----------
    downloaded.close();
    original.close();
    QVERIFY(QFile::remove(downloadedPath));
    QVERIFY2(QFile::exists(originalPath), "Original readme.txt was removed!");

    processTimer.stop();
    QObject::disconnect(factoryConn);
    delete client;
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void tst_TcpConnect::cleanupTestCase()
{
    // Stop throttler
    if (m_throttler) {
        m_throttler->endThread();
        m_throttler->wait(5000);
    }

    // Stop disk IO thread
    if (m_diskIO) {
        m_diskIO->endThread();
        m_diskIO->wait(5000);
    }

    // Stop listener
    if (m_listenSocket)
        m_listenSocket->stopListening();

    // Delete server-side clients created by the factory
    qDeleteAll(m_serverClients);
    m_serverClients.clear();

    // Reset globals
    theApp.downloadQueue = nullptr;
    theApp.uploadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    theApp.ipFilter = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_helloSpy;
    m_helloSpy = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_TcpConnect)
#include "tst_TcpConnect.moc"
