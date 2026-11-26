#pragma once

/// @file FileRelocate.h
/// @brief "Move to" (#187) for every network: a rename where the volume allows, else a
/// copy that fits the target's free space. Blocking; meant for a worker thread. A copy
/// stops at QThread::isInterruptionRequested().

#include <QString>

#include <cstdint>

namespace eMule {

/// @p src to @p dst (any old @p dst is replaced). A copy keeps holes (all-zero blocks are
/// skipped on a pre-sized file) and needs size + @p floor free; @p renamed tells which
/// ran. A copied @p src stays unless @p removeSource. @p dataSize: the bytes a sparse
/// file really holds (0: its size). Empty: done; else why not.
[[nodiscard]] QString relocateFile(const QString& src, const QString& dst, std::uint64_t floor,
                                   bool removeSource, bool* renamed = nullptr, std::uint64_t dataSize = 0);

/// Rename only: false across volumes, where QFile::rename silently copies. @p dst must be free.
[[nodiscard]] bool renameOnVolume(const QString& src, const QString& dst);

/// Folder @p from to @p to (which must not exist), files and subfolders; a copied @p from
/// is removed. A missing @p from just creates @p to. Empty: done; else why not.
[[nodiscard]] QString relocateDir(const QString& from, const QString& to, std::uint64_t floor);

/// Deletes @p path; one another reader still holds without shared delete (Windows: a
/// copied .part an upload or preview has open) is tried again every kRemoveRetryMs, up
/// to kRemoveRetries times, and given up on with a warning. True: gone now.
[[nodiscard]] bool removeWhenFree(const QString& path);
inline constexpr int kRemoveRetryMs = 5000;
inline constexpr int kRemoveRetries = 120;   // 10 min

} // namespace eMule
