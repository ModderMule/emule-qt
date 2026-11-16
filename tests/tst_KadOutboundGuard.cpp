/// @file tst_KadOutboundGuard.cpp
/// @brief Tests for KadOutboundGuard.h — our requests stay inside MFC's per-IP flood buckets.

#include "TestHelpers.h"

#include "kademlia/KadOutboundGuard.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <random>

using namespace eMule;
using namespace eMule::kad;

namespace {
constexpr uint32 kIp = 0x0A000001;
constexpr uint32 kOtherIp = 0x0A000002;
constexpr uint64 kT0 = 1'000'000;
using P = OutboundGuard::Priority;

/// Burst from a fresh bucket: sends until the guard says no
uint32 burst(OutboundGuard& g, uint8 opcode, P prio, uint64 now)
{
    uint32 n = 0;
    while (g.allows(kIp, opcode, prio, 1, now) && n < 100) {
        g.charge(kIp, opcode, now);
        ++n;
    }
    return n;
}

/// MFC's receiver (PacketTracking.cpp:150-200) for one IP and opcode, by arrival time
struct MfcBucket {
    int64 cost;
    int64 tokens = 60'000;
    uint64 latest = 0;
    bool started = false;
    bool dropped = false;
    void receive(uint64 at)
    {
        if (!started) {
            started = true;
            tokens = 60'000 - cost;
        } else {
            tokens = std::min<int64>(60'000, tokens + static_cast<int64>(at - latest)) - cost;
        }
        latest = at;
        dropped = dropped || tokens < 0;
    }
};
} // namespace

class tst_KadOutboundGuard : public QObject {
    Q_OBJECT

private slots:
    void table_matchesMfc();
    void burst_foregroundTakesNineOfTen();
    void burst_backgroundLeavesThreeForSearches();
    void refill_oneMoreEverySixSeconds();
    void publishKey_countsAllPacketsOfOneNode();
    void bucketsArePerIpAndOpcode();
    void firewalled2SharesFirewalledBucket();
    void unlimitedOpcodesAlwaysAllowed();
    void charge_countsLastMinute();
    void cleanup_dropsIdleIps();
    void sustained_neverDroppedByMfcReceiver();
};

void tst_KadOutboundGuard::table_matchesMfc()
{
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_REQ), 10u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_PUBLISH_SOURCE_REQ), 3u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_PUBLISH_KEY_REQ), 4u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_PUBLISH_NOTES_REQ), 2u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_SEARCH_KEY_REQ), 3u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_HELLO_REQ), 3u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA_CALLBACK_REQ), 1u);
    QCOMPARE(mfcRequestsPerMinute(KADEMLIA2_RES), 0u);
}

void tst_KadOutboundGuard::burst_foregroundTakesNineOfTen()
{
    // 10/min, minus the 1 s jitter margin
    OutboundGuard g;
    QCOMPARE(burst(g, KADEMLIA2_REQ, P::Foreground, kT0), 9u);
}

void tst_KadOutboundGuard::burst_backgroundLeavesThreeForSearches()
{
    OutboundGuard g;
    QCOMPARE(burst(g, KADEMLIA2_REQ, P::Background, kT0), 6u);
    // the user's search still gets through to that node
    QCOMPARE(burst(g, KADEMLIA2_REQ, P::Foreground, kT0), 3u);
    // publishes have no foreground user, so no reserve
    QCOMPARE(burst(g, KADEMLIA2_PUBLISH_SOURCE_REQ, P::Background, kT0), 2u);
}

void tst_KadOutboundGuard::refill_oneMoreEverySixSeconds()
{
    OutboundGuard g;
    burst(g, KADEMLIA2_REQ, P::Foreground, kT0);   // 9 sent, 6 s left, 7 s needed
    QVERIFY(!g.allows(kIp, KADEMLIA2_REQ, P::Foreground, 1, kT0 + 999));
    QVERIFY(g.allows(kIp, KADEMLIA2_REQ, P::Foreground, 1, kT0 + 1'000));
    g.charge(kIp, KADEMLIA2_REQ, kT0 + 1'000);
    QVERIFY(!g.allows(kIp, KADEMLIA2_REQ, P::Foreground, 1, kT0 + 6'999));
    QVERIFY(g.allows(kIp, KADEMLIA2_REQ, P::Foreground, 1, kT0 + 7'000));
}

void tst_KadOutboundGuard::publishKey_countsAllPacketsOfOneNode()
{
    // A 150-file keyword publish is 3 packets; MFC allows 4/min
    OutboundGuard g;
    QVERIFY(g.allows(kIp, KADEMLIA2_PUBLISH_KEY_REQ, P::Background, 3, kT0));
    for (int i = 0; i < 3; ++i)
        g.charge(kIp, KADEMLIA2_PUBLISH_KEY_REQ, kT0);
    QVERIFY(!g.allows(kIp, KADEMLIA2_PUBLISH_KEY_REQ, P::Background, 1, kT0));
    QVERIFY(!g.allows(kIp, KADEMLIA2_PUBLISH_KEY_REQ, P::Background, 3, kT0 + 30'000));
    QVERIFY(g.allows(kIp, KADEMLIA2_PUBLISH_KEY_REQ, P::Background, 3, kT0 + 46'000));
}

void tst_KadOutboundGuard::bucketsArePerIpAndOpcode()
{
    OutboundGuard g;
    burst(g, KADEMLIA2_REQ, P::Foreground, kT0);
    QVERIFY(!g.allows(kIp, KADEMLIA2_REQ, P::Foreground, 1, kT0));
    QVERIFY(g.allows(kOtherIp, KADEMLIA2_REQ, P::Foreground, 1, kT0));
    QVERIFY(g.allows(kIp, KADEMLIA2_PUBLISH_SOURCE_REQ, P::Background, 1, kT0));
}

void tst_KadOutboundGuard::firewalled2SharesFirewalledBucket()
{
    OutboundGuard g;
    g.charge(kIp, KADEMLIA_FIREWALLED_REQ, kT0);
    // 2/min: one left, minus the margin = none
    QVERIFY(!g.allows(kIp, KADEMLIA_FIREWALLED2_REQ, P::Background, 1, kT0));
}

void tst_KadOutboundGuard::unlimitedOpcodesAlwaysAllowed()
{
    OutboundGuard g;
    for (int i = 0; i < 50; ++i)
        QCOMPARE(g.charge(kIp, KADEMLIA2_RES, kT0), 0u);
    QVERIFY(g.allows(kIp, KADEMLIA2_RES, P::Background, 50, kT0));
    QCOMPARE(g.trackedIPs(), size_t{0});
}

void tst_KadOutboundGuard::charge_countsLastMinute()
{
    OutboundGuard g;
    QCOMPARE(g.charge(kIp, KADEMLIA2_HELLO_REQ, kT0), 1u);
    QCOMPARE(g.charge(kIp, KADEMLIA2_HELLO_REQ, kT0 + 30'000), 2u);
    QCOMPARE(g.charge(kIp, KADEMLIA2_HELLO_REQ, kT0 + 59'999), 3u);
    QCOMPARE(g.charge(kIp, KADEMLIA2_HELLO_REQ, kT0 + 60'000), 3u);   // kT0 left the window
}

void tst_KadOutboundGuard::cleanup_dropsIdleIps()
{
    OutboundGuard g;
    g.charge(kIp, KADEMLIA2_REQ, kT0);
    g.charge(kOtherIp, KADEMLIA2_REQ, kT0 + 50'000);
    g.cleanup(kT0 + 30'000);
    QCOMPARE(g.trackedIPs(), size_t{2});
    g.cleanup(kT0 + 60'000);
    QCOMPARE(g.trackedIPs(), size_t{1});
    // dropped = full bucket, as the receiver's
    QCOMPARE(burst(g, KADEMLIA2_REQ, P::Foreground, kT0 + 60'000), 9u);
}

void tst_KadOutboundGuard::sustained_neverDroppedByMfcReceiver()
{
    // Send whenever allowed for 10 min, packets arriving up to 900 ms late in random order
    // of delay; the MFC receiver must drop none, and we must still get close to 10/min.
    for (const auto [opcode, perMinute] : {std::pair<uint8, uint32>{KADEMLIA2_REQ, 10},
                                           {KADEMLIA2_PUBLISH_SOURCE_REQ, 3},
                                           {KADEMLIA2_PUBLISH_KEY_REQ, 4}}) {
        OutboundGuard g;
        MfcBucket rx{60'000 / perMinute};
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> delay(0, 900);
        uint32 sent = 0;
        uint64 lastArrival = 0;
        for (uint64 t = kT0; t < kT0 + 600'000; t += 100) {
            while (g.allows(kIp, opcode, P::Foreground, 1, t)) {
                g.charge(kIp, opcode, t);
                ++sent;
                // UDP may reorder, the receiver sees arrival order
                lastArrival = std::max<uint64>(lastArrival, t + delay(rng));
                rx.receive(lastArrival);
            }
        }
        QVERIFY2(!rx.dropped, qPrintable(QStringLiteral("opcode 0x%1").arg(opcode, 0, 16)));
        // burst of the bucket, then the refill rate
        QVERIFY(sent >= 10 * perMinute - 1);
        QVERIFY(sent <= 11 * perMinute);
    }
}

QTEST_GUILESS_MAIN(tst_KadOutboundGuard)
#include "tst_KadOutboundGuard.moc"
