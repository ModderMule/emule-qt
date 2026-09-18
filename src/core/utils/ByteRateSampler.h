#pragma once

/// @file ByteRateSampler.h
/// @brief A rate derived from a byte counter read at irregular intervals.
///
/// For when the producer reports a running total and no rate — a Usenet item,
/// whose engine measures itself as a whole. Shared by the GUI's queue model and
/// the web server so the two cannot disagree about what a row's speed is.

#include <QtGlobal>

namespace eMule {

struct ByteRateSampler {
    /// Readings closer together than this keep the previous rate: a poll and a
    /// push landing 20 ms apart would otherwise swing the figure wildly.
    static constexpr qint64 kMinWindowMs = 500;

    qint64 lastBytes = 0;
    qint64 lastMs = 0;
    qint64 rate = 0;

    /// Feed @p bytes as read at @p nowMs; returns bytes per second.
    qint64 update(qint64 bytes, qint64 nowMs)
    {
        if (lastMs == 0) {
            lastBytes = bytes;
            lastMs = nowMs;
            return rate;
        }
        const qint64 elapsed = nowMs - lastMs;
        if (elapsed >= kMinWindowMs) {
            const qint64 delta = bytes - lastBytes;
            rate = delta > 0 ? delta * 1000 / elapsed : 0;
            lastBytes = bytes;
            lastMs = nowMs;
        }
        return rate;
    }
};

} // namespace eMule
