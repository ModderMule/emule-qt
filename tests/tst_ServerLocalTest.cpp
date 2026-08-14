/// @file tst_ServerLocalTest.cpp
/// @brief Local server integration test — starts a local eNode server and
///        exercises it over eight rounds: obfuscated TCP (5565), plain TCP
///        (5555), UDP global search while disconnected (5559), plain TCP
///        against the obfuscated port (5565), the obfuscated stat crypt-ping
///        (5567), server-seeded fixture searches and sources, IPv6 over ::1,
///        and the two version surfaces a client can display. Publishes shared
///        files, searches by keyword, and verifies the results carry our
///        hashes and sizes.
///
/// Each round asserts HighID, so a firewall probe that fails — or silently
/// falls back to plaintext — is caught rather than logged and ignored.
///
/// Deterministic: we control the server and the data.
/// Requires SERVER_TEST_CMD set in .env (QSKIP if not available).
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
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "net/ServerSocket.h"
#include "net/Address.h"
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
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
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
// Fixture data seeded by the eNode server from debug_fixtures.yaml.
// These are NOT published by us — the server injects them at startup.
// ---------------------------------------------------------------------------

namespace {

// Files (hash + size + which peers offer them).
const QByteArray kDebianHash = QByteArray::fromHex("fedcba9876543210fedcba9876543210");
const QByteArray kBunnyHash  = QByteArray::fromHex("00112233445566778899aabbccddeeff");
const QByteArray kSintelHash = QByteArray::fromHex("22223333444455556666777788889999");

constexpr uint64 kDebianSize = 4700000000ULL;   // >4 GiB — exercises 64-bit size + large-file GETSOURCES
constexpr uint64 kBunnySize  = 355856889ULL;
constexpr uint64 kSintelSize = 1129240576ULL;

// Peers. Debian is offered by all three (→ 3 sources); Sintel only by peer 2.
constexpr auto kPeer1IPv4  = "203.0.113.7";     // HighID, offers Debian + bunny
constexpr auto kPeer2IPv4  = "198.51.100.42";   // HighID (crypt), offers Debian + Sintel
constexpr uint32 kPeer3LowID = 123456;          // LowID, offers Debian

} // namespace

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

    // Round 4: Plain TCP against the *obfuscated* port
    void startServerPlainOnObfuscatedPort();
    void publishFilesPlainOnObfuscatedPort();
    void searchPlainOnObfuscatedPort_data();
    void searchPlainOnObfuscatedPort();
    void stopServerPlainOnObfuscatedPort();

    // Round 5: obfuscated stat crypt-ping (port+12) round-trip
    void startServerCryptPing();
    void cryptPingRoundTrip();
    void stopServerCryptPing();

    // Round 6: server-seeded fixtures — search results + sources (TCP & UDP)
    void startServerFixtures();
    void searchFixtures_data();
    void searchFixtures();
    void requestFixtureSourcesTcp();
    void requestFixtureSourcesUdp();
    void stopServerFixtures();

    // Round 7: IPv6 — connect over ::1 (S1/S2) and confirm the fixture source path
    // still works over an IPv6-transport session. QSKIPs when the eNode under test is
    // not reachable over IPv6 (ipv6.enabled off), so it is safe in any environment.
    void startServerIPv6();
    void requestFixtureSourcesIPv6();
    void stopServerIPv6();

    // Round 8: the two version surfaces a client can display
    void startServerVersions();
    void versionFromLogin();
    void versionFromDescExchange();
    void versionLegacyDescPreservesVersion();
    void stopServerVersions();

    void cleanupTestCase();

private:
    // Helper methods
    void startServer();
    void stopServer();
    void connectToLocalServer(bool noCrypt, quint16 overridePort = 0);
    /// Connect to the local eNode over IPv6 loopback (::1). Returns false (does NOT
    /// assert) when the connection can't be established, so Round 7 can QSKIP on a
    /// server without ipv6.enabled.
    bool connectToLocalServerIPv6();
    void disconnectFromServer();
    void publishFiles();
    void searchForKeyword();
    void searchForKeywordUDP();
    void checkServerLog();
    void addSearchData();
    void addFixtureSearchData();

    /// Create a PartFile download (findable by fileByID) for a fixture file so the
    /// server's OP_FOUNDSOURCES / OP_GLOBFOUNDSOURCES reply attaches its sources to it.
    PartFile* addFixtureDownload(const QByteArray& hash, uint64 size, const QString& name);

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
    bool m_ipv6RoundActive = false;   // true only once the IPv6 (::1) connect succeeded

    // Round 8: the ServerList entry the version parsers write onto, and the string
    // the TCP login surface produced (compared against the UDP one later).
    Server* m_versionEntry = nullptr;
    QString m_tcpVersion;

    // UDP socket for Round 3
    UDPSocket* m_udpSocket = nullptr;

    // What the last OP_GLOBSERVSTATRES carried on the wire, captured in the
    // serverStatusResult lambda *before* ServerList consumes it: the payload size and
    // the raw 4 bytes at +40. Rounds 5 and 8 assert on these to pin the reflection on
    // the obfuscated and the plain channel respectively.
    uint32 m_lastStatSize = 0;
    uint32 m_lastStatObservedRaw = 0;

    // Shared file hashes (MD4, 16 bytes each) and their sizes
    QByteArray m_readmeHash;
    QByteArray m_zipHash;
    QByteArray m_testfileHash;
    uint64 m_readmeSize = 0;
    uint64 m_zipSize = 0;
    uint64 m_testfileSize = 0;
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

    // Wait for the LAST listener the server binds, not the first: 5555 comes up
    // before the obfuscated 5565, and round 1 connects to 5565. Polling only 5555
    // can therefore connect before 5565 exists.
    // The budget is 30s because dynIp resolution can stall the server's startup by
    // ~12s before it binds anything.
    const QList<quint16> readyPorts = {5555, 5565};
    bool serverReady = false;
    for (int attempt = 0; attempt < 60 && !serverReady; ++attempt) {
        if (m_serverProcess->state() == QProcess::NotRunning) {
            qWarning() << "eNode process exited prematurely, exit code:"
                       << m_serverProcess->exitCode();
            break;
        }
        bool allUp = true;
        for (quint16 port : readyPorts) {
            QTcpSocket probe;
            probe.connectToHost(QStringLiteral("127.0.0.1"), port);
            if (!probe.waitForConnected(250)) {
                allUp = false;
                break;
            }
            probe.disconnectFromHost();
        }
        if (allUp) {
            serverReady = true;
            break;
        }
        QTest::qWait(500);
    }
    QVERIFY2(serverReady, "eNode server did not become ready within 30s (ports 5555 and 5565)");
    qDebug() << "eNode server is ready on 127.0.0.1 ports 5555 and 5565";
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

void tst_ServerLocalTest::connectToLocalServer(bool noCrypt, quint16 overridePort)
{
    // Recreate local server object. overridePort lets a round target a specific
    // TCP port as the *plain* port — used by round 4 to speak plaintext to the
    // obfuscated listener.
    const quint16 tcpPort = overridePort != 0 ? overridePort : 5555;
    delete m_localServer;
    m_localServer = new Server(htonl(0x7F000001), tcpPort);
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
    // Must mirror CoreSession::start() exactly, so this test exercises the version we
    // really ship. compatClient stays 0 (standard eMule).
    cfg.emuleVersionTag = (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_UPD) <<  7);
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

    // cfg above only governs our OUTBOUND connection to the server. The server
    // probes back to our listen port to decide HighID vs LowID, and that inbound
    // socket takes its config from thePrefs (ListenSocket.cpp). Requiring
    // obfuscation there too means the server's probe must succeed *encrypted* —
    // which is what makes the HighID assertion below a real test of the server's
    // obfuscated handshake rather than of its plaintext fallback.
    thePrefs.setCryptLayerRequired(!noCrypt);

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
        qDebug() << "Server message:" << messageSpy.at(i).at(1).toString();

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

    // We are reachable on cfg.listenPort, so a correct server hands out a HighID.
    // In the obfuscated round our listener rejects plaintext (see above), so a
    // LowID here means the server's *encrypted* probe-back failed and it silently
    // fell back to an unencrypted one — the failure mode of a wrong RC4 key
    // derivation in the server's client-side handshake.
    QVERIFY2(!m_serverConnect->isLowID(),
             qPrintable(QStringLiteral("Got LowID (clientID=0x%1) but we are reachable on port %2 — "
                                       "the server's firewall probe failed")
                            .arg(m_serverConnect->clientID(), 0, 16)
                            .arg(cfg.listenPort)));
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
    QFETCH(uint64, expectedSize);
    QFETCH(QString, expectedName);

    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    bool resultReceived = false;
    auto conn = connect(m_serverConnect, &ServerConnect::searchResultReceived,
            this, [&](const uint8* data, uint32 size, bool /*moreResults*/) {
                const Server* srv = m_serverConnect->currentServer();
                const uint32  srvIP   = srv ? srv->ipAddress().toNetworkUint32() : 0;
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
    uint64 reportedSize = 0;
    QString reportedName;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (memcmp(file->fileHash(), expectedHash.constData(), 16) == 0) {
            found = true;
            reportedSize = static_cast<uint64>(file->fileSize());
            reportedName = file->fileName();
        }
    });

    QVERIFY2(found,
             qPrintable(QStringLiteral("Expected hash %1 not found in search results for \"%2\"")
                            .arg(QString::fromLatin1(expectedHash.toHex()), keyword)));

    // The server must round-trip the size we published. A zero (or truncated) size
    // means it dropped the FT_FILESIZE tag — which happens when a narrowed integer
    // tag is not decoded. Such a file is still searchable but returns no sources,
    // so nothing else in this test would notice.
    QVERIFY2(reportedSize == expectedSize,
             qPrintable(QStringLiteral("Size mismatch for \"%1\": server reported %2, published %3")
                            .arg(keyword)
                            .arg(reportedSize)
                            .arg(expectedSize)));

    // The filename round-trips through the FT_FILENAME string tag — a mangled name
    // means the string tag was decoded with the wrong length/encoding.
    QVERIFY2(reportedName == expectedName,
             qPrintable(QStringLiteral("Name mismatch for \"%1\": server reported \"%2\", expected \"%3\"")
                            .arg(keyword, reportedName, expectedName)));

    qDebug() << "PASS: Found expected hash" << expectedHash.toHex()
             << "size" << reportedSize
             << "name" << reportedName
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

    // The server logs most failures at WARN, not ERROR — matching only ERROR/PANIC
    // made this check unable to fail. Parse failures and dropped offers are exactly
    // what we want this test to catch, so match those too.
    static const QStringList badMarkers = {
        QStringLiteral("ERROR"),
        QStringLiteral("PANIC"),
        QStringLiteral("FATAL"),
        QStringLiteral("WARN"),
    };

    QStringList errorLines;
    int lineNumber = 0;
    while (!logFile.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(logFile.readLine());
        for (const auto& marker : badMarkers) {
            if (line.contains(marker)) {
                errorLines.append(QStringLiteral("Line %1: %2").arg(lineNumber).arg(line.trimmed()));
                break;
            }
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
    QTest::addColumn<uint64>("expectedSize");
    QTest::addColumn<QString>("expectedName");

    // readme.txt is under 64 KiB, so eMule sends its FT_FILESIZE as TAGTYPE_UINT16
    // rather than TAGTYPE_UINT32. A server that only accepts the 32-bit form indexes
    // it with size 0 — it still turns up in searches, so only the size assertion in
    // searchForKeyword() catches it.
    QTest::newRow("readme")   << QStringLiteral("readme")   << m_readmeHash   << m_readmeSize   << QStringLiteral("readme.txt");
    QTest::newRow("eMule")    << QStringLiteral("eMule")    << m_zipHash      << m_zipSize      << QStringLiteral("eMule0.50a.zip");
    QTest::newRow("testfile") << QStringLiteral("testfile") << m_testfileHash << m_testfileSize << QStringLiteral("eMuleQt-testfile-20MB.bin");
}

// ---------------------------------------------------------------------------
// Helper: Add fixture search data rows (server-seeded from debug_fixtures.yaml)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::addFixtureSearchData()
{
    QTest::addColumn<QString>("keyword");
    QTest::addColumn<QByteArray>("expectedHash");
    QTest::addColumn<uint64>("expectedSize");
    QTest::addColumn<QString>("expectedName");

    // These files are injected by the server itself from debug_fixtures.yaml — we do
    // not publish them. The Debian file is >4 GiB, so its size assertion exercises the
    // FT_FILESIZE + FT_FILESIZE_HI 64-bit combine path.
    QTest::newRow("Debian") << QStringLiteral("Debian") << kDebianHash << kDebianSize
                            << QStringLiteral("Debian-13-amd64-netinst.iso");
    QTest::newRow("bunny")  << QStringLiteral("bunny")  << kBunnyHash  << kBunnySize
                            << QStringLiteral("big buck bunny 1080p.mkv");
    QTest::newRow("Sintel") << QStringLiteral("Sintel") << kSintelHash << kSintelSize
                            << QStringLiteral("Sintel.2010.1080p.mkv");
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

    // Without this, inbound connections are accepted at the socket layer but no
    // UpDownClient is ever created, so OP_HELLO is never answered. The server's
    // firewall probe then times out and we are handed a LowID — which would make
    // the HighID assertion in connectToLocalServer() fail for a reason that has
    // nothing to do with the server. CoreSession::start() does the same wiring.
    connect(m_listenSocket, &ListenSocket::newClientConnection,
            m_clientList, &ClientList::handleIncomingConnection);

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
    m_readmeSize = sharedReadme->fileSize();

    auto* sharedZip = new KnownFile();
    QVERIFY2(sharedZip->createFromFile(dataIncoming, QStringLiteral("eMule0.50a.zip")),
             "Failed to create KnownFile from eMule0.50a.zip");
    QVERIFY(m_sharedFiles->safeAddKFile(sharedZip));
    m_zipHash = QByteArray(reinterpret_cast<const char*>(sharedZip->fileHash()), 16);
    m_zipSize = sharedZip->fileSize();

    auto* sharedTestfile = new KnownFile();
    QVERIFY2(sharedTestfile->createFromFile(dataIncoming,
                 QStringLiteral("eMuleQt-testfile-20MB.bin")),
             "Failed to create KnownFile from eMuleQt-testfile-20MB.bin");
    QVERIFY(m_sharedFiles->safeAddKFile(sharedTestfile));
    m_testfileHash = QByteArray(reinterpret_cast<const char*>(sharedTestfile->fileHash()), 16);
    m_testfileSize = sharedTestfile->fileSize();

    qDebug() << "Shared files:" << m_sharedFiles->getCount();
    qDebug() << "readme.txt hash:" << m_readmeHash.toHex();
    qDebug() << "eMule0.50a.zip hash:" << m_zipHash.toHex();
    qDebug() << "eMuleQt-testfile-20MB.bin hash:" << m_testfileHash.toHex();

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
    // Disconnect first so the log check also covers the disconnect path.
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();
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
    // Disconnect first so the log check also covers the disconnect path.
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();
    stopServer();
}

// ---------------------------------------------------------------------------
// Helper: Search for keyword via UDP (data-driven, called from searchUdpGlobal)
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::searchForKeywordUDP()
{
    QFETCH(QString, keyword);
    QFETCH(QByteArray, expectedHash);
    QFETCH(uint64, expectedSize);

    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    // Register the server IP so SearchList accepts the UDP response
    const uint32 serverIP = htonl(0x7F000001); // 127.0.0.1 in network byte order
    m_searchList->addSentUDPRequestIP(searchID, serverIP);

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
    uint64 reportedSize = 0;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (memcmp(file->fileHash(), expectedHash.constData(), 16) == 0) {
            found = true;
            reportedSize = static_cast<uint64>(file->fileSize());
        }
    });

    QVERIFY2(found,
             qPrintable(QStringLiteral("Expected hash %1 not found in UDP results for \"%2\"")
                            .arg(QString::fromLatin1(expectedHash.toHex()), keyword)));

    QVERIFY2(reportedSize == expectedSize,
             qPrintable(QStringLiteral("Size mismatch for \"%1\" over UDP: server reported %2, published %3")
                            .arg(keyword)
                            .arg(reportedSize)
                            .arg(expectedSize)));

    qDebug() << "PASS: Found expected hash" << expectedHash.toHex()
             << "size" << reportedSize
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

    // Wire UDP global search results → SearchList. The signal carries an Endpoint;
    // unpack it the same way CoreSession::start() does.
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& server) {
                m_searchList->processUDPSearchAnswer(data, size, true,
                                                     server.address().toNetworkUint32(),
                                                     server.port());
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
// Round 4: Plain TCP against the *obfuscated* port (5565)
//
// A client that does not speak obfuscation may still connect to the obfuscated
// port, and the server must serve it normally. The server used to feed the very
// first bytes into its DH negotiation without looking at the protocol byte: a
// plaintext OP_LOGINREQUEST is longer than the 97 bytes negotiate() requires, so
// it "succeeded", derived RC4 keys from login payload bytes, wrote 96 bytes of
// garbage back and parked the session in CS_NEGOTIATING until the 3600s
// disconnect timeout. Nothing was logged.
//
// The original inspects the protocol byte first, which is what makes this round
// pass (eNode/ed2k/packet.js:105-129).
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerPlainOnObfuscatedPort()
{
    startServer();
    // noCrypt=true forces plaintext; overridePort aims it at the obfuscated
    // listener rather than 5555.
    connectToLocalServer(/*noCrypt=*/true, /*overridePort=*/5565);
}

void tst_ServerLocalTest::publishFilesPlainOnObfuscatedPort()
{
    publishFiles();
}

void tst_ServerLocalTest::searchPlainOnObfuscatedPort_data()
{
    addSearchData();
}

void tst_ServerLocalTest::searchPlainOnObfuscatedPort()
{
    searchForKeyword();
}

void tst_ServerLocalTest::stopServerPlainOnObfuscatedPort()
{
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 5: obfuscated stat crypt-ping (port+12) round-trip
//
// serverStats() probes port+12 with a raw challenge; eNode encrypts its
// OP_GLOBSERVSTATRES with that challenge as the RC4 base key (server→client
// magic 0xA5). This exercises the fix: decryptReceivedServer must key on 0xA5,
// not 0x6B, or the reply is dropped and the crypt-ping never completes. eNode-go
// answers the crypt-ping on its obfuscated UDP listener at tcp+12 (5567).
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerCryptPing()
{
    startServer();
    // Obfuscated HighID connect so theApp.publicIP() is set — the crypt-ping
    // branch of serverStats() requires it. Staying connected keeps
    // theApp.isConnected() true so serverStats() runs.
    connectToLocalServer(/*noCrypt=*/false);
    QVERIFY2(theApp.publicIP() != 0,
             "obfuscated HighID connect should have set our public IP");
}

void tst_ServerLocalTest::cryptPingRoundTrip()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");

    // serverStats() reaches the ServerConnect through theApp; the other rounds use
    // m_serverConnect directly and never registered it. Wire it (and theApp.isConnected()
    // now sees our live ED2K connection). Restored in stopServerCryptPing.
    theApp.serverConnect = m_serverConnect;

    // A UDPSocket wired into ServerConnect so serverStats() actually transmits the
    // crypt-ping, and whose stat replies feed ServerList::processStatusResponse.
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");
    m_serverConnect->setUDPSocket(m_udpSocket);
    connect(m_udpSocket, &UDPSocket::serverStatusResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& from) {
                m_lastStatSize = size;
                m_lastStatObservedRaw = size >= 44 ? peekUInt32(data + 40) : 0;
                m_serverList->processStatusResponse(data, size, from);
            });

    // The eNode server as the sole ServerList entry, so serverStats() pings it.
    // The crypt-ping always targets port+12 regardless of the obf-port flag.
    m_serverList->removeAllServers();
    auto srv = std::make_unique<Server>(htonl(0x7F000001), 5555);
    srv->setName(QStringLiteral("(TESTING!!!) eNode"));
    // Mark it dynIP so addServer() accepts the loopback address (127.0.0.1 is not
    // "routable", which the plain-IP path rejects). The numeric IP is still set, so
    // the send goes direct with no DNS.
    srv->setDynIP(QStringLiteral("127.0.0.1"));
    srv->setUDPFlags(SrvUdpFlag::NewTags | SrvUdpFlag::Unicode | SrvUdpFlag::UdpObfuscation);
    Server* entry = m_serverList->addServer(std::move(srv));
    QVERIFY(entry != nullptr);

    thePrefs.setCryptLayerSupported(true);

    // Fire the obfuscated crypt-ping to port+12 (5567).
    m_serverList->serverStats();
    QVERIFY2(entry->cryptPingReplyPending(),
             "serverStats() should have armed the obfuscated crypt-ping");
    QVERIFY(entry->challenge() != 0);
    qDebug() << "Sent obfuscated crypt-ping to 127.0.0.1:5567, challenge=0x"
             << Qt::hex << entry->challenge();

    // eNode answers on port+12, encrypted with the challenge as base key; our
    // onReadyRead decrypts it (0xA5) and processStatusResponse clears the pending
    // state and stores eNode's UDP key. Before the fix this reply is undecryptable.
    const bool got = QTest::qWaitFor([entry] {
        return !entry->cryptPingReplyPending() && entry->serverKeyUDPRaw() != 0;
    }, 10'000);

    QVERIFY2(got, qPrintable(QStringLiteral(
        "crypt-ping reply not decrypted (pending=%1, udpKey=0x%2) — the server→client "
        "obfuscation decrypt failed; check the 0xA5 magic in decryptReceivedServer")
        .arg(entry->cryptPingReplyPending())
        .arg(entry->serverKeyUDPRaw(), 0, 16)));

    // eNode-go advertises a per-IP-derived UDP key (deriveUDPKey), not the raw configured
    // serverKey — the client treats it as opaque (stores and echoes it). The round-trip
    // success is that a non-zero key was learned, already ensured by the qWaitFor above.
    QVERIFY2(entry->serverKeyUDPRaw() != 0, "crypt-ping should have learned a non-zero server UDP key");
    qDebug() << "PASS: crypt-ping round-trip — users" << entry->users()
             << "files" << entry->files()
             << "udpKey=0x" << Qt::hex << entry->serverKeyUDPRaw();

    // The trailing observed-IPv4 reflection at +40. eNode-go appends it on the
    // obfuscated channel (Lugdunum sends it on this channel only), which takes the
    // payload to exactly 44 bytes — the size ServerList uses to tell the reflection
    // apart from a vendor tag block. Assert the wire shape, not just its effect: on a
    // loopback rig the value can never be adopted, so an assertion on publicIP() alone
    // would stay green if the server stopped sending the field altogether.
    QCOMPARE(m_lastStatSize, 44u);
    const Address observed = Address::fromNetworkOrder(m_lastStatObservedRaw);
    QCOMPARE(observed.toString(), QStringLiteral("127.0.0.1"));
    qDebug() << "PASS: obfuscated reply carried the observed-IP reflection at +40 ="
             << observed.toString();

    // ...and the validation ladder threw it away, because 127.0.0.1 is not publicly
    // routable. A rig whose reflection is a real routable address is what exercises
    // adoption; the vote/threshold logic itself is covered by tst_ServerList.
    QCOMPARE(theApp.serverCorroboratedIP(), 0u);
}

void tst_ServerLocalTest::stopServerCryptPing()
{
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();

    m_serverConnect->setUDPSocket(nullptr);
    theApp.serverConnect = nullptr;
    delete m_udpSocket;
    m_udpSocket = nullptr;
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 6: server-seeded fixtures — search results + sources (TCP & UDP)
//
// enode.local.yaml enables debug.seedFixtures, so the server injects the peers
// and files from debug_fixtures.yaml at startup (via the same Connect/AddFile
// path a real login takes). Those files become OP_SEARCHRESULT hits and the
// peers become the sources the server returns for a hash (OP_FOUNDSOURCES over
// TCP, OP_GLOBFOUNDSOURCES over UDP). This round asserts we receive and parse
// both. We publish nothing ourselves — the server has only the fixtures.
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerFixtures()
{
    startServer();
    connectToLocalServer(/*noCrypt=*/false);   // obfuscated HighID

    // isFirewalled() reaches ServerConnect through theApp; wire it so it returns
    // false (we hold a HighID). Without this the LowID fixture source is dropped as
    // unreachable and the Debian source count would be 2, not 3. A real client always
    // has theApp.serverConnect set. Restored in stopServerFixtures().
    theApp.serverConnect = m_serverConnect;
}

void tst_ServerLocalTest::searchFixtures_data()
{
    addFixtureSearchData();
}

void tst_ServerLocalTest::searchFixtures()
{
    searchForKeyword();
}

void tst_ServerLocalTest::requestFixtureSourcesTcp()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");

    // Debian is offered by all three fixture peers (2 HighID + 1 LowID).
    PartFile* pf = addFixtureDownload(kDebianHash, kDebianSize,
                                      QStringLiteral("Debian-13-amd64-netinst.iso"));
    QVERIFY2(pf != nullptr, "Failed to create Debian PartFile download");

    // Real client path: OP_GETSOURCES with the >4 GiB large-file size encoding. The
    // server's OP_FOUNDSOURCES reply flows ServerSocket::foundSourcesReceived ->
    // DownloadQueue::addServerSourceResult -> this PartFile's srcList.
    m_serverConnect->sendPacket(pf->createServerSourceRequestPacket(/*obfuscated=*/false));
    qDebug() << "Sent OP_GETSOURCES for Debian (size" << kDebianSize << ")";

    const bool got = QTest::qWaitFor([pf] { return pf->sourceCount() > 0; }, 15'000);
    qDebug() << "TCP sources for Debian:" << pf->sourceCount();
    QVERIFY2(got, "No TCP sources returned for the Debian fixture hash — a large-file "
                  "OP_GETSOURCES that omits the uint32(0) marker misses the (hash,size) lookup");

    QVERIFY2(pf->sourceCount() == 3,
             qPrintable(QStringLiteral("Expected 3 Debian sources, got %1").arg(pf->sourceCount())));

    // Verify the specific fixture peers were parsed: two HighID IPs + one LowID.
    const uint32 ip1 = Address::fromString(QString::fromLatin1(kPeer1IPv4)).toNetworkUint32();
    const uint32 ip2 = Address::fromString(QString::fromLatin1(kPeer2IPv4)).toNetworkUint32();
    bool foundIp1 = false, foundIp2 = false, foundLowID = false;
    for (const UpDownClient* src : pf->srcList()) {
        if (src->hasLowID()) {
            if (src->userIDHybrid() == kPeer3LowID)
                foundLowID = true;
        } else {
            const uint32 srcIP = src->connectAddress().toNetworkUint32();
            if (srcIP == ip1) foundIp1 = true;
            if (srcIP == ip2) foundIp2 = true;
        }
    }
    QVERIFY2(foundIp1, "HighID peer 203.0.113.7 missing from parsed Debian sources");
    QVERIFY2(foundIp2, "HighID peer 198.51.100.42 missing from parsed Debian sources");
    QVERIFY2(foundLowID, "LowID peer (id=123456) missing from parsed Debian sources");

    qDebug() << "PASS: parsed 3 Debian sources (203.0.113.7, 198.51.100.42, LowID 123456)";
}

void tst_ServerLocalTest::requestFixtureSourcesUdp()
{
    // Sintel is offered by exactly one peer (198.51.100.42) and was NOT requested over
    // TCP, so a source appearing here is provably parsed from the UDP reply rather than
    // a leftover from the TCP round.
    PartFile* pf = addFixtureDownload(kSintelHash, kSintelSize,
                                      QStringLiteral("Sintel.2010.1080p.mkv"));
    QVERIFY2(pf != nullptr, "Failed to create Sintel PartFile download");

    // UDP source socket, wired the way CoreSession does: OP_GLOBFOUNDSOURCES ->
    // DownloadQueue::addUDPGlobalSources.
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");
    connect(m_udpSocket, &UDPSocket::globalFoundSources,
            this, [this](const uint8* data, uint32 size, const Endpoint& from) {
                m_downloadQueue->addUDPGlobalSources(data, size, from);
            });

    // OP_GLOBGETSOURCES (0x9A): payload is just the file hash — eNode answers by hash
    // (GetSourcesByHash), no size needed. Send unencrypted to a plain server object on
    // eNode's UDP port (TCP + 4 = 5559), mirroring the UDP search round.
    Server udpDest(htonl(0x7F000001), 5555);
    auto packet = std::make_unique<Packet>(OP_GLOBGETSOURCES, 16);
    packet->prot = OP_EDONKEYPROT;
    std::memcpy(packet->pBuffer, kSintelHash.constData(), 16);
    m_udpSocket->sendPacket(std::move(packet), udpDest, 5559);
    qDebug() << "Sent OP_GLOBGETSOURCES for Sintel";

    const bool got = QTest::qWaitFor([pf] { return pf->sourceCount() > 0; }, 15'000);
    qDebug() << "UDP sources for Sintel:" << pf->sourceCount();
    QVERIFY2(got, "No UDP sources returned for the Sintel fixture hash");

    QVERIFY2(pf->sourceCount() == 1,
             qPrintable(QStringLiteral("Expected 1 Sintel source, got %1").arg(pf->sourceCount())));

    const uint32 ip2 = Address::fromString(QString::fromLatin1(kPeer2IPv4)).toNetworkUint32();
    const UpDownClient* src = pf->srcList().front();
    QVERIFY2(!src->hasLowID() && src->connectAddress().toNetworkUint32() == ip2,
             "Sintel UDP source is not the expected peer 198.51.100.42");

    qDebug() << "PASS: parsed 1 Sintel source (198.51.100.42) over UDP";
}

void tst_ServerLocalTest::stopServerFixtures()
{
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();

    // Drop the fixture downloads now, while the temp dir still exists — otherwise the
    // PartFiles are freed during global teardown after ~TempDir and their .part.met
    // save fails with noisy (harmless) errors.
    m_downloadQueue->deleteAll();

    theApp.serverConnect = nullptr;
    delete m_udpSocket;
    m_udpSocket = nullptr;
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 7: IPv6 — connect over ::1 (S1/S2) and confirm the fixture source path
//
// This exercises the dual-stack client against the same local eNode over IPv6
// transport: the login carries CT_MOD_IP_V6 + SRVCAP_IPV6 (S1), the session is
// sentinel-safe, and a v6-aware eNode may return an inline 0xFFFFFFFF IPv6 source
// (S3a). It QSKIPs cleanly when the server is not reachable over IPv6, so it is
// safe on any eNode build; run it against an ipv6.enabled eNode-go with IPv6
// fixture peers in debug_fixtures.yaml to validate S1/S2/S3a end to end.
// ---------------------------------------------------------------------------

bool tst_ServerLocalTest::connectToLocalServerIPv6()
{
    delete m_localServer;
    m_localServer = new Server(uint32{0}, 5555);
    m_localServer->setIpAddress(Address::fromString(QStringLiteral("::1")));
    m_localServer->setName(QStringLiteral("(TESTING!!!) eNode v6"));

    ServerConnectConfig cfg;
    cfg.safeServerConnect = true;
    cfg.autoConnectStaticOnly = false;
    cfg.useServerPriorities = false;
    cfg.reconnectOnDisconnect = false;
    cfg.addServersFromServer = false;
    cfg.serverKeepAliveTimeout = 0;
    cfg.userNick = QStringLiteral("eMuleQt-LocalTest-v6");
    cfg.listenPort = m_listenSocket->connectedPort();
    cfg.emuleVersionTag = (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_UPD) <<  7);
    cfg.connectionTimeout = 15000;
    // Plain login over loopback keeps this round independent of the obfuscation path.
    cfg.cryptLayerEnabled = false;
    cfg.cryptLayerPreferred = false;
    cfg.cryptLayerRequired = false;
    thePrefs.setCryptLayerRequired(false);

    auto userHash = thePrefs.userHash();
    std::copy(userHash.begin(), userHash.end(), cfg.userHash.begin());
    m_serverConnect->setConfig(cfg);

    m_serverConnect->connectToServer(m_localServer, false, /*noCrypt=*/true);
    return QTest::qWaitFor([this] { return m_serverConnect->isConnected(); }, 15'000);
}

void tst_ServerLocalTest::startServerIPv6()
{
    startServer();
    if (!connectToLocalServerIPv6()) {
        stopServer();
        QSKIP("eNode not reachable over IPv6 (::1) — ipv6.enabled likely off; skipping IPv6 round");
    }
    m_ipv6RoundActive = true;
    theApp.serverConnect = m_serverConnect;

    const Server* srv = m_serverConnect->currentServer();
    qDebug() << "IPv6 round connected. clientID=0x" << Qt::hex << m_serverConnect->clientID()
             << "supportsIPv6=" << (srv && srv->supportsIPv6());
}

void tst_ServerLocalTest::requestFixtureSourcesIPv6()
{
    if (!m_ipv6RoundActive)
        QSKIP("IPv6 round not active");

    QVERIFY2(m_serverConnect->isConnected(), "IPv6 round: not connected");

    // Because we advertised v6 capability at login, a v6-aware eNode may return an IPv6
    // source via the 0xFFFFFFFF sentinel inside the classic OP_FOUNDSOURCES (S3a). We
    // assert the sources parse (no desync) and log any IPv6 source that arrives.
    PartFile* pf = addFixtureDownload(kDebianHash, kDebianSize,
                                      QStringLiteral("Debian-13-amd64-netinst.iso"));
    QVERIFY2(pf != nullptr, "Failed to create Debian PartFile download");

    m_serverConnect->sendPacket(pf->createServerSourceRequestPacket(/*obfuscated=*/false));
    const bool got = QTest::qWaitFor([pf] { return pf->sourceCount() > 0; }, 15'000);
    QVERIFY2(got, "No sources returned for the Debian fixture over IPv6 transport");

    int ipv6Sources = 0;
    for (const UpDownClient* src : pf->srcList())
        if (src->openIPv6())
            ++ipv6Sources;
    qDebug() << "IPv6 round: parsed" << pf->sourceCount() << "sources," << ipv6Sources << "over IPv6";
}

void tst_ServerLocalTest::stopServerIPv6()
{
    if (!m_ipv6RoundActive)
        return;

    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();
    m_downloadQueue->deleteAll();
    theApp.serverConnect = nullptr;
    m_ipv6RoundActive = false;
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 8: the two version surfaces a client can display
//
// eNode-go publishes its version in two deliberately *different* forms, and which
// one a user sees is a client-side decision no test in the server's own Docker rig
// can reach (eNode-go docs/interop-docker-tests.md §3):
//
//   TCP OP_SERVERMESSAGE at login   "server version v0.1.0 (eNode-go)"
//   UDP OP_SERVER_DESC_RES (0xa3)   ST_VERSION = "17.14 (eNode-go v0.1.0)"
//
// So a connected user sees the first and someone merely holding us in a server list
// sees the second — and writes that into their own server.met and re-shares it. This
// round pins both, and the leading "v" that keeps the first one intact: srchybrid runs
// _stscanf("%u.%u") over the text after "server version" and, when that *succeeds*,
// reformats the whole value to a bare "%u.%02u" (srchybrid/ServerSocket.cpp:176-183),
// discarding the name. The "17.14 …" form on that transport would therefore display as
// a plain "17.14". We store the line verbatim, so that guard is about what an MFC peer
// would make of the same string, not about our own parser.
//
// Assertions are derivation-based, not literal: both strings are built from
// ed2k.ENodeVersionStr, so the same vX.Y.Z must appear in both — which survives an
// eNode-go release bump but still fails if either surface stops being derived from it.
// ---------------------------------------------------------------------------

namespace {

/// The "vX.Y.Z" that both surfaces carry — eNode-go's ed2k.ENodeVersionStr.
QRegularExpression enodeVersionRe()
{
    return QRegularExpression(QStringLiteral(R"(v\d+\.\d+\.\d+)"));
}

/// What srchybrid's _stscanf("%u.%u") accepts. A version matching this is truncated
/// to "%u.%02u" by a real MFC client, name and all.
QRegularExpression mfcTruncatableRe()
{
    return QRegularExpression(QStringLiteral(R"(^\s*\d+\.\d+)"));
}

} // namespace

void tst_ServerLocalTest::startServerVersions()
{
    startServer();

    // Both version parsers write onto the *ServerList* entry, not onto the throwaway
    // Server the socket holds: ServerConnect::onServerMessage goes through
    // resolveListEntry() and ServerList::processDescResponse through findByIPUdp().
    // The other rounds never add the eNode to the list, so the version they parse is
    // written nowhere. Register it before connecting, so the login line has a home.
    m_serverList->removeAllServers();
    auto srv = std::make_unique<Server>(htonl(0x7F000001), 5555);
    srv->setName(QStringLiteral("(TESTING!!!) eNode"));
    // Loopback is not "routable", which addServer() rejects on the plain-IP path; a
    // dynIP entry is accepted. The numeric IP stays set, so sends go direct with no DNS.
    srv->setDynIP(QStringLiteral("127.0.0.1"));
    // Deliberately no SrvUdpFlag::UdpObfuscation — belt and braces with
    // cryptLayerSupported(false) above, so UDPSocket::sendPacket cannot decide to
    // encrypt and redirect to the obfuscated port.
    srv->setUDPFlags(SrvUdpFlag::NewTags | SrvUdpFlag::Unicode);
    m_versionEntry = m_serverList->addServer(std::move(srv));
    QVERIFY2(m_versionEntry != nullptr, "Failed to add the eNode to the ServerList");

    // Plain TCP on 5555 — the transport the login-message surface lives on.
    connectToLocalServer(/*noCrypt=*/true);

    // Only now, once the login (and the server's obfuscated probe back to our listener)
    // is done: force ServerList::serverStats() down its *plain* branch, so both the stat
    // ping and the OP_SERVER_DESC_REQ it triggers go unencrypted to 5559. Round 5 already
    // covers the obfuscated crypt-ping at tcp+12. Setting this before the connect would
    // also make our listener reject the server's encrypted probe. Restored in
    // stopServerVersions().
    thePrefs.setCryptLayerSupported(false);

    // serverStats() and processStatusResponse() both reach the connection through
    // theApp. Restored in stopServerVersions().
    theApp.serverConnect = m_serverConnect;
}

void tst_ServerLocalTest::versionFromLogin()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");
    QVERIFY(m_versionEntry != nullptr);

    const bool got = QTest::qWaitFor([this] {
        return !m_versionEntry->version().isEmpty();
    }, 10'000);
    QVERIFY2(got, "No version recorded from the login: the server sent no "
                  "\"server version …\" line in its OP_SERVERMESSAGE, or "
                  "ServerConnect::onServerMessage failed to match it");

    m_tcpVersion = m_versionEntry->version();
    qDebug() << "TCP login surface: version =" << m_tcpVersion;

    QVERIFY2(m_tcpVersion.contains(QStringLiteral("eNode"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Login version \"%1\" does not name the server — "
                                       "the whole point of the string form is that the part "
                                       "eserver discards says who we are")
                            .arg(m_tcpVersion)));

    // The guard. Asserted separately from any equality so it still fires if the
    // expected string is ever updated to match a changed server.
    QVERIFY2(!mfcTruncatableRe().match(m_tcpVersion).hasMatch(),
             qPrintable(QStringLiteral("Login version \"%1\" parses as two dotted integers, so "
                                       "srchybrid's _stscanf(\"%%u.%%u\") succeeds and reformats "
                                       "the whole value to a bare \"%%u.%%02u\" "
                                       "(srchybrid/ServerSocket.cpp:176-183) — a real client would "
                                       "display only the numbers and drop the server name. A "
                                       "leading \"v\" is what makes that scanf fail")
                            .arg(m_tcpVersion)));

    QVERIFY2(enodeVersionRe().match(m_tcpVersion).hasMatch(),
             qPrintable(QStringLiteral("Login version \"%1\" carries no vX.Y.Z — it is no longer "
                                       "derived from ed2k.ENodeVersionStr")
                            .arg(m_tcpVersion)));

    // OP_SERVERIDENT (0x41) carries no version tag by design, and the only thing that
    // rewrites the version afterwards is the eFarm path in onServerIdent, which fires on
    // a "****" server hash. This pins that the ident left the login string alone.
    QVERIFY2(!m_tcpVersion.startsWith(QLatin1String("eFarm")),
             "OP_SERVERIDENT was misread as an eFarm server and rewrote the version");
}

void tst_ServerLocalTest::versionFromDescExchange()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");
    QVERIFY2(!m_tcpVersion.isEmpty(), "Login surface never produced a version");

    // UDP socket wired exactly as CoreSession::start() does: stat replies drive
    // ServerList::processStatusResponse, description replies processDescResponse.
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");
    m_serverConnect->setUDPSocket(m_udpSocket);
    connect(m_udpSocket, &UDPSocket::serverStatusResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& from) {
                m_lastStatSize = size;
                m_lastStatObservedRaw = size >= 44 ? peekUInt32(data + 40) : 0;
                m_serverList->processStatusResponse(data, size, from);
            });
    connect(m_udpSocket, &UDPSocket::serverDescResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& from) {
                m_serverList->processDescResponse(data, size, from);
            });

    // Round 5 left these set from the obfuscated reply; clear them so the plain-channel
    // assertion below cannot pass on a stale capture.
    m_lastStatSize = 0;
    m_lastStatObservedRaw = 0;

    // The real client path, not a hand-built probe: serverStats() sends the plain
    // OP_GLOBSERVSTATREQ, and processStatusResponse answers it by issuing the
    // OP_SERVER_DESC_REQ whose challenge carries INV_SERV_DESC_LEN in its low 16 bits.
    // So this also covers that automatic follow-up.
    m_serverList->serverStats();
    QVERIFY2(!m_versionEntry->cryptPingReplyPending(),
             "serverStats() took the obfuscated crypt-ping branch — this round needs the "
             "plain one (cryptLayerSupported should be off)");

    const bool got = QTest::qWaitFor([this] {
        return m_versionEntry->version() != m_tcpVersion;
    }, 15'000);

    const QString udpVersion = m_versionEntry->version();
    qDebug() << "UDP OP_SERVER_DESC_RES surface: version =" << udpVersion
             << "name =" << m_versionEntry->name()
             << "desc =" << m_versionEntry->description();

    QVERIFY2(got, qPrintable(QStringLiteral(
        "The 0xa3 description reply never updated the version (still \"%1\", "
        "descReqChallenge=0x%2) — either no reply arrived on 5559, or its ST_VERSION tag "
        "was dropped")
        .arg(udpVersion)
        .arg(m_versionEntry->descReqChallenge(), 0, 16)));

    // The observed-IPv4 reflection again, this time over the *plain* channel on 5559.
    // eNode-go deliberately sends the extended form here too, where Lugdunum answers a
    // plain 0x96 with the short 32-byte form — so this is the leg that only the eNode
    // target covers. Round 5 pins the obfuscated one.
    QCOMPARE(m_lastStatSize, 44u);
    const Address observed = Address::fromNetworkOrder(m_lastStatObservedRaw);
    QCOMPARE(observed.toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(theApp.serverCorroboratedIP(), 0u);
    qDebug() << "PASS: plain reply carried the observed-IP reflection at +40 ="
             << observed.toString();

    // Cleared only on a reply that echoed our exact challenge; a mismatched one is
    // dropped silently and would otherwise look identical to a lost datagram.
    QVERIFY2(m_versionEntry->descReqChallenge() == 0,
             "The description reply did not echo our challenge");

    // The tag block parsed as a whole, so an empty/missing version below would be a real
    // miss rather than an unread packet.
    QCOMPARE(m_versionEntry->name(), QStringLiteral("(TESTING!!!) eNode"));
    QVERIFY2(!m_versionEntry->description().isEmpty(),
             "ST_DESCRIPTION missing from the 0xa3 tag block");

    // "17.x" is a protocol-compatibility claim, not eNode's own version: eserver only
    // admits a peer to its `working` set when this parses as >= 17.7.
    QVERIFY2(udpVersion.startsWith(QStringLiteral("17.")),
             qPrintable(QStringLiteral("ST_VERSION \"%1\" no longer leads with the Lugdunum "
                                       "compatibility version — eserver's version gate needs "
                                       ">= 17.7 to flag us `working`")
                            .arg(udpVersion)));
    QVERIFY2(udpVersion.contains(QStringLiteral("eNode"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("ST_VERSION \"%1\" does not name the server")
                            .arg(udpVersion)));

    // The cross-surface invariant: both strings are built from ed2k.ENodeVersionStr, so
    // the same vX.Y.Z has to appear in both. Release-proof, unlike a literal.
    const QString tcpTail = enodeVersionRe().match(m_tcpVersion).captured();
    const QString udpTail = enodeVersionRe().match(udpVersion).captured();
    QVERIFY2(!udpTail.isEmpty(),
             qPrintable(QStringLiteral("ST_VERSION \"%1\" carries no vX.Y.Z")
                            .arg(udpVersion)));
    QVERIFY2(tcpTail == udpTail,
             qPrintable(QStringLiteral("The two surfaces report different eNode versions: login "
                                       "\"%1\" (%2) vs 0xa3 \"%3\" (%4) — one of them is no longer "
                                       "derived from ed2k.ENodeVersionStr")
                            .arg(m_tcpVersion, tcpTail, udpVersion, udpTail)));

    // They are *meant* to disagree. Harmonising them onto the "17.14 …" form would make a
    // real MFC client display a bare "17.14" on the login surface (see the guard above).
    QVERIFY2(udpVersion != m_tcpVersion,
             qPrintable(QStringLiteral("Both surfaces now report \"%1\" — the login line and the "
                                       "0xa3 tag are deliberately different forms")
                            .arg(udpVersion)));

    qDebug() << "PASS: login surface" << m_tcpVersion << "/ 0xa3 surface" << udpVersion
             << "— both carry" << udpTail;
}

void tst_ServerLocalTest::versionLegacyDescPreservesVersion()
{
    QVERIFY(m_udpSocket != nullptr);
    const QString udpVersion = m_versionEntry->version();
    QVERIFY2(udpVersion.startsWith(QStringLiteral("17.")),
             "Previous step did not leave the 0xa3 version in place");

    // Clear the name so the reply is provably observed rather than assumed.
    m_versionEntry->setName(QString());

    // A payload-less OP_SERVER_DESC_REQ (just "e3 a2"). Under 6 bytes the server answers
    // in the legacy <name><desc> form: no tag block, so no version at all. That reply
    // must not blank the version we already hold.
    auto packet = std::make_unique<Packet>(OP_SERVER_DESC_REQ, 0u);
    packet->prot = OP_EDONKEYPROT;
    m_udpSocket->sendPacket(std::move(packet), *m_versionEntry, 5559);
    qDebug() << "Sent legacy (payload-less) OP_SERVER_DESC_REQ";

    const bool got = QTest::qWaitFor([this] {
        return !m_versionEntry->name().isEmpty();
    }, 10'000);
    QVERIFY2(got, "No legacy OP_SERVER_DESC_RES came back for the payload-less request");

    QCOMPARE(m_versionEntry->name(), QStringLiteral("(TESTING!!!) eNode"));
    QCOMPARE(m_versionEntry->version(), udpVersion);

    qDebug() << "PASS: legacy 0xa3 refreshed name/desc and left the version at" << udpVersion;
}

void tst_ServerLocalTest::stopServerVersions()
{
    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();

    m_serverConnect->setUDPSocket(nullptr);
    theApp.serverConnect = nullptr;
    thePrefs.setCryptLayerSupported(true);   // back to the default this round turned off
    delete m_udpSocket;
    m_udpSocket = nullptr;
    m_versionEntry = nullptr;
    stopServer();
}

// ---------------------------------------------------------------------------
// Helper: create a PartFile download for a fixture file
// ---------------------------------------------------------------------------

PartFile* tst_ServerLocalTest::addFixtureDownload(const QByteArray& hash, uint64 size,
                                                  const QString& name)
{
    auto* pf = new PartFile();
    pf->setFileName(name, true);
    pf->setFileSize(size);
    pf->setFileHash(reinterpret_cast<const uint8*>(hash.constData()));

    const QString tempDir = thePrefs.tempDirs().isEmpty()
                                ? m_tmpDir->filePath(QStringLiteral("temp"))
                                : thePrefs.tempDirs().constFirst();
    if (!pf->createPartFile(tempDir)) {
        delete pf;
        return nullptr;
    }
    m_downloadQueue->addDownload(pf);
    return pf;
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
