/// @file tst_PushCoalescer.cpp
/// @brief Push broadcast rate limiting — eMule::Ipc::PushCoalescer.
///
/// The daemon emits one push per *item*: per search result, per download source,
/// per file visited by a shared-directory scan. A client that refetches a list on
/// each one is quadratic in the item count, so the limiter has to hold four
/// properties at once:
///
///   - an isolated event still goes out immediately (no added latency on a click);
///   - a burst collapses to a bounded number of sends, and still delivers a final
///     one so the client is never left holding stale data;
///   - suppressed events cost nothing to build, and the send that does happen
///     carries the newest state rather than the state that opened the window;
///   - when the burst stops, nothing keeps ticking.
///
/// That last one is not pedantry: the upload throttler once cost 843 wakeups/s
/// against a 150/s macOS budget, and a limiter that re-arms unconditionally would
/// leave a permanent wakeup floor for every push type that ever fired.

#include "PushCoalescer.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace eMule::Ipc;

namespace {

constexpr int kWindowMs = 100;

/// Comfortably past one window, with room for timer jitter on a loaded machine.
constexpr int kPastWindowMs = kWindowMs * 3;

/// A push carrying one integer, so a test can tell which build produced it.
IpcMessage tagged(IpcMsgType type, qint64 tag)
{
    IpcMessage msg(type, 0);
    msg.append(tag);
    return msg;
}

[[nodiscard]] qint64 tagOf(const QSignalSpy& spy, int index)
{
    return spy.at(index).at(0).value<IpcMessage>().fieldInt(0);
}

} // namespace

class tst_PushCoalescer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void isolatedEvent_sendsImmediately();
    void burst_sendsLeadingAndOneTrailing();
    void burst_trailingCarriesNewestPayload();
    void burst_suppressedEventsAreNeverBuilt();
    void quietWindow_dropsTimerInsteadOfReArming();
    void continuousTraffic_keepsSendingOncePerWindow();
    void distinctSubKeys_holdIndependentWindows();
    void distinctTypes_holdIndependentWindows();
    void zeroWindow_passesEverythingThrough();
    void nullBuilder_isIgnored();
};

void tst_PushCoalescer::initTestCase()
{
    // QSignalSpy stores each argument as a QVariant, and the tests read the payload
    // back out of it.
    qRegisterMetaType<IpcMessage>();
}

void tst_PushCoalescer::isolatedEvent_sendsImmediately()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    coalescer.post(IpcMsgType::PushDownloadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushDownloadUpdate, 0); }, kWindowMs);

    // Leading edge: out before the event loop even runs again. A user clicking
    // Pause must not wait out a window to see the row change.
    QCOMPARE(spy.count(), 1);

    // One event is not a burst, so nothing follows it.
    QTest::qWait(kPastWindowMs);
    QCOMPARE(spy.count(), 1);
}

void tst_PushCoalescer::burst_sendsLeadingAndOneTrailing()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    for (int i = 0; i < 50; ++i) {
        coalescer.post(IpcMsgType::PushSearchResult,
                       [i] { return tagged(IpcMsgType::PushSearchResult, i); }, kWindowMs);
    }

    // Everything after the first is folded into the open window.
    QCOMPARE(spy.count(), 1);

    QTest::qWait(kPastWindowMs);
    QCOMPARE(spy.count(), 2);
}

void tst_PushCoalescer::burst_trailingCarriesNewestPayload()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    for (int i = 0; i < 10; ++i) {
        coalescer.post(IpcMsgType::PushKadUpdate,
                       [i] { return tagged(IpcMsgType::PushKadUpdate, i); }, kWindowMs);
    }
    QTest::qWait(kPastWindowMs);

    QCOMPARE(spy.count(), 2);
    QCOMPARE(tagOf(spy, 0), 0);   // leading edge: the event that opened the window
    QCOMPARE(tagOf(spy, 1), 9);   // trailing: last one wins, not first-suppressed
}

void tst_PushCoalescer::burst_suppressedEventsAreNeverBuilt()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    int builds = 0;
    auto build = [&builds] {
        ++builds;
        return IpcMessage(IpcMsgType::PushStatsUpdate, 0);
    };

    for (int i = 0; i < 50; ++i)
        coalescer.post(IpcMsgType::PushStatsUpdate, build, kWindowMs);
    QTest::qWait(kPastWindowMs);

    // Building a snapshot means querying core state; a suppressed event must not
    // pay for a message nobody will ever see.
    QCOMPARE(spy.count(), 2);
    QCOMPARE(builds, 2);
}

void tst_PushCoalescer::quietWindow_dropsTimerInsteadOfReArming()
{
    PushCoalescer coalescer;

    coalescer.post(IpcMsgType::PushUploadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushUploadUpdate, 0); }, kWindowMs);
    QCOMPARE(coalescer.activeWindows(), 1);

    QTest::qWait(kPastWindowMs);

    // Nothing arrived during the window, so the entry is gone and no timer is left
    // ticking. An idle daemon must own none at all.
    QCOMPARE(coalescer.activeWindows(), 0);
}

void tst_PushCoalescer::continuousTraffic_keepsSendingOncePerWindow()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    // Feed events without pause for several windows. A restart-on-every-event
    // debounce would emit exactly once here and then starve the client for as long
    // as the burst lasted — the whole reason this is a fixed-window limiter.
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < kWindowMs * 4) {
        coalescer.post(IpcMsgType::PushKnownClientsChanged,
                       [] { return IpcMessage(IpcMsgType::PushKnownClientsChanged, 0); },
                       kWindowMs);
        QTest::qWait(5);
    }

    QVERIFY2(spy.count() >= 3,
             qPrintable(QStringLiteral("expected steady delivery during the burst, got %1")
                            .arg(spy.count())));

    // ...but bounded. Roughly one per window plus the leading edge, with slack for
    // timer jitter; nowhere near the number of events posted.
    QVERIFY2(spy.count() <= 8,
             qPrintable(QStringLiteral("expected the burst to be capped, got %1")
                            .arg(spy.count())));
}

void tst_PushCoalescer::distinctSubKeys_holdIndependentWindows()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    // Two search tabs. A flood in one must not suppress the other, which is why
    // the searchID is part of the key.
    coalescer.post(IpcMsgType::PushSearchResult,
                   [] { return tagged(IpcMsgType::PushSearchResult, 1); }, kWindowMs, 1);
    coalescer.post(IpcMsgType::PushSearchResult,
                   [] { return tagged(IpcMsgType::PushSearchResult, 2); }, kWindowMs, 2);

    QCOMPARE(spy.count(), 2);
    QCOMPARE(tagOf(spy, 0), 1);
    QCOMPARE(tagOf(spy, 1), 2);
    QCOMPARE(coalescer.activeWindows(), 2);

    // Hammering search 1 leaves search 2's window untouched.
    for (int i = 0; i < 20; ++i) {
        coalescer.post(IpcMsgType::PushSearchResult,
                       [] { return tagged(IpcMsgType::PushSearchResult, 1); }, kWindowMs, 1);
    }
    QCOMPARE(spy.count(), 2);

    QTest::qWait(kPastWindowMs);
    QCOMPARE(spy.count(), 3);            // only search 1 had anything pending
    QCOMPARE(tagOf(spy, 2), 1);
}

void tst_PushCoalescer::distinctTypes_holdIndependentWindows()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    coalescer.post(IpcMsgType::PushDownloadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushDownloadUpdate, 0); }, kWindowMs);
    coalescer.post(IpcMsgType::PushUploadUpdate,
                   [] { return IpcMessage(IpcMsgType::PushUploadUpdate, 0); }, kWindowMs);

    QCOMPARE(spy.count(), 2);
    QCOMPARE(coalescer.activeWindows(), 2);
}

void tst_PushCoalescer::zeroWindow_passesEverythingThrough()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    for (int i = 0; i < 5; ++i) {
        coalescer.post(IpcMsgType::PushServerMessage,
                       [] { return IpcMessage(IpcMsgType::PushServerMessage, 0); }, 0);
    }

    // Content-bearing pushes opt out by passing no window, and must not leave a
    // limiter behind that would swallow the next line.
    QCOMPARE(spy.count(), 5);
    QCOMPARE(coalescer.activeWindows(), 0);
}

void tst_PushCoalescer::nullBuilder_isIgnored()
{
    PushCoalescer coalescer;
    QSignalSpy spy(&coalescer, &PushCoalescer::ready);

    coalescer.post(IpcMsgType::PushDownloadAdded, {}, kWindowMs);

    QCOMPARE(spy.count(), 0);
    QCOMPARE(coalescer.activeWindows(), 0);
}

QTEST_MAIN(tst_PushCoalescer)
#include "tst_PushCoalescer.moc"
