#pragma once

/// @file QueuePositions.h
/// @brief One estimated order over several upload queues (eD2K, ed3k node, ed3k anonymous),
/// for the On Queue list's Position column.
///
/// Each queue hands out its own slots, so its k-th waiter gets one after about k / s slot
/// turns of that queue (s = slots it holds now). Sorted by that, the queues interleave by
/// their slot counts: a queue with 2× the slots moves 2× as fast.

#include <QtGlobal>

#include <span>
#include <vector>

namespace eMule {

struct QueueWaiter {
    int queue = 0;        ///< index into slotCounts
    int rank = 0;         ///< 1-based place in its own queue; 0 = unknown, last
    qint64 waitedMs = 0;  ///< tie break: longer waiting first
};

/// 1-based positions, one per @p waiters entry in their order. @p slotCounts: slots each queue
/// holds now; fewer than 1 counts as 1 (an idle queue still turns over).
[[nodiscard]] std::vector<int> mergedQueuePositions(std::span<const QueueWaiter> waiters,
                                                    std::span<const int> slotCounts);

} // namespace eMule
