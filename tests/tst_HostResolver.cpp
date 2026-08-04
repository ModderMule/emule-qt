/// @file tst_HostResolver.cpp
/// @brief Tests for net/HostResolver — literal short-circuit, family preference,
///        cancellation, timeout and caching. Nothing here needs a working nameserver:
///        literals resolve locally and "localhost" comes from the hosts file.

#include "TestHelpers.h"
#include "net/HostResolver.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <optional>

using namespace eMule;

class tst_HostResolver : public QObject {
    Q_OBJECT

private slots:
    void literal_isDeliveredAsynchronously();
    void literal_ipv6();
    void emptyHost_returnsZeroToken();
    void preference_filtersFamilies();
    void orderByPreference_ordersAndDedups();
    void cancel_suppressesCallback();
    void destruction_suppressesCallback();
    void contextDestroyed_suppressesCallback();
    void localhost_resolvesOffline();
    void timeout_reportsError();
    void cache_secondRequestStillAsync();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Spin the event loop until @p done or @p timeoutMs elapses.
bool waitFor(const bool& done, int timeoutMs = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done && clock.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done;
}

} // namespace

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

void tst_HostResolver::literal_isDeliveredAsynchronously()
{
    HostResolver resolver;
    bool called = false;
    HostResolver::Result got;

    const int token = resolver.resolve(QStringLiteral("8.8.8.8"),
                                       HostResolver::Preference::Any,
                                       [&](const HostResolver::Result& r) {
                                           called = true;
                                           got = r;
                                       });
    QVERIFY(token != 0);
    // The invariant every caller relies on: never invoked before returning.
    QVERIFY(!called);

    QVERIFY(waitFor(called));
    QVERIFY(got.ok());
    QCOMPARE(got.addresses.size(), std::size_t{1});
    QVERIFY(got.first().isIPv4());
    QCOMPARE(got.first().toString(), QStringLiteral("8.8.8.8"));
    QVERIFY(got.errorString.isEmpty());
}

void tst_HostResolver::literal_ipv6()
{
    HostResolver resolver;
    bool called = false;
    HostResolver::Result got;

    resolver.resolve(QStringLiteral("2001:db8::1"), HostResolver::Preference::Any,
                     [&](const HostResolver::Result& r) { called = true; got = r; });

    QVERIFY(waitFor(called));
    QVERIFY(got.ok());
    QVERIFY(got.first().isIPv6());
    QVERIFY(got.firstIPv4().isNull());
    QCOMPARE(got.firstIPv6().toString(), QStringLiteral("2001:db8::1"));
}

void tst_HostResolver::emptyHost_returnsZeroToken()
{
    HostResolver resolver;
    bool called = false;
    QCOMPARE(resolver.resolve(QString(), HostResolver::Preference::Any,
                              [&](const HostResolver::Result&) { called = true; }), 0);
    QCOMPARE(resolver.resolve(QStringLiteral("   "), HostResolver::Preference::Any,
                              [&](const HostResolver::Result&) { called = true; }), 0);
    QVERIFY(!resolver.hasPendingRequests());
    QVERIFY(!called);
}

// ---------------------------------------------------------------------------
// Family preference
// ---------------------------------------------------------------------------

void tst_HostResolver::preference_filtersFamilies()
{
    HostResolver resolver;

    // An IPv4 literal requested as IPv6-only yields no address, with a reason.
    bool called = false;
    HostResolver::Result got;
    resolver.resolve(QStringLiteral("8.8.8.8"), HostResolver::Preference::IPv6Only,
                     [&](const HostResolver::Result& r) { called = true; got = r; });
    QVERIFY(waitFor(called));
    QVERIFY(!got.ok());
    QVERIFY(!got.errorString.isEmpty());

    // ...and the reverse.
    called = false;
    resolver.resolve(QStringLiteral("2001:db8::1"), HostResolver::Preference::IPv4Only,
                     [&](const HostResolver::Result& r) { called = true; got = r; });
    QVERIFY(waitFor(called));
    QVERIFY(!got.ok());
}

void tst_HostResolver::orderByPreference_ordersAndDedups()
{
    const Address v4a = Address::fromString(QStringLiteral("1.2.3.4"));
    const Address v4b = Address::fromString(QStringLiteral("5.6.7.8"));
    const Address v6  = Address::fromString(QStringLiteral("2001:db8::1"));

    // Duplicates collapse, relative order within a family is kept.
    auto any = HostResolver::orderByPreference({v6, v4a, v4a, v4b},
                                               HostResolver::Preference::Any);
    QCOMPARE(any.size(), std::size_t{3});
    QCOMPARE(any[0], v6);
    QCOMPARE(any[1], v4a);
    QCOMPARE(any[2], v4b);

    auto preferV4 = HostResolver::orderByPreference({v6, v4a, v4b},
                                                    HostResolver::Preference::PreferIPv4);
    QCOMPARE(preferV4[0], v4a);
    QCOMPARE(preferV4[1], v4b);
    QCOMPARE(preferV4[2], v6);

    auto preferV6 = HostResolver::orderByPreference({v4a, v6, v4b},
                                                    HostResolver::Preference::PreferIPv6);
    QCOMPARE(preferV6[0], v6);

    QCOMPARE(HostResolver::orderByPreference({v4a, v6}, HostResolver::Preference::IPv4Only).size(),
             std::size_t{1});
    QCOMPARE(HostResolver::orderByPreference({v4a, v6}, HostResolver::Preference::IPv6Only).size(),
             std::size_t{1});
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

void tst_HostResolver::cancel_suppressesCallback()
{
    HostResolver resolver;
    bool called = false;

    const int token = resolver.resolve(QStringLiteral("8.8.8.8"),
                                       HostResolver::Preference::Any,
                                       [&](const HostResolver::Result&) { called = true; });
    resolver.cancel(token);

    // Give the queued delivery a chance to fire; it must not.
    QTest::qWait(100);
    QVERIFY(!called);
    QVERIFY(!resolver.hasPendingRequests());
}

void tst_HostResolver::destruction_suppressesCallback()
{
    bool called = false;
    {
        HostResolver resolver;
        resolver.resolve(QStringLiteral("8.8.8.8"), HostResolver::Preference::Any,
                         [&](const HostResolver::Result&) { called = true; });
    }
    QTest::qWait(100);
    QVERIFY(!called);   // and no use-after-free of the captured locals
}

void tst_HostResolver::contextDestroyed_suppressesCallback()
{
    HostResolver resolver;
    bool called = false;
    {
        QObject context;
        resolver.resolve(QStringLiteral("8.8.8.8"), HostResolver::Preference::Any, &context,
                         [&](const HostResolver::Result&) { called = true; });
    }
    QTest::qWait(100);
    QVERIFY(!called);
    QVERIFY(!resolver.hasPendingRequests());
}

// ---------------------------------------------------------------------------
// Real lookups that work without a nameserver
// ---------------------------------------------------------------------------

void tst_HostResolver::localhost_resolvesOffline()
{
    HostResolver resolver;
    bool called = false;
    HostResolver::Result got;

    resolver.resolve(QStringLiteral("localhost"), HostResolver::Preference::Any,
                     [&](const HostResolver::Result& r) { called = true; got = r; });
    QVERIFY(waitFor(called));

    if (!got.ok())
        QSKIP("localhost does not resolve in this environment");

    // Whatever families the host provides, each returned address must be a loopback.
    for (const auto& addr : got.addresses)
        QVERIFY(addr.isLan());
}

void tst_HostResolver::timeout_reportsError()
{
    HostResolver resolver;
    bool called = false;
    HostResolver::Result got;

    // A name that cannot resolve, with a 1 ms budget: either the timeout or an NXDOMAIN
    // wins, and both must arrive as a failure rather than hanging.
    resolver.resolve(QStringLiteral("invalid.invalid.emuleqt-test"),
                     HostResolver::Preference::Any, nullptr,
                     [&](const HostResolver::Result& r) { called = true; got = r; },
                     /*timeoutMs=*/1);

    QVERIFY(waitFor(called, 10'000));
    QVERIFY(!got.ok());
    QVERIFY(!got.errorString.isEmpty());
    QVERIFY(!resolver.hasPendingRequests());
}

void tst_HostResolver::cache_secondRequestStillAsync()
{
    HostResolver resolver;
    bool firstCalled = false;
    resolver.resolve(QStringLiteral("localhost"), HostResolver::Preference::Any,
                     [&](const HostResolver::Result&) { firstCalled = true; });
    QVERIFY(waitFor(firstCalled));

    // Second request for the same host is served from cache — still asynchronously.
    bool secondCalled = false;
    HostResolver::Result got;
    resolver.resolve(QStringLiteral("localhost"), HostResolver::Preference::Any,
                     [&](const HostResolver::Result& r) { secondCalled = true; got = r; });
    QVERIFY(!secondCalled);
    QVERIFY(waitFor(secondCalled));
    QCOMPARE(got.host, QStringLiteral("localhost"));

    resolver.clearCache();   // exercised so the seam cannot rot
}

#include "tst_HostResolver.moc"

QTEST_GUILESS_MAIN(tst_HostResolver)
