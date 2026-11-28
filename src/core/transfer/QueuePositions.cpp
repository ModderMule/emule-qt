#include "pch.h"
/// @file QueuePositions.cpp
/// @brief One estimated order over several upload queues — implementation.

#include "transfer/QueuePositions.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace eMule {

std::vector<int> mergedQueuePositions(std::span<const QueueWaiter> waiters, std::span<const int> slotCounts)
{
    // unknown rank: after everyone of its queue
    std::vector<int> queueSize(slotCounts.size(), 0);
    for (const auto& w : waiters)
        if (w.queue >= 0 && std::cmp_less(w.queue, queueSize.size()))
            ++queueSize[static_cast<size_t>(w.queue)];

    std::vector<double> turns(waiters.size());
    for (size_t i = 0; i < waiters.size(); ++i) {
        const auto& w = waiters[i];
        if (w.queue < 0 || std::cmp_greater_equal(w.queue, slotCounts.size())) {
            turns[i] = std::numeric_limits<double>::max();
            continue;
        }
        const auto q = static_cast<size_t>(w.queue);
        const int rank = w.rank > 0 ? w.rank : queueSize[q] + 1;
        turns[i] = double(rank) / double(std::max(1, slotCounts[q]));
    }

    std::vector<size_t> order(waiters.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
        if (turns[a] != turns[b])
            return turns[a] < turns[b];
        return waiters[a].waitedMs > waiters[b].waitedMs;
    });

    std::vector<int> pos(waiters.size(), 0);
    for (size_t i = 0; i < order.size(); ++i)
        pos[order[i]] = static_cast<int>(i + 1);
    return pos;
}

} // namespace eMule
