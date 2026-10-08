/// @file tst_LastCommonRouteFinder.cpp
/// @brief Tests for LastCommonRouteFinder — USS adaptive upload control.

#include "TestHelpers.h"
#include "net/LastCommonRouteFinder.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_LastCommonRouteFinder : public QObject {
    Q_OBJECT

private slots:
    void constructionAndDefaults();
    void setPrefsAccepted();
    void addHostsSignal();
    void uploadLimitDefault();
    void acceptNewClientDefault();
    void endThreadBeforeStart();
    void statusWhenDisabled();
    void setPrefs_limitCountsAtOnceWhenDisabled();
    void adjustUpload_floorReopensTheQueue();
    void adjustUpload_data();
    void adjustUpload();
    void rampDividers_stages();
    void pingInterval_followsTheUpload();
    void trace_findsWhereTheRoutesPart();
    void trace_dropsHostsThatAreTooCloseOrUnreachable();
    void trace_skipsASilentHopAndGivesUpWithoutRouters();
    void trace_endsWhenTooFewHostsAreLeft();
    void trace_retriesAFailedPingWithTheOtherMethod();
    void trace_takesADivergingFirstHopOnTrust();
    void setPrefs_staysOffAfterGivingUpUntilSwitchedOff();
    void trace_liveRoute();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::constructionAndDefaults()
{
    LastCommonRouteFinder finder;
    QCOMPARE(finder.getUpload(), 0u);
    QVERIFY(finder.acceptNewClient());
    QVERIFY(!finder.isRunning());
}

// ---------------------------------------------------------------------------
// Test: setPrefs returns true
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::setPrefsAccepted()
{
    LastCommonRouteFinder finder;

    USSParams params;
    params.enabled = true;
    params.curUpload = 50000;
    params.minUpload = 10;
    params.maxUpload = 100;
    params.pingTolerance = 1.5;
    params.numberOfPingsForAverage = 5;

    QVERIFY(finder.setPrefs(params));
}

// ---------------------------------------------------------------------------
// Test: addHosts with no collection active
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::addHostsSignal()
{
    LastCommonRouteFinder finder;

    // Without the thread running and requesting hosts, addHostsToCheck should return false
    std::vector<uint32> ips = {0x7F000001, 0x08080808};
    QVERIFY(!finder.addHostsToCheck(ips));
}

// ---------------------------------------------------------------------------
// Test: upload limit starts at 0
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::uploadLimitDefault()
{
    LastCommonRouteFinder finder;
    QCOMPARE(finder.getUpload(), 0u);
}

// ---------------------------------------------------------------------------
// Test: acceptNewClient defaults to true
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::acceptNewClientDefault()
{
    LastCommonRouteFinder finder;
    QVERIFY(finder.acceptNewClient());
}

// ---------------------------------------------------------------------------
// Test: endThread before start is safe
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::endThreadBeforeStart()
{
    LastCommonRouteFinder finder;
    finder.endThread(); // Should not crash or hang
    QVERIFY(!finder.isRunning());
}

// ---------------------------------------------------------------------------
// Test: status when disabled
// ---------------------------------------------------------------------------

void tst_LastCommonRouteFinder::statusWhenDisabled()
{
    LastCommonRouteFinder finder;
    USSStatus status = finder.currentStatus();
    QCOMPARE(status.latency, 0u);
    QCOMPARE(status.lowest, 0u);
    QCOMPARE(status.currentLimit, 0u);
}

// The throttler reads getUpload(); a limit that waits for the thread's next wake-up
// (up to 3 minutes) leaves "unlimited -> 50 KB/s" uncapped that long.
void tst_LastCommonRouteFinder::setPrefs_limitCountsAtOnceWhenDisabled()
{
    LastCommonRouteFinder finder;         // thread not started: setPrefs alone must do it
    USSParams params;
    params.enabled = false;
    params.maxUpload = UINT32_MAX;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), UINT32_MAX);

    params.maxUpload = 50;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 50u * 1024);

    params.maxUpload = 80;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 80u * 1024);

    // Enabled: the controller owns the value, but never above the maximum.
    params.enabled = true;
    params.maxUpload = 20;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 20u * 1024);
    params.maxUpload = 90;
    finder.setPrefs(params);
    QCOMPARE(finder.getUpload(), 20u * 1024);
}

void tst_LastCommonRouteFinder::adjustUpload_floorReopensTheQueue()
{
    // A step far below the minimum lands on it.
    bool accept = false;
    QCOMPARE(LastCommonRouteFinder::adjustUpload(500, 0, 1000, 10, 20, 1000, 1000,
                                                 1024, 50 * 1024, accept), 1024u);
    QVERIFY(accept);                      // at the floor new clients are welcome again
}

void tst_LastCommonRouteFinder::adjustUpload_data()
{
    QTest::addColumn<uint32>("upload");
    QTest::addColumn<uint32>("curUpload");
    QTest::addColumn<int>("normalizedPing");
    QTest::addColumn<uint32>("expected");
    QTest::addColumn<int>("accept");      // -1 untouched, 0 false, 1 true

    // target 80 ms over a lowest ping of 20 ms, dividers 1000, limits 10..500 KB/s.
    // step = headroom * 10240 / 1000 / 20
    const uint32 kb = 1024;
    QTest::newRow("on target: no change") << 100 * kb << 100 * kb << 80 << 100 * kb << -1;
    QTest::newRow("headroom 80: +40") << 100 * kb << 100 * kb << 0 << 100 * kb + 40u << 1;
    QTest::newRow("headroom 40: +20") << 100 * kb << 100 * kb << 40 << 100 * kb + 20u << 1;
    QTest::newRow("faster than the baseline: bigger step")
        << 100 * kb << 100 * kb << -20 << 100 * kb + 51u << 1;
    QTest::newRow("excess 100: -51") << 100 * kb << 100 * kb << 180 << 100 * kb - 51u << 0;
    QTest::newRow("excess 20: -10") << 100 * kb << 100 * kb << 100 << 100 * kb - 10u << 0;
    QTest::newRow("not using the limit: no rise") << 100 * kb << 60 * kb << 0 << 100 * kb << 1;
    QTest::newRow("within 30 KB/s of it: rises") << 100 * kb << 71 * kb << 0 << 100 * kb + 40u << 1;
    QTest::newRow("capped at the maximum") << 500 * kb - 10u << 500 * kb << 0 << 500 * kb << 1;
    QTest::newRow("floored at the minimum") << 10 * kb + 5u << 10 * kb << 180 << 10 * kb << 1;
}

void tst_LastCommonRouteFinder::adjustUpload()
{
    QFETCH(uint32, upload);
    QFETCH(uint32, curUpload);
    QFETCH(int, normalizedPing);
    QFETCH(uint32, expected);
    QFETCH(int, accept);

    for (const bool before : {false, true}) {
        bool acceptNewClient = before;
        QCOMPARE(LastCommonRouteFinder::adjustUpload(upload, curUpload, normalizedPing, 80, 20,
                                                     1000, 1000, 10 * 1024, 500 * 1024,
                                                     acceptNewClient),
                 expected);
        QCOMPARE(acceptNewClient, accept < 0 ? before : accept == 1);
    }
}

void tst_LastCommonRouteFinder::rampDividers_stages()
{
    const auto at = [](qint64 ms) {
        uint32 up = 1000, down = 2000;
        LastCommonRouteFinder::rampDividers(ms, up, down);
        return std::pair{up, down};
    };
    QCOMPARE(at(0), (std::pair{1000u, 2000u}));
    QCOMPARE(at(25'000), (std::pair{250u, 500u}));
    QCOMPARE(at(35'000), (std::pair{500u, 1000u}));
    QCOMPARE(at(50'000), (std::pair{750u, 1500u}));
    QCOMPARE(at(60'000), (std::pair{1000u, 2000u}));

    uint32 up = 2, down = 0;              // never a zero divider
    LastCommonRouteFinder::rampDividers(25'000, up, down);
    QCOMPARE(up, 1u);
    QCOMPARE(down, 1u);
}

void tst_LastCommonRouteFinder::pingInterval_followsTheUpload()
{
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(0), 1000u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(5 * 1024), 1000u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(16 * 1024), 390u);
    QCOMPARE(LastCommonRouteFinder::pingIntervalMs(200 * 1024), 125u);
}

// ---------------------------------------------------------------------------
// Hop search — MFC LastCommonRouteFinder.cpp:327-449, with a scripted pinger
// ---------------------------------------------------------------------------

namespace {

constexpr uint32 kR1 = 0x0101A8C0, kR2 = 0x01004B0A, kR3a = 0x0A0A0A0A, kR3b = 0x0B0B0B0B;

std::vector<uint32> makeHosts(std::size_t n)
{
    std::vector<uint32> hosts;
    for (uint32 i = 0; i < n; ++i)
        hosts.push_back(0x51020000u + ((i + 1) << 24));   // distinct, asymmetric
    return hosts;
}

PingStatus expired(uint32 router, float ms = 12.0f)
{
    PingStatus s;
    s.success = true;
    s.status = kPingTTLExpired;
    s.destinationAddress = router;
    s.delay = ms;
    return s;
}

PingStatus timedOut()
{
    PingStatus s;
    s.status = kPingTimedOut;
    s.error = kPingTimedOut;
    return s;
}

const std::function<bool()> kAlways = [] { return true; };
const std::function<void(uint32)> kNoPause = [](uint32) {};

} // namespace

// The port pinged the shared router itself with an echo; MFC pings a peer behind it
// with the router's TTL and measures the "TTL exceeded".
void tst_LastCommonRouteFinder::trace_findsWhereTheRoutesPart()
{
    std::vector<uint32> hosts = makeHosts(10);
    const uint32 first = hosts[0], second = hosts[1];
    int pings = 0;
    const auto ping = [&](uint32 addr, uint8 ttl, bool) {
        ++pings;
        if (ttl == 1) return expired(kR1);
        if (ttl == 2) return expired(kR2);
        return expired(addr == first ? kR3a : kR3b);
    };

    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(hosts, ping, kAlways, kNoPause);
    QVERIFY(probe.found);
    QCOMPARE(probe.lastCommonHost, kR2);
    QCOMPARE(probe.lastCommonTTL, uint8{2});
    QCOMPARE(probe.hostToPing, second);   // the host whose answer showed the fork
    QVERIFY(!probe.useUdp);
    QCOMPARE(hosts.size(), std::size_t{10});
    QCOMPARE(pings, 10 + 10 + 2);         // the third hop stops at the first difference
}

void tst_LastCommonRouteFinder::trace_dropsHostsThatAreTooCloseOrUnreachable()
{
    std::vector<uint32> hosts = makeHosts(12);
    const uint32 neighbour = hosts[2], unreachable = hosts[5], forked = hosts[11];
    const auto ping = [&](uint32 addr, uint8 ttl, bool) {
        if (addr == neighbour) {          // answers the echo itself at the first hop
            PingStatus s;
            s.success = true;
            s.status = kPingSuccess;
            s.destinationAddress = addr;
            return s;
        }
        if (addr == unreachable) {
            PingStatus s;
            s.success = true;
            s.status = kPingDestUnreachable;
            s.destinationAddress = kR1;
            return s;
        }
        if (ttl == 1) return expired(kR1);
        return expired(addr == forked ? kR3b : kR3a);
    };

    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(hosts, ping, kAlways, kNoPause);
    QVERIFY(probe.found);
    QCOMPARE(probe.lastCommonHost, kR1);
    QCOMPARE(probe.lastCommonTTL, uint8{1});
    QCOMPARE(probe.hostToPing, forked);
    QCOMPARE(hosts.size(), std::size_t{10});
    QVERIFY(std::ranges::find(hosts, neighbour) == hosts.end());
    QVERIFY(std::ranges::find(hosts, unreachable) == hosts.end());
}

void tst_LastCommonRouteFinder::trace_skipsASilentHopAndGivesUpWithoutRouters()
{
    // The first hop answers nobody: three silent hosts end that TTL, the next one works.
    std::vector<uint32> hosts = makeHosts(10);
    const uint32 last = hosts[9];
    int firstHopPings = 0;
    const auto silentFirstHop = [&](uint32 addr, uint8 ttl, bool) {
        if (ttl == 1) {
            ++firstHopPings;
            return timedOut();
        }
        if (ttl == 2) return expired(kR2);
        return expired(addr == last ? kR3b : kR3a);
    };
    RouteProbe probe =
        LastCommonRouteFinder::traceLastCommonHost(hosts, silentFirstHop, kAlways, kNoPause);
    QVERIFY(probe.found);
    QCOMPARE(probe.lastCommonHost, kR2);
    QCOMPARE(probe.lastCommonTTL, uint8{2});
    QCOMPARE(firstHopPings, 3 * 2);       // three hosts, each tried twice
    QCOMPARE(hosts.size(), std::size_t{10});

    // No router in the first four hops: nothing to trace.
    int maxTtl = 0;
    const auto nobody = [&](uint32, uint8 ttl, bool) {
        maxTtl = std::max<int>(maxTtl, ttl);
        return timedOut();
    };
    probe = LastCommonRouteFinder::traceLastCommonHost(hosts, nobody, kAlways, kNoPause);
    QVERIFY(!probe.found);
    QCOMPARE(probe.lastCommonHost, 0u);
    QCOMPARE(maxTtl, 4);
}

void tst_LastCommonRouteFinder::trace_endsWhenTooFewHostsAreLeft()
{
    std::vector<uint32> hosts = makeHosts(LastCommonRouteFinder::kTooFewHosts);
    int pings = 0;
    const auto ping = [&](uint32, uint8, bool) {
        ++pings;
        return timedOut();
    };
    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(hosts, ping, kAlways, kNoPause);
    QVERIFY(!probe.found);
    QCOMPARE(pings, 2);                   // one host, its retry, and the try is over
}

void tst_LastCommonRouteFinder::trace_retriesAFailedPingWithTheOtherMethod()
{
    std::vector<uint32> hosts = makeHosts(10);
    const uint32 last = hosts[9];
    QList<uint32> pauses;
    int icmpPings = 0, udpPings = 0;
    // ICMP is refused by the OS; UDP gets through.
    const auto ping = [&](uint32 addr, uint8 ttl, bool useUdp) {
        ++(useUdp ? udpPings : icmpPings);
        if (!useUdp) {
            PingStatus s;
            s.error = 13;                 // not a timeout
            return s;
        }
        return expired(ttl == 1 ? kR1 : (addr == last ? kR3b : kR3a));
    };
    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(
        hosts, ping, kAlways, [&](uint32 ms) { pauses.append(ms); });
    QVERIFY(probe.found);
    QVERIFY(probe.useUdp);
    QCOMPARE(probe.lastCommonHost, kR1);
    QCOMPARE(icmpPings, 2);               // once per TTL, before the method flips
    QCOMPARE(pauses, (QList<uint32>{1000, 1000}));
    QVERIFY(udpPings >= 20);
}

void tst_LastCommonRouteFinder::trace_takesADivergingFirstHopOnTrust()
{
    // Two uplinks: the routes differ at the very first hop.
    std::vector<uint32> hosts = makeHosts(10);
    const uint32 first = hosts[0];
    const auto ping = [&](uint32 addr, uint8, bool) {
        return expired(addr == first ? kR3a : kR3b);
    };
    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(hosts, ping, kAlways, kNoPause);
    QVERIFY(probe.found);
    QCOMPARE(probe.lastCommonTTL, uint8{1});
    QCOMPARE(probe.lastCommonHost, kR3b);
    QCOMPARE(probe.hostToPing, hosts[1]);

    // Stopped from outside: no result, no further pings.
    int pings = 0;
    const auto counted = [&](uint32, uint8, bool) {
        ++pings;
        return expired(kR1);
    };
    const RouteProbe stopped = LastCommonRouteFinder::traceLastCommonHost(
        hosts, counted, [] { return false; }, kNoPause);
    QVERIFY(!stopped.found);
    QCOMPARE(pings, 0);
}

// MFC switches the option off when tracing fails for good; the every-second prefs
// push must not switch the finder back on before that has happened.
void tst_LastCommonRouteFinder::setPrefs_staysOffAfterGivingUpUntilSwitchedOff()
{
    LastCommonRouteFinder finder;
    USSParams params;
    params.enabled = true;
    finder.setPrefs(params);
    QVERIFY(finder.isEnabled());

    finder.m_gaveUp = true;
    finder.setPrefs(params);
    QVERIFY(!finder.isEnabled());

    params.enabled = false;
    finder.setPrefs(params);
    params.enabled = true;
    finder.setPrefs(params);
    QVERIFY(finder.isEnabled());
}

// Against the real network: EMULE_LIVE_USS=1 ./tst_LastCommonRouteFinder trace_liveRoute
void tst_LastCommonRouteFinder::trace_liveRoute()
{
    if (!qEnvironmentVariableIsSet("EMULE_LIVE_USS"))
        QSKIP("needs ICMP and a route; set EMULE_LIVE_USS=1");

    std::vector<uint32> hosts;
    for (const char* ip : {"1.1.1.1", "8.8.8.8", "9.9.9.9", "208.67.222.222", "1.0.0.1",
                           "8.8.4.4", "149.112.112.112", "208.67.220.220", "94.140.14.14",
                           "76.76.2.0", "185.228.168.9", "64.6.64.6"})
        hosts.push_back(htonl(QHostAddress(QString::fromLatin1(ip)).toIPv4Address()));

    Pinger pinger;
    QVERIFY(pinger.isIcmpAvailable());
    const auto ping = [&](uint32 addr, uint8 ttl, bool useUdp) {
        const PingStatus s = pinger.ping(addr, ttl, useUdp);
        qInfo() << "ttl" << ttl << QHostAddress(ntohl(addr)).toString() << "->"
                << QHostAddress(ntohl(s.destinationAddress)).toString() << "status" << s.status
                << "ok" << s.success << s.delay << "ms";
        return s;
    };
    const RouteProbe probe = LastCommonRouteFinder::traceLastCommonHost(
        hosts, ping, kAlways, [](uint32 ms) { QTest::qWait(static_cast<int>(ms)); });
    qInfo() << "found" << probe.found << "common"
            << QHostAddress(ntohl(probe.lastCommonHost)).toString() << "ttl" << probe.lastCommonTTL
            << "hostToPing" << QHostAddress(ntohl(probe.hostToPing)).toString();
    QVERIFY(probe.found);

    // The control ping: through the common hop, answered by it
    const PingStatus control = pinger.ping(probe.hostToPing, probe.lastCommonTTL, probe.useUdp);
    QVERIFY(control.success);
    QCOMPARE(control.status, kPingTTLExpired);
    QCOMPARE(control.destinationAddress, probe.lastCommonHost);
}

QTEST_MAIN(tst_LastCommonRouteFinder)
#include "tst_LastCommonRouteFinder.moc"
