#pragma once

/// @file BackgroundFileWriter.h
/// @brief Atomic file writes on one worker thread, off the main thread.
///
/// QSaveFile::commit() syncs and renames: seconds on a busy disk, and the core's main
/// thread stood still meanwhile (report 23: resume data, ed3k joins). Writes queue here
/// instead, newest data per path wins, and pending() reads them back before they land.

#include <QByteArray>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
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

    /// QSaveFile with folders made: the default WriteFn.
    static bool saveFile(const QString& path, const QByteArray& data);

private:
    struct Job {
        QByteArray data;
        ProduceFn produce;   ///< set: data is made from it
        WriteFn fn;
    };

    void enqueue(const QString& path, Job job);

    void run();
    /// Waits, lock held, until @p path is not being written.
    void waitNotWriting(std::unique_lock<std::mutex>& lock, const std::function<bool(const QString&)>& match);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;   ///< work came, or stop
    std::condition_variable m_idle;   ///< a write ended
    std::map<QString, Job> m_jobs;    ///< queued, by path
    std::deque<QString> m_order;      ///< paths in queue order
    std::optional<QString> m_writing;
    Job m_writingJob;   ///< what m_writing writes, for pending()
    bool m_stop = false;
    std::thread m_thread;
};

} // namespace eMule
