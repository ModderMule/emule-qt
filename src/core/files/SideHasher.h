#pragma once

/// @file SideHasher.h
/// @brief A second hash fed from eD2K's hashing read: a new file is read once for both.
///
/// The core knows no ed3k; the daemon sets a factory (SharedFileList::setSideHasher) and
/// HashingThread feeds every byte it reads for MD4/AICH into the side hash too.
/// SideHashPipe runs a side hash on its own thread, so it costs the eD2K read no CPU;
/// KnownFile runs AICH through one too.

#include "utils/DiskLoadLimiter.h"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QThread>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace eMule {

/// One file's side hash. Lives on a hashing worker thread.
class SideHash {
public:
    virtual ~SideHash() = default;
    /// The next bytes of the file, in order.
    virtual void update(std::span<const std::byte> data) = 0;
    /// eD2K hashed the file: @p md4 its eD2K hash, @p mtimeMs its date when the read ended.
    /// Not called when eD2K failed or the file changed (the side hash is dropped).
    virtual void finish(const QByteArray& md4, const QString& path, quint64 size, qint64 mtimeMs) = 0;
};

/// Runs @p inner's update() on its own low-priority thread, fed in 1 MiB chunks through a
/// short queue; finish() waits for it and calls inner's finish() on the caller's thread.
/// A big file only: a thread per file costs more than hashing a small one inline.
class SideHashPipe final : public SideHash {
public:
    static constexpr std::size_t kChunk = 1u << 20;
    static constexpr std::size_t kDepth = 8;   ///< chunks queued at most: the reader waits beyond

    explicit SideHashPipe(std::unique_ptr<SideHash> inner) : m_inner(std::move(inner))
    {
        m_thread.reset(QThread::create([this] {
            DiskLoadLimiter::lowerThisThread();
            run();
        }));
        m_thread->start(QThread::LowPriority);
    }

    /// Without finish(): eD2K failed, the queued rest is dropped.
    ~SideHashPipe() override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_abort = true;
        }
        join();
    }

    void update(std::span<const std::byte> data) override
    {
        while (!data.empty()) {
            if (m_fill.capacity() == 0)
                m_fill = takeBuffer();
            const std::size_t n = std::min(kChunk - m_fill.size(), data.size());
            m_fill.insert(m_fill.end(), data.begin(), data.begin() + std::ptrdiff_t(n));
            data = data.subspan(n);
            if (m_fill.size() == kChunk)
                push();
        }
    }

    void finish(const QByteArray& md4, const QString& path, quint64 size, qint64 mtimeMs) override
    {
        drain();
        m_inner->finish(md4, path, size, mtimeMs);
    }

    /// Waits until inner has seen every byte; inner is then the caller's again.
    void drain()
    {
        if (!m_fill.empty())
            push();
        join();
    }

private:
    std::vector<std::byte> takeBuffer()
    {
        const std::scoped_lock lock(m_mutex);
        if (m_free.empty()) {
            std::vector<std::byte> buf;
            buf.reserve(kChunk);
            return buf;
        }
        auto buf = std::move(m_free.back());
        m_free.pop_back();
        return buf;
    }

    void push()
    {
        std::unique_lock lock(m_mutex);
        m_notFull.wait(lock, [this] { return m_queue.size() < kDepth || m_abort; });
        m_queue.push_back(std::move(m_fill));
        m_fill = {};
        m_notEmpty.notify_one();
    }

    /// Lets the worker drain the queue and end.
    void join()
    {
        if (!m_thread)
            return;
        {
            const std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_one();
        m_thread->wait();
        m_thread.reset();
    }

    void run()
    {
        std::vector<std::byte> chunk;
        for (;;) {
            {
                std::unique_lock lock(m_mutex);
                if (chunk.capacity() != 0) {
                    chunk.clear();
                    m_free.push_back(std::move(chunk));
                    chunk = {};
                }
                m_notEmpty.wait(lock, [this] { return !m_queue.empty() || m_closed || m_abort; });
                if (m_abort || m_queue.empty())
                    return;
                chunk = std::move(m_queue.front());
                m_queue.pop_front();
            }
            m_notFull.notify_one();
            m_inner->update(chunk);
        }
    }

    std::unique_ptr<SideHash> m_inner;
    std::vector<std::byte> m_fill;   ///< reader thread only
    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<std::vector<std::byte>> m_queue;
    std::vector<std::vector<std::byte>> m_free;   ///< drained chunks, reused
    bool m_closed = false;
    bool m_abort = false;
    std::unique_ptr<QThread> m_thread;
};

/// nullptr: no side hash for this file.
using SideHasherFactory = std::function<std::unique_ptr<SideHash>(const QString& path, quint64 size)>;

/// The factory as the hashing workers share it: set and cleared from the core thread.
class SideHasherSlot {
public:
    void set(SideHasherFactory factory)
    {
        const QMutexLocker lock(&m_mutex);
        m_factory = std::move(factory);
    }

    [[nodiscard]] std::unique_ptr<SideHash> make(const QString& path, quint64 size) const
    {
        SideHasherFactory factory;
        {
            const QMutexLocker lock(&m_mutex);
            factory = m_factory;
        }
        return factory ? factory(path, size) : nullptr;
    }

private:
    mutable QMutex m_mutex;
    SideHasherFactory m_factory;
};

} // namespace eMule
