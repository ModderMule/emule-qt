#include "pch.h"
/// @file FruitlessSessionLedger.cpp
/// @brief Per-peer record of download sessions that gave nothing.

#include "client/FruitlessSessionLedger.h"

#include <algorithm>
#include <cstring>

namespace eMule {

namespace {

FruitlessSessionLedger::Hash toKey(const uint8* userHash)
{
    FruitlessSessionLedger::Hash key;
    std::memcpy(key.data(), userHash, key.size());
    return key;
}

} // namespace

FruitlessSessionLedger::Verdict FruitlessSessionLedger::noteFruitlessSession(const uint8* userHash,
                                                                            uint64 now)
{
    Entry& e = entry(userHash, now);
    if (countEvent(e, now))
        return quarantine(e, now);
    if (e.heldUntil > now)
        return e.quarantined ? Verdict::Quarantined : Verdict::Paused;

    if (e.lastFruitless != 0 && now - e.lastFruitless <= kWindowMs) {
        e.lastFruitless = 0;
        if (++e.pauses >= kPausesToQuarantine)
            return quarantine(e, now);
        e.heldUntil = now + kPauseMs;
        return Verdict::Paused;
    }
    e.lastFruitless = now;
    return Verdict::Noted;
}

FruitlessSessionLedger::Verdict FruitlessSessionLedger::noteDeclinedAccept(const uint8* userHash,
                                                                          uint64 now)
{
    Entry& e = entry(userHash, now);
    if (countEvent(e, now))
        return quarantine(e, now);
    if (e.heldUntil > now)
        return e.quarantined ? Verdict::Quarantined : Verdict::Paused;
    return Verdict::Noted;
}

void FruitlessSessionLedger::noteProductiveSession(const uint8* userHash)
{
    const auto it = m_entries.find(toKey(userHash));
    if (it == m_entries.end())
        return;
    Entry& e = it->second;
    e.lastFruitless = 0;
    e.windowStart = 0;
    e.windowEvents = 0;
    if (!e.quarantined)
        e.pauses = 0;
}

uint64 FruitlessSessionLedger::heldBackFor(const uint8* userHash, uint64 now) const
{
    const Entry* e = find(userHash);
    return (e && e->heldUntil > now) ? e->heldUntil - now : 0;
}

bool FruitlessSessionLedger::isQuarantined(const uint8* userHash, uint64 now) const
{
    const Entry* e = find(userHash);
    return e && e->quarantined && e->heldUntil > now;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

std::size_t FruitlessSessionLedger::HashOf::operator()(const Hash& h) const noexcept
{
    // A user hash is random already.
    std::size_t v = 0;
    std::memcpy(&v, h.data(), sizeof(v));
    return v;
}

FruitlessSessionLedger::Entry& FruitlessSessionLedger::entry(const uint8* userHash, uint64 now)
{
    const Hash key = toKey(userHash);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        makeRoom(now);
        it = m_entries.emplace(key, Entry{}).first;
    }
    Entry& e = it->second;
    // A served quarantine is over; the peer starts clean.
    if (e.quarantined && e.heldUntil <= now)
        e = Entry{};
    e.lastUsed = now;
    return e;
}

const FruitlessSessionLedger::Entry* FruitlessSessionLedger::find(const uint8* userHash) const
{
    const auto it = m_entries.find(toKey(userHash));
    return it == m_entries.end() ? nullptr : &it->second;
}

bool FruitlessSessionLedger::countEvent(Entry& e, uint64 now)
{
    if (e.windowStart == 0 || now - e.windowStart >= kWindowMs) {
        e.windowStart = now;
        e.windowEvents = 0;
    }
    return ++e.windowEvents >= kEventsToQuarantine && !e.quarantined;
}

FruitlessSessionLedger::Verdict FruitlessSessionLedger::quarantine(Entry& e, uint64 now)
{
    e.quarantined = true;
    e.heldUntil = now + kQuarantineMs;
    e.lastFruitless = 0;
    return Verdict::Quarantined;
}

void FruitlessSessionLedger::makeRoom(uint64 now)
{
    if (m_entries.size() < kMaxEntries)
        return;
    std::erase_if(m_entries, [now](const auto& kv) {
        return now - kv.second.lastUsed >= kEntryTtlMs && kv.second.heldUntil <= now;
    });
    if (m_entries.size() < kMaxEntries)
        return;
    const auto oldest = std::ranges::min_element(m_entries, {}, [](const auto& kv) {
        return kv.second.lastUsed;
    });
    m_entries.erase(oldest);
}

} // namespace eMule
