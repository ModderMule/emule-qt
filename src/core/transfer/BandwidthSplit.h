#pragma once

/// @file BandwidthSplit.h
/// @brief One bandwidth ceiling divided between two consumers by demand.
///
/// Pure arithmetic, shared by every split the daemon runs: eD2K/Usenet download,
/// (eD2K+Usenet)/BitTorrent download and eD2K/BitTorrent upload. A group of engines
/// takes part as one consumer through EngineDemand::combine, which is how a
/// lower-priority engine is nested under the ones above it.

#include <QtGlobal>

namespace eMule {

/// What one consumer is asking of the shared line.
struct EngineDemand {
    /// Has work that wants bandwidth right now. Structural, never derived from the
    /// rate: an engine throttled to a trickle would otherwise read as uninterested
    /// and never get its share back.
    bool active = false;
    qint64 rateBytesPerSec = 0;     ///< measured over ~2 s
    qint64 recentBytesPerSec = 0;   ///< measured over ~0.5 s; spots an engine pressing its cap

    /// Two engines acting as one consumer: active if either is, rates summed.
    [[nodiscard]] static EngineDemand combine(const EngineDemand& a, const EngineDemand& b)
    {
        return {a.active || b.active, a.rateBytesPerSec + b.rateBytesPerSec,
                a.recentBytesPerSec + b.recentBytesPerSec};
    }
};

/// What a split remembers between ticks: each side's last reserve and cap, so a
/// side pressing its cap can claim its share in a few ticks instead of 25 % at a time.
struct PairState {
    qint64 firstReserveKb = 0;
    qint64 secondReserveKb = 0;
    qint64 firstCapKb = 0;          ///< 0 = none published yet
    qint64 secondCapKb = 0;

    bool operator==(const PairState&) const = default;
};

/// A ceiling divided between two sides. All values KB/s.
struct PairSplit {
    qint64 ceilingKb = 0;           ///< 0 = unlimited: nothing divided, both caps 0
    qint64 firstCapKb = 0;          ///< 0 = unlimited (only when ceilingKb is 0)
    qint64 secondCapKb = 0;

    bool operator==(const PairSplit&) const = default;
};

/// Split @p ceilingKb between two sides. @p secondSharePercent (1–99) is the
/// second side's floor while both are busy; the first gets the rest.
///
/// The share is a floor while both sides are busy, never a cap on one whose
/// counterpart is idle or under-using: each side is held back only because the
/// other is active, and only by what the other measurably reserves. Pure apart
/// from @p state (may be null = stateless).
[[nodiscard]] PairSplit computePairSplit(qint64 ceilingKb, int secondSharePercent,
                                         const EngineDemand& first, const EngineDemand& second,
                                         PairState* state = nullptr);

/// The download line divided between all three engines. KB/s.
struct DownloadPlan {
    /// Preferences::setEd2kDownloadBudget(); -1 = no rival busy, the raw ceiling
    qint64 ed2kBudgetKb = -1;
    /// 0 = unlimited (only when the ceiling is 0)
    qint64 usenetCapKb = 0;
    qint64 btCapKb = 0;

    bool operator==(const DownloadPlan&) const = default;
};

struct DownloadPlanState {
    PairState bt;           ///< eD2K+Usenet first, BitTorrent second
    PairState usenet;       ///< eD2K first, Usenet second

    bool operator==(const DownloadPlanState&) const = default;
};

/// Priority eD2K > Usenet > BitTorrent: BitTorrent is split against eD2K and Usenet
/// together, then what they get is split between them by @p usenetSharePercent.
/// @p ceilingKb 0 = unlimited.
[[nodiscard]] DownloadPlan planDownload(qint64 ceilingKb, int usenetSharePercent,
                                        int btSharePercent, const EngineDemand& ed2k,
                                        const EngineDemand& usenet, const EngineDemand& bt,
                                        DownloadPlanState* state = nullptr);

/// The upload line divided between eD2K and BitTorrent (Usenet does not upload). KB/s.
struct UploadPlan {
    /// Preferences::setEd2kUploadBudget(); -1 = BitTorrent idle, the whole line
    qint64 ed2kBudgetKb = -1;
    /// 0 = unlimited (only when the line is)
    qint64 btCapKb = 0;

    bool operator==(const UploadPlan&) const = default;
};

/// @p lineKb 0 = unlimited.
[[nodiscard]] UploadPlan planUpload(qint64 lineKb, int btSharePercent, const EngineDemand& ed2k,
                                    const EngineDemand& bt, PairState* state = nullptr);

} // namespace eMule
