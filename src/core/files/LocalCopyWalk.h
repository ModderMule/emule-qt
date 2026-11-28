#pragma once

/// @file LocalCopyWalk.h
/// @brief "Locate Files..." folder walk (#224, #251), shared by the eD2K scan
/// (LocalCopyScan) and ed3k's (Ed3kLocalScan): files of a wanted size, nothing read.

#include "utils/Types.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <vector>

namespace eMule {

/// What one Locate Files scan did; the IPC reply's fields.
struct LocalScanResult {
    int files = 0;        ///< files walked
    int candidates = 0;   ///< of a wanted size
    int hashed = 0;
    int matched = 0;      ///< downloads found
    QStringList paths;    ///< the files found
    bool cancelled = false;
};

struct LocalCopyCandidate {
    QString path;   ///< canonical
    uint64 size = 0;
    QString dir;
};

/// Walks @p folders recursively: every file whose size @p wantedSize takes, the folder
/// with most candidates first (one release folder over scattered copies, BiglyBT's
/// buildFileMap). Skips symlinks and unfinished downloads (.part, .!qb, ...).
/// Fills @p r.files / candidates / cancelled.
[[nodiscard]] std::vector<LocalCopyCandidate> walkLocalCopies(const QStringList& folders,
                                                              const std::function<bool(uint64)>& wantedSize,
                                                              const std::atomic<bool>& cancel, LocalScanResult& r);

} // namespace eMule
