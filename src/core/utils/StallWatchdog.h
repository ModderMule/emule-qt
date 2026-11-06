#pragma once

/// @file StallWatchdog.h
/// @brief Notices a main thread that stops running its event loop, and says where it is.
///
/// A monitor thread posts a heartbeat to the main thread every second. Unanswered for
/// the stall limit, it writes the main thread's stack to `<crashDir>/stall-<time>.stall`
/// and logs it (report 20: the core went silent, nothing told why). Once the loop runs
/// again, the stall's length is logged too.

#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#ifndef Q_OS_WIN
#include <pthread.h>
#endif

namespace eMule {

class StallWatchdog {
public:
    /// Under the GUI's 10 s keepalive window: a GUI that drops the core finds a stack
    static constexpr std::chrono::milliseconds kDefaultLimit{8000};
    /// Stall files kept in the crash dir, newest first
    static constexpr int kKeptReports = 10;

    /// Call on the main thread, after QCoreApplication exists.
    explicit StallWatchdog(QString crashDir, std::chrono::milliseconds limit = kDefaultLimit);
    ~StallWatchdog();
    StallWatchdog(const StallWatchdog&) = delete;
    StallWatchdog& operator=(const StallWatchdog&) = delete;

    /// Stalls reported so far. For tests.
    [[nodiscard]] int stallCount() const { return m_stalls.load(); }

    /// The last report's file, empty when none was written. For tests.
    [[nodiscard]] QString lastReportPath() const;

    /// Steady-clock now in ns
    [[nodiscard]] static int64_t nowNs();

private:
    /// Shared with queued beats: one still queued at destruction finds it alive
    struct Beat {
        std::atomic<int64_t> lastNs{0};
        std::atomic<bool> pending{false};
    };

    void watch();
    void postBeat();
    void report(std::chrono::milliseconds stalledFor);
    void pruneReports() const;
    /// The main thread's frames, symbolized; a note when the platform cannot tell.
    [[nodiscard]] QString mainThreadStack() const;
    /// Linux: the main thread's run state and kernel wait channel, empty elsewhere.
    [[nodiscard]] QString mainThreadKernelState() const;

    const QString m_crashDir;
    const std::chrono::milliseconds m_limit;
    const std::shared_ptr<Beat> m_beat = std::make_shared<Beat>();
    std::atomic<int> m_stalls{0};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    QString m_lastReport;

#ifdef Q_OS_WIN
    void* m_mainThread = nullptr;   ///< HANDLE of the watched thread, may suspend + read context
#else
    pthread_t m_mainThread{};   ///< the watched thread
#endif
#ifdef Q_OS_LINUX
    long m_mainTid = 0;         ///< its kernel thread id
#endif

    std::thread m_thread;   ///< last: starts once the members above exist
};

} // namespace eMule
