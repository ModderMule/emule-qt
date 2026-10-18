#pragma once

/// @file KadBuddyStore.h
/// @brief Kad nodes that offered to be our buddy, remembered across restarts.
///
/// Not MFC. Free buddy slots are rare (docs/protocol/kad-buddy-search.md), and the first
/// search after a start re-asks the same neighbours that refused last time. Nodes that
/// answered FINDBUDDY_RES are kept in `<ConfigDir>/kadbuddies.dat` and asked directly at
/// each buddy search. One unanswered ask or 14 days without an offer drops a node.
/// The file also keeps the last session's firewall verdict: a node that was firewalled
/// asks its remembered buddies right on connect instead of waiting for the first search.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"
#include "utils/VersionedRecordFile.h"

#include <QString>

#include <array>
#include <ctime>
#include <vector>

namespace eMule::kad {

inline constexpr auto kBuddyStoreFileName = "kadbuddies.dat";

class BuddyStore : private VersionedRecordFile {
public:
    static constexpr uint8 kFileVersion = 2;    ///< v2 adds the session verdict
    static constexpr uint8 kFileVersionV1 = 1;
    static constexpr size_t kMaxEntries = 16;
    static constexpr std::time_t kExpireSecs = 14 * 24 * 3600;
    /// Not asked again sooner; also longer than any search, so one search never asks twice.
    static constexpr std::time_t kReaskSecs = 30 * 60;
    /// An ask unanswered for this long is a miss (KadPacketTracking kTrackTimeout).
    static constexpr std::time_t kAnswerWindowSecs = 180;
    static constexpr size_t kAskPerSearch = 5;

    /// Firewall state a session ended with. Unknown: no final check result yet.
    enum class SessionVerdict : uint8 { Unknown = 0, Firewalled = 1, Open = 2 };

    struct Entry {
        uint32 ip = 0;          ///< host order
        uint16 udpPort = 0;
        uint16 tcpPort = 0;
        std::array<uint8, 16> userHash{};
        UInt128 kadId;          ///< zero when unknown
        uint8 connectOptions = 0;
        std::time_t lastOffered = 0;
        std::time_t lastAsked = 0;
        std::time_t lastConnected = 0;
    };

    /// Read the file; a missing or damaged one leaves the store empty.
    void load(const QString& path, std::time_t now);
    /// Prune, then write. False when the write failed.
    bool save(const QString& path, std::time_t now);

    /// A node offered itself (FINDBUDDY_RES): insert or refresh, evicting the oldest offer.
    void noteOffer(const Entry& offer, std::time_t now);
    /// The buddy link to this node came up.
    void noteConnected(uint32 ip, uint16 tcpPort, std::time_t now);

    /// Drop missed and expired nodes, then pick up to kAskPerSearch not asked within
    /// kReaskSecs (any, with @p ignoreReask), most recent buddy first, and stamp them asked.
    /// Most recent: last link up, then last offer.
    [[nodiscard]] std::vector<Entry> takeDue(std::time_t now, bool ignoreReask = false);

    [[nodiscard]] const std::vector<Entry>& entries() const { return m_entries; }

    [[nodiscard]] SessionVerdict lastSessionVerdict() const { return m_lastSessionVerdict; }
    void setLastSessionVerdict(SessionVerdict verdict) { m_lastSessionVerdict = verdict; }

private:
    /// Removes missed and expired entries, logging each.
    void prune(std::time_t now);

    std::vector<Entry> m_entries;
    SessionVerdict m_lastSessionVerdict = SessionVerdict::Unknown;
};

} // namespace eMule::kad
