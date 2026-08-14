/// @file tst_PanelPoller.cpp
/// @brief Visibility-gated panel refresh — eMule::PanelPoller.
///
/// MainWindow keeps the panels in a QStackedWidget, so a panel on an inactive tab
/// is genuinely hidden. Nothing used to check, and every panel kept refetching its
/// whole list twice a second regardless of whether anyone could see it. The two
/// properties that matter:
///
///   - hidden means silent, and arriving at a tab shows current data rather than
///     whatever was on screen when it was last hidden;
///   - a push event pulls the next poll forward instead of opening a second
///     refetch path beside it.

#include "utils/PanelPoller.h"

#include <QTest>
#include <QWidget>

using namespace eMule;

namespace {

constexpr int kIntervalMs = 100;

/// Long enough for several ticks, short enough to keep the suite quick.
constexpr int kSeveralTicksMs = kIntervalMs * 4;

} // namespace

class tst_PanelPoller : public QObject {
    Q_OBJECT

private slots:
    void hiddenPanel_neverRefreshes();
    void show_refreshesImmediatelyAndStartsPolling();
    void hide_stopsPolling();
    void disabled_stopsPollingEvenWhileVisible();
    void nudge_collapsesToOneRefreshPerIteration();
    void nudge_pushesTheNextTickOutAFullInterval();
    void nudge_whileHiddenIsIgnored();
    void setInterval_takesEffect();
};

void tst_PanelPoller::hiddenPanel_neverRefreshes()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    // Enabled, but never shown: the GUI starts with five of six panels like this.
    QTest::qWait(kSeveralTicksMs);

    QVERIFY(!poller.isPolling());
    QCOMPARE(refreshes, 0);
}

void tst_PanelPoller::show_refreshesImmediatelyAndStartsPolling()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Switching to a tab must not show data from the last time it was open.
    QCOMPARE(refreshes, 1);
    QVERIFY(poller.isPolling());

    QTest::qWait(kSeveralTicksMs);
    QVERIFY2(refreshes > 1, "polling did not continue while visible");
}

void tst_PanelPoller::hide_stopsPolling()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QTest::qWait(kSeveralTicksMs);
    QVERIFY(refreshes > 1);

    panel.hide();
    const int atHide = refreshes;
    QVERIFY(!poller.isPolling());

    QTest::qWait(kSeveralTicksMs);
    QCOMPARE(refreshes, atHide);
}

void tst_PanelPoller::disabled_stopsPollingEvenWhileVisible()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QTest::qWait(kSeveralTicksMs);

    // What a panel does when the IPC connection drops.
    poller.setEnabled(false);
    const int atDisable = refreshes;
    QVERIFY(!poller.isPolling());

    QTest::qWait(kSeveralTicksMs);
    QCOMPARE(refreshes, atDisable);
}

void tst_PanelPoller::nudge_collapsesToOneRefreshPerIteration()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const int afterShow = refreshes;

    // TransferPanel wires five different pushes to nudge(); several can land in the
    // same event-loop iteration.
    for (int i = 0; i < 20; ++i)
        poller.nudge();

    QCOMPARE(refreshes, afterShow);      // nothing synchronous
    QTest::qWait(20);
    QCOMPARE(refreshes, afterShow + 1);  // exactly one, not twenty
}

void tst_PanelPoller::nudge_pushesTheNextTickOutAFullInterval()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Sit just short of a tick, then nudge. The push refresh must *replace* the
    // pending poll rather than fire beside it — that doubling is what made push
    // handlers a second refetch path.
    QTest::qWait(kIntervalMs - 30);
    const int beforeNudge = refreshes;
    poller.nudge();
    QTest::qWait(50);

    QCOMPARE(refreshes, beforeNudge + 1);
}

void tst_PanelPoller::nudge_whileHiddenIsIgnored()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setInterval(kIntervalMs);
    poller.setEnabled(true);

    for (int i = 0; i < 10; ++i)
        poller.nudge();
    QTest::qWait(50);

    QCOMPARE(refreshes, 0);

    // ...and the refresh on show picks up whatever those pushes were about.
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QCOMPARE(refreshes, 1);
}

void tst_PanelPoller::setInterval_takesEffect()
{
    QWidget panel;
    int refreshes = 0;
    PanelPoller poller(&panel, [&refreshes] { ++refreshes; });
    poller.setEnabled(true);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    poller.setInterval(kIntervalMs);
    QCOMPARE(poller.interval(), kIntervalMs);

    const int before = refreshes;
    QTest::qWait(kSeveralTicksMs);
    QVERIFY2(refreshes >= before + 2,
             qPrintable(QStringLiteral("expected several ticks at %1 ms, got %2")
                            .arg(kIntervalMs).arg(refreshes - before)));
}

QTEST_MAIN(tst_PanelPoller)
#include "tst_PanelPoller.moc"
