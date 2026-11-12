#pragma once

/// @file BackgroundFileWriter.h
/// @brief Atomic file writes on one worker thread, off the main thread.
///
/// QSaveFile::commit() syncs and renames: seconds on a busy disk, and the core's main
/// thread stood still meanwhile (report 23: resume data, ed3k joins). Writes queue here
/// instead, newest data per path wins, and pending() reads them back before they land.
/// A failed write is tried again a few times (Windows: a scanner or indexer holding the
/// file blocks the rename); failed() tells a caller to write it again later.

#include <QByteArray>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace eMule {

class BackgroundFileWriter {
public:
    /// Writes @p data to @p path atomically; false: failed (logged by the writer).
    using WriteFn = std::function<bool(const QString& path, const QByteArray& data)>;
    /// Makes the bytes on the worker: an encode too slow for the main thread (report 28).
    /// Copyable and safe to call from any thread, more than once.
    using ProduceFn = std::function<QByteArray()>;

    BackgroundFileWriter();
    ~BackgroundFileWriter();   ///< flushes
    BackgroundFileWriter(const BackgroundFileWriter&) = delete;
    BackgroundFileWriter& operator=(const BackgroundFileWriter&) = delete;

    /// The one writer of the process.
    [[nodiscard]] static BackgroundFileWriter& instance();

    /// Queues @p data for @p path; replaces data still queued for it. Folders are made.
    void write(const QString& path, QByteArray data, WriteFn fn);
    /// The same, the bytes made by @p produce on the worker.
    void writeProduced(const QString& path, ProduceFn produce, WriteFn fn);
    /// What is queued or being written for @p path; nullopt: nothing, the file is current.
    /// A produced job is made on the caller's thread for this.
    [[nodiscard]] std::optional<QByteArray> pending(const QString& path) const;
    /// Drops what is queued for @p path and waits for a write of it in progress: the
    /// caller removes the file next.
    void discard(const QString& path);
    /// The same for every path under the folder @p dir.
    void discardUnder(const QString& dir);
    /// Waits until everything queued is written.
    void flush();
    /// The last write of @p path failed after its retries and nothing newer is queued:
    /// the caller writes it again (e.g. on its next save).
    [[nodiscard]] bool failed(const QString& path) const;

    /// QSaveFile with folders made: the default WriteFn.
    static bool saveFile(const QString& path, const QByteArray& data);
    /// What @p path holds once the queue is written: the queued copy, else the file;
    /// nullopt: neither (missing or unreadable).
    [[nodiscard]] static std::optional<QByteArray> readFile(const QString& path);

private:
    struct Job {
        QByteArray data;
        ProduceFn produce;   ///< set: data is made from it
        WriteFn fn;
    };

    void enqueue(const QString& path, Job job);

    void run();
    /// Writes one job, retried while it fails and nothing newer or a discard came; lock held
    /// on entry and exit.
    bool writeWithRetries(std::unique_lock<std::mutex>& lock, const QString& path, const Job& job);
    /// Waits, lock held, until @p path is not being written.
    void waitNotWriting(std::unique_lock<std::mutex>& lock, const std::function<bool(const QString&)>& match);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;   ///< work came, or stop
    std::condition_variable m_idle;   ///< a write ended
    std::map<QString, Job> m_jobs;    ///< queued, by path
    std::deque<QString> m_order;      ///< paths in queue order
    std::optional<QString> m_writing;
    Job m_writingJob;   ///< what m_writing writes, for pending()
    bool m_writingDropped = false;   ///< discard() of m_writing: no more retries
    std::set<QString> m_failed;      ///< last write failed, nothing newer queued
    bool m_stop = false;
    std::thread m_thread;
};

} // namespace eMule
