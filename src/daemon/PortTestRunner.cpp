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
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <memory>

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

QCborMap resultMap(const PortTestApi::Request& req, const QCborArray& targets)
{
    QCborMap m{
        {QStringLiteral("network"), qint64(req.network)},
        {QStringLiteral("udpPort"), qint64(req.udpPort)},
        {QStringLiteral("targets"), targets},
    };
    if (req.network == PortTestApi::Network::BitTorrent)
        m.insert(QStringLiteral("tcpPort"), qint64(req.tcpPort));
    return m;
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

    QNetworkRequest httpReq = Http::makeRequest(PortTestApi::url(req));
    httpReq.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = nam()->get(httpReq);
    logInfo(QStringLiteral("Port test: %1 port %2 via %3")
                .arg(req.network == PortTestApi::Network::BitTorrent ? QStringLiteral("BitTorrent") : QStringLiteral("ed3k"))
                .arg(req.udpPort)
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
        if (req.network == PortTestApi::Network::Ed3k)
            awaitProbe(req, nonce, parsed.targets, done);
        else
            done(true, resultMap(req, parsed.targets), 0, {});
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
    default:
        fail(PortTestApi::Failure::UnknownNetwork, QStringLiteral("Unknown network"));
        return;
    }

    if (theApp.publicIP() != 0) {
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
