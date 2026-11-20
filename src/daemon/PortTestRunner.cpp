/// @file PortTestRunner.cpp
/// @brief TestPorts — implementation.

#include "PortTestRunner.h"

#include "BtBridge.h"
#include "Ed3kBridge.h"
#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpDefaults.h"
#include "net/PortTestApi.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <memory>
#include <utility>

namespace eMule::PortTestRunner {

namespace {

/// The server probes each target for up to ~8 s (TCP 5 s + UDP 3 s), two targets at most.
constexpr int kTransferTimeoutMs = 45'000;
/// ed3k: how long the node is asked after the reply whether the PROBE arrived.
constexpr int kSeenWaitMs = 3'000;
constexpr int kSeenPollMs = 250;
/// How long a test waits for the public IPv4 (server/Kad) before going without it.
constexpr int kIpv4WaitMs = 15'000;
constexpr int kIpv4PollMs = 500;

QNetworkAccessManager* nam()
{
    static QPointer<GuardedNetworkAccessManager> s_nam;
    if (!s_nam)
        s_nam = new GuardedNetworkAccessManager(QCoreApplication::instance());
    return s_nam;
}

/// The ed3k node's public endpoint per family and whether its NAT check found it open.
std::pair<PortTestApi::PeerView, PortTestApi::PeerView> ed3kViews()
{
    const QCborMap st = Ed3kBridge::statusToCbor();
    const auto view = [&st](const QString& endpoint, const QString& cls) {
        return PortTestApi::PeerView{st.value(endpoint).toString(), st.value(cls).toInteger() == 1};
    };
    return {view(QStringLiteral("endpoint4"), QStringLiteral("class4")),
            view(QStringLiteral("endpoint6"), QStringLiteral("class6"))};
}

/// What peers see of this node per family; BitTorrent: nothing known.
std::pair<PortTestApi::PeerView, PortTestApi::PeerView> peerViews(PortTestApi::Network network)
{
    if (network == PortTestApi::Network::Ed3k)
        return ed3kViews();
    if (network != PortTestApi::Network::Ed2k)
        return {};
    PortTestApi::PeerView v4;
    if (theApp.publicIP() != 0) {
        v4.address = ipstr(theApp.publicIP());
        // High ID: a server reached our TCP port at that address
        v4.open = theApp.serverConnect && theApp.serverConnect->isConnected() && !theApp.serverConnect->isLowID();
    }
    PortTestApi::PeerView v6;
    if (!theApp.publicIPv6().isNull())
        v6.address = theApp.publicIPv6().toString();
    return {v4, v6};
}

QCborMap resultMap(const PortTestApi::Request& req, const QCborArray& targets)
{
    const auto [v4, v6] = peerViews(req.network);
    QCborMap m{
        {QStringLiteral("network"), qint64(req.network)},
        {QStringLiteral("udpPort"), qint64(req.udpPort)},
        {QStringLiteral("targets"), PortTestApi::applyPeerView(targets, v4, v6)},
    };
    if (req.network != PortTestApi::Network::Ed3k)
        m.insert(QStringLiteral("tcpPort"), qint64(req.tcpPort));
    return m;
}

QString networkName(PortTestApi::Network network)
{
    switch (network) {
    case PortTestApi::Network::BitTorrent: return QStringLiteral("BitTorrent");
    case PortTestApi::Network::Ed3k:       return QStringLiteral("ed3k");
    case PortTestApi::Network::Ed2k:       return QStringLiteral("eD2K");
    }
    return {};
}

/// ed3k: polls the node until every family the server sent to has seen the PROBE, or time is up.
void awaitProbe(const PortTestApi::Request& req, quint64 nonce, const QCborArray& targets, const Done& done)
{
    const int sent = PortTestApi::sentFamilies(targets);
    if (sent == 0) {
        done(true, resultMap(req, targets), 0, {});
        return;
    }
    auto* timer = new QTimer(QCoreApplication::instance());
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    const auto check = [timer, clock, req, nonce, targets, sent, done] {
        const int seen = Ed3kBridge::portTestSeen(nonce);
        if ((seen & sent) != sent && clock->elapsed() < kSeenWaitMs)
            return;
        timer->stop();
        timer->deleteLater();
        done(true, resultMap(req, PortTestApi::applySeen(targets, seen)), 0, {});
    };
    QObject::connect(timer, &QTimer::timeout, timer, check);
    timer->start(kSeenPollMs);
}

/// Sends the test to the website and hands its answer (ed3k: after the node checked) to @p done.
void send(PortTestApi::Request req, quint64 nonce, const Done& done)
{
    // the server sees one family; our address of the other one goes along
    if (theApp.publicIP() != 0)
        req.ip4 = ipstr(theApp.publicIP());
    req.ip6 = theApp.publicIPv6().toString();
    // ed3k: the node's own endpoints when eD2K learned none (#216: IPv6 went untested)
    if (req.network == PortTestApi::Network::Ed3k) {
        const auto [v4, v6] = ed3kViews();
        if (req.ip4.isEmpty())
            req.ip4 = PortTestApi::hostOf(v4.address);
        if (req.ip6.isEmpty())
            req.ip6 = PortTestApi::hostOf(v6.address);
    }

    QNetworkRequest httpReq = Http::makeRequest(PortTestApi::url(req));
    httpReq.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = nam()->get(httpReq);
    logInfo(QStringLiteral("Port test: %1 port %2 via %3")
                .arg(networkName(req.network))
                .arg(req.network == PortTestApi::Network::Ed3k ? req.udpPort : req.tcpPort)
                .arg(QLatin1String(kWebsiteUrl)));

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, req, nonce, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            done(false, {}, int(PortTestApi::Failure::NoAnswer), reply->errorString());
            return;
        }
        const PortTestApi::Reply parsed = PortTestApi::parseReply(body, status, req.network);
        if (!parsed.ok) {
            done(false, {}, int(parsed.failure), parsed.error);
            return;
        }
        if (req.network == PortTestApi::Network::Ed3k) {
            awaitProbe(req, nonce, parsed.targets, done);
            return;
        }
        QCborMap result = resultMap(req, parsed.targets);
        if (req.network == PortTestApi::Network::Ed2k)
            result.insert(QStringLiteral("obfuscated"), parsed.obfuscated);
        done(true, result, 0, {});
    });
}

} // namespace

void run(int network, Done done)
{
    const auto fail = [&done](PortTestApi::Failure f, const QString& detail) { done(false, {}, int(f), detail); };
    PortTestApi::Request req;
    req.network = static_cast<PortTestApi::Network>(network);
    quint64 nonce = 0;

    switch (req.network) {
    case PortTestApi::Network::BitTorrent:
        req.tcpPort = req.udpPort = BtBridge::listenPort();
        if (req.tcpPort == 0) {
            fail(PortTestApi::Failure::BtNotRunning, QStringLiteral("BitTorrent is not running"));
            return;
        }
        break;
    case PortTestApi::Network::Ed3k:
        req.udpPort = Ed3kBridge::udpPort();
        if (req.udpPort == 0) {
            fail(PortTestApi::Failure::Ed3kNotRunning, QStringLiteral("The ed3k node is not running"));
            return;
        }
        while (nonce == 0)
            nonce = QRandomGenerator::system()->generate64();
        req.probe = Ed3kBridge::portTestProbe(nonce);
        if (req.probe.isEmpty()) {
            fail(PortTestApi::Failure::NoProbe, QStringLiteral("The ed3k node made no test packet"));
            return;
        }
        break;
    case PortTestApi::Network::Ed2k: {
        req.tcpPort = theApp.advertisedTcpPort();
        req.udpPort = theApp.advertisedUdpPort();   // 0 = UDP off: TCP only
        if (req.tcpPort == 0 || !theApp.listenSocket) {
            fail(PortTestApi::Failure::Ed2kNotListening, QStringLiteral("eD2K is not listening"));
            return;
        }
        // obfuscated dial (plugin >= 1.6.0) unless we refuse obfuscation altogether
        if (thePrefs.cryptLayerSupported()) {
            const auto hash = thePrefs.userHash();
            req.userHash = QByteArray(reinterpret_cast<const char*>(hash.data()), int(hash.size()));
        }
        break;
    }
    default:
        fail(PortTestApi::Failure::UnknownNetwork, QStringLiteral("Unknown network"));
        return;
    }

    if (theApp.publicIP() != 0
        || (req.network == PortTestApi::Network::Ed3k && !ed3kViews().first.address.isEmpty())) {
        send(std::move(req), nonce, std::move(done));
        return;
    }
    // right after start the public IPv4 is not known yet: a request leaving over IPv6
    // would then test no IPv4 at all
    auto* timer = new QTimer(QCoreApplication::instance());
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, timer, [timer, clock, req, nonce, done] {
        if (theApp.publicIP() == 0 && clock->elapsed() < kIpv4WaitMs)
            return;
        timer->stop();
        timer->deleteLater();
        send(req, nonce, done);
    });
    timer->start(kIpv4PollMs);
}

} // namespace eMule::PortTestRunner
