#include <QTest>

#include "TestHelpers.h"
#include "utils/TimeUtils.h"

#include <QDateTime>
#include <QTimeZone>

#include <chrono>
#include <ctime>

using namespace std::chrono_literals;

/// @brief Tests for TimeUtils.h timing utilities.
class TimeUtilsTest : public QObject {
    Q_OBJECT

private slots:
    void testGetTickCountMonotonicity()
    {
        const auto t1 = eMule::getTickCount();
        std::this_thread::yield();
        const auto t2 = eMule::getTickCount();
        QVERIFY(t2 >= t1);
    }

    void testNowMonotonicity()
    {
        const auto t1 = eMule::now();
        const auto t2 = eMule::now();
        QVERIFY(t2 >= t1);
    }

    void testElapsedMs()
    {
        const auto start = eMule::now();
        eMule::sleepMs(20);
        const auto elapsed = eMule::elapsedMs(start);
        // Should be at least 15ms (allowing scheduler variance)
        QVERIFY(elapsed >= 15);
        // Should be less than 200ms (sanity upper bound)
        QVERIFY(elapsed < 200);
    }

    void testSleepMs()
    {
        const auto start = eMule::now();
        eMule::sleepMs(30);
        const auto elapsed = eMule::elapsedMs(start);
        QVERIFY(elapsed >= 25);
    }

    void testSleepTyped()
    {
        const auto start = eMule::now();
        eMule::sleep(30ms);
        const auto elapsed = eMule::elapsedMs(start);
        QVERIFY(elapsed >= 25);
    }

    void testHighResTimerElapsed()
    {
        eMule::HighResTimer timer;
        eMule::sleepMs(10);
        const double ms = timer.elapsedMs();
        QVERIFY(ms >= 5.0);
        QVERIFY(ms < 200.0);
    }

    void testHighResTimerRestart()
    {
        eMule::HighResTimer timer;
        eMule::sleepMs(10);
        const auto first = timer.restart();
        QVERIFY(first.count() > 0);

        eMule::sleepMs(10);
        const double ms = timer.elapsedMs();
        // After restart, elapsed should reflect only the second sleep
        QVERIFY(ms >= 5.0);
    }

    void testHighResTimerMicroseconds()
    {
        eMule::HighResTimer timer;
        eMule::sleepMs(5);
        const auto us = timer.elapsedUs();
        QVERIFY(us >= 1000);  // at least 1ms in microseconds
    }

    void testFromTimeTRoundtrip()
    {
        const std::time_t original = std::time(nullptr);
        const auto tp = eMule::fromTimeT(original);
        const std::time_t back = eMule::toTimeT(tp);
        QCOMPARE(back, original);
    }

    void testFromTimeTEpoch()
    {
        const auto tp = eMule::fromTimeT(0);
        const std::time_t back = eMule::toTimeT(tp);
        QCOMPARE(back, std::time_t{0});
    }

    void testFromTimeTKnownDate()
    {
        // 2024-01-01 00:00:00 UTC = 1704067200
        constexpr std::time_t jan1_2024 = 1704067200;
        const auto tp = eMule::fromTimeT(jan1_2024);
        const std::time_t back = eMule::toTimeT(tp);
        QCOMPARE(back, jan1_2024);
    }

    // ---- FILETIME conversion tests ------------------------------------------

    void testFileTimeToUnixTimeZero()
    {
        // Zero FILETIME should yield zero time_t
        QCOMPARE(eMule::fileTimeToUnixTime(0), std::time_t{0});
    }

    void testFileTimeToUnixTimeEpoch()
    {
        // Unix epoch (1970-01-01) as FILETIME = 116444736000000000
        constexpr std::uint64_t unixEpochAsFileTime = 116'444'736'000'000'000ULL;
        QCOMPARE(eMule::fileTimeToUnixTime(unixEpochAsFileTime), std::time_t{0});
    }

    void testFileTimeToUnixTimeKnownDate()
    {
        // 2024-01-01 00:00:00 UTC
        // time_t = 1704067200
        // FILETIME = (1704067200 + 11644473600) * 10000000
        constexpr std::time_t expected = 1704067200;
        constexpr std::uint64_t fileTime =
            (static_cast<std::uint64_t>(expected) + 11'644'473'600ULL) * 10'000'000ULL;
        QCOMPARE(eMule::fileTimeToUnixTime(fileTime), expected);
    }

    void testUnixTimeToFileTimeRoundtrip()
    {
        constexpr std::time_t original = 1704067200; // 2024-01-01
        const auto fileTime = eMule::unixTimeToFileTime(original);
        const auto back = eMule::fileTimeToUnixTime(fileTime);
        QCOMPARE(back, original);
    }

    void testUnixTimeToFileTimeZero()
    {
        QCOMPARE(eMule::unixTimeToFileTime(0), std::uint64_t{0});
    }

    void testUnixTimeToFileTimeNegative()
    {
        QCOMPARE(eMule::unixTimeToFileTime(-1), std::uint64_t{0});
    }

    // ---- QDateTime conversion tests -----------------------------------------

    void testToDateTimeFromTimeT()
    {
        constexpr std::time_t t = 1704067200; // 2024-01-01 00:00:00 UTC
        const QDateTime dt = eMule::toDateTime(t);
        QCOMPARE(dt.date(), QDate(2024, 1, 1));
        QCOMPARE(dt.time(), QTime(0, 0, 0));
        QCOMPARE(dt.toUTC(), dt); // verify it represents UTC
    }

    void testToTimeTFromQDateTime()
    {
        const QDateTime dt(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
        constexpr std::time_t expected = 1704067200;
        QCOMPARE(eMule::toTimeT(dt), expected);
    }

    void testQDateTimeTimeTRoundtrip()
    {
        const std::time_t original = std::time(nullptr);
        const QDateTime dt = eMule::toDateTime(original);
        const std::time_t back = eMule::toTimeT(dt);
        QCOMPARE(back, original);
    }

    void testFileTimeToDateTime()
    {
        // 2024-01-01 00:00:00 UTC as FILETIME
        constexpr std::time_t t = 1704067200;
        constexpr std::uint64_t fileTime =
            (static_cast<std::uint64_t>(t) + 11'644'473'600ULL) * 10'000'000ULL;
        const QDateTime dt = eMule::fileTimeToDateTime(fileTime);
        QCOMPARE(dt.date(), QDate(2024, 1, 1));
        QCOMPARE(dt.time(), QTime(0, 0, 0));
    }

    void testDateTimeToFileTimeRoundtrip()
    {
        const QDateTime dt(QDate(2024, 6, 15), QTime(12, 30, 0), QTimeZone::utc());
        const auto fileTime = eMule::dateTimeToFileTime(dt);
        const QDateTime back = eMule::fileTimeToDateTime(fileTime);
        QCOMPARE(back.date(), dt.date());
        QCOMPARE(back.time(), dt.time());
    }

    void testFileTimeToDateTimeZero()
    {
        // Zero FILETIME → epoch (1970-01-01)
        const QDateTime dt = eMule::fileTimeToDateTime(0);
        QCOMPARE(dt.date(), QDate(1970, 1, 1));
        QCOMPARE(dt.time(), QTime(0, 0, 0));
    }

    void testFileTimeConstants()
    {
        // Verify the constants are correct
        QCOMPARE(eMule::kFileTimeUnixEpochDelta, std::int64_t{11'644'473'600LL});
        QCOMPARE(eMule::kFileTimeTicksPerSecond, std::int64_t{10'000'000LL});
    }
};

QTEST_MAIN(TimeUtilsTest)
#include "tst_TimeUtils.moc"
