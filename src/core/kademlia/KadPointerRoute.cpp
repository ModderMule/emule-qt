#include "pch.h"
/// @file KadPointerRoute.cpp
/// @brief Pointer route of Kad source records (ipv6-spec §5.6.1).

#include "kademlia/KadPointerRoute.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadSearchManager.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/ArgStr.h"

#include <algorithm>
#include <iterator>

namespace eMule::kad {

std::map<UInt128, PointerRoute::Entry> PointerRoute::s_cache;
std::size_t PointerRoute::s_running = 0;
PointerRoute::LookupStarter PointerRoute::s_starter;
uint32 PointerRoute::s_pubBuddyIP = 0;
uint16 PointerRoute::s_pubBuddyPort = 0;
time_t PointerRoute::s_pubNext = 0;

namespace {

// Lookups started before the last clear() report into nothing
uint32 s_generation = 0;

} // namespace

void PointerRoute::resolve(const Kademlia::KadSourceResult& record)
{
    if (!record.clientHash || !record.fileHash || record.ip == 0 || record.tcpPort == 0
        || record.pointerVersion != KAD_POINTER_VERSION)
        return;

    HeldRecord rec;
    std::copy_n(record.fileHash, 16, rec.fileHash.begin());
    std::copy_n(record.clientHash, 16, rec.clientHash.begin());
    rec.ip = record.ip;
    rec.tcpPort = record.tcpPort;
    rec.udpPort = record.udpPort;
    rec.crypt = record.buddyCrypt;
    rec.seen = std::time(nullptr);

    const UInt128 sourceID(record.clientHash);
    if (!request(sourceID, rec.ip, rec.fileHash, std::nullopt, heldWaiter(rec)))
        hold(sourceID, rec);   // cached miss: wait for the retry, not for the next Kad search
}

bool PointerRoute::reroute(const UInt128& sourceID, uint32 sourceIP,
                           const std::array<uint8, 16>& fileHash, BuddyEndpoint failed,
                           Waiter waiter)
{
    if (sourceIP == 0 || !waiter)
        return false;
    return request(sourceID, sourceIP, fileHash, failed, std::move(waiter));
}

void PointerRoute::publishIfDue()
{
    auto* kad = Kademlia::instance();
    auto* prefs = Kademlia::getInstancePrefs();
    if (!kad || !kad->isRunning() || !prefs)
        return;

    Search::SourcePublishParams sp;
    Search::fillSourceReachability(sp);
    if (!sp.firewalled || sp.directUDPCallback || !sp.hasBuddy || sp.buddyIP == 0) {
        // No pointer to give; the old one expires (§5.6.1)
        s_pubBuddyIP = 0;
        s_pubBuddyPort = 0;
        return;
    }
    const time_t now = std::time(nullptr);
    if (sp.buddyIP == s_pubBuddyIP && sp.buddyUDPPort == s_pubBuddyPort && now < s_pubNext)
        return;

    auto* search = SearchManager::prepareLookup(SearchType::StoreFile, false,
                                                Search::pointerKey(prefs->clientHash()));
    if (!search)
        return;   // one still running: next tick
    search->setPointerPublish(true);
    search->setGUIName(QStringLiteral("Buddy pointer"));
    if (!SearchManager::startSearch(search)) {
        delete search;
        return;
    }
    s_pubBuddyIP = sp.buddyIP;
    s_pubBuddyPort = sp.buddyUDPPort;
    s_pubNext = now + KADEMLIAREPUBLISHTIMES;
    logKad(QStringLiteral("Kad: publishing buddy pointer → %1:%2").arg(ipstr(sp.buddyIP)).arg(sp.buddyUDPPort));
}

void PointerRoute::clear()
{
    ++s_generation;
    // Waiters hear the end, or a rerouted source would wait forever
    std::vector<Waiting> waiting;
    for (auto& [id, e] : s_cache)
        std::ranges::move(e.waiting, std::back_inserter(waiting));
    s_cache.clear();
    s_running = 0;
    s_pubBuddyIP = 0;
    s_pubBuddyPort = 0;
    s_pubNext = 0;
    for (const Waiting& w : waiting)
        w.waiter(std::nullopt);
}

time_t PointerRoute::cachedFor(const UInt128& sourceID)
{
    auto it = s_cache.find(sourceID);
    if (it == s_cache.end())
        return 0;
    return std::max<time_t>(0, it->second.expires - std::time(nullptr));
}

std::size_t PointerRoute::heldFor(const UInt128& sourceID)
{
    auto it = s_cache.find(sourceID);
    return it == s_cache.end() ? 0 : it->second.held.size();
}

void PointerRoute::expireNow(const UInt128& sourceID)
{
    if (auto it = s_cache.find(sourceID); it != s_cache.end())
        it->second.expires = 0;
}

void PointerRoute::process()
{
    const time_t now = std::time(nullptr);
    prune(now);
    // Held type 7 records: one new lookup per source once its miss expired (§5.6)
    for (auto& [sourceID, e] : s_cache) {
        if (s_running >= kMaxLookups)
            break;
        if (e.lookupRunning || e.held.empty() || e.expires > now)
            continue;
        std::vector<HeldRecord> held = std::move(e.held);
        e.held.clear();
        e.candidates.clear();   // a miss has none; old buddies stay in failed
        e.waiting.clear();
        for (const HeldRecord& rec : held)
            e.waiting.push_back({rec.fileHash, heldWaiter(rec)});
        if (!launch(sourceID, e)) {
            // Next tick; the records stay held
            e.waiting.clear();
            e.held = std::move(held);
            continue;
        }
        logKad(QStringLiteral("Kad: retrying buddy pointer for %1, %2 held record(s)")
                   .arg(argStr(sourceID.toHexString()), argStr(held.size())));
    }
}

bool PointerRoute::request(const UInt128& sourceID, uint32 sourceIP,
                           const std::array<uint8, 16>& fileHash,
                           std::optional<BuddyEndpoint> failed, Waiter waiter)
{
    const time_t now = std::time(nullptr);
    prune(now);

    if (auto it = s_cache.find(sourceID); it != s_cache.end()) {
        Entry& e = it->second;
        // Another publisher IP than the one checked: not this pointer (§5.6.2)
        if (e.sourceIP != sourceIP)
            return false;
        if (failed && std::ranges::find(e.failed, *failed) == e.failed.end())
            e.failed.push_back(*failed);
        if (const Search::PointerHit* c = untried(e)) {
            const Search::PointerHit hit = *c;   // the waiter may touch the cache
            waiter(hit);
            return true;
        }
        if (!e.lookupRunning)
            return false;   // cached miss, or every candidate tried
        auto w = std::ranges::find(e.waiting, fileHash, &Waiting::fileHash);
        if (w != e.waiting.end())
            w->waiter = std::move(waiter);
        else if (e.waiting.size() < kMaxWaitersPerSource)
            e.waiting.push_back({fileHash, std::move(waiter)});
        else
            return false;
        return true;
    }

    // One lookup per source (§5.6); over the cap nothing is cached
    if (s_running >= kMaxLookups || s_cache.size() >= kMaxCacheEntries)
        return false;

    Entry& e = s_cache[sourceID];
    e.sourceIP = sourceIP;
    if (failed)
        e.failed.push_back(*failed);
    e.waiting.push_back({fileHash, std::move(waiter)});
    if (!launch(sourceID, e)) {
        s_cache.erase(sourceID);
        return false;
    }
    return true;
}

bool PointerRoute::launch(const UInt128& sourceID, Entry& e)
{
    e.lookupRunning = true;
    e.expires = std::time(nullptr) + kCacheSecs;
    ++s_running;
    const uint32 gen = s_generation;
    const bool started = startLookup(Search::pointerKey(sourceID), sourceID, e.sourceIP,
        [sourceID, gen](std::optional<Search::PointerHit> hit) {
            if (gen == s_generation)
                onLookupResult(sourceID, hit);
        });
    if (!started) {
        --s_running;
        e.lookupRunning = false;
    }
    return started;
}

PointerRoute::Waiter PointerRoute::heldWaiter(const HeldRecord& rec)
{
    return [rec](std::optional<Search::PointerHit> hit) {
        if (!hit) {
            hold(UInt128(rec.clientHash.data()), rec);
            return;
        }
        Kademlia::KadSourceResult r;
        r.fileHash   = rec.fileHash.data();
        r.ip         = rec.ip;
        r.tcpPort    = rec.tcpPort;
        r.udpPort    = rec.udpPort;
        r.buddyCrypt = rec.crypt;
        r.clientHash = rec.clientHash.data();
        deliver(r, *hit);
    };
}

bool PointerRoute::hold(const UInt128& sourceID, const HeldRecord& rec)
{
    auto it = s_cache.find(sourceID);
    if (it == s_cache.end())
        return false;
    Entry& e = it->second;
    // Only through a miss: a lookup still running, or candidates left, need no hold
    if (e.sourceIP != rec.ip || e.lookupRunning || !e.candidates.empty())
        return false;
    if (auto h = std::ranges::find(e.held, rec.fileHash, &HeldRecord::fileHash); h != e.held.end()) {
        h->seen = rec.seen;
        return true;
    }
    if (e.held.size() >= kMaxWaitersPerSource)
        return false;
    e.held.push_back(rec);
    return true;
}

const Search::PointerHit* PointerRoute::untried(const Entry& e)
{
    for (const auto& c : e.candidates) {
        if (std::ranges::find(e.failed, BuddyEndpoint{c.buddyIP, c.buddyPort}) == e.failed.end())
            return &c;
    }
    return nullptr;
}

void PointerRoute::onLookupResult(const UInt128& sourceID, std::optional<Search::PointerHit> hit)
{
    auto it = s_cache.find(sourceID);
    if (it == s_cache.end() || !it->second.lookupRunning)
        return;
    Entry& e = it->second;

    if (hit) {
        // Each distinct pointer up to the cap: a planted record neither wins nor vetoes
        const bool known = std::ranges::any_of(e.candidates, [&](const Search::PointerHit& c) {
            return c.buddyIP == hit->buddyIP && c.buddyPort == hit->buddyPort;
        });
        if (known || e.candidates.size() >= kMaxCandidates)
            return;
        e.candidates.push_back(*hit);
        if (std::ranges::find(e.failed, BuddyEndpoint{hit->buddyIP, hit->buddyPort}) != e.failed.end())
            return;
        std::vector<Waiting> waiting = std::move(e.waiting);
        e.waiting.clear();
        for (const Waiting& w : waiting)
            w.waiter(*hit);
        return;
    }

    // Lookup ended
    if (s_running > 0)
        --s_running;
    e.lookupRunning = false;
    // A miss expires soon: the publisher may get its buddy any minute
    e.expires = std::time(nullptr) + (e.candidates.empty() ? kMissCacheSecs : kCacheSecs);
    std::vector<Waiting> waiting = std::move(e.waiting);
    e.waiting.clear();
    if (e.candidates.empty())
        logKad(QStringLiteral("Kad: no buddy pointer for %1, %2 waiting record(s), retry in %3 s")
                   .arg(argStr(sourceID.toHexString()), argStr(waiting.size()), argStr(kMissCacheSecs)));
    for (const Waiting& w : waiting)
        w.waiter(std::nullopt);
}

void PointerRoute::deliver(const Kademlia::KadSourceResult& rec, const Search::PointerHit& hit)
{
    const auto& cb = Kademlia::kadSourceResultCallback();
    if (!cb)
        return;
    // An ordinary type 3 source from here on: callback via the buddy, pointer again if it fails
    Kademlia::KadSourceResult r = rec;
    r.buddyIP        = hit.buddyIP;
    r.buddyPort      = hit.buddyPort;
    r.sourceType     = 3;
    r.buddyHash      = hit.buddyHash.data();
    r.pointerVersion = KAD_POINTER_VERSION;
    cb(r);
}

void PointerRoute::prune(time_t now)
{
    for (auto& [id, e] : s_cache)
        std::erase_if(e.held, [now](const HeldRecord& h) { return h.seen + kHoldSecs <= now; });
    // An expired entry with held records stays for process() to retry
    std::erase_if(s_cache, [now](const auto& kv) {
        return !kv.second.lookupRunning && kv.second.expires <= now && kv.second.held.empty();
    });
}

bool PointerRoute::startLookup(const UInt128& key, const UInt128& sourceID, uint32 sourceIP,
                               Search::PointerDone done)
{
    if (s_starter)
        return s_starter(key, sourceID, sourceIP, std::move(done));
    auto* search = SearchManager::prepareLookup(SearchType::File, false, key);
    if (!search)
        return false;
    search->setPointerLookup(sourceID, sourceIP, std::move(done));
    search->setGUIName(QStringLiteral("Buddy pointer %1").arg(sourceID.toHexString()));
    if (!SearchManager::startSearch(search)) {
        // Destructor would report the end of a lookup that never ran: disarm first
        search->setPointerLookup(sourceID, sourceIP, nullptr);
        delete search;
        return false;
    }
    return true;
}

} // namespace eMule::kad
