#pragma once

/// @file SideHasher.h
/// @brief A second hash fed from eD2K's hashing read: a new file is read once for both.
///
/// The core knows no ed3k; the daemon sets a factory (SharedFileList::setSideHasher) and
/// HashingThread feeds every byte it reads for MD4/AICH into the side hash too.

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>

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
