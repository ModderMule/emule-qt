#pragma once

/// @file ArchiveRecovery.h
/// @brief Archive recovery from partial downloads — port of MFC ArchiveRecovery.
///
/// Scans partially downloaded files for valid ZIP/RAR entries and rebuilds
/// a usable archive from the available data. Used for preview functionality.

#include "utils/Types.h"

#include <QFile>
#include <QString>

#include <functional>
#include <vector>

namespace eMule {

class PartFile;
struct Gap;

// ---------------------------------------------------------------------------
// ArchiveRecovery
// ---------------------------------------------------------------------------

class ArchiveRecovery {
public:
    /// Recover valid data from a partial download into a copy.
    /// @param preview, createCopy  kept for the callers; the result is always a
    ///        copy — the download itself is never written.
    /// @return true if at least some data was recovered
    static bool recover(PartFile* partFile, bool preview = false,
                        bool createCopy = true);

    /// The same, saying where the copy is: "<outDir>/<name>-rec.<ext>", next to the
    /// download when @p outDir is empty. Empty when nothing could be recovered.
    static QString recoverToCopy(PartFile* partFile, const QString& outDir);

    /// The work of recoverToCopy() on a plain file, for tests and callers that
    /// already hold the filled ranges.
    static QString recoverFile(const QString& srcPath, const std::vector<Gap>& filled,
                               uint64 fileSize, const QString& outDir, const QString& baseName);

    /// Where the copy of @p srcPath goes.
    [[nodiscard]] static QString copyPath(const QString& srcPath, const QString& outDir,
                                          const QString& baseName, const QString& extension);

    /// Async recovery — runs recover() on a background thread.
    /// Sets partFile->setRecoveringArchive() flag during operation.
    /// @param callback  Called on completion with success/failure result (on worker thread)
    static void recoverAsync(PartFile* partFile, bool preview, bool createCopy,
                             std::function<void(bool)> callback = {});

    /// recoverToCopy() on a thread of its own. @p callback gets the copy's path, empty
    /// on failure or when a recovery of this file is already running; it is called
    /// on the worker thread.
    static void recoverToCopyAsync(PartFile* partFile, const QString& outDir,
                                   std::function<void(const QString&)> callback);

    /// Recover valid ZIP entries from a partial file.
    static bool recoverZip(QFile& input, QFile& output,
                           const std::vector<Gap>& filled, uint64 fileSize);

    /// Recover valid RAR blocks from a partial file.
    static bool recoverRar(QFile& input, QFile& output,
                           const std::vector<Gap>& filled);

    /// Recover ISO image data from a partial file.
    static bool recoverISO(QFile& input, QFile& output,
                           const std::vector<Gap>& filled, uint64 fileSize);

    /// Recover ACE archive data from a partial file.
    static bool recoverACE(QFile& input, QFile& output,
                           const std::vector<Gap>& filled);

    /// Check if a byte range [start, end] is fully contained within filled regions.
    static bool isFilled(uint64 start, uint64 end,
                         const std::vector<Gap>& filled);

private:
    // ZIP helpers
    static bool scanForZipMarker(QFile& input, uint32 marker, uint64 searchRange);
    static bool processZipEntry(QFile& input, QFile& output,
                                uint64 entryOffset, const std::vector<Gap>& filled,
                                std::vector<uint64>& centralDirEntries);
    static void writeZipCentralDirectory(QFile& output,
                                         const std::vector<uint64>& centralDirEntries,
                                         QFile& input, const std::vector<Gap>& filled);

    // RAR helpers
    static bool processRarBlock(QFile& input, QFile& output,
                                uint64 blockOffset, const std::vector<Gap>& filled);
};

} // namespace eMule
