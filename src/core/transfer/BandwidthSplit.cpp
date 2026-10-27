#include "BandwidthSplit.h"

#include <algorithm>

namespace eMule {

namespace {

/// What a side holds back from the other: what it uses over ~2 s plus a quarter,
/// so the loan never becomes the thing capping it. Never more than its floor,
/// which is what makes the share hold once both are saturated.
///
/// The minimum is small (32 KB/s or 1/16 of the floor): a busy side at a trickle
/// used to keep a quarter of its floor idle. Restarting from there is the pressing
/// rule's job — a side at >= 90 % of its cap doubles its reserve each tick, so it
/// is back at its share within ~2 s.
qint64 reserve(const EngineDemand& d, qint64 floorKb, qint64 prevReserveKb, qint64 prevCapKb)
{
    if (!d.active)
        return 0;
    const qint64 avgKb = std::max<qint64>(0, d.rateBytesPerSec) / 1024;
    const qint64 recentKb = std::max<qint64>(0, d.recentBytesPerSec) / 1024;
    const qint64 minKb = std::min(floorKb, std::max<qint64>({1, 32, floorKb / 16}));
    qint64 want = std::max(avgKb + avgKb / 4, minKb);
    const bool pressing = prevCapKb > 0 && std::max(avgKb, recentKb) * 10 >= prevCapKb * 9;
    if (pressing)
        want = std::max(want, prevReserveKb * 2);
    return std::min(want, floorKb);
}

} // namespace

PairSplit computePairSplit(qint64 ceilingKb, int secondSharePercent, const EngineDemand& first,
                           const EngineDemand& second, PairState* state)
{
    PairSplit split;
    split.ceilingKb = std::max<qint64>(0, ceilingKb);

    // Unlimited: nothing to divide, both run free
    if (split.ceilingKb == 0) {
        if (state)
            *state = {};
        return split;
    }

    const qint64 ceiling = split.ceilingKb;
    const int percent = std::clamp(secondSharePercent, 1, 99);

    // Each floor at least 1 KB/s: a cap of 0 reads as unlimited to every consumer,
    // so a tiny ceiling must round up, never down.
    const qint64 secondFloor =
        std::clamp<qint64>(ceiling * percent / 100, 1, std::max<qint64>(1, ceiling - 1));
    const qint64 firstFloor = std::max<qint64>(1, ceiling - secondFloor);

    const qint64 firstReserve = reserve(first, firstFloor, state ? state->firstReserveKb : 0,
                                        state ? state->firstCapKb : 0);
    const qint64 secondReserve = reserve(second, secondFloor, state ? state->secondReserveKb : 0,
                                         state ? state->secondCapKb : 0);

    // Symmetric: each side is held back only by what the other reserves. An idle
    // side reserves nothing and lends its whole share.
    split.firstCapKb = std::max<qint64>(1, ceiling - secondReserve);
    split.secondCapKb = std::max<qint64>(1, ceiling - firstReserve);

    if (state) {
        state->firstReserveKb = firstReserve;
        state->secondReserveKb = secondReserve;
        state->firstCapKb = split.firstCapKb;
        state->secondCapKb = split.secondCapKb;
    }
    return split;
}

DownloadPlan planDownload(qint64 ceilingKb, int usenetSharePercent, int btSharePercent,
                          const EngineDemand& ed2k, const EngineDemand& usenet,
                          const EngineDemand& bt, DownloadPlanState* state)
{
    DownloadPlan plan;
    const PairSplit outer = computePairSplit(ceilingKb, btSharePercent,
                                             EngineDemand::combine(ed2k, usenet), bt,
                                             state ? &state->bt : nullptr);
    const PairSplit inner = computePairSplit(outer.firstCapKb, usenetSharePercent, ed2k, usenet,
                                             state ? &state->usenet : nullptr);
    plan.usenetCapKb = inner.secondCapKb;
    plan.btCapKb = outer.secondCapKb;
    // -1 tracks a ceiling change live (DownloadQueue re-reads it every 100 ms);
    // a number only while a rival holds something back
    if (outer.ceilingKb > 0 && (usenet.active || bt.active))
        plan.ed2kBudgetKb = inner.firstCapKb;
    return plan;
}

UploadPlan planUpload(qint64 lineKb, int btSharePercent, const EngineDemand& ed2k,
                      const EngineDemand& bt, PairState* state)
{
    UploadPlan plan;
    const PairSplit split = computePairSplit(lineKb, btSharePercent, ed2k, bt, state);
    plan.btCapKb = split.secondCapKb;
    if (split.ceilingKb > 0 && bt.active)
        plan.ed2kBudgetKb = split.firstCapKb;
    return plan;
}

} // namespace eMule
