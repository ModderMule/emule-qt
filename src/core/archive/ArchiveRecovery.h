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
    /// Recover valid data from a partial download.
    /// @param partFile  The partial download to recover from
    /// @param preview   If true, open result for preview after recovery
    /// @param createCopy If true, write to a copy file rather than modifying original
    /// @return true if at least some data was recovered
    static bool recover(PartFile* partFile, bool preview = false,
                        bool createCopy = true);

    /// Async recovery — runs recover() on a background thread.
    /// Sets partFile->setRecoveringArchive() flag during operation.
    /// @param callback  Called on completion with success/failure result (on worker thread)
    static void recoverAsync(PartFile* partFile, bool preview, bool createCopy,
                             std::function<void(bool)> callback = {});

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
