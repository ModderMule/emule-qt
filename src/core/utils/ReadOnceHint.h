#pragma once

/// @file ReadOnceHint.h
/// @brief A file hashed from start to end is read once: the OS is told not to keep it in
/// the page cache.
///
/// Hashing a few hundred GB of finished torrents pushed every other page out of RAM, the
/// desktop's and the core's own code included; the PC froze (reports 29/30).
///
/// - Linux: POSIX_FADV_SEQUENTIAL at the start, POSIX_FADV_DONTNEED behind the reader
///   every kDropStep, the rest at the end. Dirty pages are not dropped (only written).
/// - macOS: F_NOCACHE on the descriptor.
/// - Windows: nothing (FILE_FLAG_SEQUENTIAL_SCAN needs CreateFile; QFile can't).

#include <QtGlobal>

class QFileDevice;

namespace eMule {

class ReadOnceHint {
public:
    /// @p file must be open; it must outlive this.
    explicit ReadOnceHint(const QFileDevice& file);
    /// Drops what is left after the last readUpTo.
    ~ReadOnceHint();
    ReadOnceHint(const ReadOnceHint&) = delete;
    ReadOnceHint& operator=(const ReadOnceHint&) = delete;

    /// Bytes before @p pos are read and not needed again.
    void readUpTo(qint64 pos);

    static constexpr qint64 kDropStep = 8 * 1024 * 1024;

private:
    void drop(qint64 from, qint64 len);

    int m_fd = -1;
    qint64 m_dropped = 0;
};

} // namespace eMule
