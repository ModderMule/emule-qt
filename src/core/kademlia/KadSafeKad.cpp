#include "pch.h"
/// @file KadSafeKad.cpp
/// @brief Kademlia node reputation system — Sybil attack defense.
/// Ported from eMuleAI SafeKad.cpp

#include "kademlia/KadSafeKad.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadLog.h"
#include "utils/Opcodes.h"
#include "kademlia/KadMiscUtils.h"
#include "prefs/Preferences.h"

namespace {
bool isDisabled()
{
    return !eMule::thePrefs.useSafeKad()
        || (eMule::kad::Kademlia::instance() && eMule::kad::Kademlia::instance()->isRunningInLANMode());
}
} // anonymous namespace

namespace eMule::kad {

SafeKad::SafeKad() noexcept
    : m_lastCleanup(time(nullptr))
{
}

SafeKad::~SafeKad() noexcept
{
    for (auto& [k, v] : m_trackedNodes) delete v;
    for (auto& [k, v] : m_bannedIPs)    delete v;
    for (auto& [k, v] : m_problematicNodes) delete v;
}

// ---------------------------------------------------------------------------
// trackNode — update or create a tracking record for a Kad node
// ---------------------------------------------------------------------------

void SafeKad::trackNode(uint32 ip, uint16 port, const UInt128& id, bool verified) noexcept
{
    if (isDisabled())
        return;
    if (isBanned(ip))
        return;

    const time_t now = time(nullptr);
    Tracked* tracked;
    auto it = m_trackedNodes.find({ip, port});

    if (it == m_trackedNodes.end()) {
        cleanup();
        if (m_trackedNodes.size() >= kMaxTrackedNodes)
            return;

        tracked = new (std::nothrow) Tracked;
        if (!tracked)
            return;
        tracked->lastID = id;
        tracked->lastIDChange = now;
        tracked->idVerified = verified;
        tracked->node = {ip, port};
    } else {
        tracked = it->second;
    }

    m_trackedAge.erase(*tracked);

    if (id != tracked->lastID && !(tracked->idVerified && !verified)) {
        if (now - tracked->lastIDChange < kMinIDChangeInterval && verified) {
            banIP(ip);
            m_trackedNodes.erase(tracked->node);
            delete tracked;
            return;
        }
        tracked->lastID = id;
        tracked->lastIDChange = now;
    }

    tracked->lastReferenced = now;
    if (!tracked->idVerified)
        tracked->idVerified = verified;
    m_trackedNodes[tracked->node] = tracked;
    m_trackedAge[*tracked] = tracked;
}

// ---------------------------------------------------------------------------
// trackProblematicNode — mark a node as problematic (non-responsive)
// ---------------------------------------------------------------------------

void SafeKad::trackProblematicNode(uint32 ip, uint16 port) noexcept
{
    if (isDisabled())
        return;
    if (isBanned(ip))
        return;

    const time_t now = time(nullptr);
    Problematic* prob;
    auto it = m_problematicNodes.find({ip, port});

    if (it == m_problematicNodes.end()) {
        cleanup();
        if (m_problematicNodes.size() >= kMaxProblematicNodes)
            return;

        prob = new (std::nothrow) Problematic;
        if (!prob)
            return;
        prob->failed = now;
        prob->node = {ip, port};
    } else {
        prob = it->second;
    }

    m_problematicAge.erase(*prob);
    prob->lastReferenced = now;
    m_problematicNodes[prob->node] = prob;
    m_problematicAge[*prob] = prob;
}

// ---------------------------------------------------------------------------
// banIP — ban an IP address
// ---------------------------------------------------------------------------

void SafeKad::banIP(uint32 ip) noexcept
{
    if (isDisabled())
        return;

    const time_t now = time(nullptr);
    cleanup();

    Banned* banned;
    auto it = m_bannedIPs.find(ip);
    if (it == m_bannedIPs.end()) {
        if (m_bannedIPs.size() >= kMaxBannedIPs)
            return;
        banned = new (std::nothrow) Banned;
        if (!banned)
            return;
        banned->ip = ip;
    } else {
        banned = it->second;
    }

    m_bannedAge.erase(*banned);
    banned->banned = now;
    banned->lastReferenced = now;
    m_bannedIPs[banned->ip] = banned;
    m_bannedAge[*banned] = banned;

    logKad(QStringLiteral("SafeKad: banned IP %1").arg(ipToString(ip)));
}

// ---------------------------------------------------------------------------
// isBadNode — primary gatekeeper for accepting Kad contacts
// ---------------------------------------------------------------------------

bool SafeKad::isBadNode(uint32 ip, uint16 port, const UInt128& id,
                         uint8 kadVersion, bool verified, bool onePerIP) noexcept
{
    if (isDisabled())
        return false;

    const time_t now = time(nullptr);
    if (now - m_lastCleanup > 600) // 10 minutes
        cleanup();

    auto itTracked = m_trackedNodes.find({ip, port});

    if (isBanned(ip)) {
        if (itTracked != m_trackedNodes.end()) {
            Tracked* p = itTracked->second;
            m_trackedNodes.erase(itTracked);
            m_trackedAge.erase(*p);
            delete p;
        }
        return true;
    }

    if (itTracked != m_trackedNodes.end()) {
        Tracked* tracked = itTracked->second;
        m_trackedAge.erase(*tracked);
        tracked->lastReferenced = now;
        m_trackedAge[*tracked] = tracked;

        if (tracked->lastID != id) {
            if ((tracked->idVerified || kadVersion < KADEMLIA_VERSION8_49b) && !verified)
                return true;
            trackNode(ip, port, id, verified);
            return isBanned(ip);
        }
    } else {
        if (onePerIP) {
            // Check if any node at this IP (any port) is already tracked
            auto lo = m_trackedNodes.lower_bound({ip, 0});
            auto hi = m_trackedNodes.lower_bound({ip, 0xFFFF});
            if (lo != hi)
                return true;
        }
        trackNode(ip, port, id, verified);
    }
    return false;
}

// ---------------------------------------------------------------------------
// isBanned — check if an IP is banned (also handles ban expiry)
// ---------------------------------------------------------------------------

bool SafeKad::isBanned(uint32 ip) noexcept
{
    if (isDisabled())
        return false;

    auto it = m_bannedIPs.find(ip);
    if (it != m_bannedIPs.end()) {
        Banned* banned = it->second;
        const time_t now = time(nullptr);
        m_bannedAge.erase(*banned);
        banned->lastReferenced = now;

        if (now - banned->banned > kMaxBanTime) {
            m_bannedIPs.erase(it);
            delete banned;
        } else {
            m_bannedAge[*banned] = banned;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// isProblematic — check if a node is marked problematic
// ---------------------------------------------------------------------------

bool SafeKad::isProblematic(uint32 ip, uint16 port) noexcept
{
    if (isDisabled())
        return false;

    auto it = m_problematicNodes.find({ip, port});
    if (it != m_problematicNodes.end()) {
        Problematic* prob = it->second;
        const time_t now = time(nullptr);
        m_problematicAge.erase(*prob);
        prob->lastReferenced = now;

        if (now - prob->failed > kMaxProblematicTime) {
            m_problematicNodes.erase(it);
            delete prob;
        } else {
            m_problematicAge[*prob] = prob;
            return true;
        }
    }
    return isBanned(ip);
}

// ---------------------------------------------------------------------------
// shutdownCleanup — release all cached data
// ---------------------------------------------------------------------------

void SafeKad::shutdownCleanup() noexcept
{
    for (auto& [k, v] : m_trackedNodes) delete v;
    for (auto& [k, v] : m_bannedIPs)    delete v;
    for (auto& [k, v] : m_problematicNodes) delete v;

    m_trackedNodes.clear();
    m_trackedAge.clear();
    m_bannedIPs.clear();
    m_bannedAge.clear();
    m_problematicNodes.clear();
    m_problematicAge.clear();
    m_lastCleanup = time(nullptr);
}

// ---------------------------------------------------------------------------
// cleanup — evict old entries from all three maps
// ---------------------------------------------------------------------------

void SafeKad::cleanup(time_t nodeMaxAge, time_t banMaxAge, time_t problemMaxAge) noexcept
{
    const time_t now = time(nullptr);
    m_lastCleanup = now;

    // Evict old tracked nodes
    for (auto it = m_trackedAge.begin(); it != m_trackedAge.end(); ) {
        Tracked* tracked = (it++)->second;
        if (now - tracked->lastReferenced > nodeMaxAge) {
            m_trackedNodes.erase(tracked->node);
            m_trackedAge.erase(*tracked);
            delete tracked;
        } else {
            break;
        }
    }

    // Evict old banned IPs
    for (auto it = m_bannedAge.begin(); it != m_bannedAge.end(); ) {
        Banned* banned = (it++)->second;
        if (now - banned->lastReferenced > banMaxAge) {
            m_bannedIPs.erase(banned->ip);
            m_bannedAge.erase(*banned);
            delete banned;
        } else {
            break;
        }
    }

    // Evict old problematic nodes
    for (auto it = m_problematicAge.begin(); it != m_problematicAge.end(); ) {
        Problematic* prob = (it++)->second;
        if (now - prob->lastReferenced > problemMaxAge) {
            m_problematicNodes.erase(prob->node);
            m_problematicAge.erase(*prob);
            delete prob;
        } else {
            break;
        }
    }
}

} // namespace eMule::kad
