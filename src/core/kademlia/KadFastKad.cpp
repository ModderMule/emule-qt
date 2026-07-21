#include "pch.h"
/// @file KadFastKad.cpp
/// @brief Adaptive Kademlia timeout estimator.
/// Ported from eMuleAI FastKad.cpp

#include "kademlia/KadFastKad.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "prefs/Preferences.h"

#include <algorithm>
#include <cmath>

namespace {
bool isDisabled()
{
    return !eMule::thePrefs.useFastKad()
        || (eMule::kad::Kademlia::instance() && eMule::kad::Kademlia::instance()->isRunningInLANMode());
}
} // anonymous namespace

namespace eMule::kad {

FastKad::FastKad() noexcept
{
    // No placeholder seed: an empty pool already reports the 1000 ms default via
    // m_estMaxResponseTimeMs (header init), and recalculateResponseTime() only runs after a
    // real sample. Seeding an IP-0 entry here would either be swallowed when prefs aren't
    // loaded yet (isDisabled() true) or, when inserted, bias the mean toward 1000 ms and
    // never get evicted (5-min protect window + capacity 100).
}

FastKad::~FastKad() noexcept
{
    for (auto& [k, v] : m_responseTimes)
        delete v;
}

// ---------------------------------------------------------------------------
// addResponseTime — record a Kad peer's response time
// ---------------------------------------------------------------------------

void FastKad::addResponseTime(uint32 ip, double responseTimeMs) noexcept
{
    if (isDisabled())
        return;

    const auto now = std::chrono::steady_clock::now();
    ResponseTimeEntry* entry;
    auto it = m_responseTimes.find(ip);

    if (it == m_responseTimes.end()) {
        // Evict oldest entry if at capacity
        if (m_responseTimes.size() >= kMaxResponseTimes) {
            std::chrono::steady_clock::duration oldestAge{};
            auto itOldest = m_responseTimes.end();
            // Global-oldest fallback: the single oldest entry regardless of the protect
            // window, so a full pool of all-young entries (>100 distinct peers in 5 min)
            // still admits new samples instead of freezing the estimator.
            std::chrono::steady_clock::duration globalOldestAge{};
            auto itGlobalOldest = m_responseTimes.end();

            for (auto jt = m_responseTimes.begin(); jt != m_responseTimes.end(); ++jt) {
                auto age = now - jt->second->lastReferenced;
                if (itGlobalOldest == m_responseTimes.end() || age > globalOldestAge) {
                    globalOldestAge = age;
                    itGlobalOldest = jt;
                }
                if (age >= oldestAge && age > kProtectAge) {
                    oldestAge = age;
                    itOldest = jt;
                }
            }
            if (itOldest == m_responseTimes.end())
                itOldest = itGlobalOldest; // all young — evict the globally oldest
            if (itOldest != m_responseTimes.end()) {
                delete itOldest->second;
                m_responseTimes.erase(itOldest);
            } else {
                return; // pool empty (unreachable at capacity) — nothing to evict
            }
        }
        entry = new ResponseTimeEntry;
        entry->responseTimeMs = responseTimeMs;
    } else {
        entry = it->second;
        entry->responseTimeMs = responseTimeMs;
    }

    entry->lastReferenced = now;
    m_responseTimes[ip] = entry;

    recalculateResponseTime();
}

// ---------------------------------------------------------------------------
// shutdownCleanup — release all cached data
// ---------------------------------------------------------------------------

void FastKad::shutdownCleanup() noexcept
{
    for (auto& [k, v] : m_responseTimes)
        delete v;
    m_responseTimes.clear();
    m_mean = 0.0;
    m_variance = 0.0;
    m_estMaxResponseTimeMs = kDefaultResponseMs; // restore the safe default, not 0 ms
}

// ---------------------------------------------------------------------------
// recalculateResponseTime — compute mean + 2*sqrt(variance) + 100ms
// ---------------------------------------------------------------------------

void FastKad::recalculateResponseTime() noexcept
{
    const double count = static_cast<double>(m_responseTimes.size());
    const double missingCount = (count < kMaxResponseTimes)
        ? static_cast<double>(kMaxResponseTimes) - count
        : 0.0;
    const double total = static_cast<double>(kMaxResponseTimes);

    // Calculate mean (missing samples padded with kDefaultResponseMs)
    double sum = 0.0;
    for (const auto& [ip, entry] : m_responseTimes)
        sum += entry->responseTimeMs;
    sum += missingCount * kDefaultResponseMs;
    m_mean = sum / total;

    // Calculate variance
    double varianceSum = 0.0;
    if (m_responseTimes.size() > 1) {
        for (const auto& [ip, entry] : m_responseTimes) {
            double diff = entry->responseTimeMs - m_mean;
            varianceSum += diff * diff;
        }
    }
    // Each missing slot is assumed to sit at kDefaultResponseMs, so it contributes its
    // *squared* deviation from the mean — a variance sum accumulates squared deviations
    // (~10^6 scale), not the linear ~1000/sample the original (mis)ported padding added.
    const double missingDiff = kDefaultResponseMs - m_mean;
    varianceSum += missingCount * missingDiff * missingDiff;
    m_variance = varianceSum / (total - 1.0);

    // Estimate max expected response time with ~95% confidence + 100ms margin
    m_estMaxResponseTimeMs = std::min(kMaxTimeoutMs,
                                       m_mean + 2.0 * std::sqrt(m_variance) + 100.0);
}

} // namespace eMule::kad
