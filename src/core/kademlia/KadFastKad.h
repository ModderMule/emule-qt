/// @file KadFastKad.h
/// @brief Adaptive Kademlia timeout estimator.
/// Ported from eMuleAI FastKad.h

#pragma once

#include "utils/Types.h"

#include <chrono>
#include <map>

namespace eMule::kad {

class FastKad
{
public:
    FastKad() noexcept;
    ~FastKad() noexcept;

    void addResponseTime(uint32 ip, double responseTimeMs) noexcept;
    [[nodiscard]] double getEstMaxResponseTimeMs() const noexcept { return m_estMaxResponseTimeMs; }
    void shutdownCleanup() noexcept;

private:
    void recalculateResponseTime() noexcept;

    struct ResponseTimeEntry
    {
        double responseTimeMs = 0.0;
        std::chrono::steady_clock::time_point lastReferenced;
    };

    static constexpr size_t kMaxResponseTimes = 100;
    static constexpr double kDefaultResponseMs = 1000.0;
    static constexpr double kMaxTimeoutMs      = 3000.0;
    static constexpr auto   kProtectAge        = std::chrono::minutes{5};

    std::map<uint32, ResponseTimeEntry*> m_responseTimes;
    double m_mean                  = 0.0;
    double m_variance              = 0.0;
    double m_estMaxResponseTimeMs  = kDefaultResponseMs;
};

} // namespace eMule::kad
