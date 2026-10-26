#include "pch.h"
/// @file KadLookupHistory.cpp
/// @brief Search history tracking implementation (MFC LookupHistory.cpp).

#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadContact.h"
#include "utils/TimeUtils.h"

#include <algorithm>


namespace eMule::kad {

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void LookupHistory::contactReceived(Contact* received, Contact* from,
                                     const UInt128& distance, bool closer,
                                     bool forceInteresting)
{
    // Known contact (same distance = same ID for one target), or response credit only.
    // MFC matches the first entry it meets when received is null.
    if (received == nullptr) {
        if (!m_historyEntries.empty())
            creditResponder(from, closer, nullptr);
        return;
    }
    for (auto& entry : m_historyEntries) {
        if (entry->distance == distance) {
            creditResponder(from, closer, entry.get());
            return;
        }
    }

    auto entry = std::make_unique<HistoryEntry>();
    entry->contactID = received->getClientID();
    entry->distance = distance;
    entry->ip = received->address().toUint32();
    entry->port = received->getUDPPort();
    entry->contactVersion = received->getVersion();
    entry->forcedInteresting = forceInteresting;
    auto* raw = entry.get();
    creditResponder(from, closer, raw);
    m_historyEntries.push_back(std::move(entry));
    if (forceInteresting)
        m_interestingEntries.push_back(raw);
}

void LookupHistory::contactAskedKad(const Contact* contact)
{
    if (!contact)
        return;
    if (auto* entry = findEntry(contact->getClientID())) {
        if (!entry->isInteresting())
            m_interestingEntries.push_back(entry);
        entry->askedContactsTime = getTickCount();
    }
}

void LookupHistory::contactAskedKeyword(const Contact* contact)
{
    if (!contact)
        return;
    if (auto* entry = findEntry(contact->getClientID())) {
        if (!entry->isInteresting())
            m_interestingEntries.push_back(entry);
        entry->askedSearchItemTime = getTickCount();
    }
}

void LookupHistory::contactRespondedKeyword(uint32 contactIP, uint16 udpPort, uint32 resultCount)
{
    for (auto it = m_interestingEntries.rbegin(); it != m_interestingEntries.rend(); ++it) {
        if ((*it)->ip == contactIP && (*it)->port == udpPort) {
            (*it)->respondedSearchItem += resultCount;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

LookupHistory::HistoryEntry* LookupHistory::findEntry(const UInt128& contactID) const
{
    for (auto it = m_historyEntries.rbegin(); it != m_historyEntries.rend(); ++it)
        if ((*it)->contactID == contactID)
            return it->get();
    return nullptr;
}

int LookupHistory::interestingIdxByID(const UInt128& contactID) const
{
    for (auto i = static_cast<int>(m_interestingEntries.size()); --i >= 0;)
        if (m_interestingEntries[static_cast<size_t>(i)]->contactID == contactID)
            return i;
    return -1;
}

void LookupHistory::creditResponder(Contact* from, bool closer, HistoryEntry* received)
{
    if (!from)
        return;
    const int idx = interestingIdxByID(from->getClientID());
    if (idx < 0)
        return;
    if (received)
        received->receivedFromIdx.push_back(idx);
    auto* responder = m_interestingEntries[static_cast<size_t>(idx)];
    ++responder->respondedContact;
    if (closer)
        responder->providedCloser = true;
}

} // namespace eMule::kad
