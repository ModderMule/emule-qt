/// @file tst_ReachabilityWatch.cpp
/// @brief #156 port-opened watch: steps, gates, evidence, port change. Fake clock, no network.

#include "TestHelpers.h"
#include "net/ReachabilityWatch.h"

#include <QTest>

#include <vector>

using namespace eMule;

namespace {

struct Rig {
    ReachabilityWatch watch;
    ReachabilityWatch::State state;
    time_t clock = 1'000'000;
    int kadRechecks = 0;
    std::vector<std::pair<QString, bool>> highIdRequests;   // why, portChanged
    int portTests = 0;
    ReachabilityWatch::PortTestDone pendingTest;
    bool withPortTest = true;

    Rig()
    {
        ReachabilityWatch::Hooks hooks;
        hooks.state = [this] { return state; };
        hooks.recheckKad = [this] { ++kadRechecks; return true; };
        hooks.requestHighId = [this](const QString& why, bool portChanged) {
            highIdRequests.emplace_back(why, portChanged);
            return true;
        };
        hooks.portTest = [this](ReachabilityWatch::PortTestDone done) {
            ++portTests;
            pendingTest = std::move(done);
        };
        hooks.now = [this] { return clock; };
        watch.setHooks(std::move(hooks));
        watch.setEnabled(true);
    }

    void advanceMinutes(int minutes)
    {
        for (int i = 0; i < minutes * 4; ++i) {   // 15 s ticks
            clock += 15;
            watch.tick();
        }
    }
};

} // namespace

class tst_ReachabilityWatch : public QObject {
    Q_OBJECT

private slots:
    void off_doesNothing();
    void steps_runAt2_10_30_60MinutesOnly();
    void steps_portTestOnlyWhenExpectedReachable();
    void portTestOpen_asksForHighId();
    void portTestAfterReset_isDropped();
    void reachable_stopsTheSteps();
    void evidence_asksServerAndKad();
    void evidence_kadRecheckGap();
    void evidence_ignoredWhenReachable();
    void kadUdpOpened_isEvidence();
    void lanMode_noWatch();
    void mappedPortChange_restartsAndTellsTheServer();
};

void tst_ReachabilityWatch::off_doesNothing()
{
    Rig rig;
    rig.watch.setEnabled(false);
    rig.state.ed2kLowId = true;
    rig.advanceMinutes(70);
    rig.watch.onReachable(QStringLiteral("peer"));
    rig.watch.onMappedPortChanged(1000);
    rig.watch.onMappedPortChanged(2000);
    QCOMPARE(rig.kadRechecks, 0);
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
    QCOMPARE(rig.portTests, 0);
}

void tst_ReachabilityWatch::steps_runAt2_10_30_60MinutesOnly()
{
    Rig rig;
    rig.state.kadRunning = true;
    rig.state.kadTcpFirewalled = true;
    rig.watch.tick();
    QCOMPARE(rig.watch.watchingSince(), rig.clock);

    rig.advanceMinutes(1);
    QCOMPARE(rig.kadRechecks, 0);
    rig.advanceMinutes(1);
    QCOMPARE(rig.kadRechecks, 1);   // 2 min
    rig.advanceMinutes(7);
    QCOMPARE(rig.kadRechecks, 1);
    rig.advanceMinutes(1);
    QCOMPARE(rig.kadRechecks, 2);   // 10 min
    rig.advanceMinutes(20);
    QCOMPARE(rig.kadRechecks, 3);   // 30 min
    rig.advanceMinutes(30);
    QCOMPARE(rig.kadRechecks, 4);   // 60 min
    rig.advanceMinutes(120);
    QCOMPARE(rig.kadRechecks, 4);   // then the hourly Kad check alone
    QCOMPARE(rig.watch.stepsDone(), 4);
}

void tst_ReachabilityWatch::steps_portTestOnlyWhenExpectedReachable()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.watch.tick();
    rig.advanceMinutes(2);
    QCOMPARE(rig.portTests, 0);   // never reachable: website left alone

    rig.state.lastHighIdAt = rig.clock - 3600;   // HighID an hour ago
    rig.advanceMinutes(8);
    QCOMPARE(rig.portTests, 1);

    rig.pendingTest(PortTestApi::Verdict::Closed);
    rig.state.lastHighIdAt = 0;
    rig.state.portMapped = true;
    rig.advanceMinutes(20);
    QCOMPARE(rig.portTests, 2);
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
}

void tst_ReachabilityWatch::portTestOpen_asksForHighId()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.state.portMapped = true;
    rig.watch.tick();
    rig.advanceMinutes(2);
    QCOMPARE(rig.portTests, 1);
    rig.pendingTest(PortTestApi::Verdict::Open);
    QCOMPARE(rig.highIdRequests.size(), size_t{1});
    QCOMPARE(rig.highIdRequests[0].second, false);
}

void tst_ReachabilityWatch::portTestAfterReset_isDropped()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.state.portMapped = true;
    rig.watch.tick();
    rig.advanceMinutes(2);
    QCOMPARE(rig.portTests, 1);
    rig.watch.reset();
    rig.pendingTest(PortTestApi::Verdict::Open);
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
}

void tst_ReachabilityWatch::reachable_stopsTheSteps()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.watch.tick();
    QVERIFY(rig.watch.watchingSince() != 0);
    rig.state.ed2kLowId = false;
    rig.watch.tick();
    QCOMPARE(rig.watch.watchingSince(), time_t{0});
    QCOMPARE(rig.watch.stepsDone(), 0);
}

void tst_ReachabilityWatch::evidence_asksServerAndKad()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.state.kadRunning = true;
    rig.state.kadTcpFirewalled = true;
    rig.watch.onReachable(QStringLiteral("inbound connection"));
    QCOMPARE(rig.highIdRequests.size(), size_t{1});
    QCOMPARE(rig.highIdRequests[0].first, QStringLiteral("inbound connection"));
    QCOMPARE(rig.kadRechecks, 1);
}

void tst_ReachabilityWatch::evidence_kadRecheckGap()
{
    Rig rig;
    rig.state.kadRunning = true;
    rig.state.kadTcpFirewalled = true;
    rig.watch.onReachable(QStringLiteral("a"));
    rig.clock += 30;
    rig.watch.onReachable(QStringLiteral("b"));
    QCOMPARE(rig.kadRechecks, 1);
    rig.clock += ReachabilityWatch::kKadRecheckGapSecs;
    rig.watch.onReachable(QStringLiteral("c"));
    QCOMPARE(rig.kadRechecks, 2);
}

void tst_ReachabilityWatch::evidence_ignoredWhenReachable()
{
    Rig rig;
    rig.state.kadRunning = true;   // open Kad, HighID
    rig.watch.onReachable(QStringLiteral("peer"));
    QCOMPARE(rig.kadRechecks, 0);
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
}

void tst_ReachabilityWatch::kadUdpOpened_isEvidence()
{
    Rig rig;
    rig.state.ed2kLowId = true;
    rig.state.kadRunning = true;
    rig.watch.tick();
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
    rig.state.kadUdpOpen = true;
    rig.watch.tick();
    QCOMPARE(rig.highIdRequests.size(), size_t{1});
    rig.watch.tick();   // only the change counts
    QCOMPARE(rig.highIdRequests.size(), size_t{1});
}

void tst_ReachabilityWatch::lanMode_noWatch()
{
    Rig rig;
    rig.state.kadRunning = true;
    rig.state.kadTcpFirewalled = true;
    rig.state.lanMode = true;
    rig.advanceMinutes(70);
    QCOMPARE(rig.kadRechecks, 0);
}

void tst_ReachabilityWatch::mappedPortChange_restartsAndTellsTheServer()
{
    Rig rig;
    rig.watch.onMappedPortChanged(40000);   // first grant: nothing to tell
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
    rig.watch.onMappedPortChanged(40000);   // renewal
    QCOMPARE(rig.highIdRequests.size(), size_t{0});
    rig.watch.onMappedPortChanged(0);       // lost: remembered port stays
    rig.state.ed2kLowId = true;
    rig.watch.tick();
    rig.advanceMinutes(5);
    QCOMPARE(rig.watch.stepsDone(), 1);

    rig.watch.onMappedPortChanged(41000);
    QCOMPARE(rig.highIdRequests.size(), size_t{1});
    QCOMPARE(rig.highIdRequests[0].second, true);
    QCOMPARE(rig.watch.stepsDone(), 0);     // steps start over
}

QTEST_MAIN(tst_ReachabilityWatch)
#include "tst_ReachabilityWatch.moc"
