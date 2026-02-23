#pragma once

/// @file EncryptedStreamSocket.h
/// @brief RC4 obfuscation protocol over QTcpSocket — replaces MFC CEncryptedStreamSocket.
///
/// Implements eMule's custom protocol obfuscation (NOT standard TLS) for TCP connections.
/// Uses RC4 stream cipher with MD5-derived keys. Must be protocol-compatible with
/// existing eMule clients/servers.

#include "utils/Types.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QTcpSocket>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace eMule {

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

inline constexpr int kErrWrongHeader = 0x01;
inline constexpr int kErrTooBig = 0x02;
inline constexpr int kErrEncryption = 0x03;
inline constexpr int kErrEncryptionNotAllowed = 0x04;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/// State of the encryption layer on this socket.
enum class StreamCryptState : uint8 {
    None = 0,       ///< Disabled or not available.
    Unknown,        ///< Incoming connection; will test first incoming data.
    Pending,        ///< Outgoing client connection; will start handshake.
    PendingServer,  ///< Outgoing server connection; will start DH handshake.
    Negotiating,    ///< Handshake in progress.
    Encrypting      ///< Encryption enabled and active.
};

/// Sub-state during handshake negotiation.
enum class NegotiatingState : uint8 {
    None,
    ClientA_RandomPart,
    ClientA_MagicValue,
    ClientA_MethodTagsPadLen,
    ClientA_Padding,
    ClientB_MagicValue,
    ClientB_MethodTagsPadLen,
    ClientB_Padding,
    Server_DHAnswer,
    Server_MagicValue,
    Server_MethodTagsPadLen,
    Server_Padding,
    Server_DelayedSending,
    Complete
};

/// Supported encryption methods.
enum class EncryptionMethod : uint8 {
    Obfuscation = 0x00
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Encryption configuration passed to the socket (replaces thePrefs access).
struct ObfuscationConfig {
    bool cryptLayerEnabled = false;
    bool cryptLayerRequired = false;
    bool cryptLayerRequiredStrict = false;
    std::array<uint8, 16> userHash{};
    uint8 cryptTCPPaddingLength = 128;  ///< Max random padding (0-255).
};

// ---------------------------------------------------------------------------
// EncryptedStreamSocket
// ---------------------------------------------------------------------------

/// TCP socket with eMule's RC4 obfuscation protocol.
///
/// Subclass this instead of QTcpSocket. Before connecting, call
/// setObfuscationConfig() and setConnectionEncryption().
class EncryptedStreamSocket : public QTcpSocket {
    Q_OBJECT

public:
    explicit EncryptedStreamSocket(QObject* parent = nullptr);
    ~EncryptedStreamSocket() override;

    /// Set the encryption configuration.
    void setObfuscationConfig(const ObfuscationConfig& config);

    /// Configure encryption for an outgoing connection.
    /// @param enabled              Enable obfuscation.
    /// @param targetClientHash     Remote client's user hash (16 bytes), or nullptr.
    /// @param serverConnection     True if connecting to a server (DH key exchange).
    void setConnectionEncryption(bool enabled, const uint8* targetClientHash, bool serverConnection);

    /// Encrypt outgoing payload data in-place (after handshake is complete).
    void cryptPrepareSendData(uint8* buffer, uint32 len);

    /// Whether the socket is currently encrypting data.
    [[nodiscard]] bool isObfuscating() const;

    /// Whether the encryption layer is ready for payload send/receive.
    [[nodiscard]] bool isEncryptionLayerReady() const;

    /// How many bytes were actually received (including obfuscation overhead).
    [[nodiscard]] int getRealReceivedBytes() const { return m_obfuscatedBytesReceived; }

    /// Whether this is a server-side encrypted connection.
    [[nodiscard]] bool isServerCryptEnabledConnection() const { return m_serverCrypt; }

signals:
    /// Emitted on encryption handshake failure.
    void encryptionError(int errorCode);

protected:
    /// Must be implemented by subclasses to handle socket errors.
    virtual void onError(int errorCode) = 0;

    /// Called when the encryption handshake completes.  Subclasses can
    /// override to defer connection-established until the crypto layer is
    /// ready.  Default implementation does nothing.
    virtual void onEncryptionHandshakeComplete() {}

    /// Get a string representation of the peer IP (for logging).
    [[nodiscard]] QString dbgGetIPString() const;

    /// Generate a random byte that doesn't match any protocol header.
    [[nodiscard]] static uint8 getSemiRandomNotProtocolMarker();

    /// Called when the socket is connected and ready to send.
    void onSocketConnected();

    /// Process received raw data through the encryption layer.
    /// @return Number of decrypted bytes available, or 0 if handshake consumed all data.
    int processReceivedData(void* buf, int len);

    /// Whether the encryption handshake is currently in progress.
    [[nodiscard]] bool isNegotiating() const { return m_streamCryptState == StreamCryptState::Negotiating; }

    StreamCryptState m_streamCryptState = StreamCryptState::None;
    EncryptionMethod m_encryptionMethod = EncryptionMethod::Obfuscation;
    bool m_fullReceive = true;
    bool m_serverCrypt = false;

private:
    int negotiate(const uint8* buffer, int len);
    void startNegotiation(bool outgoing);
    int sendNegotiatingData(const void* buf, int bufLen, int startCryptFromByte = 0, bool delaySend = false);

    ObfuscationConfig m_config;
    std::optional<RC4Key> m_sendKey;
    std::optional<RC4Key> m_receiveKey;
    std::unique_ptr<SafeMemFile> m_receiveBuffer;
    std::unique_ptr<SafeMemFile> m_sendBuffer;
    uint32 m_randomKeyPart = 0;
    int m_receiveBytesWanted = 0;
    int m_obfuscatedBytesReceived = 0;
    NegotiatingState m_negotiatingState = NegotiatingState::None;

    // DH key exchange data for server connections
    std::array<uint8, 16> m_dhExponentA{};
};

} // namespace eMule
