#pragma once

/// @file KadLookupHistory.h
/// @brief Search history tracking for GUI visualization.
///
/// Ported from kademlia/utils/LookupHistory.h. The GUI draws only the
/// "interesting" entries (asked or forced); receivedFromIdx indexes that list.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

namespace eMule::kad {

class Contact;

/// Tracks contacts discovered during a Kademlia search.
class LookupHistory {
public:
    struct HistoryEntry {
        UInt128 contactID;
        UInt128 distance;
        std::vector<int> receivedFromIdx;   // indexes into getHistoryEntries()
        uint64 askedContactsTime = 0;       // getTickCount(), 0 = never
        uint32 respondedContact = 0;
        uint64 askedSearchItemTime = 0;     // getTickCount(), 0 = never
        uint32 respondedSearchItem = 0;
        uint32 ip = 0;
        uint16 port = 0;
        uint8 contactVersion = 0;
        bool providedCloser = false;
        bool forcedInteresting = false;

        [[nodiscard]] bool isInteresting() const
        {
            return askedContactsTime != 0 || askedSearchItemTime != 0 || forcedInteresting;
        }
    };

    LookupHistory() = default;

    /// received == nullptr only credits `from` with a response (MFC).
    void contactReceived(Contact* received, Contact* from,
                         const UInt128& distance, bool closer,
                         bool forceInteresting = false);
    void contactAskedKad(const Contact* contact);
    void contactAskedKeyword(const Contact* contact);
    void contactRespondedKeyword(uint32 contactIP, uint16 udpPort, uint32 resultCount);

    void setSearchStopped() { m_searchStopped = true; }
    void setSearchDeleted() { m_searchDeleted = true; }
    void setGUIName(const QString& name) { m_guiName = name; }
    void setSearchType(uint32 type) { m_type = type; }

    /// Entries the graph shows, in the order they became interesting (MFC GetHistoryEntries).
    [[nodiscard]] const std::vector<HistoryEntry*>& getHistoryEntries() const { return m_interestingEntries; }
    /// Every contact seen, interesting or not.
    [[nodiscard]] size_t allEntryCount() const { return m_historyEntries.size(); }
    [[nodiscard]] const QString& getGUIName() const { return m_guiName; }
    [[nodiscard]] uint32 getType() const { return m_type; }
    [[nodiscard]] bool isSearchStopped() const { return m_searchStopped; }
    [[nodiscard]] bool isSearchDeleted() const { return m_searchDeleted; }

private:
    HistoryEntry* findEntry(const UInt128& contactID) const;
    int interestingIdxByID(const UInt128& contactID) const;
    void creditResponder(Contact* from, bool closer, HistoryEntry* received);

    std::vector<std::unique_ptr<HistoryEntry>> m_historyEntries;
    std::vector<HistoryEntry*> m_interestingEntries;
    QString m_guiName;
    uint32 m_type = 0;
    bool m_searchStopped = false;
    bool m_searchDeleted = false;
};

} // namespace eMule::kad
