/// @file KadLookupHistory.cpp
/// @brief Search history tracking implementation.

#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadContact.h"

#include <ctime>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void LookupHistory::contactReceived(Contact* received, Contact* from,
                                     const UInt128& distance, bool closer,
                                     bool forceInteresting)
{
    if (!received)
        return;

    int fromIdx = -1;
    if (from)
        fromIdx = findEntry(from->getClientID());

    // Check if this contact is already tracked
    int existingIdx = findEntry(received->getClientID());
    if (existingIdx >= 0) {
        auto* entry = m_historyEntries[static_cast<size_t>(existingIdx)].get();
        if (fromIdx >= 0)
            entry->receivedFromIdx.push_back(fromIdx);
        return;
    }

    // Add new entry
    auto entry = std::make_unique<HistoryEntry>();
    entry->contactID = received->getClientID();
    entry->distance = distance;
    entry->ip = received->getIPAddress();
    entry->port = received->getUDPPort();
    entry->contactVersion = received->getVersion();
    entry->providedCloser = closer;
    entry->forcedInteresting = forceInteresting;
    if (fromIdx >= 0)
        entry->receivedFromIdx.push_back(fromIdx);

    auto* rawPtr = entry.get();
    m_historyEntries.push_back(std::move(entry));

    if (rawPtr->isInteresting())
        m_interestingEntries.push_back(rawPtr);
}

void LookupHistory::contactAskedKad(const Contact* contact)
{
    if (!contact)
        return;
    int idx = findEntry(contact->getClientID());
    if (idx >= 0) {
        auto* entry = m_historyEntries[static_cast<size_t>(idx)].get();
        entry->askedContactsTime = static_cast<uint32>(time(nullptr));
        if (!entry->isInteresting()) {
            // Now interesting since we asked it
        }
        if (std::find(m_interestingEntries.begin(), m_interestingEntries.end(), entry) == m_interestingEntries.end())
            m_interestingEntries.push_back(entry);
    }
}

void LookupHistory::contactAskedKeyword(const Contact* contact)
{
    if (!contact)
        return;
    int idx = findEntry(contact->getClientID());
    if (idx >= 0) {
        m_historyEntries[static_cast<size_t>(idx)]->askedSearchItemTime =
            static_cast<uint32>(time(nullptr));
    }
}

void LookupHistory::contactRespondedKeyword(uint32 contactIP, uint16 /*udpPort*/, uint32 resultCount)
{
    for (auto& entry : m_historyEntries) {
        if (entry->ip == contactIP) {
            entry->respondedSearchItem = resultCount;
            break;
        }
    }
}

void LookupHistory::setSearchStopped()
{
    m_searchStopped = true;
}

void LookupHistory::setSearchDeleted()
{
    m_searchDeleted = true;
}

void LookupHistory::setGUIDeleted()
{
    // Mark that GUI no longer references this
}

void LookupHistory::setGUIName(const QString& name)
{
    m_guiName = name;
}

void LookupHistory::setSearchType(uint32 type)
{
    m_type = type;
}

std::vector<LookupHistory::HistoryEntry*>& LookupHistory::getHistoryEntries()
{
    m_entryPtrCache.clear();
    m_entryPtrCache.reserve(m_historyEntries.size());
    for (auto& entry : m_historyEntries)
        m_entryPtrCache.push_back(entry.get());
    return m_entryPtrCache;
}

std::vector<LookupHistory::HistoryEntry*>& LookupHistory::getInterestingEntries()
{
    return m_interestingEntries;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

int LookupHistory::findEntry(const UInt128& contactID) const
{
    for (size_t i = 0; i < m_historyEntries.size(); ++i) {
        if (m_historyEntries[i]->contactID == contactID)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace eMule::kad
