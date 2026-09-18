#pragma once

/// @file FriendConnectProgress.h
/// @brief Steps a friend connection attempt reports while it runs.
///
/// Kept in its own header, like ServerMsgType.h, because core, the IPC layer and the GUI
/// all name these steps: core raises them, the daemon forwards them, and the GUI turns
/// them into the "*** Connecting" status lines MFC's chat window shows. The text stays on
/// the GUI side, where tr() reaches it — the daemon installs no translation.

namespace eMule {

/// MFC reports these as literal strings from CFriend::UpdateFriendConnectionState
/// (srchybrid/Friend.cpp:259-371) and CChatSelector::ConnectingResult.
enum class ChatConnectProgress : int {
    Connecting = 0,   ///< dialling the friend
    Authenticating,   ///< connected, waiting for secure identification
    SearchingKad,     ///< the address failed; looking the friend up by Kad ID
    FoundInKad,       ///< Kad answered with an address, dialling again
    Connected,        ///< terminal: the chat session is up
    Failed            ///< terminal: we could not reach the friend
};

} // namespace eMule
