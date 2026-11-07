#pragma once

/// @file DiskLoadLimiter.h
/// @brief Keeps a hashing read loop from saturating the disk.
///
/// Times each read and pauses in proportion, so the disk is busy at most the
/// configured share of the time (Preferences::hashingDiskLoad). Read time is
/// what is measured, not bytes: a cached file or a fast disk reads in no time
/// and is not slowed down. Works the same on every OS and file system.
///
/// Limiters reading the same volume share one budget (setFile): each hashing thread
/// on its own made 3 × 80 % of one disk (report 23: eD2K and ed3k hashing together).

#include <QtGlobal>

#include <QString>

#include <functional>
#include <memory>

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

    /// The calling thread's limiter. Read time adds up across files: a small file is
    /// read long before kMinBusyNs, and one limiter per file never paused at all (#46).
    [[nodiscard]] static DiskLoadLimiter& thisThread();

    /// Below-normal CPU and disk priority for the calling thread, as MFC hashes
    /// (CAddFileThread, THREAD_PRIORITY_BELOW_NORMAL). Not undone: hashing threads only.
    static void lowerThisThread();

    /// The reads that follow are of @p path: they share the budget of its volume.
    void setFile(const QString& path);
    /// The same by volume key (tests; empty: own budget).
    void setVolume(const QString& key);

    void beginRead();
    void endRead();

    /// Idle time that makes @p busyNs of reading come to @p percent of the total.
    [[nodiscard]] static qint64 pauseNs(qint64 busyNs, int percent);

    /// Reading is collected up to here before a pause: no sleep per 64 KB read.
    static constexpr qint64 kMinBusyNs = 20'000'000;
    /// One pause never lasts longer, so a stop request is not kept waiting.
    static constexpr qint64 kMaxPauseNs = 2'000'000'000;

private:
    /// One volume's timeline: reads of every limiter on it reserve their share of it.
    struct Ledger;
    [[nodiscard]] static std::shared_ptr<Ledger> ledgerFor(const QString& key);
    [[nodiscard]] static QString volumeKey(const QString& path);

    Hooks m_hooks;
    std::shared_ptr<Ledger> m_ledger;
    QString m_volume;
    bool m_active = true;
    qint64 m_readStartNs = 0;
    qint64 m_busyNs = 0;
};

} // namespace eMule
