/// @file DeadSourceList.cpp
/// @brief Dead source tracking implementation — replaces MFC CDeadSourceList.

#include "client/DeadSourceList.h"
#include "utils/Opcodes.h"

namespace eMule {

// ---------------------------------------------------------------------------
// DeadSourceKey
// ---------------------------------------------------------------------------

bool operator==(const DeadSourceKey& a, const DeadSourceKey& b) noexcept
{
    // High-ID or low-ID with server: match by ID + port (+ serverIP for low-ID)
    if (a.userID != 0 && a.userID == b.userID) {
        bool portMatch = (a.port != 0 && a.port == b.port)
                      || (a.kadPort != 0 && a.kadPort == b.kadPort);
        bool serverMatch = (a.serverIP == b.serverIP) || !isLowID(a.userID);
        if (portMatch && serverMatch)
            return true;
    }
    // Low-ID without server: match by user hash
    if (isLowID(a.userID) && !isnulmd4(a.hash.data()) && md4equ(a.hash.data(), b.hash.data()))
        return true;

    return false;
}

// ---------------------------------------------------------------------------
// DeadSourceList
// ---------------------------------------------------------------------------

static constexpr auto kCleanupInterval = std::chrono::minutes(60);
static constexpr auto kBlockTimeGlobal = std::chrono::minutes(15);
static constexpr auto kBlockTimeLocal  = std::chrono::minutes(45);
static constexpr auto kBlockTimeFwGlobal = std::chrono::minutes(30);
static constexpr auto kBlockTimeFwLocal  = std::chrono::minutes(45);

void DeadSourceList::init(bool globalList)
{
    m_globalList = globalList;
    m_lastCleanUp = std::chrono::steady_clock::now();
}

void DeadSourceList::addDeadSource(const DeadSourceKey& key, bool lowID)
{
    auto now = std::chrono::steady_clock::now();
    auto blockTime = lowID
        ? (m_globalList ? kBlockTimeFwGlobal : kBlockTimeFwLocal)
        : (m_globalList ? kBlockTimeGlobal : kBlockTimeLocal);

    m_sources[key] = now + blockTime;

    if (now >= m_lastCleanUp + kCleanupInterval)
        cleanUp();
}

void DeadSourceList::removeDeadSource(const DeadSourceKey& key)
{
    m_sources.erase(key);
}

bool DeadSourceList::isDeadSource(const DeadSourceKey& key) const
{
    auto it = m_sources.find(key);
    if (it == m_sources.end())
        return false;
    return std::chrono::steady_clock::now() < it->second;
}

void DeadSourceList::cleanUp()
{
    m_lastCleanUp = std::chrono::steady_clock::now();
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if (m_lastCleanUp >= it->second)
            it = m_sources.erase(it);
        else
            ++it;
    }
}

} // namespace eMule
