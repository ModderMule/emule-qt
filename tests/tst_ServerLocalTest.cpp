/// @file tst_ServerLocalTest.cpp
/// @brief Local server integration test — starts a local eNode server,
///        connects with obfuscation AND plain TCP in two rounds, plus a
///        third UDP global search round while disconnected. Publishes
///        shared files, searches by keyword, and verifies results contain
///        our file hashes.
///
/// Deterministic: we control the server and the data.
/// Requires SERVER_TEST_CMD set in .env (QSKIP if not available).
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "net/ServerSocket.h"
#include "net/UDPSocket.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_ServerLocalTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // Round 1: Obfuscated connection
    void startServerObfuscated();
    void publishFilesObfuscated();
    void searchObfuscated_data();
    void searchObfuscated();
    void stopServerObfuscated();

    // Round 2: Plain TCP connection
    void startServerPlain();
    void publishFilesPlain();
    void searchPlain_data();
    void searchPlain();
    void stopServerPlain();

    // Round 3: UDP Global Search (disconnected)
    void startServerUdpSearch();
    void searchUdpGlobal_data();
    void searchUdpGlobal();
    void stopServerUdpSearch();

    void cleanupTestCase();

private:
    // Helper methods
    void startServer();
    void stopServer();
    void connectToLocalServer(bool noCrypt);
    void disconnectFromServer();
    void publishFiles();
    void searchForKeyword();
    void searchForKeywordUDP();
    void checkServerLog();
    void addSearchData();

    // eNode server process
    QProcess* m_serverProcess = nullptr;
    QString m_enodeExecutable;
    QStringList m_enodeArgs;
    QString m_enodeWorkDir;

    // Core infrastructure
    TempDir* m_tmpDir = nullptr;
    ServerList* m_serverList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    ClientList* m_clientList = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    DownloadQueue* m_downloadQueue = nullptr;
    SearchList* m_searchList = nullptr;
    Server* m_localServer = nullptr;

    // UDP socket for Round 3
    UDPSocket* m_udpSocket = nullptr;

    // Shared file hashes (MD4, 16 bytes each)
    QByteArray m_readmeHash;
    QByteArray m_zipHash;
    QByteArray m_dmgHash;
};

// ---------------------------------------------------------------------------
// Helper: Start eNode server process
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServer()
{
    // Delete old log so each round starts fresh
    QFile::remove(m_enodeWorkDir + QStringLiteral("/logs/enode.log"));

    m_serverProcess = new QProcess(this);
    m_serverProcess->setWorkingDirectory(m_enodeWorkDir);
    m_serverProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    m_serverProcess->start(m_enodeExecutable, m_enodeArgs);
    QVERIFY2(m_serverProcess->waitForStarted(5000), "Failed to start eNode server process");

    // Poll TCP connect to 127.0.0.1:5555 every 500ms (up to 10s) for readiness
    bool serverReady = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (m_serverProcess->state() == QProcess::NotRunning) {
            qWarning() << "eNode process exited prematurely, exit code:"
                       << m_serverProcess->exitCode();
            break;
        }
        QTcpSocket probe;
        probe.connectToHost(QStringLiteral("127.0.0.1"), 5555);
        if (probe.waitForConnected(500)) {
            probe.disconnectFromHost();
            serverReady = true;
            break;
        }
        QTest::qWait(500);
    }
    QVERIFY2(serverReady, "eNode server did not become ready within 10s");
    qDebug() << "eNode server is ready on 127.0.0.1:5555";
}

// ---------------------------------------------------------------------------
// Helper: Stop eNode server process
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::stopServer()
{
    if (m_serverProcess) {
        m_serverProcess->terminate();
        if (!m_serverProcess->waitForFinished(3000))
            m_serverProcess->kill();
        qDebug() << "eNode server exit code:" << m_serverProcess->exitCode();
        delete m_serverProcess;
        m_serverProcess = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Helper: Connect to local server
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::connectToLocalServer(bool noCrypt)
{
    // Recreate local server object
    delete m_localServer;
    m_localServer = new Server(htonl(0x7F000001), 5555);
    m_localServer->setName(QStringLiteral("(TESTING!!!) eNode"));
    m_localServer->setObfuscationPortTCP(5565);
    m_localServer->setObfuscationPortUDP(5569);
    m_localServer->setTCPFlags(SrvTcpFlag::Compression | SrvTcpFlag::NewTags
                               | SrvTcpFlag::Unicode | SrvTcpFlag::TcpObfuscation);
    m_localServer->setUDPFlags(SrvUdpFlag::NewTags | SrvUdpFlag::Unicode
                               | SrvUdpFlag::UdpObfuscation);

    // Configure crypto settings
    ServerConnectConfig cfg;
    cfg.safeServerConnect = true;
    cfg.autoConnectStaticOnly = false;
    cfg.useServerPriorities = false;
    cfg.reconnectOnDisconnect = false;
    cfg.addServersFromServer = false;
    cfg.serverKeepAliveTimeout = 0;
    cfg.userNick = QStringLiteral("eMuleQt-LocalTest");
    cfg.listenPort = m_listenSocket->connectedPort();
    constexpr uint32 SO_EMULE = 4;
    cfg.emuleVersionTag = (SO_EMULE << 24) | (0u << 17) | (50u << 10) | (0u << 7);
    cfg.connectionTimeout = 30000;

    if (noCrypt) {
        cfg.cryptLayerEnabled = false;
        cfg.cryptLayerPreferred = false;
        cfg.cryptLayerRequired = false;
    } else {
        cfg.cryptLayerEnabled = true;
        cfg.cryptLayerPreferred = true;
        cfg.cryptLayerRequired = true;
    }

    auto userHash = thePrefs.userHash();
    std::copy(userHash.begin(), userHash.end(), cfg.userHash.begin());

    m_serverConnect->setConfig(cfg);

    QSignalSpy messageSpy(m_serverConnect, &ServerConnect::serverMessageReceived);

    // noCrypt param: false=allow crypto, true=force plain
    m_serverConnect->connectToServer(m_localServer, false, noCrypt);

    const bool connected = QTest::qWaitFor([this] {
        return m_serverConnect->isConnected();
    }, 15'000);

    for (int i = 0; i < messageSpy.count(); ++i)
        qDebug() << "Server message:" << messageSpy.at(i).first().toString();

    QVERIFY2(connected, "Failed to connect to local eNode server within 15s");

    if (noCrypt) {
        QVERIFY2(!m_serverConnect->isConnectedObfuscated(),
                 "Connection should NOT be obfuscated for plain TCP round");
    } else {
        QVERIFY2(m_serverConnect->isConnectedObfuscated(),
                 "Connection should be obfuscated for encrypted round");
    }

    qDebug() << "Connected to local eNode server, obfuscated:"
             << m_serverConnect->isConnectedObfuscated()
             << "clientID:" << Qt::hex << m_serverConnect->clientID();

    // Allow login response / server status to settle
    QTest::qWait(1000);
}

// ---------------------------------------------------------------------------
// Helper: Disconnect from server
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::disconnectFromServer()
{
    if (m_serverConnect)
        m_serverConnect->disconnect();
}

// ---------------------------------------------------------------------------
// Helper: Publish shared files
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::publishFiles()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connection step failed");

    m_sharedFiles->clearED2KPublishFlags();
    m_sharedFiles->sendListToServer();

    qDebug() << "Sent shared file list to server ("
             << m_sharedFiles->getCount() << "files)";

    // Wait for server to index the files
    QTest::qWait(3000);

    QVERIFY2(m_serverConnect->isConnected(),
             "Server disconnected after sending shared files");
}

// ---------------------------------------------------------------------------
// Helper: Search for keyword (data-driven, called from test slots)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::searchForKeyword()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — earlier test failed");

    QFETCH(QString, keyword);
    QFETCH(QByteArray, expectedHash);

    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    bool resultReceived = false;
    auto conn = connect(m_serverConnect, &ServerConnect::searchResultReceived,
            this, [&](const uint8* data, uint32 size, bool /*moreResults*/) {
                const Server* srv = m_serverConnect->currentServer();
                const uint32  srvIP   = srv ? srv->ip()   : 0;
                const uint16  srvPort = srv ? srv->port() : 0;
                m_searchList->processSearchAnswer(data, size, true, srvIP, srvPort);
                resultReceived = true;
            });

    const QByteArray keywordUtf8 = keyword.toUtf8();
    const uint32 keyLen = static_cast<uint32>(keywordUtf8.size());
    const uint32 payloadSize = 1 + 2 + keyLen;

    auto packet = std::make_unique<Packet>(OP_SEARCHREQUEST, payloadSize);
    packet->prot = OP_EDONKEYPROT;

    uint8* p = reinterpret_cast<uint8*>(packet->pBuffer);
    *p++ = 0x01;                                       // type = filename keyword
    *p++ = static_cast<uint8>(keyLen & 0xFF);          // length lo
    *p++ = static_cast<uint8>((keyLen >> 8) & 0xFF);   // length hi
    std::memcpy(p, keywordUtf8.constData(), keyLen);

    m_serverConnect->sendPacket(std::move(packet));
    qDebug() << "Sent OP_SEARCHREQUEST for" << keyword;

    (void)QTest::qWaitFor([&resultReceived] { return resultReceived; }, 15'000);

    const uint32 count = m_searchList->resultCount(searchID);
    qDebug() << "TCP search results for" << keyword << ":" << count;
    QVERIFY2(count > 0,
             qPrintable(QStringLiteral("No search results for \"%1\" — "
                                       "we just published, server should have them")
                            .arg(keyword)));

    bool found = false;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (memcmp(file->fileHash(), expectedHash.constData(), 16) == 0)
            found = true;
    });

    QVERIFY2(found,
             qPrintable(QStringLiteral("Expected hash %1 not found in search results for \"%2\"")
                            .arg(QString::fromLatin1(expectedHash.toHex()), keyword)));

    qDebug() << "PASS: Found expected hash" << expectedHash.toHex()
             << "in results for" << keyword;

    disconnect(conn);
}

// ---------------------------------------------------------------------------
// Helper: Check server log for errors
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::checkServerLog()
{
    const QString logPath = m_enodeWorkDir + QStringLiteral("/logs/enode.log");

    QFile logFile(logPath);
    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Could not open eNode log at" << logPath << "— skipping log check";
        return;
    }

    QStringList errorLines;
    int lineNumber = 0;
    while (!logFile.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(logFile.readLine());
        if (line.contains(QStringLiteral("ERROR")) || line.contains(QStringLiteral("PANIC"))) {
            errorLines.append(QStringLiteral("Line %1: %2").arg(lineNumber).arg(line.trimmed()));
        }
    }

    if (!errorLines.isEmpty()) {
        for (const auto& line : errorLines)
            qWarning() << "eNode log error:" << line;
    }

    QVERIFY2(errorLines.isEmpty(),
             qPrintable(QStringLiteral("eNode server log contains %1 ERROR/PANIC line(s)")
                            .arg(errorLines.size())));

    qDebug() << "eNode server log is clean (no ERROR/PANIC lines)";
}

// ---------------------------------------------------------------------------
// Helper: Add search data rows (shared by both rounds)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::addSearchData()
{
    QTest::addColumn<QString>("keyword");
    QTest::addColumn<QByteArray>("expectedHash");

    QTest::newRow("readme")              << QStringLiteral("readme")              << m_readmeHash;
    QTest::newRow("eMule")               << QStringLiteral("eMule")               << m_zipHash;
    QTest::newRow("qt-online-installer") << QStringLiteral("qt-online-installer") << m_dmgHash;
}

// ---------------------------------------------------------------------------
// initTestCase — core infrastructure (once)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::initTestCase()
{
    loadProjectEnv();

    // 1. Read SERVER_TEST_CMD — QSKIP if not set
    const QString serverCmd = qEnvironmentVariable("SERVER_TEST_CMD");
    if (serverCmd.isEmpty())
        QSKIP("SERVER_TEST_CMD not set in .env — skipping local server test");

    // 2. Parse command: first token = executable, rest = args
    const QStringList parts = QProcess::splitCommand(serverCmd);
    QVERIFY2(!parts.isEmpty(), "SERVER_TEST_CMD is empty after parsing");

    m_enodeExecutable = parts.first();
    m_enodeArgs = parts.mid(1);

    // Extract working directory from -config arg
    for (int i = 0; i < m_enodeArgs.size(); ++i) {
        if (m_enodeArgs[i] == QStringLiteral("-config") && i + 1 < m_enodeArgs.size()) {
            m_enodeWorkDir = m_enodeArgs[i + 1];
            if (QFileInfo(m_enodeWorkDir).isFile())
                m_enodeWorkDir = QFileInfo(m_enodeWorkDir).absolutePath();
            break;
        }
    }
    if (m_enodeWorkDir.isEmpty())
        m_enodeWorkDir = QFileInfo(m_enodeExecutable).absolutePath();

    qDebug() << "eNode executable:" << m_enodeExecutable;
    qDebug() << "eNode args:" << m_enodeArgs;
    qDebug() << "eNode working dir:" << m_enodeWorkDir;

    // 3. Core infrastructure setup
    m_tmpDir = new TempDir();

    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());

    const QString incomingDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    const QString tempDir = m_tmpDir->filePath(QStringLiteral("temp"));
    QDir().mkpath(incomingDir);
    QDir().mkpath(tempDir);
    thePrefs.setIncomingDir(incomingDir);
    thePrefs.setTempDirs({tempDir});

    auto* creditsList = new ClientCreditsList();
    theApp.clientCredits = creditsList;

    m_clientList = new ClientList(this);
    theApp.clientList = m_clientList;

    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(0), "Failed to start TCP listener");
    theApp.listenSocket = m_listenSocket;
    thePrefs.setPort(m_listenSocket->connectedPort());

    m_throttler = new UploadBandwidthThrottler(this);
    m_throttler->start();
    theApp.uploadBandwidthThrottler = m_throttler;

    theApp.ipFilter = nullptr;

    // 4. KnownFileList + SharedFileList
    m_knownFiles = new KnownFileList();
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.knownFileList = m_knownFiles;
    theApp.sharedFileList = m_sharedFiles;

    // 5. Share files from data/incoming
    const QString dataIncoming = projectDataDir() + QStringLiteral("/incoming");

    auto* sharedReadme = new KnownFile();
    QVERIFY2(sharedReadme->createFromFile(dataIncoming, QStringLiteral("readme.txt")),
             "Failed to create KnownFile from readme.txt");
    QVERIFY(m_sharedFiles->safeAddKFile(sharedReadme));
    m_readmeHash = QByteArray(reinterpret_cast<const char*>(sharedReadme->fileHash()), 16);

    auto* sharedZip = new KnownFile();
    QVERIFY2(sharedZip->createFromFile(dataIncoming, QStringLiteral("eMule0.50a.zip")),
             "Failed to create KnownFile from eMule0.50a.zip");
    QVERIFY(m_sharedFiles->safeAddKFile(sharedZip));
    m_zipHash = QByteArray(reinterpret_cast<const char*>(sharedZip->fileHash()), 16);

    auto* sharedDmg = new KnownFile();
    QVERIFY2(sharedDmg->createFromFile(dataIncoming,
                 QStringLiteral("qt-online-installer-macOS-x64-4.10.0.dmg")),
             "Failed to create KnownFile from qt-online-installer");
    QVERIFY(m_sharedFiles->safeAddKFile(sharedDmg));
    m_dmgHash = QByteArray(reinterpret_cast<const char*>(sharedDmg->fileHash()), 16);

    qDebug() << "Shared files:" << m_sharedFiles->getCount();
    qDebug() << "readme.txt hash:" << m_readmeHash.toHex();
    qDebug() << "eMule0.50a.zip hash:" << m_zipHash.toHex();
    qDebug() << "qt-online-installer hash:" << m_dmgHash.toHex();

    // 6. DownloadQueue
    m_downloadQueue = new DownloadQueue(this);
    m_downloadQueue->setSharedFileList(m_sharedFiles);
    m_downloadQueue->setKnownFileList(m_knownFiles);
    m_downloadQueue->setClientList(m_clientList);
    theApp.downloadQueue = m_downloadQueue;

    // 7. ServerList + ServerConnect
    m_serverList = new ServerList(this);
    theApp.serverList = m_serverList;

    m_serverConnect = new ServerConnect(*m_serverList, this);

    m_sharedFiles->setServerConnect(m_serverConnect);
    m_downloadQueue->setServerConnect(m_serverConnect);

    // 8. SearchList
    m_searchList = new SearchList();
}

// ---------------------------------------------------------------------------
// Round 1: Obfuscated connection
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerObfuscated()
{
    startServer();
    connectToLocalServer(/*noCrypt=*/false);
}

void tst_ServerLocalTest::publishFilesObfuscated()
{
    publishFiles();
}

void tst_ServerLocalTest::searchObfuscated_data()
{
    addSearchData();
}

void tst_ServerLocalTest::searchObfuscated()
{
    searchForKeyword();
}

void tst_ServerLocalTest::stopServerObfuscated()
{
    checkServerLog();
    disconnectFromServer();
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 2: Plain TCP connection
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerPlain()
{
    startServer();
    connectToLocalServer(/*noCrypt=*/true);
}

void tst_ServerLocalTest::publishFilesPlain()
{
    publishFiles();
}

void tst_ServerLocalTest::searchPlain_data()
{
    addSearchData();
}

void tst_ServerLocalTest::searchPlain()
{
    searchForKeyword();
}

void tst_ServerLocalTest::stopServerPlain()
{
    checkServerLog();
    disconnectFromServer();
    stopServer();
}

// ---------------------------------------------------------------------------
// Helper: Search for keyword via UDP (data-driven, called from searchUdpGlobal)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::searchForKeywordUDP()
{
    QFETCH(QString, keyword);
    QFETCH(QByteArray, expectedHash);

    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    // Register the server IP so SearchList accepts the UDP response
    const uint32 serverIP = htonl(0x7F000001); // 127.0.0.1 in network byte order
    m_searchList->addSentUDPRequestIP(serverIP);

    // Build OP_GLOBSEARCHREQ packet (same keyword payload format as OP_SEARCHREQUEST)
    const QByteArray keywordUtf8 = keyword.toUtf8();
    const uint32 keyLen = static_cast<uint32>(keywordUtf8.size());
    const uint32 payloadSize = 1 + 2 + keyLen;

    auto packet = std::make_unique<Packet>(OP_GLOBSEARCHREQ, payloadSize);
    packet->prot = OP_EDONKEYPROT;

    uint8* p = reinterpret_cast<uint8*>(packet->pBuffer);
    *p++ = 0x01;                                       // type = filename keyword
    *p++ = static_cast<uint8>(keyLen & 0xFF);          // length lo
    *p++ = static_cast<uint8>((keyLen >> 8) & 0xFF);   // length hi
    std::memcpy(p, keywordUtf8.constData(), keyLen);

    // Send via UDPSocket to eNode UDP port 5559
    m_udpSocket->sendPacket(std::move(packet), *m_localServer, 5559);
    qDebug() << "Sent OP_GLOBSEARCHREQ for" << keyword;

    // Wait for results
    const bool gotResults = QTest::qWaitFor([&] {
        return m_searchList->resultCount(searchID) > 0;
    }, 15'000);

    const uint32 count = m_searchList->resultCount(searchID);
    qDebug() << "UDP search results for" << keyword << ":" << count;
    QVERIFY2(gotResults,
             qPrintable(QStringLiteral("No UDP search results for \"%1\" — "
                                       "server should have them from prior publish")
                            .arg(keyword)));

    bool found = false;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (memcmp(file->fileHash(), expectedHash.constData(), 16) == 0)
            found = true;
    });

    QVERIFY2(found,
             qPrintable(QStringLiteral("Expected hash %1 not found in UDP results for \"%2\"")
                            .arg(QString::fromLatin1(expectedHash.toHex()), keyword)));

    qDebug() << "PASS: Found expected hash" << expectedHash.toHex()
             << "in UDP results for" << keyword;
}

// ---------------------------------------------------------------------------
// Round 3: UDP Global Search (disconnected)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerUdpSearch()
{
    // Start eNode and connect via plain TCP to publish files
    startServer();
    connectToLocalServer(/*noCrypt=*/true);
    publishFiles();

    // Disconnect TCP — UDP search should work while disconnected
    disconnectFromServer();
    QVERIFY2(!m_serverConnect->isConnected(),
             "Should be disconnected before UDP search round");

    // Recreate local server object for UDP (disconnectFromServer doesn't delete it,
    // but we need serverKeyUDP=0 to ensure unencrypted UDP)
    delete m_localServer;
    m_localServer = new Server(htonl(0x7F000001), 5555);
    m_localServer->setName(QStringLiteral("(TESTING!!!) eNode"));

    // Create UDPSocket for direct UDP communication
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");

    // Wire UDP global search results → SearchList (lambda bridges the optUTF8 param)
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, uint32 srvIP, uint16 srvPort) {
                m_searchList->processUDPSearchAnswer(data, size, true, srvIP, srvPort);
            });

    qDebug() << "UDP search round: disconnected from TCP, UDPSocket created";
}

void tst_ServerLocalTest::searchUdpGlobal_data()
{
    addSearchData();
}

void tst_ServerLocalTest::searchUdpGlobal()
{
    searchForKeywordUDP();
}

void tst_ServerLocalTest::stopServerUdpSearch()
{
    checkServerLog();

    delete m_udpSocket;
    m_udpSocket = nullptr;

    stopServer();
}

// ---------------------------------------------------------------------------
// cleanupTestCase — tear down core infrastructure
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::cleanupTestCase()
{
    // Kill eNode if still running (safety net)
    stopServer();

    // Disconnect from server
    if (m_serverConnect)
        m_serverConnect->disconnect();

    delete m_udpSocket;
    m_udpSocket = nullptr;

    delete m_searchList;
    m_searchList = nullptr;

    if (m_throttler) {
        m_throttler->endThread();
        m_throttler->wait(5000);
    }

    if (m_listenSocket)
        m_listenSocket->stopListening();

    theApp.downloadQueue = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    theApp.serverList = nullptr;
    theApp.ipFilter = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_localServer;
    m_localServer = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_ServerLocalTest)
#include "tst_ServerLocalTest.moc"
