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
    void secureIdent_completesHandshake();
    void download_completesFile();
    void download_askedForAnotherFile();
    void download_completesZipFile();
    void cleanupTestCase();

private:
    /// Create a minimal PartFile for download connection tests.
    std::unique_ptr<PartFile> makePartFile();

    TempDir* m_tmpDir = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    ClientList* m_clientList = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;

    // Kept alive across related tests (1 & 2)
    UpDownClient* m_connectedClient = nullptr;
    std::unique_ptr<PartFile> m_connPartFile;  // prevent use-after-free in test 2
    QSignalSpy* m_helloSpy = nullptr;

    // Download infrastructure (used by test 5)
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    UploadDiskIOThread* m_diskIO = nullptr;
    UploadQueue* m_uploadQueue = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    KnownFile* m_sharedFile = nullptr;
    KnownFile* m_sharedZipFile = nullptr;
    KnownFile* m_sharedDmgFile = nullptr;
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

    // 6b. Wire production incoming connection handler
    QObject::connect(m_listenSocket, &ListenSocket::newClientConnection,
                     m_clientList, &ClientList::handleIncomingConnection);

    // Track server-side clients created by the production handler
    QObject::connect(m_clientList, &ClientList::clientAdded,
                     this, [this](UpDownClient* c) { m_serverClients.append(c); });

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

    // 15. Create KnownFile from data/incoming/eMule0.50a.zip and share it
    QVERIFY2(QFile::exists(dataIncoming + QStringLiteral("/eMule0.50a.zip")),
             "Missing data/incoming/eMule0.50a.zip");

    m_sharedZipFile = new KnownFile();
    QVERIFY2(m_sharedZipFile->createFromFile(dataIncoming, QStringLiteral("eMule0.50a.zip")),
             "Failed to create KnownFile from eMule0.50a.zip");
    QVERIFY(m_sharedFiles->safeAddKFile(m_sharedZipFile));

    // 16. Create KnownFile from data/incoming/qt-online-installer and share it
    const QString dmgName = QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg");
    QVERIFY2(QFile::exists(dataIncoming + QDir::separator() + dmgName),
             "Missing data/incoming/qt-online-installer-macOS-x64-4.10.0.dmg");

    m_sharedDmgFile = new KnownFile();
    QVERIFY2(m_sharedDmgFile->createFromFile(dataIncoming, dmgName),
             "Failed to create KnownFile from qt-online-installer DMG");
    QVERIFY(m_sharedFiles->safeAddKFile(m_sharedDmgFile));
}

// ---------------------------------------------------------------------------
// Test 1: tryToConnect → TCP → connectionEstablished (happy path)
// ---------------------------------------------------------------------------

void tst_TcpConnect::tryToConnect_establishesTcpConnection()
{
    m_connPartFile = makePartFile();

    // Create client targeting loopback on the listener port.
    // userId = htonl(0x7F000001) with ed2kID=true → m_connectIP = userId (NBO)
    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    m_connPartFile.get(), true, this);

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
    m_connPartFile.reset();
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
// Test 5: SecureIdent handshake completes with Identified on both sides
// ---------------------------------------------------------------------------

void tst_TcpConnect::secureIdent_completesHandshake()
{
    // -- Cleanup from earlier tests ------------------------------------------
    m_listenSocket->killAllSockets();
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
    qDeleteAll(m_serverClients);
    m_serverClients.clear();

    // -- Initiate connection (no PartFile — just handshake) ------------------
    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    nullptr, true, this);

    QVERIFY(client->tryToConnect());

    // -- Wait for TCP + HELLO exchange ---------------------------------------
    QTRY_COMPARE_WITH_TIMEOUT(client->connectingState(), ConnectingState::None, 10000);

    // -- Wait for client-side SecureIdent to complete ------------------------
    QTRY_VERIFY_WITH_TIMEOUT(client->hasPassedSecureIdent(false), 15000);

    // Verify client-side
    QVERIFY(client->credits() != nullptr);
    QVERIFY(client->credits()->secIDKeyLen() > 0);
    QCOMPARE(client->credits()->currentIdentState(loopbackNBO),
             IdentState::Identified);
    QCOMPARE(client->secureIdentState(), SecureIdentState::AllRequestsSend);

    // -- Wait for server-side SecureIdent to complete ------------------------
    QVERIFY(!m_serverClients.isEmpty());
    auto* serverClient = m_serverClients.first();
    QTRY_VERIFY_WITH_TIMEOUT(serverClient->hasPassedSecureIdent(false), 15000);

    // Verify server-side
    QVERIFY(serverClient->credits() != nullptr);
    QVERIFY(serverClient->credits()->secIDKeyLen() > 0);
    QCOMPARE(serverClient->secureIdentState(), SecureIdentState::AllRequestsSend);

    // -- Cleanup -------------------------------------------------------------
    delete client;
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
    qDeleteAll(m_serverClients);
    m_serverClients.clear();
}

// ---------------------------------------------------------------------------
// Test 6: Full loopback file download
// ---------------------------------------------------------------------------

void tst_TcpConnect::download_completesFile()
{
    QVERIFY2(m_sharedFile != nullptr, "Shared KnownFile not set up from initTestCase");

    // -- Cleanup from earlier tests ------------------------------------------
    // Kill orphaned server-side sockets from tests 1–3 so they don't time out
    // and cause cascading disconnects during this test.
    m_listenSocket->killAllSockets();
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
    qDeleteAll(m_serverClients);
    m_serverClients.clear();

    // Stop the throttler thread.  The throttler calls sendFileAndControlData()
    // from its own thread, which triggers QSocketNotifier cross-thread warnings
    // and corrupts Qt socket state (QTcpSocket is not thread-safe).
    // We manually drain server socket queues from the main thread instead.
    m_throttler->endThread();

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
    delete client;
}

// ---------------------------------------------------------------------------
// Test 7: Asked for Another File — one client, three files, sequential download
//
// The downloader requests 3 files via A4AF (Ask for Another File), but the
// uploader only ever sends 1 file at a time — as specified in MFC eMule.
// Each UpDownClient has a single m_uploadFile pointer; the server switches
// to the next file only after the current one completes.
// ---------------------------------------------------------------------------

void tst_TcpConnect::download_askedForAnotherFile()
{
    QVERIFY2(m_sharedFile != nullptr, "Shared readme.txt not set up from initTestCase");
    QVERIFY2(m_sharedZipFile != nullptr, "Shared zip not set up from initTestCase");
    QVERIFY2(m_sharedDmgFile != nullptr, "Shared DMG not set up from initTestCase");

    // -- Cleanup from earlier tests ------------------------------------------
    m_listenSocket->killAllSockets();
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
    qDeleteAll(m_serverClients);
    m_serverClients.clear();

    // Stop the throttler thread — same reason as download_completesFile:
    // sendFileAndControlData() from the throttler thread triggers cross-thread
    // QSocketNotifier warnings and corrupts Qt socket state.
    m_throttler->endThread();

    // -- Process timer -------------------------------------------------------
    QTimer processTimer;
    QObject::connect(&processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_uploadQueue->process();

        for (auto* serverClient : m_serverClients) {
            if (auto* sock = serverClient->socket())
                sock->sendFileAndControlData(1024 * 1024, 0);
        }
    });
    processTimer.start(100);

    const QString tempDir = thePrefs.tempDirs().first();

    // -- Setup: create PartFile A (readme.txt) for first download ------------
    const uint8* hashA = m_sharedFile->fileHash();
    const uint64 sizeA = static_cast<uint64>(m_sharedFile->fileSize());

    auto* partFileA = new PartFile();
    partFileA->setFileName(QStringLiteral("readme.txt"));
    partFileA->setFileSize(static_cast<EMFileSize>(sizeA));
    partFileA->setFileHash(hashA);

    QVERIFY2(partFileA->createPartFile(tempDir), "Failed to create .part for readme.txt");
    partFileA->setStatus(PartFileStatus::Ready);

    m_downloadQueue->addDownload(partFileA);

    // -- Setup: create PartFile B (zip) and register as A4AF -----------------
    const uint8* hashB = m_sharedZipFile->fileHash();
    const uint64 sizeB = static_cast<uint64>(m_sharedZipFile->fileSize());

    auto* partFileB = new PartFile();
    partFileB->setFileName(QStringLiteral("eMule0.50a.zip"));
    partFileB->setFileSize(static_cast<EMFileSize>(sizeB));
    partFileB->setFileHash(hashB);

    QVERIFY2(partFileB->createPartFile(tempDir), "Failed to create .part for zip");
    partFileB->setStatus(PartFileStatus::Ready);

    m_downloadQueue->addDownload(partFileB);

    // -- Setup: create PartFile C (qt-online-installer DMG) as A4AF ----------
    const uint8* hashC = m_sharedDmgFile->fileHash();
    const uint64 sizeC = static_cast<uint64>(m_sharedDmgFile->fileSize());

    auto* partFileC = new PartFile();
    partFileC->setFileName(QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg"));
    partFileC->setFileSize(static_cast<EMFileSize>(sizeC));
    partFileC->setFileHash(hashC);

    QVERIFY2(partFileC->createPartFile(tempDir), "Failed to create .part for DMG");
    partFileC->setStatus(PartFileStatus::Ready);

    m_downloadQueue->addDownload(partFileC);

    // -- Spies ---------------------------------------------------------------
    QSignalSpy moveFinishedSpyA(partFileA->partNotifier(),
                                &PartFileNotifier::fileMoveFinished);
    QSignalSpy moveFinishedSpyB(partFileB->partNotifier(),
                                &PartFileNotifier::fileMoveFinished);
    QSignalSpy moveFinishedSpyC(partFileC->partNotifier(),
                                &PartFileNotifier::fileMoveFinished);

    // -- Initiate connection for file A (readme.txt) -------------------------
    const uint32 loopbackNBO = htonl(0x7F000001);
    const uint16 port = m_listenSocket->connectedPort();
    auto* client = new UpDownClient(port, loopbackNBO, 0, 0,
                                    partFileA, true, this);

    // Register files B and C as "asked for another file" on this client.
    // The downloader requests all 3 files, but the server-side uploader
    // only sends 1 file at a time (single m_uploadFile pointer per client).
    QVERIFY(client->addRequestForAnotherFile(partFileB));
    QVERIFY(client->addRequestForAnotherFile(partFileC));

    QVERIFY(client->tryToConnect());

    // ========================================================================
    // File A (readme.txt, ~12 KB) — uploader sends only this file
    // ========================================================================
    QTRY_VERIFY_WITH_TIMEOUT(moveFinishedSpyA.count() >= 1, 30000);
    QVERIFY2(moveFinishedSpyA.first().first().toBool(), "File A move failed");
    QCOMPARE(partFileA->status(), PartFileStatus::Complete);

    // Verify server is still connected and was uploading only 1 file
    QVERIFY(!m_serverClients.isEmpty());
    auto* serverClient = m_serverClients.first();
    QVERIFY(serverClient->uploadFile() != nullptr);

    // -- Switch client to file B (zip) — manual A4AF swap --------------------
    // In real eMule this happens via swapToAnotherFile(). Here we manually
    // switch reqFile and reqUpFileId, then re-send the file request — exactly
    // what doSwap() + the download queue would do.
    client->setReqFile(partFileB);
    client->setReqUpFileId(hashB);
    client->setDownloadState(DownloadState::Connected);
    client->sendFileRequest();
    client->sendStartupLoadReq();

    // ========================================================================
    // File B (eMule0.50a.zip, ~2.9 MB) — uploader switches to this file
    // ========================================================================
    QTRY_VERIFY_WITH_TIMEOUT(moveFinishedSpyB.count() >= 1, 120000);
    QVERIFY2(moveFinishedSpyB.first().first().toBool(), "File B move failed");
    QCOMPARE(partFileB->status(), PartFileStatus::Complete);

    // -- Switch client to file C (DMG) — manual A4AF swap --------------------
    client->setReqFile(partFileC);
    client->setReqUpFileId(hashC);
    client->setDownloadState(DownloadState::Connected);
    client->sendFileRequest();
    client->sendStartupLoadReq();

    // ========================================================================
    // File C (qt-online-installer DMG, ~23 MB, multi-part) — uploader
    // switches to this file; hashset is exchanged automatically for files
    // larger than PARTSIZE (~9.7 MB)
    // ========================================================================
    QTRY_VERIFY_WITH_TIMEOUT(moveFinishedSpyC.count() >= 1, 300000);
    QVERIFY2(moveFinishedSpyC.first().first().toBool(), "File C move failed");
    QCOMPARE(partFileC->status(), PartFileStatus::Complete);

    // -- Verify downloaded files ---------------------------------------------
    const QString downloadedA = thePrefs.incomingDir() + QDir::separator()
                                + QStringLiteral("readme.txt");
    QVERIFY2(QFile::exists(downloadedA),
             qPrintable(QStringLiteral("Downloaded readme.txt not found at %1").arg(downloadedA)));
    QFileInfo fiA(downloadedA);
    QCOMPARE(static_cast<uint64>(fiA.size()), sizeA);

    const QString downloadedB = thePrefs.incomingDir() + QDir::separator()
                                + QStringLiteral("eMule0.50a.zip");
    QVERIFY2(QFile::exists(downloadedB),
             qPrintable(QStringLiteral("Downloaded zip not found at %1").arg(downloadedB)));
    QFileInfo fiB(downloadedB);
    QCOMPARE(static_cast<uint64>(fiB.size()), sizeB);

    const QString downloadedC = thePrefs.incomingDir() + QDir::separator()
                                + QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg");
    QVERIFY2(QFile::exists(downloadedC),
             qPrintable(QStringLiteral("Downloaded DMG not found at %1").arg(downloadedC)));
    QFileInfo fiC(downloadedC);
    QCOMPARE(static_cast<uint64>(fiC.size()), sizeC);

    // Byte-for-byte content comparison with originals
    const QString dataIncoming = projectDataDir() + QStringLiteral("/incoming");

    {
        QFile original(dataIncoming + QStringLiteral("/readme.txt"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QFile downloaded(downloadedA);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), original.readAll());
    }
    {
        QFile original(dataIncoming + QStringLiteral("/eMule0.50a.zip"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QFile downloaded(downloadedB);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), original.readAll());
    }
    {
        QFile original(dataIncoming + QStringLiteral("/qt-online-installer-macOS-x64-4.10.0.dmg"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QFile downloaded(downloadedC);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), original.readAll());
    }

    // -- Cleanup: delete downloaded copies -----------------------------------
    QVERIFY(QFile::remove(downloadedA));
    QVERIFY(QFile::remove(downloadedB));
    QVERIFY(QFile::remove(downloadedC));

    processTimer.stop();
    delete client;
}

// ---------------------------------------------------------------------------
// Test 8: Full loopback file download — eMule0.50a.zip (~2.9 MB)
// ---------------------------------------------------------------------------

void tst_TcpConnect::download_completesZipFile()
{
    QVERIFY2(m_sharedZipFile != nullptr, "Shared KnownFile for zip not set up from initTestCase");

    // -- Cleanup from earlier tests ------------------------------------------
    m_listenSocket->killAllSockets();
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
    qDeleteAll(m_serverClients);
    m_serverClients.clear();

    // -- Process timer -------------------------------------------------------
    QTimer processTimer;
    QObject::connect(&processTimer, &QTimer::timeout, this, [this] {
        m_downloadQueue->process();
        m_uploadQueue->process();

        for (auto* serverClient : m_serverClients) {
            if (auto* sock = serverClient->socket()) {
                sock->sendFileAndControlData(1024 * 1024, 0);
                sock->flush();
            }
        }
    });
    processTimer.start(100);

    // -- Setup: create PartFile for download ----------------------------------
    const uint8* fileHash = m_sharedZipFile->fileHash();
    const uint64 fileSize = static_cast<uint64>(m_sharedZipFile->fileSize());

    auto* partFile = new PartFile();
    partFile->setFileName(QStringLiteral("eMule0.50a.zip"));
    partFile->setFileSize(static_cast<EMFileSize>(fileSize));
    partFile->setFileHash(fileHash);

    const QString tempDir = thePrefs.tempDirs().first();
    QVERIFY2(partFile->createPartFile(tempDir), "Failed to create .part file for zip");
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

    // -- Wait for download to complete (up to 120s for a ~2.9 MB file) -------
    QTRY_VERIFY_WITH_TIMEOUT(moveFinishedSpy.count() >= 1, 120000);

    // Verify move succeeded
    QVERIFY2(moveFinishedSpy.first().first().toBool(), "File move failed");

    // Verify PartFile status is Complete
    QCOMPARE(partFile->status(), PartFileStatus::Complete);

    // -- Verify downloaded file ----------------------------------------------
    const QString downloadedPath = thePrefs.incomingDir() + QDir::separator()
                                   + QStringLiteral("eMule0.50a.zip");
    QVERIFY2(QFile::exists(downloadedPath),
             qPrintable(QStringLiteral("Downloaded file not found at %1").arg(downloadedPath)));

    QFileInfo fi(downloadedPath);
    QCOMPARE(static_cast<uint64>(fi.size()), fileSize);

    // Byte-for-byte content comparison with original
    const QString originalPath = projectDataDir() + QStringLiteral("/incoming/eMule0.50a.zip");
    QFile original(originalPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QFile downloaded(downloadedPath);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), original.readAll());

    // -- Cleanup: delete downloaded copy, verify original unchanged -----------
    downloaded.close();
    original.close();
    QVERIFY(QFile::remove(downloadedPath));
    QVERIFY2(QFile::exists(originalPath), "Original eMule0.50a.zip was removed!");

    processTimer.stop();
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

    // Delete server-side clients created by the production handler
    for (auto* c : m_serverClients)
        m_clientList->removeClient(c);
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
