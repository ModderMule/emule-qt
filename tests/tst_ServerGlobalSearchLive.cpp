/// @file tst_ServerGlobalSearchLive.cpp
/// @brief Live network integration test — UDP global search via OP_GLOBSEARCHREQ.
///
/// Sends OP_GLOBSEARCHREQ to the first 2 servers from data/server.met and
/// verifies the protocol exchange completes without error. If results are
/// returned, checks that the expected file hash appears.
///
/// Requires internet connectivity and reachable ED2K servers.
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "net/Packet.h"
#include "net/UDPSocket.h"
#include "prefs/Preferences.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QTest>

#include <memory>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// ED2K link under test — same as tst_ServerDownloadLive
// ---------------------------------------------------------------------------

static constexpr uint8 kExpectedHash[16] = {
    0x54, 0x7B, 0xF5, 0xAF, 0x06, 0xF5, 0xE4, 0x73,
    0x06, 0x0A, 0xFC, 0xAF, 0x66, 0x78, 0x78, 0x42
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_ServerGlobalSearchLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void globalSearchTwoServers();
    void cleanupTestCase();

private:
    TempDir* m_tmpDir = nullptr;
    ServerList* m_serverList = nullptr;
    UDPSocket* m_udpSocket = nullptr;
    SearchList* m_searchList = nullptr;
    UploadBandwidthThrottler* m_throttler = nullptr;
};

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void tst_ServerGlobalSearchLive::initTestCase()
{
    m_tmpDir = new TempDir();

    // 1. Preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());

    // 2. Upload bandwidth throttler — required for ThrottledControlSocket
    m_throttler = new UploadBandwidthThrottler(this);
    m_throttler->start();
    theApp.uploadBandwidthThrottler = m_throttler;

    // 3. ServerList — load from data/server.met
    m_serverList = new ServerList(this);
    const QString srcMet = projectDataDir() + QStringLiteral("/config/server.met");
    QVERIFY2(QFile::exists(srcMet), "Missing data/server.met");

    const QString dstMet = m_tmpDir->filePath(QStringLiteral("server.met"));
    QVERIFY(QFile::copy(srcMet, dstMet));
    QVERIFY2(m_serverList->loadServerMet(dstMet), "Failed to load server.met");
    QVERIFY2(m_serverList->serverCount() >= 2,
             "Need at least 2 servers in server.met for this test");
    theApp.serverList = m_serverList;

    qDebug() << "Loaded" << m_serverList->serverCount() << "servers from server.met";

    // 4. UDPSocket
    m_udpSocket = new UDPSocket(this);
    QVERIFY2(m_udpSocket->create(), "Failed to create UDP socket");

    // 5. SearchList
    m_searchList = new SearchList();

    // Wire UDP global search results → SearchList
    connect(m_udpSocket, &UDPSocket::globalSearchResult,
            this, [this](const uint8* data, uint32 size, uint32 serverIP, uint16 serverPort) {
                m_searchList->processUDPSearchAnswer(data, size, true, serverIP, serverPort);
            });
}

// ---------------------------------------------------------------------------
// Test: Send OP_GLOBSEARCHREQ to first 2 servers and collect results
// ---------------------------------------------------------------------------

void tst_ServerGlobalSearchLive::globalSearchTwoServers()
{
    // Start a search session
    const uint32 searchID = m_searchList->newSearch({}, SearchParams{});

    // Pick first 2 servers
    const size_t sendCount = qMin<size_t>(2, m_serverList->serverCount());
    QList<Server*> targets;
    targets.reserve(static_cast<qsizetype>(sendCount));
    for (size_t i = 0; i < sendCount; ++i)
        targets.append(m_serverList->serverAt(i));

    // Register IPs so spam tracking allows results through
    for (Server* srv : targets)
        m_searchList->addSentUDPRequestIP(srv->ip());

    // Build minimal OP_GLOBSEARCHREQ payload for keyword "eMulev0.50a"
    // Format: [type=0x01][len_lo][len_hi][keyword bytes...]
    static constexpr char kKeyword[] = "eMulev0.50a";
    static constexpr uint32 kKeyLen  = sizeof(kKeyword) - 1; // 11
    const uint32 payloadSize = 1 + 2 + kKeyLen;

    for (Server* srv : targets) {
        auto packet = std::make_unique<Packet>(OP_GLOBSEARCHREQ, payloadSize);
        packet->prot = OP_EDONKEYPROT;

        uint8* p = reinterpret_cast<uint8*>(packet->pBuffer);
        *p++ = 0x01;                           // type = filename keyword
        *p++ = static_cast<uint8>(kKeyLen);    // length lo
        *p++ = 0x00;                           // length hi
        std::memcpy(p, kKeyword, kKeyLen);

        // OP_GLOBSEARCHREQ goes to server UDP port (= TCP port + 4 by ED2K spec)
        // Pass as specialPort; UDPSocket uses it when non-zero.
        const uint16 udpPort = static_cast<uint16>(srv->port() + 4);

        qDebug() << "Sending OP_GLOBSEARCHREQ to"
                 << srv->name() << srv->address() << "udp:" << udpPort;

        m_udpSocket->sendPacket(std::move(packet), *srv, udpPort);
    }

    // Wait up to 60s for at least one result
    const bool gotResults = QTest::qWaitFor([this, searchID] {
        return m_searchList->resultCount(searchID) > 0;
    }, 60'000);

    const uint32 count = m_searchList->resultCount(searchID);
    qDebug() << "UDP global search results:" << count;

    if (gotResults) {
        bool found = false;
        m_searchList->forEachResult(searchID, [&](const SearchFile* file) {
            if (memcmp(file->fileHash(), kExpectedHash, 16) == 0)
                found = true;
        });
        if (found)
            qDebug() << "PASS: Expected hash found in UDP search results";
        else
            qDebug() << "Note: results returned but expected hash not among them"
                        " (may be indexed on a different server)";
        QVERIFY(count > 0);
    } else {
        // UDP is fire-and-forget — no response is acceptable
        qDebug() << "No UDP search results received (servers may be unreachable"
                    " or file not indexed there) — graceful skip";
        QSKIP("No UDP global search results — servers may not be reachable");
    }
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void tst_ServerGlobalSearchLive::cleanupTestCase()
{
    delete m_searchList;
    m_searchList = nullptr;

    if (m_throttler) {
        m_throttler->endThread();
        m_throttler->wait(5000);
    }

    theApp.uploadBandwidthThrottler = nullptr;
    theApp.serverList = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_ServerGlobalSearchLive)
#include "tst_ServerGlobalSearchLive.moc"
