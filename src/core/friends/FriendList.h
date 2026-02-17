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

private:
    std::vector<std::unique_ptr<Friend>> m_friends;
};

} // namespace eMule
