#pragma once

/// @file Friend.h
/// @brief Friend data class — replaces MFC CFriend.
///
/// Stores friend identity (user hash, Kad ID), network address (IP/port),
/// display name, timestamps, and friend slot flag. Serializable to the
/// emfriends.met binary format via SafeFile + Tag.

#include "friends/FriendConnectProgress.h"
#include "kademlia/KadClientSearcher.h"
#include "net/Address.h"
#include "utils/Types.h"

#include <QString>

#include <array>
#include <ctime>

namespace eMule {

class FileDataIO;
class UpDownClient;

// Friend-specific tag IDs (matching MFC FF_NAME / FF_KADID)
inline constexpr uint8 kFriendTagName  = 0x01;
inline constexpr uint8 kFriendTagKadID = 0x02;

/// Where a connection attempt to this friend currently stands.
/// MFC EFriendConnectState (srchybrid/Friend.h:24-29).
enum class FriendConnectState : uint8 {
    None = 0,      ///< no attempt running
    Connecting,    ///< a socket is being established
    Auth,          ///< connected, waiting for secure identification
    KadSearching   ///< the address failed; asking Kad where the friend is now
};

/// What just happened to the attempt. MFC EFriendConnectReport (srchybrid/Friend.h:31-38).
enum class FriendConnectReport : uint8 {
    Established = 0,    ///< the connection came up
    Disconnected,       ///< it dropped, or never came up
    UserHashVerified,   ///< secure identification succeeded
    UserHashFailed,     ///< the peer at that address is somebody else
    SecureIdentFailed,  ///< it claims to be our friend but cannot prove it
    Deleted             ///< the client object is going away underneath us
};

/// Represents a single friend entry in the friend list.
class Friend : public kad::KadClientSearcher {
public:
    Friend();
    Friend(const uint8* userHash, std::time_t lastSeen, uint32 lastUsedIP,
           uint16 lastUsedPort, std::time_t lastChatted,
           const QString& name, bool hasHash);

    /// Unlinks the client, so a client outliving its friend entry is left with neither a
    /// dangling friendPtr() nor a friend slot. MFC CFriend::~CFriend (srchybrid/Friend.cpp:83).
    ~Friend();

    Friend(const Friend&) = delete;
    Friend& operator=(const Friend&) = delete;

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

    [[nodiscard]] const Address& lastUsedAddress() const { return m_lastUsedAddress; }
    void setLastUsedAddress(const Address& addr) { m_lastUsedAddress = addr; }

    [[nodiscard]] uint16 lastUsedPort() const { return m_lastUsedPort; }
    void setLastUsedPort(uint16 port) { m_lastUsedPort = port; }

    [[nodiscard]] std::time_t lastSeen() const { return m_lastSeen; }
    void setLastSeen(std::time_t t) { m_lastSeen = t; }

    [[nodiscard]] std::time_t lastChatted() const { return m_lastChatted; }
    void setLastChatted(std::time_t t) { m_lastChatted = t; }

    // -- Linked client --------------------------------------------------------

    /// The connected client this friend currently is, or nullptr while offline.
    /// MFC CFriend::GetLinkedClient (srchybrid/Friend.h:70).
    ///
    /// @param validate  check the pointer against the client list before returning it, for
    ///                  callers that may be running after the client was destroyed.
    [[nodiscard]] UpDownClient* linkedClient(bool validate = false) const;

    /// Bind this friend to a connected client, or unbind with nullptr.
    /// MFC CFriend::SetLinkedClient (srchybrid/Friend.cpp:171-198).
    ///
    /// This is the whole reason the friend slot behaves: the flag lives on the client while
    /// one is linked, so granting, moving or revoking a slot on the Friend reaches the
    /// client that the upload queue actually scores. It also unlinks the previous client and
    /// clears its slot, which is what stops a stale flag surviving a hash change, a friend
    /// removal, or the slot being moved to somebody else.
    void setLinkedClient(UpDownClient* client);

    // -- Connecting -----------------------------------------------------------

    /// The client to hold a chat session on: the linked one, or a fresh client built from
    /// the stored address and registered with the client list. Null only when we have no
    /// address to dial at all. MFC CFriend::GetClientForChatSession (srchybrid/Friend.cpp:210).
    [[nodiscard]] UpDownClient* clientForChatSession();

    /// Start (or join) an attempt to reach this friend. Progress and the final verdict are
    /// reported through FriendList's signals rather than MFC's listener list — the one
    /// listener lives on the other side of the IPC seam.
    /// MFC CFriend::TryToConnect (srchybrid/Friend.cpp:226-257).
    bool tryToConnect();

    /// Feed the attempt what just happened to the linked client.
    /// MFC CFriend::UpdateFriendConnectionState (srchybrid/Friend.cpp:259-371).
    void updateFriendConnectionState(FriendConnectReport report);

    /// The chat window closed: cancel a dial of ours that is still running and put the
    /// linked client back to ChatState::None. MFC only does the latter
    /// (CChatSelector::EndSession, srchybrid/ChatSelector.cpp:464-490) because it has no
    /// connect state machine of its own to unwind.
    void endChatSession();

    [[nodiscard]] bool isTryingToConnect() const { return m_connectState != FriendConnectState::None; }
    [[nodiscard]] FriendConnectState connectState() const { return m_connectState; }

    /// Learn this friend's Kad ID from the address it is on, so a later move can be
    /// followed. MFC CFriend::FindKadID (srchybrid/Friend.cpp:373-381).
    void findKadID();

    /// Send now if the session is up, otherwise park the text on the chat client and dial.
    /// False only when the friend cannot be reached at all.
    bool sendOrQueueChatMessage(const QString& message);

    // -- kad::KadClientSearcher -----------------------------------------------

    void kadSearchNodeIDByIPResult(kad::KadClientSearchResult status, const uchar* nodeID) override;
    void kadSearchIPByNodeIDResult(kad::KadClientSearchResult status, uint32 ip, uint16 port) override;

    // -- Friend slot ----------------------------------------------------------

    /// MFC CFriend::GetFriendSlot (srchybrid/Friend.cpp:166-169) — the linked client is
    /// authoritative while there is one, so this never disagrees with what the queue sees.
    [[nodiscard]] bool friendSlot() const;

    /// MFC CFriend::SetFriendSlot (srchybrid/Friend.cpp:158-163) — propagates to the linked
    /// client. FriendList::removeAllFriendSlots() relies on that propagation to enforce the
    /// one-slot-at-a-time rule at the client level.
    void setFriendSlot(bool val);

private:
    /// Tell whoever is watching how far the attempt has got.
    void reportProgress(ChatConnectProgress step) const;

    /// End the attempt. @p touchClient is false on the Deleted path, where the linked
    /// client is already inside its own destructor and must not be called into.
    void finishConnecting(bool success, bool touchClient = true);

    UpDownClient* m_linkedClient = nullptr;   ///< not owned
    std::array<uint8, 16> m_userHash{};
    std::array<uint8, 16> m_kadID{};
    QString m_name;
    Address m_lastUsedAddress;
    uint16 m_lastUsedPort = 0;
    std::time_t m_lastSeen = 0;
    std::time_t m_lastChatted = 0;
    bool m_friendSlot = false;
    FriendConnectState m_connectState = FriendConnectState::None;
    uint32 m_lastKadSearch = 0;   ///< tick of the last Kad lookup; MFC's 10-minute gate
};

} // namespace eMule
