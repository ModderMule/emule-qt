#pragma once

/// @file EncryptedDatagramSocket.h
/// @brief Static utility class for UDP packet encryption/obfuscation.
///
/// Replaces CEncryptedDatagramSocket. All methods are static — no instance state.
/// Key material is passed as parameters instead of reading from global thePrefs/theApp.

#include "utils/Types.h"

#include <cstdint>

namespace eMule {

/// Result of a UDP decryption attempt.
struct DecryptResult {
    uint8* data = nullptr;      ///< Pointer into original buffer (decrypted in-place).
    int length = -1;            ///< Decrypted data length (-1 = not encrypted / pass through).
    uint32 receiverVerifyKey = 0;
    uint32 senderVerifyKey = 0;
};

/// Static utility class for eMule UDP packet encryption/obfuscation.
///
/// Implements the eMule custom obfuscation protocol for UDP packets.
/// This is NOT standard encryption — it's protocol obfuscation for ISP evasion
/// using RC4 with MD5-derived keys.
class EncryptedDatagramSocket {
public:
    EncryptedDatagramSocket() = delete; // all static

    /// Decrypt a received client UDP packet.
    /// @param buf          Raw received buffer (modified in-place).
    /// @param len          Buffer length.
    /// @param ip           Sender's IP address (network byte order).
    /// @param userHash     Our user hash (16 bytes), or nullptr if unavailable.
    /// @param kadID        Our Kad node ID (16 bytes), or nullptr if Kad unavailable.
    /// @param kadRecvKey   Our Kad UDP verify key for the sender, or 0 if unavailable.
    /// @return             DecryptResult with pointer to decrypted data and verify keys.
    [[nodiscard]] static DecryptResult decryptReceivedClient(
        uint8* buf, int len, uint32 ip,
        const uint8* userHash, const uint8* kadID, uint32 kadRecvKey);

    /// Encrypt a client UDP packet for sending.
    /// @param buf                  Buffer with space reserved at front for header.
    /// @param len                  Payload length (data starts after overhead area).
    /// @param clientHashOrKadID    Remote client's hash or Kad ID (16 bytes), or nullptr.
    /// @param isKad                True for Kad packets.
    /// @param receiverVerifyKey    Kad receiver verify key (0 if not Kad or unknown).
    /// @param senderVerifyKey      Kad sender verify key (0 if not Kad or unknown).
    /// @param publicIP             Our public IP (network byte order), needed for ED2K packets.
    /// @return                     Total buffer length including encryption overhead.
    static uint32 encryptSendClient(
        uint8* buf, uint32 len,
        const uint8* clientHashOrKadID, bool isKad,
        uint32 receiverVerifyKey, uint32 senderVerifyKey,
        uint32 publicIP);

    /// Decrypt a received server UDP packet.
    [[nodiscard]] static DecryptResult decryptReceivedServer(
        uint8* buf, int len, uint32 baseKey);

    /// Encrypt a server UDP packet for sending.
    static uint32 encryptSendServer(uint8* buf, uint32 len, uint32 baseKey);

    /// Get the encryption overhead size in bytes.
    [[nodiscard]] static int encryptOverheadSize(bool isKad);
};

} // namespace eMule
