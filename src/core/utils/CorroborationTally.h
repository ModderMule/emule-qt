#pragma once

/// @file CorroborationTally.h
/// @brief Distinct-voter agreement over claims we cannot verify ourselves.
///
/// Some facts about *us* can only be learned from someone else — the public address a
/// server or a peer says it sees us on. A single such claim is one unauthenticated
/// assertion; several independent hosts making the same claim is evidence. This tracks
/// "N distinct voters reported the same value inside a freshness window" so callers can
/// act on the second form and ignore the first.
///
/// One voter is one vote. A repeat from a voter already on record refreshes its
/// timestamp and never counts twice, so a single chatty host cannot manufacture a
/// majority. The voter key must therefore be the *observed* remote address of whoever
/// made the claim, never anything it declared about itself.
///
/// Validation of the candidate, and all logging, stay with the caller — this class has
/// no opinion about what a plausible value looks like.
///
/// Header-only.

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace eMule {

/// What becomes of an adopted candidate once the votes behind it age out.
enum class CorroborationMode : uint8 {
    /// Dropped, like any other candidate that no longer meets the threshold. Right when
    /// evidence keeps arriving, so an adopted value that stops being re-confirmed is
    /// genuinely stale.
    Reelect,
    /// Kept until a *different* candidate reaches the threshold. Right when the evidence
    /// arrives in bursts far apart — there the votes are guaranteed to expire long before
    /// fresh ones exist, and falling back to "unknown" would be strictly worse than
    /// keeping the last thing N voters agreed on.
    Sticky,
};

struct CorroborationOutcome {
    /// adopted() differs from what it was before the recompute() that returned this.
    bool        adoptedChanged = false;
    /// Distinct fresh voters behind the leading candidate — reported whether or not the
    /// leader reached the threshold, so callers can log "2/3 agree".
    std::size_t bestCount      = 0;
};

/// @tparam Candidate the claimed value; needs a strict weak ordering (std::map key).
/// @tparam Voter     identity of who claimed it; likewise ordered.
template <typename Candidate, typename Voter>
class CorroborationTally {
public:
    explicit CorroborationTally(CorroborationMode mode = CorroborationMode::Reelect) noexcept
        : m_mode(mode)
    {
    }

    /// Register or refresh @p voter's vote for @p candidate. Does not elect — election is
    /// a separate step so the caller can supply live threshold/window values and react to
    /// the outcome in one place.
    ///
    /// A voter speaks for one value at a time, so this retracts any vote it had cast for a
    /// different candidate: a host that now names a new address has *changed* its claim, not
    /// added one. Without the retraction a single host would prop up two candidates at once,
    /// and after our address really changed the stale candidate would keep the vote count it
    /// no longer deserves until the window expired.
    void record(const Candidate& candidate, const Voter& voter, std::int64_t nowMs)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->first == candidate) {
                ++it;
                continue;
            }
            it->second.voters.erase(voter);
            if (it->second.voters.empty())
                it = m_entries.erase(it);
            else
                ++it;
        }
        m_entries[candidate].voters[voter] = nowMs;
    }

    /// Expire votes older than @p windowMs, drop candidates left with none, then elect the
    /// candidate backed by the most distinct voters once it reaches @p threshold.
    ///
    /// Threshold and window are parameters rather than state so they stay readable from
    /// preferences at the call site and take effect immediately when changed.
    CorroborationOutcome recompute(std::int64_t nowMs, std::size_t threshold,
                                   std::int64_t windowMs)
    {
        CorroborationOutcome out;

        auto best = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            auto& voters = it->second.voters;
            for (auto vit = voters.begin(); vit != voters.end();) {
                if (nowMs - vit->second > windowMs)
                    vit = voters.erase(vit);
                else
                    ++vit;
            }
            if (voters.empty()) {
                it = m_entries.erase(it);   // takes the warn flag with it
                continue;
            }
            // Strict >: on a tie the first in map order keeps the lead, so the winner is
            // a pure function of the vote state and not of arrival order.
            if (voters.size() > out.bestCount) {
                out.bestCount = voters.size();
                best = it;
            }
            ++it;
        }

        std::optional<Candidate> next;
        if (best != m_entries.end() && out.bestCount >= threshold)
            next = best->first;
        else if (m_mode == CorroborationMode::Sticky)
            next = m_adopted;               // hold what we had; only a winner replaces it

        if (next != m_adopted) {
            m_adopted = std::move(next);
            out.adoptedChanged = true;
        }
        return out;
    }

    /// The adopted candidate, empty until the threshold has been met at least once.
    /// In Sticky mode this is held by value and survives its own votes expiring, so
    /// voterCount(*adopted()) may legitimately be 0.
    [[nodiscard]] const std::optional<Candidate>& adopted() const noexcept { return m_adopted; }

    /// Distinct voters currently on record for @p candidate; 0 if unknown.
    [[nodiscard]] std::size_t voterCount(const Candidate& candidate) const
    {
        const auto it = m_entries.find(candidate);
        return it == m_entries.end() ? 0u : it->second.voters.size();
    }

    /// True the first time it is called for @p candidate, false afterwards — a one-log-line
    /// latch whose lifetime is the candidate's, so it cannot grow without bound. An unknown
    /// candidate reports false. Mutating, hence [[nodiscard]]: put it last in an && chain.
    [[nodiscard]] bool markWarnedOnce(const Candidate& candidate)
    {
        const auto it = m_entries.find(candidate);
        if (it == m_entries.end() || it->second.warned)
            return false;
        it->second.warned = true;
        return true;
    }

    /// Drop whole candidates for which @p pred(candidate, voterCount) is true. Does not
    /// re-elect — call recompute() next. Returns how many candidates were dropped.
    template <typename Pred>
    std::size_t eraseCandidatesIf(Pred pred)
    {
        std::size_t dropped = 0;
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (pred(it->first, it->second.voters.size())) {
                it = m_entries.erase(it);
                ++dropped;
            } else {
                ++it;
            }
        }
        return dropped;
    }

    /// Forget every vote *and* the adopted value.
    void clear() noexcept
    {
        m_entries.clear();
        m_adopted.reset();
    }

private:
    struct Entry {
        std::map<Voter, std::int64_t> voters;   ///< voter -> last-seen tick (ms)
        bool                          warned = false;
    };

    std::map<Candidate, Entry> m_entries;
    std::optional<Candidate>   m_adopted;
    CorroborationMode          m_mode;
};

} // namespace eMule
