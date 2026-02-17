#pragma once

/// @file KadLookupHistory.h
/// @brief Search history tracking for GUI visualization.
///
/// Ported from kademlia/kademlia/LookupHistory.h.

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
        std::vector<int> receivedFromIdx;
        uint32 askedContactsTime = 0;
        uint32 respondedContact = 0;
        uint32 askedSearchItemTime = 0;
        uint32 respondedSearchItem = 0;
        uint32 ip = 0;
        uint16 port = 0;
        uint8 contactVersion = 0;
        bool providedCloser = false;
        bool forcedInteresting = false;

        bool isInteresting() const
        {
            return askedContactsTime > 0 || forcedInteresting;
        }
    };

    LookupHistory() = default;

    void contactReceived(Contact* received, Contact* from,
                         const UInt128& distance, bool closer,
                         bool forceInteresting = false);
    void contactAskedKad(const Contact* contact);
    void contactAskedKeyword(const Contact* contact);
    void contactRespondedKeyword(uint32 contactIP, uint16 udpPort, uint32 resultCount);

    void setSearchStopped();
    void setSearchDeleted();
    void setGUIDeleted();
    void setGUIName(const QString& name);
    void setSearchType(uint32 type);

    [[nodiscard]] std::vector<HistoryEntry*>& getHistoryEntries();
    [[nodiscard]] std::vector<HistoryEntry*>& getInterestingEntries();
    [[nodiscard]] const QString& getGUIName() const { return m_guiName; }
    [[nodiscard]] uint32 getType() const { return m_type; }
    [[nodiscard]] bool isSearchStopped() const { return m_searchStopped; }
    [[nodiscard]] bool isSearchDeleted() const { return m_searchDeleted; }

    void incRef() { ++m_refCount; }
    bool decRef() { return --m_refCount == 0; }

private:
    int findEntry(const UInt128& contactID) const;

    std::vector<std::unique_ptr<HistoryEntry>> m_historyEntries;
    std::vector<HistoryEntry*> m_interestingEntries;
    std::vector<HistoryEntry*> m_entryPtrCache; // raw pointer view for getHistoryEntries()
    QString m_guiName;
    uint32 m_refCount = 0;
    uint32 m_type = 0;
    bool m_searchStopped = false;
    bool m_searchDeleted = false;
};

} // namespace eMule::kad
