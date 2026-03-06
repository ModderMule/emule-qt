#include "pch.h"
/// @file EncryptedStreamSocket.cpp
/// @brief RC4 obfuscation protocol implementation for TCP sockets.
///
/// Protocol specification (from original eMule):
///
/// Client <-> Client:
///   SendKey    = MD5(<UserHashB 16><MagicValue34 1><RandomKeyPartA 4>)
///   ReceiveKey = MD5(<UserHashB 16><MagicValue203 1><RandomKeyPartA 4>)
///   First 1024 bytes discarded (RC4-drop[1024]).
///
///   Client A sends: <SemiRandomMarker 1[plain]><RandomKeyPart 4[plain]><MagicValue 4><Methods 1><Preferred 1><PadLen 1><Padding>
///   Client B sends: <MagicValue 4><MethodSelected 1><PadLen 1><Padding>
///
/// Client -> Server (DH):
///   Client sends: <SemiRandomMarker 1[plain]><g^a 96[plain]><PadLen+Padding>
///   Server sends: <g^b 96[plain]><MagicValue 4><Methods 1><Preferred 1><PadLen 1><Padding>
///   Client sends: <MagicValue 4><MethodSelected 1><PadLen 1><Padding> (delayed with first payload)
///   Keys derived from DH shared secret S: SendKey=MD5(<S 96><34>), RecvKey=MD5(<S 96><203>)

#include "net/EncryptedStreamSocket.h"
#include "crypto/MD5Hash.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"

#include <openssl/bn.h>

#include <cstring>

namespace eMule {

namespace {

constexpr uint8 kMagicValueRequester = 34;
constexpr uint8 kMagicValueServer = 203;
constexpr uint32 kMagicValueSync = 0x835E6FC4u;

constexpr int kPrimeSizeBytes = 96;

// 768-bit DH prime used for server obfuscation key exchange
const unsigned char dh768_p[] = {
    0xF2, 0xBF, 0x52, 0xC5, 0x5F, 0x58, 0x7A, 0xDD, 0x53, 0x71, 0xA9, 0x36,
    0xE8, 0x86, 0xEB, 0x3C, 0x62, 0x17, 0xA3, 0x3E, 0xC3, 0x4C, 0xB4, 0x0D,
    0xC7, 0x3A, 0x41, 0xA6, 0x43, 0xAF, 0xFC, 0xE7, 0x21, 0xFC, 0x28, 0x63,
    0x66, 0x53, 0x5B, 0xDB, 0xCE, 0x25, 0x9F, 0x22, 0x86, 0xDA, 0x4A, 0x91,
    0xB2, 0x07, 0xCB, 0xAA, 0x52, 0x55, 0xD4, 0xF6, 0x1C, 0xCE, 0xAE, 0xD4,
    0x5A, 0xD5, 0xE0, 0x74, 0x7D, 0xF7, 0x78, 0x18, 0x28, 0x10, 0x5F, 0x34,
    0x0F, 0x76, 0x23, 0x87, 0xF8, 0x8B, 0x28, 0x91, 0x42, 0xFB, 0x42, 0x68,
    0x8F, 0x05, 0x15, 0x0F, 0x54, 0x8B, 0x5F, 0x43, 0x6A, 0xF7, 0x0D, 0xF3
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

EncryptedStreamSocket::EncryptedStreamSocket(QObject* parent)
    : QTcpSocket(parent)
{
}

EncryptedStreamSocket::~EncryptedStreamSocket() = default;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void EncryptedStreamSocket::setObfuscationConfig(const ObfuscationConfig& config)
{
    m_config = config;
    if (m_streamCryptState == StreamCryptState::None && config.cryptLayerEnabled)
        m_streamCryptState = StreamCryptState::Unknown;
}

void EncryptedStreamSocket::setConnectionEncryption(bool enabled, const uint8* targetClientHash, bool serverConnection)
{
    if (m_streamCryptState != StreamCryptState::Unknown && m_streamCryptState != StreamCryptState::None)
        return;

    if (enabled && targetClientHash != nullptr && !serverConnection) {
        m_streamCryptState = StreamCryptState::Pending;

        // Create obfuscation keys
        m_randomKeyPart = getRandomUInt32();

        uint8 keyData[21];
        md4cpy(keyData, targetClientHash);
        keyData[16] = kMagicValueRequester;
        pokeUInt32(&keyData[17], m_randomKeyPart);
        MD5Hasher md5Send(keyData, sizeof keyData);
        m_sendKey = rc4CreateKey({md5Send.getRawHash(), 16});

        keyData[16] = kMagicValueServer;
        MD5Hasher md5Recv(keyData, sizeof keyData);
        m_receiveKey = rc4CreateKey({md5Recv.getRawHash(), 16});
    } else if (serverConnection && enabled) {
        m_serverCrypt = true;
        m_streamCryptState = StreamCryptState::PendingServer;
    } else {
        m_streamCryptState = StreamCryptState::None;
    }
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void EncryptedStreamSocket::cryptPrepareSendData(uint8* buffer, uint32 len)
{
    if (!isEncryptionLayerReady())
        return;

    if (m_streamCryptState == StreamCryptState::Unknown) {
        m_streamCryptState = StreamCryptState::None;
        logWarning(QStringLiteral("EncryptedStreamSocket: Premature send, overwriting ECS_UNKNOWN with ECS_NONE (%1)").arg(dbgGetIPString()));
    }

    if (m_streamCryptState == StreamCryptState::Encrypting && m_sendKey) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("cryptPrepareSendData: encrypting %1 bytes, sendKey x=%2 y=%3 peer=%4:%5")
                         .arg(len).arg(m_sendKey->x).arg(m_sendKey->y)
                         .arg(peerAddress().toString()).arg(peerPort()));
        rc4Crypt(buffer, len, *m_sendKey);
    }
}

bool EncryptedStreamSocket::isObfuscating() const
{
    return m_streamCryptState == StreamCryptState::Encrypting
        && m_encryptionMethod == EncryptionMethod::Obfuscation;
}

bool EncryptedStreamSocket::isEncryptionLayerReady() const
{
    return (m_streamCryptState == StreamCryptState::None
         || m_streamCryptState == StreamCryptState::Encrypting
         || m_streamCryptState == StreamCryptState::Unknown)
        && (!m_sendBuffer || (m_serverCrypt && m_negotiatingState == NegotiatingState::Server_DelayedSending));
}

// ---------------------------------------------------------------------------
// Socket event handlers
// ---------------------------------------------------------------------------

void EncryptedStreamSocket::onSocketConnected()
{
    if (m_streamCryptState == StreamCryptState::Pending || m_streamCryptState == StreamCryptState::PendingServer) {
        startNegotiation(true);
        return;
    }
    if (m_sendBuffer) {
        sendNegotiatingData(nullptr, 0);
    }
}

int EncryptedStreamSocket::processReceivedData(void* buf, int len)
{
    m_obfuscatedBytesReceived = len;
    m_fullReceive = true; // simplified; original tracked if recv returned exactly requested size

    if (len <= 0)
        return len;

    switch (m_streamCryptState) {
    case StreamCryptState::None:
        break;

    case StreamCryptState::Unknown: {
        auto firstByte = *static_cast<uint8*>(buf);
        switch (firstByte) {
        case OP_EDONKEYPROT:
        case OP_PACKEDPROT:
        case OP_EMULEPROT:
            // Normal unencrypted header
            break;
        default: {
            int nRead = 1;
            startNegotiation(false);
            int nNegRes = negotiate(static_cast<uint8*>(buf) + nRead, len - nRead);
            if (nNegRes != -1) {
                nRead += nNegRes;
                if (nRead != len) {
                    logWarning(QStringLiteral("EncryptedStreamSocket: Client %1 sent more data than expected while negotiating (1)").arg(dbgGetIPString()));
                    onError(kErrEncryption);
                }
            }
            return 0;
        }
        }
        // Not encrypted
        m_streamCryptState = StreamCryptState::None;

        if (m_config.cryptLayerRequired) {
            if (m_config.cryptLayerRequiredStrict) {
                onError(kErrEncryptionNotAllowed);
                return 0;
            }
            // Allow unencrypted for firewall checks (simplified — full check needs server connect info)
        }
        break;
    }

    case StreamCryptState::Pending:
    case StreamCryptState::PendingServer:
        logWarning(QStringLiteral("EncryptedStreamSocket: Received data before sending on outgoing connection"));
        m_streamCryptState = StreamCryptState::None;
        break;

    case StreamCryptState::Negotiating: {
        int nRead = negotiate(static_cast<uint8*>(buf), len);
        if (nRead != -1 && nRead != len) {
            if (m_streamCryptState == StreamCryptState::Encrypting) {
                // Finished handshake with extra payload — decrypt the remaining bytes.
                // MFC CEncryptedStreamSocket::OnReceive does RC4Crypt on leftover data.
                auto* remaining = static_cast<uint8*>(buf) + nRead;
                int remainingLen = len - nRead;
                if (m_receiveKey)
                    rc4Crypt(remaining, static_cast<uint32>(remainingLen), *m_receiveKey);
                std::memmove(buf, remaining, static_cast<std::size_t>(remainingLen));
                return remainingLen;
            }
            logWarning(QStringLiteral("EncryptedStreamSocket: Client %1 sent more data than expected while negotiating (2)").arg(dbgGetIPString()));
            onError(kErrEncryption);
        }
        return 0;
    }

    case StreamCryptState::Encrypting:
        if (m_receiveKey)
            rc4Crypt(static_cast<uint8*>(buf), static_cast<uint32>(len), *m_receiveKey);
        if (thePrefs.logRawSocketPackets()) {
            // Debug: dump first bytes after decryption
            int dumpLen = std::min(len, 16);
            QString hex;
            for (int i = 0; i < dumpLen; ++i)
                hex += QStringLiteral("%1 ").arg(static_cast<const uint8*>(buf)[i], 2, 16, QLatin1Char('0'));
            logDebug(QStringLiteral("EncryptedStreamSocket::Encrypting — decrypted %1 bytes, first: %2 (peer %3:%4)")
                         .arg(len).arg(hex).arg(peerAddress().toString()).arg(peerPort()));
        }
        break;
    }

    return len;
}

// ---------------------------------------------------------------------------
// Handshake initiation
// ---------------------------------------------------------------------------

void EncryptedStreamSocket::startNegotiation(bool outgoing)
{
    if (!outgoing) {
        // Incoming: wait for client A's random key part
        m_negotiatingState = NegotiatingState::ClientA_RandomPart;
        m_streamCryptState = StreamCryptState::Negotiating;
        m_receiveBytesWanted = 4;
    } else if (m_streamCryptState == StreamCryptState::Pending) {
        // Outgoing client connection
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EncryptedStreamSocket::startNegotiation(outgoing) — sending handshake to %1:%2")
                         .arg(peerAddress().toString()).arg(peerPort()));
        SafeMemFile fileRequest;
        const uint8 marker = getSemiRandomNotProtocolMarker();
        fileRequest.writeUInt8(marker);
        fileRequest.writeUInt32(m_randomKeyPart);
        fileRequest.writeUInt32(kMagicValueSync);
        const uint8 supportedMethod = static_cast<uint8>(EncryptionMethod::Obfuscation);
        fileRequest.writeUInt8(supportedMethod);
        fileRequest.writeUInt8(supportedMethod); // preferred
        uint8 padding = static_cast<uint8>(getRandomUInt16() % (m_config.cryptTCPPaddingLength + 1));
        fileRequest.writeUInt8(padding);
        for (int i = padding; --i >= 0;)
            fileRequest.writeUInt8(static_cast<uint8>(getRandomUInt16() & 0xFF));

        m_negotiatingState = NegotiatingState::ClientB_MagicValue;
        m_streamCryptState = StreamCryptState::Negotiating;
        m_receiveBytesWanted = 4;

        const auto& buf = fileRequest.buffer();
        sendNegotiatingData(buf.constData(), static_cast<int>(buf.size()), 5);
    } else if (m_streamCryptState == StreamCryptState::PendingServer) {
        // Server DH connection — generate g^a mod p and send to server
        SafeMemFile fileRequest;
        const uint8 marker = getSemiRandomNotProtocolMarker();
        fileRequest.writeUInt8(marker);

        // Generate random 128-bit exponent a
        for (auto& byte : m_dhExponentA)
            byte = static_cast<uint8>(getRandomUInt16() & 0xFF);

        // Compute g^a mod p using OpenSSL BIGNUM
        BN_CTX* ctx = BN_CTX_new();
        BIGNUM* bnA = BN_bin2bn(m_dhExponentA.data(), static_cast<int>(m_dhExponentA.size()), nullptr);
        BIGNUM* bnP = BN_bin2bn(dh768_p, kPrimeSizeBytes, nullptr);
        BIGNUM* bnG = BN_new();
        BN_set_word(bnG, 2);
        BIGNUM* bnResult = BN_new();

        BN_mod_exp(bnResult, bnG, bnA, bnP, ctx);

        // Encode g^a as 96 bytes big-endian
        uint8 aBuffer[kPrimeSizeBytes]{};
        int resultBytes = BN_num_bytes(bnResult);
        BN_bn2bin(bnResult, aBuffer + (kPrimeSizeBytes - resultBytes));

        BN_free(bnResult);
        BN_free(bnG);
        BN_free(bnP);
        BN_free(bnA);
        BN_CTX_free(ctx);

        fileRequest.write(aBuffer, kPrimeSizeBytes);

        // Add random padding (0-15 bytes)
        uint8 padding = static_cast<uint8>(getRandomUInt16() % 16);
        fileRequest.writeUInt8(padding);
        for (int i = padding; --i >= 0;)
            fileRequest.writeUInt8(static_cast<uint8>(getRandomUInt16() & 0xFF));

        m_negotiatingState = NegotiatingState::Server_DHAnswer;
        m_streamCryptState = StreamCryptState::Negotiating;
        m_receiveBytesWanted = kPrimeSizeBytes;

        const auto& buf = fileRequest.buffer();
        sendNegotiatingData(buf.constData(), static_cast<int>(buf.size()), static_cast<int>(buf.size()));
    } else {
        m_streamCryptState = StreamCryptState::None;
    }
}

// ---------------------------------------------------------------------------
// Handshake negotiation state machine
// ---------------------------------------------------------------------------

int EncryptedStreamSocket::negotiate(const uint8* buffer, int len)
{
    try {
        int nRead = 0;
        while (m_negotiatingState != NegotiatingState::Complete && m_receiveBytesWanted > 0) {
            if (m_receiveBytesWanted > 512)
                return 0;

            if (!m_receiveBuffer)
                m_receiveBuffer = std::make_unique<SafeMemFile>();

            const int toRead = std::min(len - nRead, m_receiveBytesWanted);
            m_receiveBuffer->write(buffer + nRead, toRead);
            nRead += toRead;
            m_receiveBytesWanted -= toRead;
            if (m_receiveBytesWanted > 0)
                return nRead;

            const uint32 currentBytesLen = static_cast<uint32>(m_receiveBuffer->position());

            // Decrypt buffered data (unless we're still reading the unencrypted random key part)
            if (m_negotiatingState != NegotiatingState::ClientA_RandomPart
                && m_negotiatingState != NegotiatingState::Server_DHAnswer
                && m_receiveKey) {
                // Read back, decrypt, rewrite
                QByteArray data = m_receiveBuffer->buffer();
                rc4Crypt(reinterpret_cast<uint8*>(data.data()), currentBytesLen, *m_receiveKey);
                m_receiveBuffer = std::make_unique<SafeMemFile>(data);
            } else {
                // Seek to beginning for reading
                m_receiveBuffer->seek(0, SEEK_SET);
            }

            switch (m_negotiatingState) {
            case NegotiatingState::None:
                return 0;

            case NegotiatingState::ClientA_RandomPart: {
                // Incoming connection: read remote client's random key part and derive keys
                uint8 keyData[21];
                md4cpy(keyData, m_config.userHash.data());
                keyData[16] = kMagicValueRequester;
                m_receiveBuffer->read(&keyData[17], 4);

                MD5Hasher md5Recv(keyData, sizeof keyData);
                m_receiveKey = rc4CreateKey({md5Recv.getRawHash(), 16});

                keyData[16] = kMagicValueServer;
                MD5Hasher md5Send(keyData, sizeof keyData);
                m_sendKey = rc4CreateKey({md5Send.getRawHash(), 16});

                m_negotiatingState = NegotiatingState::ClientA_MagicValue;
                m_receiveBytesWanted = 4;
                break;
            }

            case NegotiatingState::ClientA_MagicValue: {
                uint32 value = m_receiveBuffer->readUInt32();
                if (value != kMagicValueSync) {
                    logWarning(QStringLiteral("EncryptedStreamSocket: Wrong magic value from %1").arg(dbgGetIPString()));
                    onError(kErrEncryption);
                    return -1;
                }
                m_negotiatingState = NegotiatingState::ClientA_MethodTagsPadLen;
                m_receiveBytesWanted = 3;
                break;
            }

            case NegotiatingState::ClientA_MethodTagsPadLen: {
                /*uint8 encryptionSupported =*/ m_receiveBuffer->readUInt8();
                /*uint8 encryptionRequested =*/ m_receiveBuffer->readUInt8();
                m_receiveBytesWanted = m_receiveBuffer->readUInt8();
                m_negotiatingState = NegotiatingState::ClientA_Padding;
                if (m_receiveBytesWanted > 0)
                    break;
            }
            [[fallthrough]];

            case NegotiatingState::ClientA_Padding: {
                // Ignore padding, send response
                SafeMemFile fileResponse;
                fileResponse.writeUInt32(kMagicValueSync);
                const uint8 selectedMethod = static_cast<uint8>(EncryptionMethod::Obfuscation);
                fileResponse.writeUInt8(selectedMethod);
                uint8 padding = static_cast<uint8>(getRandomUInt16() % (m_config.cryptTCPPaddingLength + 1));
                fileResponse.writeUInt8(padding);
                for (int i = padding; --i >= 0;)
                    fileResponse.writeUInt8(static_cast<uint8>(getRandomUInt16() & 0xFF));

                const auto& responseBuf = fileResponse.buffer();
                sendNegotiatingData(responseBuf.constData(), static_cast<int>(responseBuf.size()));
                m_negotiatingState = NegotiatingState::Complete;
                m_streamCryptState = StreamCryptState::Encrypting;
                onEncryptionHandshakeComplete();
                break;
            }

            case NegotiatingState::ClientB_MagicValue: {
                uint32 receivedMagic = m_receiveBuffer->readUInt32();
                if (thePrefs.logRawSocketPackets())
                    logDebug(QStringLiteral("EncryptedStreamSocket: ClientB_MagicValue — received 0x%1, expected 0x%2 from %3:%4")
                                 .arg(receivedMagic, 8, 16, QLatin1Char('0'))
                                 .arg(kMagicValueSync, 8, 16, QLatin1Char('0'))
                                 .arg(peerAddress().toString()).arg(peerPort()));
                if (receivedMagic != kMagicValueSync) {
                    logWarning(QStringLiteral("EncryptedStreamSocket: Wrong sync magic from %1").arg(dbgGetIPString()));
                    onError(kErrEncryption);
                    return -1;
                }
                m_negotiatingState = NegotiatingState::ClientB_MethodTagsPadLen;
                m_receiveBytesWanted = 2;
                break;
            }

            case NegotiatingState::ClientB_MethodTagsPadLen: {
                uint8 methodSet = m_receiveBuffer->readUInt8();
                if (methodSet != static_cast<uint8>(EncryptionMethod::Obfuscation)) {
                    logWarning(QStringLiteral("EncryptedStreamSocket: Unsupported encryption method %1 from %2").arg(methodSet).arg(dbgGetIPString()));
                    onError(kErrEncryption);
                    return -1;
                }
                m_receiveBytesWanted = m_receiveBuffer->readUInt8();
                m_negotiatingState = NegotiatingState::ClientB_Padding;
                if (m_receiveBytesWanted > 0)
                    break;
            }
            [[fallthrough]];

            case NegotiatingState::ClientB_Padding:
                m_negotiatingState = NegotiatingState::Complete;
                m_streamCryptState = StreamCryptState::Encrypting;
                if (thePrefs.logRawSocketPackets())
                    logDebug(QStringLiteral("negotiate: handshake complete — sendKey x=%1 y=%2, recvKey x=%3 y=%4, peer=%5:%6")
                                 .arg(m_sendKey ? m_sendKey->x : -1).arg(m_sendKey ? m_sendKey->y : -1)
                                 .arg(m_receiveKey ? m_receiveKey->x : -1).arg(m_receiveKey ? m_receiveKey->y : -1)
                                 .arg(peerAddress().toString()).arg(peerPort()));
                onEncryptionHandshakeComplete();
                break;

            case NegotiatingState::Server_DHAnswer: {
                // Read server's g^b, compute shared secret S = (g^b)^a mod p
                uint8 aBuffer[kPrimeSizeBytes + 1]{};
                m_receiveBuffer->read(aBuffer, kPrimeSizeBytes);

                BN_CTX* ctx = BN_CTX_new();
                BIGNUM* bnGbModP = BN_bin2bn(aBuffer, kPrimeSizeBytes, nullptr);
                BIGNUM* bnP = BN_bin2bn(dh768_p, kPrimeSizeBytes, nullptr);
                BIGNUM* bnA = BN_bin2bn(m_dhExponentA.data(), static_cast<int>(m_dhExponentA.size()), nullptr);
                BIGNUM* bnResult = BN_new();

                BN_mod_exp(bnResult, bnGbModP, bnA, bnP, ctx);

                // Clear the exponent
                m_dhExponentA.fill(0);

                // Encode shared secret as 96 bytes big-endian
                std::memset(aBuffer, 0, sizeof(aBuffer));
                int resultBytes = BN_num_bytes(bnResult);
                BN_bn2bin(bnResult, aBuffer + (kPrimeSizeBytes - resultBytes));

                BN_free(bnResult);
                BN_free(bnA);
                BN_free(bnP);
                BN_free(bnGbModP);
                BN_CTX_free(ctx);

                // Derive RC4 keys from shared secret
                aBuffer[kPrimeSizeBytes] = kMagicValueRequester;
                MD5Hasher md5Send(aBuffer, kPrimeSizeBytes + 1);
                m_sendKey = rc4CreateKey({md5Send.getRawHash(), 16});

                aBuffer[kPrimeSizeBytes] = kMagicValueServer;
                MD5Hasher md5Recv(aBuffer, kPrimeSizeBytes + 1);
                m_receiveKey = rc4CreateKey({md5Recv.getRawHash(), 16});

                m_negotiatingState = NegotiatingState::Server_MagicValue;
                m_receiveBytesWanted = 4;
                break;
            }

            case NegotiatingState::Server_MagicValue: {
                uint32 value = m_receiveBuffer->readUInt32();
                if (value != kMagicValueSync) {
                    logWarning(QStringLiteral("EncryptedStreamSocket: Wrong magic value after DH from server %1").arg(dbgGetIPString()));
                    onError(kErrEncryption);
                    return -1;
                }
                m_negotiatingState = NegotiatingState::Server_MethodTagsPadLen;
                m_receiveBytesWanted = 3;
                break;
            }

            case NegotiatingState::Server_MethodTagsPadLen: {
                /*uint8 encryptionSupported =*/ m_receiveBuffer->readUInt8();
                /*uint8 encryptionRequested =*/ m_receiveBuffer->readUInt8();
                m_receiveBytesWanted = m_receiveBuffer->readUInt8();
                m_negotiatingState = NegotiatingState::Server_Padding;
                if (m_receiveBytesWanted > 0)
                    break;
            }
            [[fallthrough]];

            case NegotiatingState::Server_Padding: {
                // Ignore padding, send delayed response
                SafeMemFile fileResponse;
                fileResponse.writeUInt32(kMagicValueSync);
                const auto selectedMethod = static_cast<uint8>(EncryptionMethod::Obfuscation);
                fileResponse.writeUInt8(selectedMethod);
                auto padding = static_cast<uint8>(getRandomUInt16() % 16);
                fileResponse.writeUInt8(padding);
                for (int i = padding; --i >= 0;)
                    fileResponse.writeUInt8(static_cast<uint8>(getRandomUInt16() & 0xFF));

                m_negotiatingState = NegotiatingState::Server_DelayedSending;
                const auto& responseBuf = fileResponse.buffer();
                sendNegotiatingData(responseBuf.constData(), static_cast<int>(responseBuf.size()), 0, true);
                m_streamCryptState = StreamCryptState::Encrypting;
                onEncryptionHandshakeComplete();
                break;
            }

            case NegotiatingState::Server_DelayedSending:
                break;

            case NegotiatingState::Complete:
                break;
            }

            // Reset receive buffer for next state
            m_receiveBuffer.reset();
        }

        return nRead;
    } catch (const std::exception& ex) {
        logWarning(QStringLiteral("EncryptedStreamSocket: Exception during negotiation: %1").arg(QLatin1StringView(ex.what())));
        onError(kErrEncryption);
        m_receiveBuffer.reset();
        return -1;
    }
}

// ---------------------------------------------------------------------------
// Send negotiating data
// ---------------------------------------------------------------------------

int EncryptedStreamSocket::sendNegotiatingData(const void* buf, int bufLen, int startCryptFromByte, bool delaySend)
{
    QByteArray encrypted;
    if (buf != nullptr) {
        encrypted.resize(bufLen);
        const auto* src = static_cast<const uint8*>(buf);

        // Copy plaintext prefix
        if (startCryptFromByte > 0)
            std::memcpy(encrypted.data(), src, static_cast<std::size_t>(startCryptFromByte));

        // Encrypt the rest
        if (bufLen > startCryptFromByte && m_sendKey) {
            rc4Crypt(src + startCryptFromByte,
                     reinterpret_cast<uint8*>(encrypted.data()) + startCryptFromByte,
                     static_cast<uint32>(bufLen - startCryptFromByte), *m_sendKey);
        }

        // If we already have a pending send buffer, append to it
        if (m_sendBuffer) {
            if (m_negotiatingState == NegotiatingState::Server_DelayedSending)
                m_negotiatingState = NegotiatingState::Complete;
            m_sendBuffer->seek(0, SEEK_END);
            m_sendBuffer->write(encrypted.constData(), encrypted.size());
            encrypted.clear();
        }
    }

    if (encrypted.isEmpty() && buf == nullptr) {
        // Processing pending data
        if (!m_sendBuffer)
            return 0;
        encrypted = m_sendBuffer->takeBuffer();
        m_sendBuffer.reset();
    }

    if (encrypted.isEmpty() && m_sendBuffer) {
        // Data was appended to existing send buffer — try to flush it
        QByteArray pending = m_sendBuffer->takeBuffer();
        m_sendBuffer.reset();
        encrypted = std::move(pending);
    }

    if (delaySend) {
        m_sendBuffer = std::make_unique<SafeMemFile>();
        m_sendBuffer->write(encrypted.constData(), encrypted.size());
        return 0;
    }

    qint64 result = write(encrypted.constData(), encrypted.size());
    if (result < 0) {
        // Write failed, buffer it
        m_sendBuffer = std::make_unique<SafeMemFile>();
        m_sendBuffer->write(encrypted.constData(), encrypted.size());
    } else if (result < encrypted.size()) {
        // Partial write
        m_sendBuffer = std::make_unique<SafeMemFile>();
        m_sendBuffer->write(encrypted.constData() + result, encrypted.size() - result);
    }

    return static_cast<int>(result);
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

QString EncryptedStreamSocket::dbgGetIPString() const
{
    return peerAddress().toString();
}

uint8 EncryptedStreamSocket::getSemiRandomNotProtocolMarker()
{
    for (int i = 32; --i >= 0;) {
        uint8 marker = static_cast<uint8>(getRandomUInt16() & 0xFF);
        switch (marker) {
        case OP_EDONKEYPROT:
        case OP_PACKEDPROT:
        case OP_EMULEPROT:
            continue;
        }
        return marker;
    }
    return 0x01;
}

} // namespace eMule
