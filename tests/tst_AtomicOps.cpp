#include <QTest>

#include "TestHelpers.h"
#include "utils/ThreadUtils.h"

#include <thread>
#include <vector>

using namespace std::chrono_literals;

/// @brief Tests for ThreadUtils.h threading primitives.
class AtomicOpsTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Atomic stress test ----

    void testAtomicIncrementMultiThread()
    {
        constexpr int kThreads = 8;
        constexpr int kIncrementsPerThread = 10000;

        eMule::Atomic<int> counter{0};
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);

        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&counter] {
                for (int j = 0; j < kIncrementsPerThread; ++j)
                    counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        // jthread destructor joins automatically
        threads.clear();

        QCOMPARE(counter.load(), kThreads * kIncrementsPerThread);
    }

    // ---- ManualResetEvent ----

    void testManualResetEventSetAndWait()
    {
        eMule::ManualResetEvent event;
        QVERIFY(!event.isSet());

        event.set();
        QVERIFY(event.isSet());

        // wait() should return immediately when already set
        event.wait();
        QVERIFY(event.isSet());  // stays set
    }

    void testManualResetEventReset()
    {
        eMule::ManualResetEvent event(true);  // start signalled
        QVERIFY(event.isSet());

        event.reset();
        QVERIFY(!event.isSet());
    }

    void testManualResetEventWaitFromThread()
    {
        eMule::ManualResetEvent event;
        eMule::Atomic<bool> threadDone{false};

        std::jthread worker([&] {
            event.wait();
            threadDone.store(true);
        });

        // Give the thread time to start waiting
        eMule::Atomic<int> spin{0};
        while (spin.fetch_add(1) < 1000)
            std::this_thread::yield();

        QVERIFY(!threadDone.load());
        event.set();
        worker.join();
        QVERIFY(threadDone.load());
    }

    // ---- AutoResetEvent ----

    void testAutoResetEventSingleWake()
    {
        eMule::AutoResetEvent event;

        event.set();
        // First wait should succeed and auto-reset
        const bool ok = event.waitFor(100ms);
        QVERIFY(ok);

        // Second wait should timeout because event auto-reset
        const bool timeout = event.waitFor(50ms);
        QVERIFY(!timeout);
    }

    // ---- Mutex RAII ----

    void testMutexRAII()
    {
        eMule::Mutex mtx;
        int sharedValue = 0;

        {
            eMule::Lock lock(mtx);
            sharedValue = 42;
        }

        // Lock is released, should be able to re-acquire
        {
            eMule::Lock lock(mtx);
            QCOMPARE(sharedValue, 42);
        }
    }

    void testMutexContention()
    {
        eMule::Mutex mtx;
        int sharedValue = 0;
        constexpr int kIterations = 10000;

        std::jthread t1([&] {
            for (int i = 0; i < kIterations; ++i) {
                eMule::Lock lock(mtx);
                ++sharedValue;
            }
        });

        std::jthread t2([&] {
            for (int i = 0; i < kIterations; ++i) {
                eMule::Lock lock(mtx);
                ++sharedValue;
            }
        });

        t1.join();
        t2.join();

        QCOMPARE(sharedValue, kIterations * 2);
    }

    // ---- waitFor timeout ----

    void testManualResetEventWaitForTimeout()
    {
        eMule::ManualResetEvent event;

        const bool result = event.waitFor(50ms);
        QVERIFY(!result);  // should timeout
    }

    void testManualResetEventWaitForSuccess()
    {
        eMule::ManualResetEvent event;

        std::jthread setter([&] {
            std::this_thread::sleep_for(20ms);
            event.set();
        });

        const bool result = event.waitFor(2s);
        QVERIFY(result);  // should succeed
    }
};

QTEST_MAIN(AtomicOpsTest)
#include "tst_AtomicOps.moc"
