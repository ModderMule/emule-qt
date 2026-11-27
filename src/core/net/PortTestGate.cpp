/// @file PortTestGate.cpp
/// @brief The window of a running eD2K port test — implementation.

#include "net/PortTestGate.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

namespace eMule::PortTestGate {

namespace {

using Clock = std::chrono::steady_clock;

/// a flood of filtered peers must not grow the list without end
constexpr std::size_t kMaxHits = 16;

struct State {
    std::mutex mutex;
    std::vector<Address> exempt;
    std::vector<Hit> hits;
    Clock::time_point deadline;
};

State& state()
{
    static State s;
    return s;
}

/// fast path for every packet while no test runs
std::atomic_bool s_open{false};

bool openLocked(const State& s)
{
    return s_open.load(std::memory_order_relaxed) && Clock::now() < s.deadline;
}

} // namespace

void open(std::vector<Address> exempt, int durationMs)
{
    State& s = state();
    std::scoped_lock lock(s.mutex);
    s.exempt = std::move(exempt);
    s.hits.clear();
    s.deadline = Clock::now() + std::chrono::milliseconds(durationMs);
    s_open.store(true, std::memory_order_relaxed);
}

std::vector<Hit> close()
{
    State& s = state();
    std::scoped_lock lock(s.mutex);
    s_open.store(false, std::memory_order_relaxed);
    s.exempt.clear();
    return std::exchange(s.hits, {});
}

bool isExempt(const Address& address)
{
    if (!s_open.load(std::memory_order_relaxed))
        return false;
    State& s = state();
    std::scoped_lock lock(s.mutex);
    return openLocked(s) && std::ranges::find(s.exempt, address) != s.exempt.end();
}

void noteFiltered(const Address& address, const QString& description, bool tcp)
{
    if (!s_open.load(std::memory_order_relaxed))
        return;
    State& s = state();
    std::scoped_lock lock(s.mutex);
    if (!openLocked(s) || s.hits.size() >= kMaxHits)
        return;
    const bool known = std::ranges::any_of(s.hits, [&](const Hit& h) { return h.address == address && h.tcp == tcp; });
    if (!known)
        s.hits.push_back({address, description, tcp});
}

} // namespace eMule::PortTestGate
