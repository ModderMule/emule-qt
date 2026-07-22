/// @file tst_ServerConnectLive.cpp
/// @brief Live diagnostic — connect to real ED2K servers from server.met and
///        run a UDP global search, gated on the supplied port configuration.
///
/// Purpose: characterize "flaky/slow" server connectivity with a repeatable,
/// guarded instrument. Unlike tst_ServerDownloadLive (which QSKIPs and offers
/// files), this test focuses on the connect handshake itself:
///
/// Test 1 (connectToFirstReachableServer): walks data/config/server.met,
///   timing each connection attempt, and CONCLUDES SUCCESS ON THE FIRST server
///   that connects. The per-server latency/outcome table it prints surfaces
///   slow or dead entries at the top of the list — the usual "slow connect"
///   cause under safeServerConnect (one attempt at a time).
///
/// Test 2 (udpGlobalSearch): sends OP_GLOBSEARCHREQ to the connected server
///   (plus the first couple of servers) over UDP and verifies a result comes
///   back. If UDP is blocked/unreachable it QSKIPs ("if possible").
///
/// Port configuration (env vars EMULE_TCP_PORT, EMULE_UDP_PORT — same pattern
/// as tst_PortTestLive / tst_KadLiveNetwork):
///   - Both set → bind the TCP listener to EMULE_TCP_PORT (we declare ourselves
///     reachable) → expect a HighID from the server.
///   - Unset    → bind a random port (firewalled) → expect a LowID.
/// HighID is a TCP-callback decision, so only the TCP listen port gates it. When
/// EMULE_UDP_PORT is set, the server UDP socket is bound to it (via serverUDPPort()),
/// so global-search replies traverse the router forward and can actually arrive;
/// unset → a random UDP port (firewalled), replies dropped.
///
/// Optional env knobs:
///   - EMULE_SEARCH_KEYWORD      overrides the global-search keyword ("eMule").
///   - EMULE_CONNECT_TIMEOUT_MS  per-server connect budget (default 12000).
///   - EMULE_CONNECT_MAX_SERVERS cap on servers tried (default 10).
///   - EMULE_CONNECT_NOCRYPT=0   use obfuscated TCP instead of the plain default,
///                               to reproduce the obfuscated-login failure below.
///   - EMULE_SMART_LOWID=1       re-enable smart-LowID (see below).
///
/// Findings this diagnostic surfaced (root causes of "flaky/slow"):
///   1. Obfuscated *server* login does not complete against current server.met
///      hosts: the server accepts the obfuscated TCP socket, receives our
///      encrypted OP_LoginRequest, holds the socket open, but never returns
///      OP_IDCHANGE — so the client waits out the whole timeout. Plain login to
///      the same host works. Hence the plain default here.
///   2. Smart-LowID (ServerConnectConfig::smartLowIdCheck, default true) makes a
///      firewalled client DISCONNECT on every LowID and retry another server,
///      never settling. Turned OFF here so the assigned ID can be measured.
///   3. Public servers tarpit repeated connection storms — hammering the same
///      IPs in quick succession degrades to full-timeout hangs. Run sparingly.
///
/// Requires internet connectivity and reachable ED2K servers.
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "net/UDPSocket.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

/// Per-server plain-login budget. Real refused/dead servers fail fast; flaky
/// public servers can take ~11s to complete login, so give them room.
static constexpr int kPerServerTimeoutMs = 12'000;

/// Cap on servers tried, so a run of dead entries at the top of server.met stays
/// within the CMake TIMEOUT (worst case ≈ 10 × (7s obf + 12s plain) = 190s).
static constexpr size_t kMaxServersToTry = 10;

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_ServerConnectLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void connectToFirstReachableServer();
    void udpGlobalSearch();
    void cleanupTestCase();

private:
    TempDir* m_tmpDir = nullptr;
    ServerList* m_serverList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    ClientList* m_clientList = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    UDPSocket* m_udpSocket = nullptr;
    SearchList* m_searchList = nullptr;

    bool m_portsOpen = false;   ///< True when both EMULE_TCP/UDP_PORT are set (expect HighID).
};

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void tst_ServerConnectLive::initTestCase()
{
    loadProjectEnv();

    m_tmpDir = new TempDir();

    // 1. Preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());

    // Diagnostic: EMULE_LOG_RAW=1 dumps the raw (encrypted) TCP handshake/login
    // bytes and decrypted receive bytes, for wire-level inspection of the
    // obfuscated server-login handshake.
    if (qEnvironmentVariableIntValue("EMULE_LOG_RAW") == 1)
        thePrefs.setLogRawSocketPackets(true);

    // 2. Port configuration — EMULE_TCP_PORT / EMULE_UDP_PORT select specific
    //    forwarded ports. When both are set we bind the TCP listener to the
    //    forwarded port so the server's callback reaches us → HighID. When
    //    unset we bind a random port (firewalled) → LowID.
    const int envTcpPort = qEnvironmentVariableIntValue("EMULE_TCP_PORT");
    const int envUdpPort = qEnvironmentVariableIntValue("EMULE_UDP_PORT");
    m_portsOpen = (envTcpPort > 0 && envTcpPort <= 65535
                && envUdpPort > 0 && envUdpPort <= 65535);
    const auto tcpBindPort = m_portsOpen ? static_cast<uint16>(envTcpPort) : uint16{0};

    // 3. User hash — needed for secure identification during login / obfuscation.
    auto hash = thePrefs.userHash();
    if (std::all_of(hash.begin(), hash.end(), [](uint8 b) { return b == 0; }))
        thePrefs.setUserHash(Preferences::generateUserHash());

    // 4. Client credits — needed by the login secure-ident path.
    theApp.clientCredits = new ClientCreditsList();

    // 5. ClientList
    m_clientList = new ClientList(this);
    theApp.clientList = m_clientList;

    // 6. TCP listener — reachable-by-forwarding when m_portsOpen, else firewalled.
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(tcpBindPort),
             qPrintable(QStringLiteral("Failed to start TCP listener on port %1").arg(tcpBindPort)));
    theApp.listenSocket = m_listenSocket;
    thePrefs.setPort(m_listenSocket->connectedPort());

    // Adopt inbound callback sockets into UpDownClients so the server's HighID-callback OP_HELLO
    // gets an OP_HELLOANSWER — mirrors production CoreSession wiring (CoreSession.cpp:643-644).
    // Without this the callback socket is orphaned → no answer → LowID even with the port open.
    connect(m_listenSocket, &ListenSocket::newClientConnection,
            m_clientList, &ClientList::handleIncomingConnection);

    // 7. Upload throttler — flushes control packets from the queue.
    m_throttler = new UploadBandwidthThrottler(this);
    m_throttler->start();
    theApp.uploadBandwidthThrottler = m_throttler;

    // 8. No IP filter.
    theApp.ipFilter = nullptr;

    // 9. Empty known/shared file lists — no files are offered, but the login
    //    path may reference theApp.sharedFileList, so provide safe empties.
    m_knownFiles = new KnownFileList();
    m_sharedFiles = new SharedFileList(m_knownFiles, this);
    theApp.knownFileList = m_knownFiles;
    theApp.sharedFileList = m_sharedFiles;

    // 10. ServerList — load from data/config/server.met.
    m_serverList = new ServerList(this);
    const QString srcMet = projectDataDir() + QStringLiteral("/config/server.met");
    QVERIFY2(QFile::exists(srcMet), "Missing data/config/server.met");

    const QString dstMet = m_tmpDir->filePath(QStringLiteral("server.met"));
    QVERIFY(QFile::copy(srcMet, dstMet));
    QVERIFY2(m_serverList->loadServerMet(dstMet), "Failed to load server.met");
    QVERIFY2(m_serverList->serverCount() > 0, "No servers in server.met");
    theApp.serverList = m_serverList;

    qDebug() << "Loaded" << m_serverList->serverCount()
             << "servers from server.met; ports:"
             << (m_portsOpen ? "OPEN (expect HighID)" : "random (expect LowID)")
             << "TCP:" << m_listenSocket->connectedPort();

    // 11. UDPSocket — for the global search (also handed to ServerConnect).
    //     Bind the server UDP socket to the forwarded port when open, so global-search
    //     replies traverse the router forward instead of landing on an unforwarded random
    //     port; 65535 = random (firewalled case). create() now honors serverUDPPort()
    //     (MFC CUDPSocket::Create), so setting the pref is all that's needed.
    thePrefs.setServerUDPPort(m_portsOpen ? static_cast<uint16>(envUdpPort) : uint16{65535});
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDP socket");
    thePrefs.setUdpPort(m_udpSocket->localPort());
    qDebug() << "Server UDP socket bound on port" << m_udpSocket->localPort()
             << (m_portsOpen ? "(forwarded — replies can arrive)" : "(random — firewalled)");

    // 12. SearchList — wired to receive UDP global-search results.
    m_searchList = new SearchList();
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, const Endpoint& server) {
                m_searchList->processUDPSearchAnswer(data, size, true,
                                                     server.address().toNetworkUint32(),
                                                     server.port());
            });

    // 13. ServerConnect — production-representative config (mirrors CoreSession).
    m_serverConnect = new ServerConnect(*m_serverList, this);

    ServerConnectConfig cfg;
    cfg.safeServerConnect = true;
    cfg.autoConnectStaticOnly = false;
    cfg.useServerPriorities = false;
    cfg.reconnectOnDisconnect = false;
    cfg.addServersFromServer = false;
    cfg.cryptLayerPreferred = true;
    cfg.cryptLayerRequired = false;
    cfg.cryptLayerEnabled = true;
    cfg.serverKeepAliveTimeout = 0;
    cfg.userNick = QStringLiteral("eMuleQt-TestClient");
    cfg.listenPort = m_listenSocket->connectedPort();
    // CT_EMULE_VERSION: (major << 17) | (minor << 10) | (update << 7) — mirrors
    // CoreSession::start() so the test exercises the version we ship.
    cfg.emuleVersionTag = (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10)
                        | (static_cast<uint32>(SEND_EMULE_VERSION_UPD) <<  7);
    cfg.connectionTimeout = 15000;
    // Smart-LowID makes the client DISCONNECT on a LowID and try another server
    // hoping for a HighID. When firewalled (our default/closed-ports case) every
    // server hands out a LowID, so the client bounces forever and never settles —
    // exactly the "flaky/slow" symptom for firewalled users. The diagnostic must
    // accept the assigned ID to measure it, so smart-LowID is OFF by default here;
    // set EMULE_SMART_LOWID=1 to observe the bounce.
    cfg.smartLowIdCheck = (qEnvironmentVariableIntValue("EMULE_SMART_LOWID") == 1);

    auto userHash = thePrefs.userHash();
    std::copy(userHash.begin(), userHash.end(), cfg.userHash.begin());

    m_serverConnect->setConfig(cfg);
    m_serverConnect->setUDPSocket(m_udpSocket);
}

// ---------------------------------------------------------------------------
// Test 1: connect to the first reachable server, timing each attempt
// ---------------------------------------------------------------------------

void tst_ServerConnectLive::connectToFirstReachableServer()
{
    struct Row {
        QString name;
        QString addr;
        uint16  port = 0;
        bool    connected = false;
        qint64  ms = 0;
        QString mode;   ///< "obf", "plain", or "-" (how/if it connected)
    };
    QList<Row> table;

    // Env-tunable knobs — a diagnostic needs to isolate the crypto path and give
    // slow servers a fair budget. Per-phase budget: each server gets an obfuscated
    // attempt AND a plain attempt (unless EMULE_CONNECT_NOCRYPT pins one mode).
    int phaseTimeout = qEnvironmentVariableIntValue("EMULE_CONNECT_TIMEOUT_MS");
    if (phaseTimeout <= 0)
        phaseTimeout = kPerServerTimeoutMs;
    int maxServers = qEnvironmentVariableIntValue("EMULE_CONNECT_MAX_SERVERS");
    if (maxServers <= 0)
        maxServers = static_cast<int>(kMaxServersToTry);

    // Crypt mode. Default is PLAIN because the obfuscated *server* login is
    // currently broken (servers hold the obf socket open but never return
    // IDCHANGE — see the file header); plain is the only reliable baseline.
    // EMULE_CONNECT_NOCRYPT=0 reproduces the obfuscated failure for diagnosis.
    const bool nocryptSet = qEnvironmentVariableIsSet("EMULE_CONNECT_NOCRYPT");
    const bool useObf = nocryptSet
                        && qEnvironmentVariableIntValue("EMULE_CONNECT_NOCRYPT") == 0;
    const QString modeName = useObf ? QStringLiteral("obf") : QStringLiteral("plain");

    qDebug() << "Connect params: phaseTimeout" << phaseTimeout << "ms  mode:"
             << modeName << " maxServers:" << maxServers;

    QSignalSpy messageSpy(m_serverConnect, &ServerConnect::serverMessageReceived);

    const size_t tryCount = std::min<size_t>(m_serverList->serverCount(),
                                             static_cast<size_t>(maxServers));
    for (size_t i = 0; i < tryCount; ++i) {
        Server* srv = m_serverList->serverAt(i);
        Row row{srv->name(), srv->address(), srv->port(), false, 0, QStringLiteral("-")};

        m_serverConnect->stopConnectionTry();

        QElapsedTimer timer;
        timer.start();
        m_serverConnect->connectToServer(srv, /*multiconnect*/ false, /*noCrypt*/ !useObf);
        row.connected = QTest::qWaitFor([this] {
            return m_serverConnect->isConnected();
        }, phaseTimeout);
        row.ms = timer.elapsed();
        if (row.connected)
            row.mode = modeName;

        table.append(row);
        if (row.connected)
            break;
    }

    // Diagnostic table — the point of this test. Surfaces slow/dead servers and
    // the obfuscated-vs-plain login split.
    qDebug() << "---- server.met connect latency ----";
    for (const Row& r : table) {
        qDebug().noquote()
            << QStringLiteral("  %1  %2  %3:%4  %5  %6 ms")
                   .arg(r.connected ? QStringLiteral("OK  ") : QStringLiteral("FAIL"))
                   .arg(r.mode, -5)
                   .arg(r.addr)
                   .arg(r.port)
                   .arg(r.name.left(28), -28)
                   .arg(r.ms);
    }
    for (int i = 0; i < messageSpy.count(); ++i)
        qDebug() << "Server message:" << messageSpy.at(i).first().toString();

    // Success = at least one server accepted us (concludes on the first).
    QVERIFY2(m_serverConnect->isConnected(),
             qPrintable(QStringLiteral("No server from server.met accepted our connection "
                                       "(tried %1)").arg(table.size())));

    // Let IDCHANGE / server status settle so isLowID() reflects the final ID.
    QTest::qWait(1000);

    const Server* srv = m_serverConnect->currentServer();
    qDebug() << "Connected to" << (srv ? srv->name() : QStringLiteral("?"))
             << "clientID:" << Qt::hex << m_serverConnect->clientID()
             << "obfuscated:" << m_serverConnect->isConnectedObfuscated();

    // Port contract: reachable → HighID, firewalled → LowID.
    if (m_portsOpen) {
        QVERIFY2(!m_serverConnect->isLowID(),
                 qPrintable(QStringLiteral("EMULE_TCP_PORT %1 declared open but the server "
                                           "gave a LowID (clientID=0x%2) — the firewall "
                                           "probe-back failed")
                                .arg(m_listenSocket->connectedPort())
                                .arg(m_serverConnect->clientID(), 0, 16)));
    } else {
        QVERIFY2(m_serverConnect->isLowID(),
                 qPrintable(QStringLiteral("No forwarded port (random %1) but got a HighID "
                                           "(clientID=0x%2) — unexpected")
                                .arg(m_listenSocket->connectedPort())
                                .arg(m_serverConnect->clientID(), 0, 16)));
    }
}

// ---------------------------------------------------------------------------
// Test 2: UDP global search — verify a result comes back (if UDP is usable)
// ---------------------------------------------------------------------------

void tst_ServerConnectLive::udpGlobalSearch()
{
    if (!m_serverConnect->isConnected())
        QSKIP("Not connected — connectToFirstReachableServer did not succeed");

    const QString keyword =
        qEnvironmentVariable("EMULE_SEARCH_KEYWORD", QStringLiteral("eMule"));
    const QByteArray kw = keyword.toUtf8();
    QVERIFY2(kw.size() > 0 && kw.size() <= 0xFFFF, "Invalid search keyword");

    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    // Targets: the connected (known-alive) server plus the first couple of
    // servers, deduped by IP — maximizes the odds of a UDP answer.
    QList<Server*> targets;
    QSet<uint32> seen;
    auto addTarget = [&](Server* srv) {
        if (!srv) return;
        const uint32 ip = srv->ipAddress().toNetworkUint32();
        if (ip == 0 || seen.contains(ip)) return;
        seen.insert(ip);
        targets.append(srv);
    };
    addTarget(m_serverConnect->currentServer());
    for (size_t i = 0; i < std::min<size_t>(2, m_serverList->serverCount()); ++i)
        addTarget(m_serverList->serverAt(i));

    // Register IPs so SearchList's UDP spam guard lets the answers through.
    for (Server* srv : targets)
        m_searchList->addSentUDPRequestIP(srv->ipAddress().toNetworkUint32());

    // OP_GLOBSEARCHREQ payload: [type=0x01][len_lo][len_hi][keyword bytes...]
    const auto keyLen = static_cast<uint16>(kw.size());
    const uint32 payloadSize = 1u + 2u + keyLen;

    for (Server* srv : targets) {
        auto packet = std::make_unique<Packet>(OP_GLOBSEARCHREQ, payloadSize);
        packet->prot = OP_EDONKEYPROT;

        uint8* p = reinterpret_cast<uint8*>(packet->pBuffer);
        *p++ = 0x01;                                        // type = filename keyword
        *p++ = static_cast<uint8>(keyLen & 0xFF);           // length lo
        *p++ = static_cast<uint8>((keyLen >> 8) & 0xFF);    // length hi
        std::memcpy(p, kw.constData(), keyLen);

        // ED2K server UDP port = TCP port + 4.
        const uint16 udpPort = static_cast<uint16>(srv->port() + 4);
        qDebug() << "Sending OP_GLOBSEARCHREQ \"" << keyword << "\" to"
                 << srv->name() << srv->address() << "udp:" << udpPort;
        m_udpSocket->sendPacket(std::move(packet), *srv, udpPort);
    }

    const bool gotResults = QTest::qWaitFor([this, searchID] {
        return m_searchList->resultCount(searchID) > 0;
    }, 60'000);

    const uint32 count = m_searchList->resultCount(searchID);
    qDebug() << "UDP global search results:" << count;

    if (!gotResults) {
        // UDP is fire-and-forget — no answer is acceptable (blocked / unreachable).
        QSKIP("No UDP global search results — UDP may be blocked or servers unreachable");
    }

    // Log a few names as evidence the results are real.
    int shown = 0;
    m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
        if (shown++ < 5)
            qDebug().noquote() << "  result:" << file->fileName();
    });

    QVERIFY2(count > 0, "Expected at least one UDP global-search result");
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void tst_ServerConnectLive::cleanupTestCase()
{
    if (m_serverConnect)
        m_serverConnect->disconnect();

    if (m_throttler) {
        m_throttler->endThread();
        m_throttler->wait(5000);
    }

    if (m_listenSocket)
        m_listenSocket->stopListening();

    delete m_searchList;
    m_searchList = nullptr;

    theApp.serverList = nullptr;
    theApp.sharedFileList = nullptr;
    theApp.knownFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    theApp.ipFilter = nullptr;
    delete theApp.clientCredits;
    theApp.clientCredits = nullptr;

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_ServerConnectLive)
#include "tst_ServerConnectLive.moc"
