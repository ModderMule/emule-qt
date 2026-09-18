#pragma once

/// @file FriendList.h
/// @brief Friend list manager — replaces MFC CFriendList.
///
/// QObject-based friend collection that loads/saves the emfriends.met
/// binary file and emits signals for GUI notification. Decoupled from
/// GUI — the GUI module connects to signals to display the friend list.

#include "friends/Friend.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace eMule {

/// File name for the friends database.
inline constexpr auto kFriendsMetFilename = "emfriends.met";

class FriendList : public QObject {
    Q_OBJECT

public:
    explicit FriendList(QObject* parent = nullptr);
    ~FriendList() override;

    // -- Persistence ----------------------------------------------------------

    /// Load the friend list from configDir/emfriends.met.
    /// Returns true on success.
    bool load(const QString& configDir);

    /// Save the friend list to configDir/emfriends.met.
    void save(const QString& configDir) const;

    // -- Friend management ----------------------------------------------------

    /// Add a friend by user hash, IP, port, name.
    /// Returns a pointer to the new friend, or nullptr if duplicate/invalid.
    Friend* addFriend(const uint8* userHash, uint32 lastUsedIP,
                      uint16 lastUsedPort, const QString& name = {},
                      bool hasHash = true);

    /// Same, for a peer whose address may be IPv6. The uint32 overload delegates here;
    /// duplicate detection and the "hash or address required" rule are shared.
    Friend* addFriend(const uint8* userHash, const Address& lastUsedAddress,
                      uint16 lastUsedPort, const QString& name = {},
                      bool hasHash = true);

    /// Remove a friend. Returns true if found and removed.
    bool removeFriend(Friend* f);

    /// Remove all friends.
    void removeAll();

    // -- Queries --------------------------------------------------------------

    /// Search for a friend by user hash and/or IP+port.
    /// Hash-based friends match by hash; IP-only friends match by IP+port.
    [[nodiscard]] Friend* searchFriend(const uint8* userHash, uint32 ip = 0,
                                       uint16 port = 0) const;

    /// Check if a friend with the given hex user hash string exists.
    [[nodiscard]] bool isAlreadyFriend(const QString& hexUserHash) const;

    /// Validate that a friend pointer belongs to this list.
    [[nodiscard]] bool isValid(const Friend* f) const;

    /// Number of friends.
    [[nodiscard]] int count() const { return static_cast<int>(m_friends.size()); }

    /// Access friends (for iteration).
    [[nodiscard]] const std::vector<std::unique_ptr<Friend>>& friends() const { return m_friends; }

    /// Remove all friend slots (priority upload flags).
    void removeAllFriendSlots();

signals:
    void friendAdded(eMule::Friend* f);
    void friendRemoved(const QString& name);
    void friendUpdated(eMule::Friend* f);
    void listLoaded(int count);

    /// A connection attempt moved on. MFC reports these through
    /// CFriendConnectionListener; here the one listener is the GUI, across the IPC seam,
    /// so the list is the signal source and CoreNotifierBridge forwards them.
    void friendConnectionProgress(eMule::Friend* f, eMule::ChatConnectProgress step);
    /// Terminal verdict for an attempt.
    void friendConnectingResult(eMule::Friend* f, bool success);

private:
    /// Friend raises the two signals above through these, so the emit stays with the
    /// list that owns the entry.
    friend class Friend;
    void emitConnectionProgress(Friend* f, ChatConnectProgress step);
    void emitConnectingResult(Friend* f, bool success);

    std::vector<std::unique_ptr<Friend>> m_friends;
};

} // namespace eMule
