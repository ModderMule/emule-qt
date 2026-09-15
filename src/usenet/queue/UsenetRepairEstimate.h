#pragma once

/// @file UsenetRepairEstimate.h
/// @brief Whether a download that is still running can ever be repaired.
///
/// Not advice, unlike UsenetHealth: this one may stop a download, so it only
/// ever answers from a *bound*. At least this many blocks are damaged, the
/// release carries at most that many, and only when the first exceeds the
/// second is the answer "unrepairable". Every figure that could be wrong is
/// taken in the direction that keeps downloading:
///
///   - Damage is counted only for a file every one of whose articles has been
///     resolved, and priced from the size the PAR2 set declares minus what
///     arrived — never from the NZB's `bytes`, which is the encoded size and
///     which real NZBs misstate.
///   - Recovery capacity is counted for *every* file the set does not cover as
///     a source, by its size rather than by a `.vol00+01.par2` name, because
///     an obfuscated post names its volumes nothing of the kind.
///   - A file that cannot be matched to the set contributes no damage at all.

#include "post/Par2Verifier.h"

#include <QList>

namespace eMule::usenet {

class UsenetQueueItem;

struct UsenetRepairEstimate {
    enum class Verdict : quint8 {
        Unknown,        ///< nothing to judge from: no block size, no matched file, no sizes
        Repairable,     ///< as far as the bounds can tell
        Unrepairable,   ///< damagedBlocks > recoveryBlocks, provably
    };

    Verdict verdict = Verdict::Unknown;
    qint64 damagedBlocks = 0;    ///< at least this many
    qint64 recoveryBlocks = 0;   ///< at most this many
};

/// Judge @p item against the recovery set its index .par2 describes.
///
/// @p blockSize and @p setFiles are Par2FileList's, read without verifying
/// anything; a zero block size or an empty list is Unknown.
[[nodiscard]] UsenetRepairEstimate estimateRepair(const UsenetQueueItem& item, qint64 blockSize,
                                                  const QList<Par2SetFile>& setFiles);

} // namespace eMule::usenet
