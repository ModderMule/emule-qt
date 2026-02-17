#pragma once

/// @file ClientStateDefs.h
/// @brief Scoped enums for client state machine — replaces MFC ClientStateDefs.h.
///
/// Converts all unscoped MFC enums to C++23 scoped enums with uint8 underlying
/// type. Values are preserved exactly for protocol compatibility.

#include "utils/Types.h"

#include <type_traits>

namespace eMule {

enum class UploadState : uint8 {
    Uploading,
    OnUploadQueue,
    Connecting,
    Banned,
    None
};

enum class DownloadState : uint8 {
    Downloading,
    OnQueue,
    Connected,
    Connecting,
    WaitCallback,
    WaitCallbackKad,
    ReqHashSet,
    NoNeededParts,
    TooManyConns,
    TooManyConnsKad,  // unused since 0.49b
    LowToLowIP,
    Banned,
    Error,
    None,
    RemoteQueueFull   // not used yet, except in statistics
};

enum class ChatState : uint8 {
    None,
    Chatting,
    Connecting,
    UnableToConnect
};

enum class KadState : uint8 {
    None,
    QueuedFwCheck,
    ConnectingFwCheck,
    ConnectedFwCheck,
    QueuedBuddy,
    IncomingBuddy,
    ConnectingBuddy,
    ConnectedBuddy,
    QueuedFwCheckUDP,
    FwCheckUDP,
    ConnectingFwCheckUDP
};

enum class ClientSoftware : uint8 {
    eMule          = 0,   // default
    cDonkey        = 1,   // ET_COMPATIBLECLIENT
    xMule          = 2,   // ET_COMPATIBLECLIENT
    aMule          = 3,   // ET_COMPATIBLECLIENT
    Shareaza       = 4,   // ET_COMPATIBLECLIENT
    MLDonkey       = 10,  // ET_COMPATIBLECLIENT
    lphant         = 20,  // ET_COMPATIBLECLIENT
    eDonkeyHybrid  = 50,
    eDonkey,
    OldEMule,
    URL,
    Unknown
};

enum class SecureIdentState : uint8 {
    Unavailable       = 0,
    AllRequestsSend   = 0,
    SignatureNeeded   = 1,
    KeyAndSigNeeded   = 2
};

enum class InfoPacketState : uint8 {
    None             = 0,
    EDonkeyProtPack  = 1,
    EMuleProtPack    = 2,
    Both             = 3
};

// Bitwise operators for InfoPacketState (used as bitmask flags)
[[nodiscard]] constexpr InfoPacketState operator|(InfoPacketState a, InfoPacketState b) noexcept {
    using U = std::underlying_type_t<InfoPacketState>;
    return static_cast<InfoPacketState>(static_cast<U>(a) | static_cast<U>(b));
}
constexpr InfoPacketState& operator|=(InfoPacketState& a, InfoPacketState b) noexcept {
    a = a | b; return a;
}
[[nodiscard]] constexpr InfoPacketState operator&(InfoPacketState a, InfoPacketState b) noexcept {
    using U = std::underlying_type_t<InfoPacketState>;
    return static_cast<InfoPacketState>(static_cast<U>(a) & static_cast<U>(b));
}

enum class SourceFrom : uint8 {
    Server           = 0,
    Kademlia         = 1,
    SourceExchange   = 2,
    Passive          = 3,
    Link             = 4
};

enum class ChatCaptchaState : uint8 {
    None             = 0,
    ChallengeSent,
    CaptchaSolved,
    Accepting,
    CaptchaRecv,
    SolutionSent
};

enum class ConnectingState : uint8 {
    None             = 0,
    DirectTCP,
    DirectCallback,
    KadCallback,
    ServerCallback,
    Preconditions
};

enum class IdentState : uint8 {
    NotAvailable,
    IdNeeded,
    Identified,
    IdFailed,
    IdBadGuy
};

} // namespace eMule
