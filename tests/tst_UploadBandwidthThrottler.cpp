/// @file tst_UploadBandwidthThrottler.cpp
/// @brief Tests for transfer/UploadBandwidthThrottler.

#include "TestHelpers.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Opcodes.h"

#include <QTest>

using namespace eMule;

class tst_UploadBandwidthThrottler : public QObject {
    Q_OBJECT

private slots:
    void construction_defaults();
    void startStop_noCrash();
    void sentByteAccounting();
    void slotLimit_calculation();
    void pause_resume();
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

QTEST_GUILESS_MAIN(tst_UploadBandwidthThrottler)
#include "tst_UploadBandwidthThrottler.moc"
