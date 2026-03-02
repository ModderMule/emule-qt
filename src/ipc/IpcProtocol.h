#pragma once

/// @file IpcProtocol.h
/// @brief IPC wire protocol: message types, framing constants, encode/decode.
///
/// Every IPC message is a length-prefixed CBOR payload:
///   [4 bytes: big-endian uint32 payload length][CBOR payload]
///
/// The CBOR payload is always a QCborArray:
///   [ msgType: int, seqId: int, ...fields ]

#include <QByteArray>
#include <QCborArray>

#include <cstdint>
#include <optional>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Message type enumeration
// ---------------------------------------------------------------------------

enum class IpcMsgType : int {
    // -- Requests (GUI -> Core) -----------------------------------------------

    Handshake            = 100,  ///< [version: string]
    GetDownloads         = 110,
    GetDownload          = 111,  ///< [hash: string]
    PauseDownload        = 112,  ///< [hash: string]
    ResumeDownload       = 113,  ///< [hash: string]
    CancelDownload       = 114,  ///< [hash: string]
    SetDownloadPriority  = 115,  ///< [hash, priority, isAuto]
    ClearCompleted       = 116,  ///< [] — remove completed downloads
    GetUploads           = 120,
    GetDownloadClients   = 121,  ///< [] — source clients we are downloading from
    GetKnownClients      = 122,  ///< [] — all known clients
    GetServers           = 130,
    RemoveServer         = 131,  ///< [ip: int64, port: int64]
    RemoveAllServers     = 132,  ///< []
    SetServerPriority    = 133,  ///< [ip: int64, port: int64, priority: int]
    SetServerStatic      = 134,  ///< [ip: int64, port: int64, isStatic: bool]
    AddServer            = 135,  ///< [address: string, port: int64, name: string]
    GetConnection        = 140,
    ConnectToServer      = 141,
    DisconnectFromServer = 142,
    StartSearch          = 150,  ///< [expression, fileType, method, minSize, maxSize, avail, ext, completeSrc]
    GetSearchResults     = 151,  ///< [searchID]
    StopSearch           = 152,  ///< [searchID: int]
    RemoveSearch         = 153,  ///< [searchID: int]
    ClearAllSearches     = 154,  ///< []
    DownloadSearchFile   = 155,  ///< [hash: string, fileName: string, fileSize: int64]
    GetSharedFiles       = 160,
    SetSharedFilePriority = 161, ///< [hash: string, priority: int, isAuto: bool]
    ReloadSharedFiles    = 162, ///< [] — rescan shared directories from disk
    GetFriends           = 170,
    AddFriend            = 171,  ///< [hash, name, ip, port]
    RemoveFriend         = 172,  ///< [hash]
    SendChatMessage      = 173,  ///< [hash: string, message: string]
    SetFriendSlot        = 174,  ///< [hash: string, enabled: bool]
    GetStats             = 180,
    GetPreferences       = 190,
    SetPreferences       = 191,  ///< [key, value, ...]
    Subscribe            = 200,  ///< [eventMask: int]
    GetKadContacts       = 210,
    GetKadStatus         = 211,
    BootstrapKad         = 212,  ///< [ip: string, port: int]  (empty = from nodes.dat)
    DisconnectKad        = 213,
    SyncLogs             = 214,  ///< [lastLogId: int64]  — request buffered logs since ID
    Shutdown             = 215,  ///< [] — request graceful daemon shutdown
    GetKadSearches       = 216,
    GetKadLookupHistory  = 217,  ///< [searchId: int] — lookup history for a search
    GetNetworkInfo       = 218,  ///< [] — all network info for the Network Information dialog
    RecheckFirewall      = 219,  ///< [] — restart TCP + UDP firewall checks

    // -- Responses (Core -> GUI) ---------------------------------------------

    HandshakeOk          = 300,  ///< [version, motd]
    Result               = 301,  ///< [success, data]
    Error                = 302,  ///< [code, message]

    // -- Push Events (Core -> GUI, seqId=0) ----------------------------------

    PushStatsUpdate      = 400,
    PushDownloadUpdate   = 410,
    PushDownloadAdded    = 411,
    PushDownloadRemoved  = 412,
    PushServerState      = 420,
    PushSearchResult     = 430,
    PushLogMessage       = 450,
    PushSharedFileUpdate = 460,
    PushUploadUpdate     = 470,
    PushKadUpdate        = 480,
    PushKadSearchesChanged = 481,
    PushKnownClientsChanged = 490,
    PushChatMessage       = 500,  ///< [senderHash, senderName, message]
    PushFriendListChanged = 510,  ///< [] — friend list changed
};

// ---------------------------------------------------------------------------
// Framing constants
// ---------------------------------------------------------------------------

/// Size of the length prefix in bytes.
inline constexpr int FrameHeaderSize = 4;

/// Maximum allowed payload size (16 MiB).
inline constexpr uint32_t MaxPayloadSize = 16 * 1024 * 1024;

/// Default IPC TCP port.
inline constexpr uint16_t DefaultIpcPort = 4712;

/// Protocol version string.
inline constexpr const char* ProtocolVersion = "1.0";

// ---------------------------------------------------------------------------
// Framing functions
// ---------------------------------------------------------------------------

/// Encode a CBOR payload into a length-prefixed frame.
/// Returns [4-byte big-endian length][payload].
[[nodiscard]] QByteArray encodeFrame(const QByteArray& cborPayload);

/// Convenience: encode a QCborArray directly into a frame.
[[nodiscard]] QByteArray encodeFrame(const QCborArray& message);

/// Result of a frame decode attempt.
struct DecodeResult {
    QCborArray message;    ///< Decoded CBOR array.
    int bytesConsumed = 0; ///< Total bytes consumed (header + payload).
};

/// Try to decode one frame from the front of @p buffer.
/// Returns std::nullopt if insufficient data or invalid framing.
/// On success, the caller should remove bytesConsumed from the buffer.
[[nodiscard]] std::optional<DecodeResult> tryDecodeFrame(const QByteArray& buffer);

} // namespace eMule::Ipc
