#pragma once

/// @file TimeUtils.h
/// @brief Portable time utilities replacing GetTickCount(), Sleep(),
///        SYSTEMTIME, and FILETIME.
///
/// Header-only.  Uses std::chrono and QDateTime throughout.
/// - getTickCount()  replaces GetTickCount()  (170 MFC uses)
/// - sleepMs()       replaces Sleep()          (70 MFC uses)
/// - HighResTimer    replaces CTimeTick / QueryPerformanceCounter
/// - QDateTime       replaces SYSTEMTIME / FILETIME
/// - fileTimeToUnixTime() replaces FileTimeToUnixTime()

#include <QDateTime>
#include <QTimeZone>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <thread>

namespace eMule {

// ---- Convenience type aliases ------------------------------------------------

using SteadyClock    = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;
using SystemClock    = std::chrono::system_clock;
using SystemTimePoint = SystemClock::time_point;

// ---- Tick / elapsed helpers -------------------------------------------------

/// Returns monotonic milliseconds since an arbitrary epoch (like GetTickCount
/// but 64-bit, so no 49-day wrap).
[[nodiscard]] inline std::uint64_t getTickCount() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            SteadyClock::now().time_since_epoch())
            .count());
}

/// Returns the current steady-clock time point.
[[nodiscard]] inline SteadyTimePoint now() noexcept
{
    return SteadyClock::now();
}

/// Milliseconds elapsed since @p start (steady clock).
[[nodiscard]] inline std::uint64_t elapsedMs(SteadyTimePoint start) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            SteadyClock::now() - start)
            .count());
}

// ---- Sleep helpers ----------------------------------------------------------

/// Sleep for @p ms milliseconds (replaces Windows Sleep()).
inline void sleepMs(std::uint32_t ms) noexcept
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/// Typed sleep — accepts any std::chrono::duration.
template <typename Rep, typename Period>
inline void sleep(std::chrono::duration<Rep, Period> duration) noexcept
{
    std::this_thread::sleep_for(duration);
}

// ---- High-resolution timer --------------------------------------------------

/// Drop-in replacement for MFC CTimeTick / QueryPerformanceCounter.
/// Measures wall-clock intervals with the highest available resolution.
class HighResTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    HighResTimer() noexcept : m_start(Clock::now()) {}

    /// Restart the timer, returning elapsed time since last start/restart.
    std::chrono::nanoseconds restart() noexcept
    {
        auto prev = m_start;
        m_start = Clock::now();
        return m_start - prev;
    }

    /// Nanoseconds since construction or last restart().
    [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept
    {
        return Clock::now() - m_start;
    }

    /// Convenience: elapsed time in milliseconds (double).
    [[nodiscard]] double elapsedMs() const noexcept
    {
        return std::chrono::duration<double, std::milli>(elapsed()).count();
    }

    /// Convenience: elapsed time in microseconds.
    [[nodiscard]] std::int64_t elapsedUs() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count();
    }

private:
    Clock::time_point m_start;
};

// ---- time_t conversion helpers ----------------------------------------------

/// Convert a std::time_t to a system_clock::time_point.
[[nodiscard]] inline SystemTimePoint fromTimeT(std::time_t t) noexcept
{
    return SystemClock::from_time_t(t);
}

/// Convert a system_clock::time_point to std::time_t.
[[nodiscard]] inline std::time_t toTimeT(SystemTimePoint tp) noexcept
{
    return SystemClock::to_time_t(tp);
}

// ---- Windows FILETIME conversion helpers ------------------------------------
// FILETIME = 100-nanosecond intervals since 1601-01-01 00:00:00 UTC.
// These helpers replace the MFC FileTimeToUnixTime() and related functions.

/// Seconds between the Windows FILETIME epoch (1601-01-01) and Unix epoch (1970-01-01).
inline constexpr std::int64_t kFileTimeUnixEpochDelta = 11'644'473'600LL;

/// Windows FILETIME ticks per second (100-nanosecond intervals).
inline constexpr std::int64_t kFileTimeTicksPerSecond = 10'000'000LL;

/// Convert a Windows FILETIME value (100ns intervals since 1601-01-01)
/// to a Unix time_t.  Returns 0 for a zero FILETIME.
[[nodiscard]] inline std::time_t fileTimeToUnixTime(std::uint64_t fileTime) noexcept
{
    if (fileTime == 0) return 0;
    return static_cast<std::time_t>(
        static_cast<std::int64_t>(fileTime / kFileTimeTicksPerSecond) - kFileTimeUnixEpochDelta);
}

/// Convert a Unix time_t to a Windows FILETIME value.
/// Returns 0 for non-positive input.
[[nodiscard]] inline std::uint64_t unixTimeToFileTime(std::time_t unixTime) noexcept
{
    if (unixTime <= 0) return 0;
    return static_cast<std::uint64_t>(
        (static_cast<std::int64_t>(unixTime) + kFileTimeUnixEpochDelta) * kFileTimeTicksPerSecond);
}

// ---- QDateTime conversion helpers -------------------------------------------
// Replace SYSTEMTIME (broken-down date/time struct) with QDateTime.

/// Convert std::time_t to QDateTime (UTC).
[[nodiscard]] inline QDateTime toDateTime(std::time_t t)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t), QTimeZone::utc());
}

/// Convert QDateTime to std::time_t.
[[nodiscard]] inline std::time_t toTimeT(const QDateTime& dt) noexcept
{
    return static_cast<std::time_t>(dt.toSecsSinceEpoch());
}

/// Convert a Windows FILETIME value to QDateTime (UTC).
[[nodiscard]] inline QDateTime fileTimeToDateTime(std::uint64_t fileTime)
{
    return toDateTime(fileTimeToUnixTime(fileTime));
}

/// Convert QDateTime to a Windows FILETIME value.
[[nodiscard]] inline std::uint64_t dateTimeToFileTime(const QDateTime& dt) noexcept
{
    return unixTimeToFileTime(toTimeT(dt));
}

} // namespace eMule
