/// @file tst_UploadBandwidthThrottler.cpp
/// @brief Tests for transfer/UploadBandwidthThrottler.

#include "TestHelpers.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <algorithm>
#include <atomic>

using namespace eMule;

// ---------------------------------------------------------------------------
// Fakes — the throttler's send loop drives these from its own thread
// ---------------------------------------------------------------------------

/// Control-only socket holding `pending` bytes, drained in full by one
/// sendControlData() call — which is exactly how the real UDP sockets behave and
/// why hasControlQueue() has to exist.
class FakeControlSocket : public ThrottledControlSocket {
public:
    SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/) override
    {
        sendCalls.fetch_add(1, std::memory_order_relaxed);
        SocketSentBytes result;
        result.success = true;
        const uint32 have = pending.load(std::memory_order_relaxed);
        if (have > 0) {
            const uint32 n = std::min(have, maxNumberOfBytesToSend);
            pending.fetch_sub(n, std::memory_order_relaxed);
            result.sentBytesControlPackets = n;
        }
        return result;
    }

    [[nodiscard]] bool hasControlQueue() const override
    {
        return pending.load(std::memory_order_relaxed) > 0;
    }

    std::atomic<uint32> pending{0};
    std::atomic<int> sendCalls{0};
};

/// File socket that always reports data ready, so the trickle / equal-bandwidth
/// loops service it.
class FakeFileSocket : public ThrottledFileSocket {
public:
    SocketSentBytes sendControlData(uint32, uint32) override { return {0, 0, true}; }

    SocketSentBytes sendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/) override
    {
        fileSendCalls.fetch_add(1, std::memory_order_relaxed);
        SocketSentBytes result;
        result.success = true;
        result.sentBytesStandardPackets = std::min(maxNumberOfBytesToSend, 1024u);
        return result;
    }

    [[nodiscard]] uint32 getLastCalledSend() const override { return 0; }
    [[nodiscard]] uint32 getNeededBytes() override { return 1024; }
    [[nodiscard]] bool isBusyExtensiveCheck() override { return false; }
    [[nodiscard]] bool isBusyQuickCheck() const override { return false; }
    [[nodiscard]] bool isEnoughFileDataQueued(uint32) const override { return true; }
    [[nodiscard]] bool hasQueues(bool) const override { return true; }
    [[nodiscard]] bool hasControlQueue() const override { return false; }

    std::atomic<int> fileSendCalls{0};
};

/// Loop iterations an idle throttler may run in 500 ms. One 100 ms wait per
/// iteration, so 5 is the measured value. Before the idle tier existed it was ~500.
static constexpr uint64 kMaxIdleIterationsPer500ms = 40;

class tst_UploadBandwidthThrottler : public QObject {
    Q_OBJECT

private slots:
    void construction_defaults();
    void startStop_noCrash();
    void sentByteAccounting();
    void slotLimit_calculation();
    void pause_resume();
    void idleDoesNotSpin();
    void idleWakesFastOnData();
    void wakesOnControlPacketQueued();
    void wakesOnAddToStandardList();
    void controlQueueDrainsWhenSocketEmpty();
};

void tst_UploadBandwidthThrottler::construction_defaults()
{
    UploadBandwidthThrottler throttler;
    QCOMPARE(throttler.standardListSize(), 0);
    QCOMPARE(throttler.getSentBytesSinceLastCallAndReset(), uint64(0));
    QCOMPARE(throttler.getSentBytesOverheadSinceLastCallAndReset(), uint64(0));
    QCOMPARE(throttler.getHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset(), 0);
    throttler.endThread();
}

void tst_UploadBandwidthThrottler::startStop_noCrash()
{
    {
        UploadBandwidthThrottler throttler;
        QVERIFY(throttler.isRunning());
        throttler.endThread();
        QVERIFY(!throttler.isRunning());
    }
    // Verify double-stop doesn't crash
    {
        UploadBandwidthThrottler throttler;
        throttler.endThread();
        throttler.endThread();
    }
}

void tst_UploadBandwidthThrottler::sentByteAccounting()
{
    UploadBandwidthThrottler throttler;

    // Initially zero
    uint64 bytes = throttler.getSentBytesSinceLastCallAndReset();
    QCOMPARE(bytes, uint64(0));

    // Second call should also be zero (was reset)
    bytes = throttler.getSentBytesSinceLastCallAndReset();
    QCOMPARE(bytes, uint64(0));

    uint64 overhead = throttler.getSentBytesOverheadSinceLastCallAndReset();
    QCOMPARE(overhead, uint64(0));

    throttler.endThread();
}

void tst_UploadBandwidthThrottler::slotLimit_calculation()
{
    UploadBandwidthThrottler throttler;

    // Very low speed — minimum slots
    QCOMPARE(throttler.getSlotLimit(5 * 1024),
             static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED));

    // Medium speed
    uint32 slots10k = throttler.getSlotLimit(10 * 1024);
    QVERIFY(slots10k >= MIN_UP_CLIENTS_ALLOWED);
    QVERIFY(slots10k <= MIN_UP_CLIENTS_ALLOWED + 1);

    // Higher speed
    uint32 slots50k = throttler.getSlotLimit(50 * 1024);
    QVERIFY(slots50k >= MIN_UP_CLIENTS_ALLOWED + 3);

    // Very high speed — more slots
    uint32 slots200k = throttler.getSlotLimit(200 * 1024);
    QVERIFY(slots200k >= MIN_UP_CLIENTS_ALLOWED + 3);
    QVERIFY(slots200k > slots50k);

    throttler.endThread();
}

void tst_UploadBandwidthThrottler::pause_resume()
{
    UploadBandwidthThrottler throttler;

    // Pause and resume shouldn't crash
    throttler.pause(true);
    QTest::qWait(50);
    throttler.pause(false);
    QTest::qWait(50);

    QVERIFY(throttler.isRunning());
    throttler.endThread();
}

// ---------------------------------------------------------------------------
// Idle wakeup behaviour — the macOS kernel wakeup monitor allows 150 wakes/s
// sustained, and the 1 ms MFC poll put the daemon at ~850.
// ---------------------------------------------------------------------------

void tst_UploadBandwidthThrottler::idleDoesNotSpin()
{
    UploadBandwidthThrottler throttler;

    QTest::qWait(200);  // let the loop settle into the idle branch
    const uint64 before = throttler.loopIterations();
    QTest::qWait(500);
    const uint64 delta = throttler.loopIterations() - before;

    QVERIFY2(delta < kMaxIdleIterationsPer500ms,
             qPrintable(QStringLiteral("idle throttler ran %1 loops in 500ms").arg(delta)));

    throttler.endThread();
}

void tst_UploadBandwidthThrottler::idleWakesFastOnData()
{
    UploadBandwidthThrottler throttler;

    QTest::qWait(200);
    const uint64 before = throttler.loopIterations();

    // The 100 ms timeout is a safety net, not the delivery mechanism — a notify
    // must break the wait immediately.
    throttler.newUploadDataAvailable();
    QTRY_VERIFY_WITH_TIMEOUT(throttler.loopIterations() > before, 50);

    throttler.endThread();
}

void tst_UploadBandwidthThrottler::wakesOnControlPacketQueued()
{
    FakeControlSocket fake;
    UploadBandwidthThrottler throttler;

    QTest::qWait(200);
    fake.pending.store(256);
    // Kad and server UDP reach the throttler only through this call.
    throttler.queueForSendingControlPacket(&fake);

    QTRY_VERIFY_WITH_TIMEOUT(fake.sendCalls.load() > 0, 50);
    QTRY_VERIFY_WITH_TIMEOUT(fake.pending.load() == 0u, 200);

    throttler.removeFromAllQueues(static_cast<ThrottledControlSocket*>(&fake));
    throttler.endThread();
}

void tst_UploadBandwidthThrottler::wakesOnAddToStandardList()
{
    FakeFileSocket fake;
    UploadBandwidthThrottler throttler;

    QTest::qWait(200);
    // A new upload slot arrives without any standard packet being queued yet.
    throttler.addToStandardList(0, &fake);

    QTRY_VERIFY_WITH_TIMEOUT(fake.fileSendCalls.load() > 0, 50);

    throttler.removeFromAllQueues(static_cast<ThrottledFileSocket*>(&fake));
    throttler.endThread();
}

void tst_UploadBandwidthThrottler::controlQueueDrainsWhenSocketEmpty()
{
    FakeControlSocket fake;
    UploadBandwidthThrottler throttler;

    QTest::qWait(200);

    // Two enqueues for what the socket drains in one call — the real pattern, since
    // queueForSendingControlPacket() is called once per packet. The second pop must
    // not re-queue the socket, or m_controlQueue never empties and the loop can
    // never detect idleness again.
    fake.pending.store(256);
    throttler.queueForSendingControlPacket(&fake);
    throttler.queueForSendingControlPacket(&fake);

    QTRY_VERIFY_WITH_TIMEOUT(fake.pending.load() == 0u, 200);
    QTest::qWait(200);  // let it fall back into the idle branch

    const uint64 before = throttler.loopIterations();
    QTest::qWait(500);
    const uint64 delta = throttler.loopIterations() - before;

    // Always report the margin: the 100 ms idle wait puts the expected value at ~5,
    // two orders of magnitude below the 1 ms spin this guards against, but the
    // headroom is machine-dependent and worth seeing on a passing run too.
    qInfo("idle loops in 500ms after draining control queue: %llu (limit %llu)",
          static_cast<unsigned long long>(delta),
          static_cast<unsigned long long>(kMaxIdleIterationsPer500ms));

    QVERIFY2(delta < kMaxIdleIterationsPer500ms,
             qPrintable(QStringLiteral("throttler ran %1 loops in 500ms after draining control queue").arg(delta)));

    throttler.removeFromAllQueues(static_cast<ThrottledControlSocket*>(&fake));
    throttler.endThread();
}

QTEST_GUILESS_MAIN(tst_UploadBandwidthThrottler)
#include "tst_UploadBandwidthThrottler.moc"
