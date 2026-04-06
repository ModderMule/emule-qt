/// @file tst_PortTestLive.cpp
/// @brief Live port test — verifies TCP/UDP connectivity via the official
///        eMule port test service (porttest.emule-project.net).
///
/// Exercises the same endpoint that the Options → Connection "Test Ports"
/// button uses, but programmatically via HTTP GET instead of opening a browser.
/// Tests both with and without protocol obfuscation enabled.
///
/// Port configuration (env vars EMULE_TCP_PORT, EMULE_UDP_PORT):
///   - Both set → bind to those ports, expect open results.
///   - Unset   → use default 5662/5672, expect closed results.
///
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------

class tst_PortTestLive : public QObject {
    Q_OBJECT

public:
    struct PortTestResult {
        bool tcpOk = false;
        bool udpOk = false;
        QString html;           ///< Raw HTML/JSON for diagnostics on failure.
        QString ip;             ///< Public IP reported by the test service.
    };

private:
    /// Fetch the port test page and parse results (legacy emule-project.net).
    PortTestResult runPortTest(bool obfuscation);

    /// Port test via emule-qt.org REST API (JSON response).
    PortTestResult runPortTestQt();

    bool m_portsOpen = false;
    bool m_emuleQtTest = false;
    uint16 m_tcpPort = 5662;
    uint16 m_udpPort = 5672;
    ListenSocket* m_listenSocket = nullptr;
    ClientUDPSocket* m_clientUDP = nullptr;
    QNetworkAccessManager m_nam;

private slots:
    void initTestCase();
    void testPortsNoObfuscation();
    void testPortsWithObfuscation();
    void testPortsQt();
    void cleanupTestCase();
};

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

void tst_PortTestLive::initTestCase()
{
    loadProjectEnv();

    m_emuleQtTest = qEnvironmentVariableIntValue("EMULE_PORTTEST_QT") == 1;

    // Port configuration — same pattern as tst_KadLiveNetwork.
    const int envTcpPort = qEnvironmentVariableIntValue("EMULE_TCP_PORT");
    const int envUdpPort = qEnvironmentVariableIntValue("EMULE_UDP_PORT");
    m_portsOpen = (envTcpPort > 0 && envTcpPort <= 65535
                && envUdpPort > 0 && envUdpPort <= 65535);

    m_tcpPort = m_portsOpen ? static_cast<uint16>(envTcpPort) : uint16{5662};
    m_udpPort = m_portsOpen ? static_cast<uint16>(envUdpPort) : uint16{5672};

    // Generate a user hash if none exists (needed for obfuscation test).
    auto hash = thePrefs.userHash();
    bool allZero = true;
    for (auto b : hash) {
        if (b != 0) { allZero = false; break; }
    }
    if (allZero)
        thePrefs.setUserHash(Preferences::generateUserHash());

    // Start TCP listener.
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(m_tcpPort),
             qPrintable(QStringLiteral("Failed to start TCP listener on port %1").arg(m_tcpPort)));
    theApp.listenSocket = m_listenSocket;

    // Start UDP socket.
    m_clientUDP = new ClientUDPSocket(this);
    QVERIFY(m_clientUDP->rebind(m_udpPort));
    theApp.clientUDP = m_clientUDP;

    // Wire UDP port test → reply on TCP (matches legacy MFC behaviour).
    connect(m_clientUDP, &ClientUDPSocket::portTestReceived, this, [this]() {
        m_listenSocket->sendPortTestReply('1', true);
    });

    thePrefs.setPort(m_listenSocket->connectedPort());
    thePrefs.setUdpPort(m_clientUDP->connectedPort());

    qDebug() << "Port test:" << (m_portsOpen ? "OPEN expected" : "CLOSED expected")
             << "TCP:" << m_listenSocket->connectedPort()
             << "UDP:" << m_clientUDP->connectedPort();
}

void tst_PortTestLive::cleanupTestCase()
{
    theApp.listenSocket = nullptr;
    theApp.clientUDP = nullptr;
}

// ---------------------------------------------------------------------------
// Port test helper
// ---------------------------------------------------------------------------

tst_PortTestLive::PortTestResult tst_PortTestLive::runPortTest(bool obfuscation)
{
    // Build URL — hit ct_noframe.php directly (skips the frameset redirect).
    QUrl url(QStringLiteral("https://porttest.emule-project.net/ct_noframe.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tcpport"),
                       QString::number(m_listenSocket->connectedPort()));
    query.addQueryItem(QStringLiteral("udpport"),
                       QString::number(m_clientUDP->connectedPort()));

    if (obfuscation) {
        query.addQueryItem(QStringLiteral("obf"), QStringLiteral("1"));
        auto hash = thePrefs.userHash();
        query.addQueryItem(QStringLiteral("clienthash"),
                           md4str(hash.data()));
    } else {
        query.addQueryItem(QStringLiteral("obf"), QStringLiteral("0"));
    }
    url.setQuery(query);

    qDebug() << "Fetching port test:" << url.toString();

    // Configure obfuscation preference for this run.
    thePrefs.setCryptLayerRequested(obfuscation);

    // HTTP GET with redirect following.
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, eMule::kUserAgent);

    QNetworkReply* reply = m_nam.get(req);

    // Wait for response — allow up to 30 seconds for the port tester to
    // probe our ports and return the result page.
    QSignalSpy finished(reply, &QNetworkReply::finished);
    PortTestResult result;
    if (!finished.wait(30'000)) {
        qWarning() << "Port test HTTP request timed out";
        reply->deleteLater();
        return result;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Port test HTTP error:" << reply->errorString();
        reply->deleteLater();
        return result;
    }

    result.html = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    // Parse results from the HTML.
    //
    // The page emits inline results like:
    //   TCP ok:   <img src="result_ok.gif" ...><b>TCP test successful...</b>
    //   TCP fail: <img src="result_err.gif" ...><b>TCP test failed!</b>
    //   UDP ok:   <img src="result_ok.gif" ...><b>UDP test successful...</b>
    //   UDP fail: <img src="result_err.gif" ...><b>UDP test failed!</b>
    //   UDP skip: "UDP test will not be performed." (when TCP fails)
    //
    // The "Results in detail" section repeats with:
    //   "TCP connection test successful." / "TCP connection test failed."
    //   "UDP connection test successful." / "UDP connection test failed."
    const QString html = result.html.toLower();

    // TCP result — check for explicit success or failure strings.
    if (html.contains(QStringLiteral("tcp test successful"))
        || html.contains(QStringLiteral("tcp connection test successful"))) {
        result.tcpOk = true;
    } else if (html.contains(QStringLiteral("tcp test failed"))
               || html.contains(QStringLiteral("tcp connection test failed"))) {
        result.tcpOk = false;
    }

    // UDP result — evaluated independently from TCP.
    if (html.contains(QStringLiteral("udp test successful"))
        || html.contains(QStringLiteral("udp connection test successful"))) {
        result.udpOk = true;
    } else if (html.contains(QStringLiteral("udp test will not be performed"))
               || html.contains(QStringLiteral("udp test failed"))
               || html.contains(QStringLiteral("udp connection test failed"))) {
        result.udpOk = false;
    }

    qDebug() << "Port test result: TCP" << (result.tcpOk ? "OPEN" : "CLOSED")
             << "UDP" << (result.udpOk ? "OPEN" : "CLOSED")
             << "obfuscation:" << obfuscation;

    return result;
}

// ---------------------------------------------------------------------------
// emule-qt.org REST API port test
// ---------------------------------------------------------------------------

tst_PortTestLive::PortTestResult tst_PortTestLive::runPortTestQt()
{
    // Use curl -4 to force IPv4 — QNetworkAccessManager has no IPv4-only mode,
    // and dual-stack hosts may prefer IPv6 which the port test server can't probe.
    const QString url = QStringLiteral(
        "https://emule-qt.org/wp-json/emqt/v1/porttest?tcpport=%1&udpport=%2")
        .arg(m_listenSocket->connectedPort())
        .arg(m_clientUDP->connectedPort());

    qDebug() << "Fetching emule-qt.org port test:" << url;

    QProcess curl;
    curl.start(QStringLiteral("curl"), {
        QStringLiteral("-4"),
        QStringLiteral("-s"),
        QStringLiteral("-L"),
        QStringLiteral("--max-time"), QStringLiteral("30"),
        QStringLiteral("-A"), QStringLiteral("eMuleQt PortTest/1.0"),
        url
    });

    PortTestResult result;
    // Use QSignalSpy instead of waitForFinished() — the blocking wait would
    // starve the Qt event loop and prevent ListenSocket from accepting the
    // incoming TCP probe from the remote port test server.
    QSignalSpy curlDone(&curl, &QProcess::finished);
    if (!curlDone.wait(35'000)) {
        qWarning() << "emule-qt.org port test timed out";
        curl.kill();
        return result;
    }
    if (curl.exitCode() != 0) {
        qWarning() << "emule-qt.org port test curl error:" << curl.readAllStandardError();
        return result;
    }

    const QByteArray body = curl.readAllStandardOutput();
    result.html = QString::fromUtf8(body);

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qWarning() << "emule-qt.org port test: invalid JSON response";
        return result;
    }

    const QJsonObject root = doc.object();
    if (root[QStringLiteral("error")].toBool()) {
        qWarning() << "emule-qt.org port test error:" << root[QStringLiteral("errorMsg")].toString();
        return result;
    }

    const QJsonObject data = root[QStringLiteral("data")].toObject();
    const QJsonObject tcp = data[QStringLiteral("tcp")].toObject();
    const QJsonObject udp = data[QStringLiteral("udp")].toObject();

    result.tcpOk = tcp[QStringLiteral("open")].toBool();
    result.udpOk = udp[QStringLiteral("open")].toBool();
    result.ip = data[QStringLiteral("ip")].toString();

    qDebug() << "emule-qt.org port test result: TCP" << (result.tcpOk ? "OPEN" : "CLOSED")
             << "UDP" << (result.udpOk ? "OPEN" : "CLOSED")
             << "IP:" << result.ip;

    return result;
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

void tst_PortTestLive::testPortsNoObfuscation()
{
    auto result = runPortTest(false);

    if (m_portsOpen) {
        QVERIFY2(result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 should be open (no obfuscation)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        QVERIFY2(result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 should be open (no obfuscation)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    } else {
        QVERIFY2(!result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 expected closed (no forwarding)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        // UDP cannot pass if TCP failed (server skips UDP test).
        QVERIFY2(!result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 expected closed (no forwarding)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    }
}

void tst_PortTestLive::testPortsWithObfuscation()
{
    auto result = runPortTest(true);

    if (m_portsOpen) {
        QVERIFY2(result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 should be open (obfuscation)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        QVERIFY2(result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 should be open (obfuscation)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    } else {
        QVERIFY2(!result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 expected closed (no forwarding)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        QVERIFY2(!result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 expected closed (no forwarding)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    }
}

void tst_PortTestLive::testPortsQt()
{
    if (!m_emuleQtTest)
        QSKIP("EMULE_PORTTEST_QT not set — skipping emule-qt.org port test");

    auto result = runPortTestQt();

    if (m_portsOpen) {
        QVERIFY2(result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 should be open (emule-qt.org)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        QVERIFY2(result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 should be open (emule-qt.org)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    } else {
        QVERIFY2(!result.tcpOk,
                 qPrintable(QStringLiteral("TCP port %1 expected closed (emule-qt.org)\n%2")
                     .arg(m_listenSocket->connectedPort()).arg(result.html)));
        QVERIFY2(!result.udpOk,
                 qPrintable(QStringLiteral("UDP port %1 expected closed (emule-qt.org)\n%2")
                     .arg(m_clientUDP->connectedPort()).arg(result.html)));
    }
}

// ---------------------------------------------------------------------------

QTEST_MAIN(tst_PortTestLive)
#include "tst_PortTestLive.moc"
