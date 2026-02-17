#pragma once

/// @file Friend.h
/// @brief Friend data class — replaces MFC CFriend.
///
/// Stores friend identity (user hash, Kad ID), network address (IP/port),
/// display name, timestamps, and friend slot flag. Serializable to the
/// emfriends.met binary format via SafeFile + Tag.

#include "utils/Types.h"

#include <QString>

#include <array>
#include <ctime>

namespace eMule {

class FileDataIO;

// Friend-specific tag IDs (matching MFC FF_NAME / FF_KADID)
inline constexpr uint8 kFriendTagName  = 0x01;
inline constexpr uint8 kFriendTagKadID = 0x02;

/// Represents a single friend entry in the friend list.
class Friend {
public:
    Friend();
    Friend(const uint8* userHash, std::time_t lastSeen, uint32 lastUsedIP,
           uint16 lastUsedPort, std::time_t lastChatted,
           const QString& name, bool hasHash);

    // -- Serialization --------------------------------------------------------

    /// Read friend data from a binary stream (emfriends.met record).
    void loadFromFile(FileDataIO& file);

    /// Write friend data to a binary stream (emfriends.met record).
    void writeToFile(FileDataIO& file) const;

    // -- Hash queries ---------------------------------------------------------

    [[nodiscard]] bool hasUserhash() const;
    [[nodiscard]] bool hasKadID() const;

    // -- Accessors ------------------------------------------------------------

    [[nodiscard]] const std::array<uint8, 16>& userHash() const { return m_userHash; }
    void setUserHash(const uint8* hash);

    [[nodiscard]] const std::array<uint8, 16>& kadID() const { return m_kadID; }
    void setKadID(const uint8* id);

    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    [[nodiscard]] uint32 lastUsedIP() const { return m_lastUsedIP; }
    void setLastUsedIP(uint32 ip) { m_lastUsedIP = ip; }

    [[nodiscard]] uint16 lastUsedPort() const { return m_lastUsedPort; }
    void setLastUsedPort(uint16 port) { m_lastUsedPort = port; }

    [[nodiscard]] std::time_t lastSeen() const { return m_lastSeen; }
    void setLastSeen(std::time_t t) { m_lastSeen = t; }

    [[nodiscard]] std::time_t lastChatted() const { return m_lastChatted; }
    void setLastChatted(std::time_t t) { m_lastChatted = t; }

    [[nodiscard]] bool friendSlot() const { return m_friendSlot; }
    void setFriendSlot(bool val) { m_friendSlot = val; }

private:
    std::array<uint8, 16> m_userHash{};
    std::array<uint8, 16> m_kadID{};
    QString m_name;
    uint32 m_lastUsedIP = 0;
    uint16 m_lastUsedPort = 0;
    std::time_t m_lastSeen = 0;
    std::time_t m_lastChatted = 0;
    bool m_friendSlot = false;
};

} // namespace eMule
