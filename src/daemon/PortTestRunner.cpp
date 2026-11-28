/// @file PortTestRunner.cpp
/// @brief TestPorts — implementation.

#include "PortTestRunner.h"

#include "BtBridge.h"
#include "Ed3kBridge.h"
#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "net/BindAddress.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpDefaults.h"
#include "net/PortTestApi.h"
#include "net/PortTestGate.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/Kademlia.h"
#include "server/ServerConnect.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/ArgStr.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <memory>
#include <utility>
#include <vector>

namespace eMule::PortTestRunner {

namespace {

/// The server probes each target for up to ~8 s (TCP 5 s + UDP 3 s), two targets at most.
constexpr int kTransferTimeoutMs = 45'000;
/// ed3k: how long the node is asked after the reply whether the PROBE arrived.
constexpr int kSeenWaitMs = 3'000;
constexpr int kSeenPollMs = 250;
/// ed3k: website requests at most, the 2nd only when peers prove the port open (#216).
constexpr std::size_t kEd3kAttempts = 2;
constexpr int kRetryDelayMs = 2'000;
/// How long a test waits for the public IPv4 (server/Kad) before going without it.
constexpr int kIpv4WaitMs = 15'000;
constexpr int kIpv4PollMs = 500;
/// eD2K: porttest/source answers at once; an older server's 404 too.
constexpr int kSourceTimeoutMs = 8'000;
/// eD2K: the gate closes with the reply; this only ends it should the reply never come.
constexpr int kGateMs = kTransferTimeoutMs + 5'000;

QNetworkAccessManager* nam()
{
    static QPointer<GuardedNetworkAccessManager> s_nam;
    if (!s_nam)
        s_nam = new GuardedNetworkAccessManager(QCoreApplication::instance());
    return s_nam;
}

/// Kad's UDP firewall check passed: eD2K clients reach the eD2K UDP port from outside.
bool kadUdpOpen()
{
    const auto* kad = kad::Kademlia::instance();
    return kad && kad->isRunning() && kad::UDPFirewallTester::isOpenAndVerified();
}

/// The ed3k node's public endpoint per family and what proves its UDP port open: its own NAT
/// check (other ed3k nodes), or Kad when ed3k shares the eD2K UDP port (IPv4 only).
std::pair<PortTestApi::PeerView, PortTestApi::PeerView> ed3kViews()
{
    const QCborMap st = Ed3kBridge::statusToCbor();
    const bool sharesKadPort = Ed3kBridge::udpPort() != 0 && Ed3kBridge::udpPort() == thePrefs.udpPort();
    const auto view = [&st](const QString& endpoint, const QString& cls) {
        PortTestApi::PeerView v;
        v.address = st.value(endpoint).toString();
        if (st.value(cls).toInteger() == 1)
            v.udpProof = PortTestApi::kProofEd3k;
        return v;
    };
    auto v4 = view(QStringLiteral("endpoint4"), QStringLiteral("class4"));
    auto v6 = view(QStringLiteral("endpoint6"), QStringLiteral("class6"));
    if (v4.udpProof.isEmpty() && sharesKadPort && kadUdpOpen()) {
        v4.udpProof = PortTestApi::kProofKad;
        if (v4.address.isEmpty() && theApp.publicIP() != 0)
            v4.address = ipstr(theApp.publicIP());
    }
    return {v4, v6};
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
        if (theApp.serverConnect && theApp.serverConnect->isConnected() && !theApp.serverConnect->isLowID())
            v4.tcpProof = PortTestApi::kProofServer;
        if (theApp.advertisedUdpPort() != 0 && kadUdpOpen())
            v4.udpProof = PortTestApi::kProofKad;
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
    // bound interface of one family: the GUI says why the other one went untested (#246)
    if (req.families != 0 && req.families != PortTestApi::kBothFamilies) {
        const BindAddress::Resolution r = BindAddress::current();
        m.insert(QStringLiteral("families"), qint64(req.families));
        m.insert(QStringLiteral("bindInterface"), r.friendlyName.isEmpty() ? r.selector : r.friendlyName);
    }
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

/// ed3k: the attempts of one test so far — every nonce asked, packets sent per family.
struct Ed3kRun {
    std::vector<quint64> nonces;
    int packets4 = 0;
    int packets6 = 0;
    /// the first attempt's targets: a retry the server did not send (rate limit) keeps them
    QCborArray firstTargets;

    /// @p targets, a target the server sent nothing to replaced by the first attempt's one
    [[nodiscard]] QCborArray merged(const QCborArray& targets) const
    {
        QCborArray out;
        for (const QCborValue& t : targets) {
            const QCborMap m = t.toMap();
            QCborValue use = t;
            if (m.value(QStringLiteral("udp")).toMap().value(QStringLiteral("reason")).toString() != PortTestApi::kReasonSent)
                for (const QCborValue& f : firstTargets)
                    if (f.toMap().value(QStringLiteral("family")) == m.value(QStringLiteral("family"))
                        && f.toMap().value(QStringLiteral("udp")).toMap().value(QStringLiteral("reason")).toString()
                               == PortTestApi::kReasonSent)
                        use = f;
            out.append(use);
        }
        return out;
    }

    [[nodiscard]] int seen() const
    {
        int bits = 0;
        for (const quint64 n : nonces)
            bits |= Ed3kBridge::portTestSeen(n);
        return bits;
    }
};

/// A family the server sent to and the node did not see, while peers prove the tested
/// address open: the packet was lost, worth one more try.
bool retryWorthwhile(const QCborArray& targets, int missing)
{
    const auto [v4, v6] = ed3kViews();
    for (const QCborValue& t : PortTestApi::applyPeerView(targets, v4, v6)) {
        const QCborMap m = t.toMap();
        const int bit = m.value(QStringLiteral("family")).toInteger() == 6 ? PortTestApi::kSeenIPv6
                                                                            : PortTestApi::kSeenIPv4;
        if ((missing & bit) && m.value(QStringLiteral("peerSame")).toBool() && m.contains(QStringLiteral("peerUdp")))
            return true;
    }
    return false;
}

void send(PortTestApi::Request req, std::shared_ptr<Ed3kRun> run, const Done& done);

/// ed3k: polls the node until every family the server sent to has seen a PROBE, or time is
/// up. A packet that did not arrive while peers prove the port open is tried once more (#216).
void awaitProbe(const PortTestApi::Request& req, std::shared_ptr<Ed3kRun> run, const QCborArray& targets,
                const Done& done)
{
    if (run->firstTargets.isEmpty())
        run->firstTargets = targets;
    const QCborArray all = run->merged(targets);
    const int sent = PortTestApi::sentFamilies(all);
    run->packets4 += PortTestApi::sentCopies(targets, PortTestApi::kSeenIPv4);
    run->packets6 += PortTestApi::sentCopies(targets, PortTestApi::kSeenIPv6);
    const auto finish = [req, run, all, done] {
        done(true, resultMap(req, PortTestApi::applySeen(all, run->seen(), run->packets4, run->packets6)), 0, {});
    };
    if (sent == 0) {
        finish();
        return;
    }
    auto* timer = new QTimer(QCoreApplication::instance());
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    const auto check = [timer, clock, req, run, all, sent, done, finish] {
        const int seen = run->seen();
        if ((seen & sent) != sent && clock->elapsed() < kSeenWaitMs)
            return;
        timer->stop();
        timer->deleteLater();
        const int missing = sent & ~seen;
        if (missing == 0 || run->nonces.size() >= kEd3kAttempts || !retryWorthwhile(all, missing)) {
            finish();
            return;
        }
        quint64 nonce = 0;
        while (nonce == 0)
            nonce = QRandomGenerator::system()->generate64();
        PortTestApi::Request again = req;
        again.probe = Ed3kBridge::portTestProbe(nonce);
        if (again.probe.isEmpty()) {
            finish();
            return;
        }
        run->nonces.push_back(nonce);
        logInfo(QStringLiteral("Port test: ed3k test packet did not arrive, peers reach the port: trying again"));
        // a failed second request still has the first result to show
        QTimer::singleShot(kRetryDelayMs, QCoreApplication::instance(), [again, run, done, finish] {
            send(again, run, [finish, done](bool ok, const QCborMap& result, int failure, const QString& detail) {
                if (ok)
                    done(ok, result, failure, detail);
                else
                    finish();
            });
        });
    };
    QObject::connect(timer, &QTimer::timeout, timer, check);
    timer->start(kSeenPollMs);
}

/// Sends the test to the website and hands its answer (ed3k: after the node checked) to @p done.
void send(PortTestApi::Request req, std::shared_ptr<Ed3kRun> run, const Done& done)
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
    // HTTP is not pinned: only the bound interface's families go along (#246)
    if (!(req.families & PortTestApi::kSeenIPv4))
        req.ip4.clear();
    if (!(req.families & PortTestApi::kSeenIPv6))
        req.ip6.clear();

    QNetworkRequest httpReq = Http::makeRequest(PortTestApi::url(req));
    httpReq.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = nam()->get(httpReq);
    logInfo(QStringLiteral("Port test: %1 port %2 via %3")
                .arg(argStr(networkName(req.network)),
                     argStr(req.network == PortTestApi::Network::Ed3k ? req.udpPort : req.tcpPort),
                     QLatin1String(kWebsiteUrl)));

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, req, run, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            done(false, {}, int(PortTestApi::Failure::NoAnswer), reply->errorString());
            return;
        }
        PortTestApi::Reply parsed = PortTestApi::parseReply(body, status, req.network);
        if (!parsed.ok) {
            done(false, {}, int(parsed.failure), parsed.error);
            return;
        }
        // a server older than plugin 1.8.0 tested the observed address of any family
        parsed.targets = PortTestApi::onlyFamilies(parsed.targets, req.families);
        if (parsed.targets.isEmpty()) {
            done(false, {}, int(PortTestApi::Failure::NoBoundAddress),
                 QStringLiteral("No public address of the bound interface known to test"));
            return;
        }
        if (req.network == PortTestApi::Network::Ed3k) {
            awaitProbe(req, run, parsed.targets, done);
            return;
        }
        QCborMap result = resultMap(req, parsed.targets);
        if (req.network == PortTestApi::Network::Ed2k)
            result.insert(QStringLiteral("obfuscated"), parsed.obfuscated);
        done(true, result, 0, {});
    });
}

/// eD2K: the IP filter hits of the gate, as the result's `filterHits`.
QCborArray hitsToCbor(const std::vector<PortTestGate::Hit>& hits)
{
    QCborArray out;
    for (const PortTestGate::Hit& h : hits) {
        out.append(QCborMap{
            {QStringLiteral("address"), ipstr(h.address)},
            {QStringLiteral("description"), h.description},
            {QStringLiteral("tcp"), h.tcp},
        });
        logWarning(QStringLiteral("Port test: the IP filter rejected %1 %2 during the test (%3)")
                       .arg(h.tcp ? QStringLiteral("TCP") : QStringLiteral("UDP"), ipstr(h.address), h.description));
    }
    return out;
}

/// eD2K: the test server's addresses pass the IP filter while the test runs, and whatever
/// else the filter rejects meanwhile goes into the result (#266).
void sendEd2k(PortTestApi::Request req, std::shared_ptr<Ed3kRun> run, Done done)
{
    QNetworkRequest httpReq = Http::makeRequest(PortTestApi::sourceUrl());
    httpReq.setTransferTimeout(kSourceTimeoutMs);
    QNetworkReply* reply = nam()->get(httpReq);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, req, run, done] {
        reply->deleteLater();
        std::vector<Address> exempt;
        QCborArray blocked;   // test server addresses our filter would have rejected
        for (const QString& ip : PortTestApi::parseSources(reply->readAll())) {
            const Address a = Address::fromString(ip);
            if (a.isNull())
                continue;
            exempt.push_back(a);
            if (theApp.ipFilter && theApp.ipFilter->isFiltered(a, thePrefs.ipFilterLevel())) {
                const QString why = theApp.ipFilter->lastHitDescription();
                blocked.append(QCborMap{{QStringLiteral("address"), ip}, {QStringLiteral("description"), why}});
                logInfo(QStringLiteral("Port test: the IP filter blocks the test server %1 (%2): let through for the test")
                            .arg(ip, why));
            }
        }
        PortTestGate::open(std::move(exempt), kGateMs);
        send(req, run, [done, blocked](bool ok, const QCborMap& result, int failure, const QString& detail) {
            const QCborArray hits = hitsToCbor(PortTestGate::close());
            if (!ok) {
                done(ok, result, failure, detail);
                return;
            }
            QCborMap out = result;
            if (!hits.isEmpty())
                out.insert(QStringLiteral("filterHits"), hits);
            if (!blocked.isEmpty())
                out.insert(QStringLiteral("serverFiltered"), blocked);
            done(ok, out, failure, detail);
        });
    });
}

/// Sends the test; eD2K through the IP filter gate.
void start(PortTestApi::Request req, std::shared_ptr<Ed3kRun> run, Done done)
{
    if (req.network == PortTestApi::Network::Ed2k)
        sendEd2k(std::move(req), std::move(run), std::move(done));
    else
        send(std::move(req), std::move(run), done);
}

} // namespace

void run(int network, Done done)
{
    const auto fail = [&done](PortTestApi::Failure f, const QString& detail) { done(false, {}, int(f), detail); };
    PortTestApi::Request req;
    req.network = static_cast<PortTestApi::Network>(network);
    auto ed3kRun = std::make_shared<Ed3kRun>();
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
        ed3kRun->nonces.push_back(nonce);
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

    req.families = PortTestApi::boundFamilies(BindAddress::current());
    if (req.families == 0) {
        fail(PortTestApi::Failure::NoBoundAddress, QStringLiteral("The bound interface has no usable address"));
        return;
    }
    if (theApp.publicIP() != 0 || !(req.families & PortTestApi::kSeenIPv4)
        || (req.network == PortTestApi::Network::Ed3k && !ed3kViews().first.address.isEmpty())) {
        start(std::move(req), std::move(ed3kRun), std::move(done));
        return;
    }
    // right after start the public IPv4 is not known yet: a request leaving over IPv6
    // would then test no IPv4 at all
    auto* timer = new QTimer(QCoreApplication::instance());
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, timer, [timer, clock, req, ed3kRun, done] {
        if (theApp.publicIP() == 0 && clock->elapsed() < kIpv4WaitMs)
            return;
        timer->stop();
        timer->deleteLater();
        start(req, ed3kRun, done);
    });
    timer->start(kIpv4PollMs);
}

} // namespace eMule::PortTestRunner
