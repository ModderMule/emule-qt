#include "pch.h"
/// @file EncryptedDatagramSocket.cpp
/// @brief UDP packet encryption/obfuscation implementation.
///
/// Protocol specification (from original eMule):
///
/// ED2K Client packets:
///   Key = MD5(<UserHash 16><IP 4><MagicValue91 1><RandomKeyPart 2>)
///   Header: <SemiRandomMarker 1[plain]><RandomKeyPart 2[plain]><MagicValue 4><PaddingLen 1><Padding>
///
/// Kad packets (NodeID key):
///   Key = MD5(<KadID 16><RandomKeyPart 2>)
///   Header: <SemiRandomMarker 1[plain]><RandomKeyPart 2[plain]><MagicValue 4><PaddingLen 1><Padding><RecvKey 4><SendKey 4>
///
/// Kad packets (ReceiverKey):
///   Key = MD5(<ReceiverKey 4><RandomKeyPart 2>)
///
/// Server packets:
///   SendKey = MD5(<BaseKey 4><MagicClientServer 1><RandomKeyPart 2>)
///   RecvKey = MD5(<BaseKey 4><MagicServerClient 1><RandomKeyPart 2>)

#include "net/EncryptedDatagramSocket.h"
#include "crypto/MD5Hash.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"

#include <cstring>

namespace eMule {

namespace {

// Padding is currently disabled for UDP
constexpr int kCryptHeaderPadding = 0;

// Overhead structures
constexpr int kCryptHeaderWithoutPadding = 3 + 4 + 1; // marker + randomKeyPart + magic + paddingLen = 8
constexpr int kCryptHeaderSize = kCryptHeaderWithoutPadding + kCryptHeaderPadding;
constexpr int kCryptHeaderKad = kCryptHeaderSize + 4 + 4; // + receiverKey + senderKey

// Magic values
constexpr uint8 kMagicValueUDP = 91;
constexpr uint32 kMagicValueUDPSyncClient = 0x395F2EC1u;
constexpr uint32 kMagicValueUDPSyncServer = 0x13EF24D5u;
constexpr uint8 kMagicValueUDPServerClient = 0xA5;
constexpr uint8 kMagicValueUDPClientServer = 0x6B;

/// Check if a byte matches a known protocol header (not encrypted).
bool isProtocolHeader(uint8 byte)
{
    switch (byte) {
    case OP_EMULEPROT:
    case OP_KADEMLIAPACKEDPROT:
    case OP_KADEMLIAHEADER:
    case OP_UDPRESERVEDPROT1:
    case OP_UDPRESERVEDPROT2:
    case OP_PACKEDPROT:
        return true;
    default:
        return false;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// decryptReceivedClient
// ---------------------------------------------------------------------------

DecryptResult EncryptedDatagramSocket::decryptReceivedClient(
    uint8* buf, int len, uint32 ip,
    const uint8* userHash, const uint8* kadID, uint32 kadRecvKey)
{
    DecryptResult result;
    result.data = buf;
    result.length = len;

    if (len <= kCryptHeaderSize)
        return result;

    // Check if it's already a plaintext protocol packet
    if (isProtocolHeader(buf[0]))
        return result;

    // Try to decrypt with different keys based on marker bits
    RC4Key keyReceiveKey{};
    uint8 byCurrentTry;
    uint8 byTries;

    if (kadID == nullptr && kadRecvKey == 0) {
        // No Kad available, only try ED2K
        byTries = 1;
        byCurrentTry = 1;
    } else {
        byTries = 3;
        byCurrentTry = (buf[0] & 1) ? 1 : (buf[0] & 2);
    }

    uint16 randomKeyPart;
    std::memcpy(&randomKeyPart, &buf[1], 2);

    bool isKad = false;
    uint32 dwValue = 0;

    do {
        --byTries;
        MD5Hasher md5;

        switch (byCurrentTry) {
        case 0: // Kad packet with NodeID as key
            isKad = true;
            if (kadID != nullptr) {
                uint8 keyData[18];
                md4cpy(keyData, kadID);
                pokeUInt16(&keyData[16], randomKeyPart);
                md5.calculate(keyData, sizeof keyData);
            }
            break;

        case 1: // ED2K packet
            isKad = false;
            if (userHash != nullptr) {
                uint8 keyData[23];
                md4cpy(keyData, userHash);
                pokeUInt32(&keyData[16], ip);
                keyData[20] = kMagicValueUDP;
                pokeUInt16(&keyData[21], randomKeyPart);
                md5.calculate(keyData, sizeof keyData);
            }
            break;

        case 2: // Kad packet with ReceiverKey
            isKad = true;
            if (kadRecvKey != 0) {
                uint8 keyData[6];
                pokeUInt32(keyData, kadRecvKey);
                pokeUInt16(&keyData[4], randomKeyPart);
                md5.calculate(keyData, sizeof keyData);
            }
            break;

        default:
            break;
        }

        keyReceiveKey = rc4CreateKey({md5.getRawHash(), 16}, true);
        rc4Crypt(&buf[3], reinterpret_cast<uint8*>(&dwValue), 4, keyReceiveKey);
        byCurrentTry = (byCurrentTry + 1) % 3;
    } while (dwValue != kMagicValueUDPSyncClient && byTries > 0);

    if (dwValue == kMagicValueUDPSyncClient) {
        // Decrypt the padding length byte
        uint8 paddingByte;
        rc4Crypt(&buf[7], &paddingByte, 1, keyReceiveKey);
        paddingByte &= 0x0F;

        int remaining = len - kCryptHeaderWithoutPadding;
        if (remaining <= paddingByte) {
            logWarning(QStringLiteral("Invalid obfuscated UDP packet: padding size (%1) >= remaining bytes").arg(paddingByte));
            result.length = len; // pass through
            return result;
        }

        // Skip padding
        if (paddingByte > 0) {
            rc4Crypt(static_cast<uint8*>(nullptr), paddingByte, keyReceiveKey);
            remaining -= paddingByte;
        }

        if (isKad) {
            if (remaining <= 8) {
                logWarning(QStringLiteral("Obfuscated Kad packet with missing verify keys"));
                result.length = len;
                return result;
            }
            // Read verify keys
            uint8* keyStart = &buf[len - remaining];
            rc4Crypt(keyStart, reinterpret_cast<uint8*>(&result.receiverVerifyKey), 4, keyReceiveKey);
            rc4Crypt(keyStart + 4, reinterpret_cast<uint8*>(&result.senderVerifyKey), 4, keyReceiveKey);
            remaining -= 8;
        }

        // Decrypt the actual payload
        result.data = &buf[len - remaining];
        rc4Crypt(result.data, static_cast<uint32>(remaining), keyReceiveKey);
        result.length = remaining;
    }
    // else: pass through with original length (not encrypted)

    return result;
}

// ---------------------------------------------------------------------------
// encryptSendClient
// ---------------------------------------------------------------------------

uint32 EncryptedDatagramSocket::encryptSendClient(
    uint8* buf, uint32 len,
    const uint8* clientHashOrKadID, bool isKad,
    uint32 receiverVerifyKey, uint32 senderVerifyKey,
    uint32 publicIP)
{
    uint8 kadRecKeyUsed = 0; // NodeID marker
    const uint16 randomKeyPart = getRandomUInt16();
    MD5Hasher md5;

    if (isKad) {
        if ((clientHashOrKadID == nullptr || isnulmd4(clientHashOrKadID)) && receiverVerifyKey != 0) {
            kadRecKeyUsed = 2; // RecKey marker
            uint8 keyData[6];
            pokeUInt32(keyData, receiverVerifyKey);
            pokeUInt16(&keyData[4], randomKeyPart);
            md5.calculate(keyData, sizeof keyData);
        } else if (clientHashOrKadID != nullptr && !isnulmd4(clientHashOrKadID)) {
            uint8 keyData[18];
            md4cpy(keyData, clientHashOrKadID);
            pokeUInt16(&keyData[16], randomKeyPart);
            md5.calculate(keyData, sizeof keyData);
        } else {
            return len; // cannot encrypt
        }
    } else {
        uint8 keyData[23];
        md4cpy(keyData, clientHashOrKadID);
        pokeUInt32(&keyData[16], publicIP);
        keyData[20] = kMagicValueUDP;
        pokeUInt16(&keyData[21], randomKeyPart);
        md5.calculate(keyData, sizeof keyData);
    }

    RC4Key keySendKey = rc4CreateKey({md5.getRawHash(), 16}, true);

    // Generate semi-random protocol marker byte
    uint8 marker;
    for (int i = 32; i > 0; --i) {
        marker = static_cast<uint8>(getRandomUInt16() & 0xFF);
        if (isKad) {
            marker &= ~3u;          // clear marker bits
            marker |= kadRecKeyUsed; // set Kad reckey/nodeid marker bit
        } else {
            marker |= 1u;           // set ED2K marker bit
        }
        if (!isProtocolHeader(marker))
            break;
        marker = OP_EMULEPROT; // sentinel to detect loop exhaustion
    }
    if (marker == OP_EMULEPROT) {
        // Extremely unlikely, fallback
        marker = static_cast<uint8>(!isKad) | kadRecKeyUsed;
    }

    // Write the header
    buf[0] = marker;
    pokeUInt16(&buf[1], randomKeyPart);

    // Write magic value + padding at offset 3
    pokeUInt32(&buf[3], kMagicValueUDPSyncClient);
    buf[7] = kCryptHeaderPadding;

    if (isKad) {
        pokeUInt32(&buf[kCryptHeaderSize], receiverVerifyKey);
        pokeUInt32(&buf[kCryptHeaderSize + 4], senderVerifyKey);
    }

    const uint32 cryptHeaderLen = static_cast<uint32>(encryptOverheadSize(isKad));
    len += cryptHeaderLen;

    // Encrypt everything from magic value onward (offset 3)
    rc4Crypt(&buf[3], len - 3, keySendKey);

    return len;
}

// ---------------------------------------------------------------------------
// decryptReceivedServer
// ---------------------------------------------------------------------------

DecryptResult EncryptedDatagramSocket::decryptReceivedServer(
    uint8* buf, int len, uint32 baseKey)
{
    DecryptResult result;
    result.data = buf;
    result.length = len;

    if (len <= kCryptHeaderSize || baseKey == 0)
        return result;

    if (buf[0] == OP_EDONKEYPROT)
        return result; // not encrypted

    uint16 randomKeyPart;
    std::memcpy(&randomKeyPart, &buf[1], 2);

    uint8 keyData[7];
    pokeUInt32(keyData, baseKey);
    keyData[4] = kMagicValueUDPClientServer;
    pokeUInt16(&keyData[5], randomKeyPart);
    MD5Hasher md5(keyData, sizeof keyData);

    RC4Key keyReceiveKey = rc4CreateKey({md5.getRawHash(), 16}, true);

    uint32 dwMagic;
    rc4Crypt(&buf[3], reinterpret_cast<uint8*>(&dwMagic), 4, keyReceiveKey);

    if (dwMagic == kMagicValueUDPSyncServer) {
        // Decrypt padding byte
        uint8 paddingByte;
        rc4Crypt(&buf[7], &paddingByte, 1, keyReceiveKey);
        paddingByte &= 0x0F;

        int remaining = len - kCryptHeaderWithoutPadding;
        if (remaining <= paddingByte) {
            logWarning(QStringLiteral("Invalid obfuscated server UDP packet: padding too large"));
            result.length = len;
            return result;
        }

        if (paddingByte > 0) {
            rc4Crypt(static_cast<uint8*>(nullptr), paddingByte, keyReceiveKey);
            remaining -= paddingByte;
        }

        result.data = &buf[len - remaining];
        rc4Crypt(result.data, static_cast<uint32>(remaining), keyReceiveKey);
        result.length = remaining;
    }

    return result;
}

// ---------------------------------------------------------------------------
// encryptSendServer
// ---------------------------------------------------------------------------

uint32 EncryptedDatagramSocket::encryptSendServer(uint8* buf, uint32 len, uint32 baseKey)
{
    const uint16 randomKeyPart = getRandomUInt16();

    uint8 keyData[7];
    pokeUInt32(keyData, baseKey);
    keyData[4] = kMagicValueUDPClientServer;
    pokeUInt16(&keyData[5], randomKeyPart);
    MD5Hasher md5(keyData, sizeof keyData);

    RC4Key keySendKey = rc4CreateKey({md5.getRawHash(), 16}, true);

    // Generate marker
    uint8 marker;
    for (int i = 8; i > 0; --i) {
        marker = static_cast<uint8>(getRandomUInt16() & 0xFF);
        if (marker != OP_EDONKEYPROT)
            break;
    }
    if (marker == OP_EDONKEYPROT)
        marker = 0x01;

    buf[0] = marker;
    pokeUInt16(&buf[1], randomKeyPart);
    pokeUInt32(&buf[3], kMagicValueUDPSyncServer);
    buf[7] = kCryptHeaderPadding;

    const uint32 cryptHeaderLen = static_cast<uint32>(encryptOverheadSize(false));
    len += cryptHeaderLen;
    rc4Crypt(&buf[3], len - 3, keySendKey);

    return len;
}

// ---------------------------------------------------------------------------
// encryptOverheadSize
// ---------------------------------------------------------------------------

int EncryptedDatagramSocket::encryptOverheadSize(bool isKad)
{
    return isKad ? kCryptHeaderKad : kCryptHeaderSize;
}

} // namespace eMule
