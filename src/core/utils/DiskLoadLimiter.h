#pragma once

/// @file DiskLoadLimiter.h
/// @brief Keeps a hashing read loop from saturating the disk.
///
/// Times each read and pauses in proportion, so the disk is busy at most the
/// configured share of the time (Preferences::hashingDiskLoad). Read time is
/// what is measured, not bytes: a cached file or a fast disk reads in no time
/// and is not slowed down. Works the same on every OS and file system.

#include <QtGlobal>

#include <functional>

namespace eMule {

class DiskLoadLimiter {
public:
    /// Replaceable for tests. An empty hook means the real thing.
    struct Hooks {
        std::function<qint64()> nowNs;
        std::function<void(qint64)> sleepMs;
        std::function<int()> percent;
        bool allowMainThread = false;
    };

    DiskLoadLimiter();
    explicit DiskLoadLimiter(Hooks hooks);

    /// Brackets one read; the pause, if one is due, happens in the destructor.
    class Read {
    public:
        explicit Read(DiskLoadLimiter& limiter) : m_limiter(limiter) { m_limiter.beginRead(); }
        ~Read() { m_limiter.endRead(); }
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;
    private:
        DiskLoadLimiter& m_limiter;
    };

    void beginRead();
    void endRead();

    /// Idle time that makes @p busyNs of reading come to @p percent of the total.
    [[nodiscard]] static qint64 pauseNs(qint64 busyNs, int percent);

    /// Reading is collected up to here before a pause: no sleep per 64 KB read.
    static constexpr qint64 kMinBusyNs = 20'000'000;
    /// One pause never lasts longer, so a stop request is not kept waiting.
    static constexpr qint64 kMaxPauseNs = 2'000'000'000;

private:
    Hooks m_hooks;
    bool m_active = true;
    qint64 m_readStartNs = 0;
    qint64 m_busyNs = 0;
};

} // namespace eMule
