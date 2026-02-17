/// @file tst_Pinger.cpp
/// @brief Tests for net/Pinger — ICMP echo ping to localhost.

#include "TestHelpers.h"
#include "net/Pinger.h"

#include <QTest>

#include <arpa/inet.h>

using namespace eMule;

class tst_Pinger : public QObject {
    Q_OBJECT

private slots:
    void icmpSocketAvailability();
    void pingLocalhost();
    void pingInvalidAddress();
    void pingStatusDefaults();
    void multipleSequentialPings();
};

// ---------------------------------------------------------------------------
// ICMP socket availability
// ---------------------------------------------------------------------------

void tst_Pinger::icmpSocketAvailability()
{
    Pinger pinger;

    // On most CI systems and dev machines, ICMP SOCK_DGRAM should be available.
    // If it isn't, the remaining ping tests will be skipped.
    if (!pinger.isIcmpAvailable())
        QSKIP("ICMP socket not available (may need privileges)");

    QVERIFY(pinger.isIcmpAvailable());
}

// ---------------------------------------------------------------------------
// Ping localhost — should succeed with very low latency
// ---------------------------------------------------------------------------

void tst_Pinger::pingLocalhost()
{
    Pinger pinger;
    if (!pinger.isIcmpAvailable())
        QSKIP("ICMP socket not available");

    uint32 localhost = htonl(INADDR_LOOPBACK); // 127.0.0.1

    PingStatus result = pinger.ping(localhost);

    QVERIFY(result.success);
    QCOMPARE(result.status, kPingSuccess);
    QCOMPARE(result.error, 0u);
    QVERIFY(result.delay < 100.0f); // Localhost should respond in < 100ms
    QVERIFY(result.delay >= 0.0f);
}

// ---------------------------------------------------------------------------
// Ping invalid/unreachable address — should fail or timeout
// ---------------------------------------------------------------------------

void tst_Pinger::pingInvalidAddress()
{
    Pinger pinger;
    if (!pinger.isIcmpAvailable())
        QSKIP("ICMP socket not available");

    // 192.0.2.1 is TEST-NET-1 (RFC 5737) — should not route anywhere
    uint32 testNet = htonl(0xC0000201); // 192.0.2.1

    // Use short TTL=1 to avoid long timeout
    PingStatus result = pinger.ping(testNet, 1);

    // Should either timeout or get TTL expired — either way, NOT a clean success
    // (On some networks this might succeed if there's a router at hop 1)
    // We just verify the function returns without crashing
    QVERIFY(result.delay >= 0.0f);
}

// ---------------------------------------------------------------------------
// PingStatus default construction
// ---------------------------------------------------------------------------

void tst_Pinger::pingStatusDefaults()
{
    PingStatus ps;
    QCOMPARE(ps.delay, 0.0f);
    QCOMPARE(ps.destinationAddress, 0u);
    QCOMPARE(ps.status, 0u);
    QCOMPARE(ps.error, 0u);
    QCOMPARE(ps.ttl, static_cast<uint8>(0));
    QVERIFY(!ps.success);
}

// ---------------------------------------------------------------------------
// Multiple sequential pings — verify sequence counter works
// ---------------------------------------------------------------------------

void tst_Pinger::multipleSequentialPings()
{
    Pinger pinger;
    if (!pinger.isIcmpAvailable())
        QSKIP("ICMP socket not available");

    uint32 localhost = htonl(INADDR_LOOPBACK);

    for (int i = 0; i < 3; ++i) {
        PingStatus result = pinger.ping(localhost);
        QVERIFY(result.success);
        QCOMPARE(result.status, kPingSuccess);
    }
}

QTEST_MAIN(tst_Pinger)
#include "tst_Pinger.moc"
