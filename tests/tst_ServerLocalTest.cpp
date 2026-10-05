/// @file tst_ServerLocalTest.cpp
/// @brief Local server integration test — starts a local eNode server and
///        exercises it over eleven rounds: obfuscated TCP (5565), plain TCP
///        (5555), UDP global search while disconnected (5559), plain TCP
///        against the obfuscated port (5565), the obfuscated stat crypt-ping
///        (5567), server-seeded fixture searches and sources, IPv6 over ::1,
///        the two version surfaces a client can display, the server's IPv6
///        dial-back verdict (ST_IPV6_STATUS) on an IPv6 session, search
///        constraints, TCP framing and search-tree width, and the server's
///        hardening against amplification and pre-login abuse. Publishes shared
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
#include "net/LocalIPv6.h"
#include "net/UDPSocket.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchExprParser.h"
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
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

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

/// Top-level `name:` of an eNode YAML config (quotes stripped), empty if absent.
QString readEnodeConfigName(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression re(
        QStringLiteral("^name:\\s*\"?(.*?)\"?\\s*$"), QRegularExpression::MultilineOption);
    return re.match(QString::fromUtf8(f.readAll())).captured(1);
}

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

    // Round 9: IPv6 dial-back verdict (ST_IPV6_STATUS) on an IPv6 session. eNode-go
    // probes whichever family the session arrived on, so a v6 session behind a
    // stateful firewall reads HAVE|PROBED. Needs a global IPv6 on a local interface.
    void startServerIPv6Probe();
    void ipv6ProbeReachable();
    void ipv6ProbeFirewalled();
    void stopServerIPv6Probe();

    // Round 10: search constraints as eMuleQt builds them (>= / <= operators, media
    // strings, live source counts), packet framing across TCP reads, and the
    // search-tree width limit. Plain TCP; the server has only the seeded fixtures.
    void startServerSearchFilters();
    void searchFilters_data();
    void searchFilters();
    void searchFiltersUdp_data();
    void searchFiltersUdp();
    void rawSplitHeader();
    void rawOversizedSearchTree();
    void rawUnknownProtocolCloses();
    void stopServerSearchFilters();

    // Round 11: search words with separators, Archive/CD-image type searches, UDP
    // replies packed and capped, the callback crypt trailer, the observed address a
    // LowID is told, and a pre-login packet the server refuses at its header.
    void startServerHardening();
    void searchWordsAndTypes_data();
    void searchWordsAndTypes();
    void udpSearchPackedRecords();
    void rawUdpGetSourcesCapped();
    void rawIDChangeOmitsLoopback();
    void callbackCarriesRequesterHash();
    void rawPreLoginOversizedHeader();
    void stopServerHardening();

    void cleanupTestCase();

private:
    // Helper methods
    void startServer();
    void stopServer();
    void connectToLocalServer(bool noCrypt, quint16 overridePort = 0);
    /// Connect to the local eNode over IPv6 (loopback ::1 by default). Returns false
    /// (does NOT assert) when the connection can't be established, so Round 7 can
    /// QSKIP on a server without ipv6.enabled.
    bool connectToLocalServerIPv6(const Address& serverAddr = Address::fromString(QStringLiteral("::1")));
    /// Connect over IPv6 and wait for the ST_IPV6_STATUS verdict into @p status.
    void connectAndAwaitIPv6Status(uint8& status);
    /// Disconnect and verify the verdict is cleared, so the next login is re-probed.
    void disconnectAndExpectIPv6StatusCleared();
    void disconnectFromServer();
    void publishFiles();
    void searchForKeyword();
    void searchForKeywordUDP();
    /// Fail on any WARN/ERROR line in the eNode log, except lines containing one of
    /// @p expectedWarnings — each of which must then appear at least once.
    void checkServerLog(const QStringList& expectedWarnings = {});
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
    QString m_enodeName;   // top-level `name:` of the -config YAML; what the server reports

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

    // Round 9: our global IPv6 the eNode is reached on (and dials back to).
    Address m_probeServerV6;
    bool m_ipv6ProbeActive = false;

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
            // Darwin 27 reports connected() for a refused connect (EISCONN); only a
            // known peer port means the server really accepted.
            if (!probe.waitForConnected(250) || probe.peerPort() != port) {
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
                m_searchList->processSearchAnswer(data, size, true,
                    srv ? Endpoint(srv->ipAddress(), srv->port()) : Endpoint());
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

void tst_ServerLocalTest::checkServerLog(const QStringList& expectedWarnings)
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
    QStringList missingExpected = expectedWarnings;
    int lineNumber = 0;
    while (!logFile.atEnd()) {
        ++lineNumber;
        const QString line = QString::fromUtf8(logFile.readLine());
        // A warning the round provoked on purpose is not a failure — its absence is.
        bool expected = false;
        for (const auto& text : expectedWarnings) {
            if (line.contains(text)) {
                missingExpected.removeAll(text);
                expected = true;
            }
        }
        if (expected)
            continue;
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
    QVERIFY2(missingExpected.isEmpty(),
             qPrintable(QStringLiteral("eNode server log lacks the expected warning(s): %1")
                            .arg(missingExpected.join(QStringLiteral("; ")))));

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
            if (QFileInfo(m_enodeWorkDir).isFile()) {
                m_enodeName = readEnodeConfigName(m_enodeWorkDir);
                m_enodeWorkDir = QFileInfo(m_enodeWorkDir).absolutePath();
            }
            break;
        }
    }
    if (m_enodeWorkDir.isEmpty())
        m_enodeWorkDir = QFileInfo(m_enodeExecutable).absolutePath();

    qDebug() << "eNode executable:" << m_enodeExecutable;
    qDebug() << "eNode args:" << m_enodeArgs;
    qDebug() << "eNode working dir:" << m_enodeWorkDir;
    qDebug() << "eNode name:" << m_enodeName;

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
    m_searchList->addSentUDPRequestIP(searchID, Address::fromString(QStringLiteral("127.0.0.1")));

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
                m_searchList->processUDPSearchAnswer(data, size, true, server);
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

    // Back to the shipped default (supported + requested, not required) now the
    // encrypted HighID probe is done. Required rejects every server source without crypt
    // flags + hash (MFC DownloadQueue.cpp:478), i.e. all of plain OP_FOUNDSOURCES.
    thePrefs.setCryptLayerRequired(false);

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
// transport: the login carries CT_MOD_IP_V6 (S1), the session is
// sentinel-safe, and a v6-aware eNode may return an inline 0xFFFFFFFF IPv6 source
// (S3a). It QSKIPs cleanly when the server is not reachable over IPv6, so it is
// safe on any eNode build; run it against an ipv6.enabled eNode-go with IPv6
// fixture peers in debug_fixtures.yaml to validate S1/S2/S3a end to end.
// ---------------------------------------------------------------------------

bool tst_ServerLocalTest::connectToLocalServerIPv6(const Address& serverAddr)
{
    delete m_localServer;
    m_localServer = new Server(uint32{0}, 5555);
    m_localServer->setIpAddress(serverAddr);
    m_localServer->setName(QStringLiteral("(TESTING!!!) eNode v6"));

    ServerConnectConfig cfg;
    cfg.safeServerConnect = true;
    cfg.autoConnectStaticOnly = false;
    cfg.useServerPriorities = false;
    cfg.reconnectOnDisconnect = false;
    cfg.addServersFromServer = false;
    cfg.serverKeepAliveTimeout = 0;
    cfg.userNick = QStringLiteral("eMuleQt-LocalTest-v6");
    cfg.listenPort = thePrefs.port();   // == the listener's port; round 9 may have closed it
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
    QCOMPARE(m_versionEntry->name(), m_enodeName);
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

    QCOMPARE(m_versionEntry->name(), m_enodeName);
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
// Round 9: IPv6 dial-back verdict (ST_IPV6_STATUS) on an IPv6 session
//
// eNode-go dial-back-probes our advertised IPv6 on whichever family the session
// arrived on (ipv6-client-implementation-spec.md §3a). Loopback can't carry that —
// ::1 is not public, so the server holds no IPv6 for us — so the session goes to
// one of our own global addresses, and the server dials back to our listener.
// "Firewalled" is simulated by closing the listener: the dial-back is refused, the
// same verdict a stateful router firewall that drops the SYN produces.
//
// The round runs eNode on a variant of the SERVER_TEST_CMD config:
//  - address ""  : dual-stack bind. Not "::" — eNode's firstRoutableIP() doesn't
//                  treat it as a wildcard, advertises it as the IPv4, and the
//                  OP_SERVERIDENT build then fails silently (no ident at all);
//  - dynIp       : a TEST-NET address, so the ident has an IPv4 to advertise;
//  - ipv6.enabled / ipv6.probeReachability forced on.
// ---------------------------------------------------------------------------

void tst_ServerLocalTest::startServerIPv6Probe()
{
    m_probeServerV6 = selectPreferredIPv6(scanLocalIPv6());
    if (m_probeServerV6.isNull())
        QSKIP("No global-unicast IPv6 on a local interface — skipping IPv6 dial-back round");

    const qsizetype cfgIdx = m_enodeArgs.indexOf(QStringLiteral("-config")) + 1;
    if (cfgIdx <= 0 || cfgIdx >= m_enodeArgs.size())
        QSKIP("SERVER_TEST_CMD has no -config argument — can't derive the IPv6 probe config");

    QFile src(m_enodeArgs[cfgIdx]);
    QVERIFY2(src.open(QIODevice::ReadOnly), qPrintable(QStringLiteral("Can't read %1").arg(src.fileName())));
    QString yaml = QString::fromUtf8(src.readAll());

    // Each key must exist exactly where expected, or the round would test the wrong config.
    const auto force = [&yaml](const QString& pattern, const QString& replacement) {
        const QRegularExpression re(pattern, QRegularExpression::MultilineOption);
        if (!re.match(yaml).hasMatch())
            return false;
        yaml.replace(re, replacement);
        return true;
    };
    QVERIFY2(force(QStringLiteral(R"(^address:.*$)"), QStringLiteral(R"(address: "")")),
             "config has no top-level address key");
    QVERIFY2(force(QStringLiteral(R"(^dynIp:.*$)"), QStringLiteral(R"(dynIp: "192.0.2.1")")),
             "config has no top-level dynIp key");
    QVERIFY2(force(QStringLiteral(R"(^(ipv6:\n\s+enabled:).*$)"), QStringLiteral(R"(\1 true)")),
             "config has no ipv6.enabled key");
    QVERIFY2(force(QStringLiteral(R"(^(\s+probeReachability:).*$)"), QStringLiteral(R"(\1 true)")),
             "config has no ipv6.probeReachability key");

    const QString probeCfg = m_tmpDir->filePath(QStringLiteral("enode.v6probe.yaml"));
    QFile dst(probeCfg);
    QVERIFY(dst.open(QIODevice::WriteOnly | QIODevice::Truncate));
    dst.write(yaml.toUtf8());
    dst.close();

    // Relative paths in the config (logs, fixtures, ipfilter) still resolve against
    // m_enodeWorkDir — startServer() keeps it as the working directory.
    const QStringList origArgs = m_enodeArgs;
    m_enodeArgs[cfgIdx] = probeCfg;
    startServer();
    m_enodeArgs = origArgs;
    if (QTest::currentTestFailed())
        return;

    m_ipv6ProbeActive = true;
    theApp.serverConnect = m_serverConnect;
    qDebug() << "IPv6 dial-back round: eNode reached at" << m_probeServerV6.toString();
}

void tst_ServerLocalTest::ipv6ProbeReachable()
{
    if (!m_ipv6ProbeActive)
        QSKIP("IPv6 dial-back round not active");

    uint8 status = 0;
    connectAndAwaitIPv6Status(status);
    if (QTest::currentTestFailed())
        return;

    // Our dual-stack listener answers the dial-back hello → verified reachable.
    QCOMPARE(status, uint8{IPV6ST_HAVE | IPV6ST_REACHABLE | IPV6ST_PROBED});
    QVERIFY(!theApp.publicIPv6ProbedUnreachable());
    QVERIFY(theApp.shouldAdvertisePublicIPv6());

    disconnectAndExpectIPv6StatusCleared();
}

void tst_ServerLocalTest::ipv6ProbeFirewalled()
{
    if (!m_ipv6ProbeActive)
        QSKIP("IPv6 dial-back round not active");

    // Close our listener so the dial-back is refused. The login still advertises the
    // port (cfg.listenPort comes from thePrefs). Reopen it whatever the outcome.
    const uint16 port = thePrefs.port();
    m_listenSocket->stopListening();
    auto relisten = qScopeGuard([this, port] {
        if (!m_listenSocket->startListening(port))
            qWarning() << "Failed to reopen the TCP listener on port" << port;
    });

    uint8 status = 0;
    connectAndAwaitIPv6Status(status);
    if (QTest::currentTestFailed())
        return;

    // Tested and unreachable: HAVE|PROBED without REACHABLE, on a v6 session.
    QCOMPARE(status, uint8{IPV6ST_HAVE | IPV6ST_PROBED});
    QVERIFY(theApp.publicIPv6ProbedUnreachable());
    QVERIFY2(!theApp.shouldAdvertisePublicIPv6(),
             "A firewalled IPv6 must not be advertised to peers");
    // Outgoing v6 dials stay allowed: only inbound is blocked.
    QVERIFY(theApp.hasConfidentPublicIPv6());

    disconnectAndExpectIPv6StatusCleared();
    QVERIFY2(theApp.shouldAdvertisePublicIPv6(),
             "Disconnect must clear the verdict so the next login advertises and is re-probed");
}

void tst_ServerLocalTest::stopServerIPv6Probe()
{
    if (!m_ipv6ProbeActive)
        return;

    disconnectFromServer();
    QTest::qWait(500);
    checkServerLog();
    theApp.serverConnect = nullptr;
    m_ipv6ProbeActive = false;
    stopServer();
}

void tst_ServerLocalTest::connectAndAwaitIPv6Status(uint8& status)
{
    QVERIFY2(connectToLocalServerIPv6(m_probeServerV6),
             "Failed to connect to eNode over our global IPv6 within 15s");

    // The verdict rides OP_SERVERIDENT right after login (the dial-back runs before it).
    const bool got = QTest::qWaitFor([] { return theApp.publicIPv6Status() != 0; }, 10'000);
    QVERIFY2(got, "No ST_IPV6_STATUS in OP_SERVERIDENT — is eNode's ipv6.publishSources on?");

    status = theApp.publicIPv6Status();
    qDebug() << "IPv6 session: clientID=0x" << Qt::hex << m_serverConnect->clientID()
             << "publicIPv6=" << theApp.publicIPv6().toString() << "ST_IPV6_STATUS=0x" << status;
}

void tst_ServerLocalTest::disconnectAndExpectIPv6StatusCleared()
{
    disconnectFromServer();
    const bool cleared = QTest::qWaitFor([] { return theApp.publicIPv6Status() == 0; }, 5'000);
    QVERIFY2(cleared, "ST_IPV6_STATUS verdict survived the disconnect");
}

// ---------------------------------------------------------------------------
// Round 10: search constraints, packet framing, search-tree width
//
// Every search here is built by buildSearchTermsPayload() — the code the search
// window uses — so the server sees exactly what eMuleQt (and MFC) send: numeric
// filters as >= / <= (ED2K_SEARCH_OP_GREATER_EQUAL / LESS_EQUAL), a >4 GiB size as
// a 64-bit leaf, media constraints as FT_MEDIA_ARTIST / ALBUM / TITLE string leaves.
// eNode-go used to understand only > and <, so each of those filters was dropped
// and the unfiltered result list returned; its memory engine also reported 0
// sources for every file.
//
// The raw-socket slots speak the protocol by hand, because they need control no
// real client exposes: where a TCP write is cut, a garbage header byte, and a
// search tree far wider than any client builds.
//
// Fixture data (eNode-go tests/data/debug_fixtures.yaml): Debian has 3 sources, 2
// complete; bunny 1 source, complete; Sintel 1 source, incomplete; the Snow Fight
// mp3 carries artist/album/title, bitrate 192 and length 245.
// ---------------------------------------------------------------------------

namespace {

const QByteArray kSnowHash = QByteArray::fromHex("33334444555566667777888899990000");

/// One eD2K frame read off a raw socket; a PR_ZLIB payload arrives inflated.
struct RawFrame {
    uint8 opcode = 0;
    QByteArray payload;
};

/// Frame @p payload as eD2K packet @p opcode, ready for a raw socket write.
QByteArray ed2kFrame(uint8 opcode, const QByteArray& payload)
{
    Packet packet(opcode, static_cast<uint32>(payload.size()));
    if (!payload.isEmpty())
        std::memcpy(packet.pBuffer, payload.constData(), static_cast<size_t>(payload.size()));
    return QByteArray(packet.getPacket(), static_cast<qsizetype>(packet.getRealPacketSize()));
}

/// Take one complete eD2K frame off the front of @p pending, without reading.
/// 1 when @p out holds a frame, 0 when more bytes are needed, -1 on a bad frame.
int takeRawFrame(QByteArray& pending, RawFrame& out)
{
    if (pending.size() < 6)
        return 0;
    const auto* raw = reinterpret_cast<const uint8*>(pending.constData());
    const uint32 declared = raw[1] | (raw[2] << 8) | (raw[3] << 16) | (uint32(raw[4]) << 24);
    if (declared == 0) {
        qWarning() << "raw frame: zero declared size";
        return -1;
    }
    const qsizetype total = 5 + static_cast<qsizetype>(declared);
    if (pending.size() < total)
        return 0;
    Packet packet(pending.constData());
    packet.pBuffer = new char[packet.size + 1];
    std::memcpy(packet.pBuffer, pending.constData() + 6, packet.size);
    pending.remove(0, total);
    if (packet.prot == OP_PACKEDPROT && !packet.unPackPacket(2'000'000u)) {
        qWarning() << "raw frame: inflate failed, opcode" << packet.opcode;
        return -1;
    }
    out.opcode = packet.opcode;
    out.payload = QByteArray(packet.pBuffer, static_cast<qsizetype>(packet.size));
    return 1;
}

/// Read the next eD2K frame from @p socket into @p out. @p pending carries bytes
/// read past the previous frame. False on timeout, disconnect or a bad frame.
bool readRawFrame(QTcpSocket& socket, QByteArray& pending, RawFrame& out, int timeoutMs = 15'000)
{
    QDeadlineTimer deadline(timeoutMs);
    for (;;) {
        if (const int taken = takeRawFrame(pending, out); taken != 0)
            return taken > 0;
        if (deadline.hasExpired() || socket.state() != QAbstractSocket::ConnectedState) {
            qWarning() << "raw frame: none within the deadline, socket state" << socket.state()
                       << "buffered" << pending.size() << "bytes";
            return false;
        }
        if (socket.waitForReadyRead(static_cast<int>(qMax<qint64>(1, deadline.remainingTime()))))
            pending += socket.readAll();
    }
}

/// Read frames until one with @p opcode arrives, skipping the rest.
bool readRawFrameWithOpcode(QTcpSocket& socket, QByteArray& pending, uint8 opcode, RawFrame& out)
{
    while (readRawFrame(socket, pending, out)) {
        if (out.opcode == opcode)
            return true;
    }
    return false;
}

/// Open a raw TCP session to the local eNode and log in. We claim port 1, where
/// nothing listens, so the server's firewall probe fails at once and we get a LowID —
/// the session is fully logged in either way. @p hashSeed keeps each session's user
/// hash distinct, so the server never sees a duplicate login.
bool rawLogin(QTcpSocket& socket, QByteArray& pending, uint8 hashSeed, QByteArray* idChange = nullptr)
{
    socket.connectToHost(QStringLiteral("127.0.0.1"), 5555);
    if (!socket.waitForConnected(5000) || socket.peerPort() != 5555)
        return false;
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    uint8 hash[16];
    for (int i = 0; i < 16; ++i)
        hash[i] = static_cast<uint8>(0xA0 + hashSeed + i);
    hash[5] = 14;    // eMule's user-hash markers
    hash[14] = 111;

    SafeMemFile data;
    data.writeHash16(hash);
    data.writeUInt32(0);   // client ID
    data.writeUInt16(1);   // listen port
    data.writeUInt32(2);   // tag count
    Tag(static_cast<uint8>(CT_NAME), QStringLiteral("eMuleQt-RawTest")).writeTagToFile(data);
    Tag(static_cast<uint8>(CT_VERSION), static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);
    socket.write(ed2kFrame(OP_LOGINREQUEST, data.buffer()));
    socket.flush();

    RawFrame frame;
    if (!readRawFrameWithOpcode(socket, pending, OP_IDCHANGE, frame))
        return false;
    if (idChange)
        *idChange = frame.payload;
    return true;
}

/// A single-keyword OP_SEARCHREQUEST payload.
QByteArray keywordSearch(const QString& keyword)
{
    SearchParams params;
    params.expression = keyword;
    return buildSearchTermsPayload(params);
}

/// A balanced OR tree of @p leaves one-letter keyword leaves. Balanced keeps it
/// shallow, so only a width limit can reject it.
QByteArray balancedOrTree(int leaves)
{
    if (leaves == 1)
        return QByteArray::fromHex("01010061");   // keyword "a"
    return QByteArray::fromHex("0001") + balancedOrTree(leaves / 2)
         + balancedOrTree(leaves - leaves / 2);
}

/// Whether an OP_SEARCHRESULT payload lists the file @p hash.
bool searchResultHas(const QByteArray& payload, const QByteArray& hash)
{
    return payload.indexOf(hash) >= 4;
}

uint32 searchResultCount(const QByteArray& payload)
{
    if (payload.size() < 4)
        return UINT32_MAX;
    const auto* p = reinterpret_cast<const uint8*>(payload.constData());
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32(p[3]) << 24);
}

} // namespace

void tst_ServerLocalTest::startServerSearchFilters()
{
    startServer();
    connectToLocalServer(/*noCrypt=*/true);

    // UDP socket for the global-search rows, wired like Round 3.
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& server) {
                m_searchList->processUDPSearchAnswer(data, size, true, server);
            });
}

void tst_ServerLocalTest::searchFilters_data()
{
    QTest::addColumn<QString>("expression");
    QTest::addColumn<uint32>("availability");
    QTest::addColumn<uint32>("completeSources");
    QTest::addColumn<uint64>("minSize");
    QTest::addColumn<uint64>("maxSize");
    QTest::addColumn<uint32>("minBitrate");
    QTest::addColumn<uint32>("minLength");
    QTest::addColumn<QString>("artist");
    QTest::addColumn<QString>("album");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QByteArray>("present");   // hash that must be in the results, or empty
    QTest::addColumn<QByteArray>("absent");    // hash that must not be, or empty

    const QString none;
    const QByteArray noHash;

    // FT_SOURCES >= n. Debian has exactly 3 sources: >= 3 keeps it, >= 4 drops it.
    QTest::newRow("availability >= 3 keeps Debian")
        << QStringLiteral("Debian") << 3u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << none << none << none << kDebianHash << noHash;
    QTest::newRow("availability >= 4 drops Debian")
        << QStringLiteral("Debian") << 4u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << none << none << none << noHash << kDebianHash;

    // FT_COMPLETE_SOURCES >= 1: bunny's only source is complete, Sintel's is not.
    QTest::newRow("complete >= 1 keeps bunny, drops Sintel")
        << QStringLiteral("1080p") << 0u << 1u << uint64(0) << uint64(0) << 0u << 0u
        << none << none << none << kBunnyHash << kSintelHash;

    // FT_FILESIZE >= / <= at the boundary, on a >4 GiB file (64-bit leaf).
    QTest::newRow("min size = exact size keeps Debian")
        << QStringLiteral("Debian") << 0u << 0u << kDebianSize << uint64(0) << 0u << 0u
        << none << none << none << kDebianHash << noHash;
    QTest::newRow("min size = size + 1 drops Debian")
        << QStringLiteral("Debian") << 0u << 0u << kDebianSize + 1 << uint64(0) << 0u << 0u
        << none << none << none << noHash << kDebianHash;
    QTest::newRow("max size = exact size keeps Debian")
        << QStringLiteral("Debian") << 0u << 0u << uint64(0) << kDebianSize << 0u << 0u
        << none << none << none << kDebianHash << noHash;
    QTest::newRow("max size = size - 1 drops Debian")
        << QStringLiteral("Debian") << 0u << 0u << uint64(0) << kDebianSize - 1 << 0u << 0u
        << none << none << none << noHash << kDebianHash;

    // FT_MEDIA_BITRATE / FT_MEDIA_LENGTH >= n.
    QTest::newRow("bitrate >= 192 keeps Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 192u << 0u
        << none << none << none << kSnowHash << noHash;
    QTest::newRow("bitrate >= 193 drops Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 193u << 0u
        << none << none << none << noHash << kSnowHash;
    QTest::newRow("length >= 245 keeps Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 245u
        << none << none << none << kSnowHash << noHash;
    QTest::newRow("length >= 246 drops Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 246u
        << none << none << none << noHash << kSnowHash;

    // FT_MEDIA_ARTIST / ALBUM / TITLE: case-insensitive substring.
    QTest::newRow("artist Morgenstern keeps Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << QStringLiteral("morgenstern") << none << none << kSnowHash << noHash;
    QTest::newRow("artist Nobody drops Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << QStringLiteral("Nobody") << none << none << noHash << kSnowHash;
    QTest::newRow("album Sintel OST keeps Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << none << QStringLiteral("Sintel OST") << none << kSnowHash << noHash;
    QTest::newRow("title Blizzard drops Snow Fight")
        << QStringLiteral("Snow") << 0u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << none << none << QStringLiteral("Blizzard") << noHash << kSnowHash;

    // A hand-written 20-way OR is well inside the server's 64-leaf limit. (Not more:
    // the parser emits an OR chain one level deeper per operator, and eMule's depth
    // limit of 24 — which the server shares — rejects a longer chain regardless.)
    QStringList terms;
    for (int i = 1; i < 20; ++i)
        terms << QStringLiteral("nomatch%1").arg(i);
    terms << QStringLiteral("Debian");
    QTest::newRow("20-keyword OR finds Debian")
        << terms.join(QStringLiteral(" OR ")) << 0u << 0u << uint64(0) << uint64(0) << 0u << 0u
        << none << none << none << kDebianHash << noHash;
}

void tst_ServerLocalTest::searchFilters()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");

    QFETCH(QString, expression);
    QFETCH(uint32, availability);
    QFETCH(uint32, completeSources);
    QFETCH(uint64, minSize);
    QFETCH(uint64, maxSize);
    QFETCH(uint32, minBitrate);
    QFETCH(uint32, minLength);
    QFETCH(QString, artist);
    QFETCH(QString, album);
    QFETCH(QString, title);
    QFETCH(QByteArray, present);
    QFETCH(QByteArray, absent);

    SearchParams params;
    params.expression = expression;
    params.availability = availability;
    params.completeSources = completeSources;
    params.minSize = minSize;
    params.maxSize = maxSize;
    params.minBitrate = minBitrate;
    params.minLength = minLength;
    params.artist = artist;
    params.album = album;
    params.title = title;
    const QByteArray terms = buildSearchTermsPayload(params);
    QVERIFY2(!terms.isEmpty(), "buildSearchTermsPayload produced nothing");

    const uint32 searchID = m_searchList->newSearch({}, params);
    bool answered = false;
    auto conn = connect(m_serverConnect, &ServerConnect::searchResultReceived,
            this, [&](const uint8* data, uint32 size, bool /*moreResults*/) {
                const Server* srv = m_serverConnect->currentServer();
                m_searchList->processSearchAnswer(data, size, true,
                    srv ? Endpoint(srv->ipAddress(), srv->port()) : Endpoint());
                answered = true;
            });
    auto disconnectGuard = qScopeGuard([&] { disconnect(conn); });

    auto packet = std::make_unique<Packet>(OP_SEARCHREQUEST, static_cast<uint32>(terms.size()));
    std::memcpy(packet->pBuffer, terms.constData(), static_cast<size_t>(terms.size()));
    m_serverConnect->sendPacket(std::move(packet));
    qDebug() << "Sent OP_SEARCHREQUEST" << expression << "terms" << terms.toHex();

    // The server answers every search, an empty one included.
    QVERIFY2(QTest::qWaitFor([&answered] { return answered; }, 15'000),
             "No OP_SEARCHRESULT within 15s");

    bool hasPresent = false;
    bool hasAbsent = false;
    uint32 sources = 0;
    uint32 complete = 0;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (!present.isEmpty() && memcmp(file->fileHash(), present.constData(), 16) == 0) {
            hasPresent = true;
            sources = file->sourceCount();
            complete = file->completeSourceCount();
        }
        if (!absent.isEmpty() && memcmp(file->fileHash(), absent.constData(), 16) == 0)
            hasAbsent = true;
    });
    qDebug() << "results:" << m_searchList->resultCount(searchID)
             << "present:" << hasPresent << "absent-hit:" << hasAbsent
             << "sources:" << sources << "complete:" << complete;

    if (!present.isEmpty())
        QVERIFY2(hasPresent, qPrintable(QStringLiteral("%1 missing from the results")
                                            .arg(QString::fromLatin1(present.toHex()))));
    if (!absent.isEmpty())
        QVERIFY2(!hasAbsent, qPrintable(QStringLiteral("%1 returned although the constraint "
                                                       "excludes it — the server ignored the filter")
                                            .arg(QString::fromLatin1(absent.toHex()))));

    // The counts a result carries are the live ones, not the 0 the server used to report.
    if (present == kDebianHash) {
        QCOMPARE(sources, 3u);
        QCOMPARE(complete, 2u);
    }
}

void tst_ServerLocalTest::searchFiltersUdp_data()
{
    QTest::addColumn<uint32>("availability");
    QTest::addColumn<bool>("expectDebian");

    QTest::newRow("udp availability >= 3 keeps Debian") << 3u << true;
    QTest::newRow("udp availability >= 4 drops Debian") << 4u << false;
}

void tst_ServerLocalTest::searchFiltersUdp()
{
    QFETCH(uint32, availability);
    QFETCH(bool, expectDebian);

    SearchParams params;
    params.expression = QStringLiteral("Debian");
    params.availability = availability;
    const QByteArray terms = buildSearchTermsPayload(params);
    QVERIFY(!terms.isEmpty());

    // No UDP flags: the legacy OP_GLOBSEARCHREQ, unobfuscated — as in Round 3.
    Server udpServer(htonl(0x7F000001), 5555);
    auto packet = buildGlobalSearchPacket(udpServer, terms, /*is64BitSearch=*/false);
    QVERIFY(packet != nullptr);

    const uint32 searchID = m_searchList->newSearch({}, params);
    m_searchList->addSentUDPRequestIP(searchID, Address::fromString(QStringLiteral("127.0.0.1")));
    m_udpSocket->sendPacket(std::move(packet), udpServer, 5559);
    qDebug() << "Sent OP_GLOBSEARCHREQ Debian availability >=" << availability;

    auto hasDebian = [&] {
        bool found = false;
        m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
            if (memcmp(file->fileHash(), kDebianHash.constData(), 16) == 0)
                found = true;
        });
        return found;
    };

    if (expectDebian) {
        QVERIFY2(QTest::qWaitFor(hasDebian, 10'000), "Debian missing from the UDP results");
    } else {
        // A UDP search with no match gets no reply, so wait out a fixed window.
        QTest::qWait(3000);
        QVERIFY2(!hasDebian(), "Debian returned over UDP although availability >= 4 excludes it");
    }
}

void tst_ServerLocalTest::rawSplitHeader()
{
    // Two pipelined searches with the boundary inside the second one's 6-byte header.
    // eNode-go used to drop a header fragment, so the second search went unanswered
    // and the stream after it was misframed.
    for (const int cut : {1, 3, 5}) {
        QTcpSocket socket;
        QByteArray pending;
        QVERIFY2(rawLogin(socket, pending, static_cast<uint8>(cut)), "raw login failed");

        const QByteArray first = ed2kFrame(OP_SEARCHREQUEST, keywordSearch(QStringLiteral("Debian")));
        const QByteArray second = ed2kFrame(OP_SEARCHREQUEST, keywordSearch(QStringLiteral("bunny")));
        const QByteArray stream = first + second;
        const qsizetype split = first.size() + cut;

        socket.write(stream.left(split));
        socket.flush();
        QVERIFY(socket.waitForBytesWritten(2000) || socket.bytesToWrite() == 0);
        QTest::qWait(200);   // separate TCP segments, separate server reads
        socket.write(stream.mid(split));
        socket.flush();
        qDebug() << "cut" << cut << ": wrote" << split << "then" << stream.size() - split << "bytes";

        RawFrame r1;
        RawFrame r2;
        QVERIFY2(readRawFrameWithOpcode(socket, pending, OP_SEARCHRESULT, r1),
                 qPrintable(QStringLiteral("cut %1: no reply to the first search").arg(cut)));
        QVERIFY2(readRawFrameWithOpcode(socket, pending, OP_SEARCHRESULT, r2),
                 qPrintable(QStringLiteral("cut %1: no reply to the search whose header was split").arg(cut)));
        QVERIFY2(searchResultHas(r1.payload, kDebianHash),
                 qPrintable(QStringLiteral("cut %1: first reply lacks Debian").arg(cut)));
        QVERIFY2(searchResultHas(r2.payload, kBunnyHash),
                 qPrintable(QStringLiteral("cut %1: second reply lacks bunny").arg(cut)));
        qDebug() << "PASS: cut" << cut << "— both searches answered";
        socket.disconnectFromHost();
    }
}

void tst_ServerLocalTest::rawOversizedSearchTree()
{
    QTcpSocket socket;
    QByteArray pending;
    QVERIFY2(rawLogin(socket, pending, 0x20), "raw login failed");

    // 100 leaves is past the server's limit (64) and far past any real client tree.
    // The server refuses to evaluate it but still answers, with an empty page, so a
    // client is never left waiting out its search timeout.
    socket.write(ed2kFrame(OP_SEARCHREQUEST, balancedOrTree(100)));
    socket.flush();

    RawFrame reply;
    QVERIFY2(readRawFrameWithOpcode(socket, pending, OP_SEARCHRESULT, reply),
             "no OP_SEARCHRESULT for an oversized search tree");
    QCOMPARE(searchResultCount(reply.payload), 0u);

    // The session survives: a normal search on it still works.
    socket.write(ed2kFrame(OP_SEARCHREQUEST, keywordSearch(QStringLiteral("Debian"))));
    socket.flush();
    QVERIFY2(readRawFrameWithOpcode(socket, pending, OP_SEARCHRESULT, reply),
             "session unusable after the oversized search");
    QVERIFY(searchResultHas(reply.payload, kDebianHash));
    socket.disconnectFromHost();
}

void tst_ServerLocalTest::rawUnknownProtocolCloses()
{
    QTcpSocket socket;
    QByteArray pending;
    QVERIFY2(rawLogin(socket, pending, 0x30), "raw login failed");

    // 0x00 is no eD2K protocol byte: the stream has lost its framing. MFC drops such
    // a peer; eNode-go used to discard the chunk and read on, mid-packet.
    socket.write(QByteArray::fromHex("000500000016"));
    socket.flush();

    const bool closed = QTest::qWaitFor([&socket] {
        socket.waitForReadyRead(100);
        socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
    }, 5'000);
    QVERIFY2(closed, "server kept a session whose stream carried an unknown protocol byte");
}

void tst_ServerLocalTest::stopServerSearchFilters()
{
    disconnectFromServer();
    QTest::qWait(500);
    // Exactly the two warnings this round provokes on purpose.
    checkServerLog({QStringLiteral("tcp unknown protocol"),
                    QStringLiteral("search request parse failed")});

    delete m_udpSocket;
    m_udpSocket = nullptr;
    stopServer();
}

// ---------------------------------------------------------------------------
// Round 11: separators, type aliases, UDP amplification, callback trailer,
// pre-login limits
//
// Searches again go through buildSearchTermsPayload, so "snow-fight" and the Iso
// type are exactly what the search window sends; eMuleQt's lexer keeps the hyphen
// inside the word and sends the CD-image type as "Iso", which eNode-go stores as
// "Pro". The UDP rows check that eMuleQt still reads the server's replies now that
// several search results share one datagram, and that one source request for many
// files is answered for at most 35 of them. The callback row has the eMuleQt client
// (a HighID here) ask for a raw LowID session, which must be told the requester's
// user hash so it can call back obfuscated.
// ---------------------------------------------------------------------------

namespace {

uint32 le32At(const QByteArray& b, qsizetype at)
{
    const auto* p = reinterpret_cast<const uint8*>(b.constData()) + at;
    return uint32(p[0]) | (uint32(p[1]) << 8) | (uint32(p[2]) << 16) | (uint32(p[3]) << 24);
}

} // namespace

void tst_ServerLocalTest::startServerHardening()
{
    startServer();
    connectToLocalServer(/*noCrypt=*/true);

    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDPSocket");
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& server) {
                m_searchList->processUDPSearchAnswer(data, size, true, server);
            });
}

void tst_ServerLocalTest::searchWordsAndTypes_data()
{
    QTest::addColumn<QString>("expression");
    QTest::addColumn<QString>("fileType");
    QTest::addColumn<QByteArray>("present");

    // "Morgenstern - Snow Fight.mp3": the words match whatever separates them.
    QTest::newRow("snow-fight finds Snow Fight") << QStringLiteral("snow-fight") << QString() << kSnowHash;
    QTest::newRow("snow.fight finds Snow Fight") << QStringLiteral("snow.fight") << QString() << kSnowHash;
    // Debian's ISO is stored as type Pro; eMuleQt sends the CD-image and archive types as-is.
    QTest::newRow("type Iso finds Debian")
        << QStringLiteral("Debian") << QStringLiteral(ED2KFTSTR_CDIMAGE) << kDebianHash;
    QTest::newRow("type Arc finds Debian")
        << QStringLiteral("Debian") << QStringLiteral(ED2KFTSTR_ARCHIVE) << kDebianHash;
}

void tst_ServerLocalTest::searchWordsAndTypes()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");
    QFETCH(QString, expression);
    QFETCH(QString, fileType);
    QFETCH(QByteArray, present);

    SearchParams params;
    params.expression = expression;
    params.fileType = fileType;
    const QByteArray terms = buildSearchTermsPayload(params);
    QVERIFY2(!terms.isEmpty(), "buildSearchTermsPayload produced nothing");

    const uint32 searchID = m_searchList->newSearch({}, params);
    bool answered = false;
    auto conn = connect(m_serverConnect, &ServerConnect::searchResultReceived,
            this, [&](const uint8* data, uint32 size, bool /*moreResults*/) {
                const Server* srv = m_serverConnect->currentServer();
                m_searchList->processSearchAnswer(data, size, true,
                    srv ? Endpoint(srv->ipAddress(), srv->port()) : Endpoint());
                answered = true;
            });
    auto disconnectGuard = qScopeGuard([&] { disconnect(conn); });

    auto packet = std::make_unique<Packet>(OP_SEARCHREQUEST, static_cast<uint32>(terms.size()));
    std::memcpy(packet->pBuffer, terms.constData(), static_cast<size_t>(terms.size()));
    m_serverConnect->sendPacket(std::move(packet));
    qDebug() << "Sent OP_SEARCHREQUEST" << expression << "type" << fileType << "terms" << terms.toHex();

    QVERIFY2(QTest::qWaitFor([&answered] { return answered; }, 15'000),
             "No OP_SEARCHRESULT within 15s");
    bool found = false;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (memcmp(file->fileHash(), present.constData(), 16) == 0)
            found = true;
    });
    qDebug() << "results:" << m_searchList->resultCount(searchID) << "found:" << found;
    QVERIFY2(found, qPrintable(QStringLiteral("%1 missing from the results of %2")
                                   .arg(QString::fromLatin1(present.toHex()), expression)));
}

void tst_ServerLocalTest::udpSearchPackedRecords()
{
    // Four fixtures match, and the server now packs their records into one datagram
    // instead of sending four. eMuleQt must read every record in it.
    SearchParams params;
    params.expression = QStringLiteral("Debian OR bunny OR Sintel OR Snow");
    const QByteArray terms = buildSearchTermsPayload(params);
    QVERIFY(!terms.isEmpty());

    Server udpServer(htonl(0x7F000001), 5555);
    auto packet = buildGlobalSearchPacket(udpServer, terms, /*is64BitSearch=*/false);
    QVERIFY(packet != nullptr);
    const uint32 searchID = m_searchList->newSearch({}, params);
    m_searchList->addSentUDPRequestIP(searchID, Address::fromString(QStringLiteral("127.0.0.1")));
    m_udpSocket->sendPacket(std::move(packet), udpServer, 5559);

    const QList<QByteArray> want = {kDebianHash, kBunnyHash, kSintelHash, kSnowHash};
    auto allFound = [&] {
        int found = 0;
        m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
            for (const QByteArray& h : want)
                if (memcmp(file->fileHash(), h.constData(), 16) == 0)
                    ++found;
        });
        return found == want.size();
    };
    QVERIFY2(QTest::qWaitFor(allFound, 10'000),
             qPrintable(QStringLiteral("only %1 of 4 UDP results read").arg(m_searchList->resultCount(searchID))));
}

void tst_ServerLocalTest::rawUdpGetSourcesCapped()
{
    // One OP_GLOBGETSOURCES naming 50 files. eMule asks for at most 35 per datagram;
    // the server used to answer every hash, which made one spoofed datagram a
    // ~100x amplifier.
    QUdpSocket udp;
    QVERIFY(udp.bind(QHostAddress::LocalHost, 0));
    QByteArray request("\xE3", 1);
    request.append(static_cast<char>(OP_GLOBGETSOURCES));
    for (int i = 0; i < 50; ++i)
        request.append(kDebianHash);
    QCOMPARE(udp.writeDatagram(request, QHostAddress::LocalHost, 5559), request.size());

    int replies = 0;
    QDeadlineTimer quiet(1500);
    while (!quiet.hasExpired()) {
        if (udp.waitForReadyRead(static_cast<int>(qMax<qint64>(1, quiet.remainingTime())))) {
            while (udp.hasPendingDatagrams()) {
                char buf[8192];
                udp.readDatagram(buf, sizeof(buf));
                ++replies;
            }
            quiet.setRemainingTime(1500);
        }
    }
    qDebug() << "50 hashes requested, replies:" << replies;
    QVERIFY2(replies > 0, "no OP_GLOBFOUNDSOURCES at all");
    QVERIFY2(replies <= 35, qPrintable(QStringLiteral("%1 replies to one datagram").arg(replies)));
}

void tst_ServerLocalTest::rawIDChangeOmitsLoopback()
{
    // OP_IDCHANGE +12 tells a LowID client its public IPv4. Seen over loopback (or a
    // LAN, a docker bridge) the address is not public, and eMule would adopt it: it
    // must be 0, which clients skip.
    QTcpSocket socket;
    QByteArray pending;
    QByteArray idChange;
    QVERIFY2(rawLogin(socket, pending, 0x40, &idChange), "raw login failed");
    QVERIFY2(idChange.size() >= 16, "OP_IDCHANGE without the observed-IP field");
    const uint32 id = le32At(idChange, 0);
    const uint32 observed = le32At(idChange, 12);
    qDebug() << "IDCHANGE id" << Qt::hex << id << "observed" << observed;
    QVERIFY2(id < 0x01000000, "the raw session was expected to be a LowID");
    QCOMPARE(observed, 0u);
    socket.disconnectFromHost();
}

void tst_ServerLocalTest::callbackCarriesRequesterHash()
{
    QVERIFY2(m_serverConnect->isConnected(), "Not connected — connect step failed");

    // The target: a raw LowID session.
    QTcpSocket target;
    QByteArray pending;
    QByteArray idChange;
    QVERIFY2(rawLogin(target, pending, 0x50, &idChange), "raw login failed");
    const uint32 lowID = le32At(idChange, 0);
    QVERIFY2(lowID < 0x01000000, "the target was expected to be a LowID");

    // The requester: this client, a HighID. OP_CALLBACKREQUEST names the LowID.
    auto packet = std::make_unique<Packet>(OP_CALLBACKREQUEST, 4u);
    pokeUInt32(packet->pBuffer, lowID);
    m_serverConnect->sendPacket(std::move(packet));

    // The target is told who calls: IP, port, then crypt options and user hash. MFC and
    // eMuleQt read the trailer from 23 bytes on; without it a callback to a requester
    // that requires obfuscation always fails. The client's socket writes from the event
    // loop, so spin it while collecting the target's frames instead of blocking on them.
    RawFrame frame;
    const bool relayed = QTest::qWaitFor([&] {
        pending += target.readAll();
        RawFrame f;
        while (takeRawFrame(pending, f) > 0) {
            if (f.opcode == OP_CALLBACKREQUESTED) {
                frame = f;
                return true;
            }
        }
        return false;
    }, 10'000);
    QVERIFY2(relayed, "no OP_CALLBACKREQUESTED for the LowID target");
    qDebug() << "OP_CALLBACKREQUESTED payload" << frame.payload.toHex();
    QCOMPARE(frame.payload.size(), 23);
    const auto hash = thePrefs.userHash();
    QVERIFY2(memcmp(frame.payload.constData() + 7, hash.data(), 16) == 0,
             "the trailer does not carry the requester's user hash");
    target.disconnectFromHost();
}

void tst_ServerLocalTest::rawPreLoginOversizedHeader()
{
    // Before login only a login of a few hundred bytes may follow. A header declaring
    // 2 MB used to reserve that much and wait for it; now the session is dropped at
    // the header.
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), 5555);
    QVERIFY(socket.waitForConnected(5000));
    socket.write(QByteArray::fromHex("e381841e0001"));   // PR_ED2K, size 2,000,001, OP_LOGINREQUEST
    socket.flush();
    const bool closed = QTest::qWaitFor([&socket] {
        socket.waitForReadyRead(100);
        socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
    }, 5'000);
    QVERIFY2(closed, "server kept a pre-login socket that declared a 2 MB packet");
}

void tst_ServerLocalTest::stopServerHardening()
{
    disconnectFromServer();
    QTest::qWait(500);
    // Exactly the warning this round provokes on purpose.
    checkServerLog({QStringLiteral("tcp oversized packet before login")});

    delete m_udpSocket;
    m_udpSocket = nullptr;
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
