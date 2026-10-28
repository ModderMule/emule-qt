#pragma once

/// @file BlockRuns.h
/// @brief A block bitmap (one bit per block of a file) as alternating run lengths, for IPC:
/// the ed3k download rows (Ed3kDownloadsBridge → GUI progress bar).
///
/// Encoding: unsigned LEB128 varints, the lengths of runs that alternate set / clear,
/// starting with a set run (0 when the first block is clear). The runs add up to the
/// block count. A half-done file in order takes 2-3 bytes.

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

#include <cstddef>
#include <optional>
#include <vector>

namespace eMule {

/// Most blocks a bitmap may have when decoded (16 Mi blocks: 4 TiB of 256 KiB blocks).
inline constexpr std::size_t kMaxBlockRunBits = std::size_t(1) << 24;

[[nodiscard]] QByteArray encodeBlockRuns(const std::vector<bool>& bits);

/// nullopt: damaged, or the runs do not add up to @p count (> kMaxBlockRunBits: refused).
[[nodiscard]] std::optional<std::vector<bool>> decodeBlockRuns(QByteArrayView runs, std::size_t count);

/// The blocks whose bit is @p value as flat inclusive byte ranges [start, end, ...] (the
/// shape of DownloadRow::gaps); the last block ends at @p fileSize - 1.
[[nodiscard]] QList<qint64> blockRanges(const std::vector<bool>& bits, bool value, qint64 blockSize, qint64 fileSize);

} // namespace eMule
