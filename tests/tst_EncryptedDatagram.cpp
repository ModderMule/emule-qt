/// @file tst_EncryptedDatagram.cpp
/// @brief Tests for EncryptedDatagramSocket — encrypt/decrypt roundtrips.

#include "TestHelpers.h"
#include "net/EncryptedDatagramSocket.h"
#include "crypto/MD5Hash.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QByteArray>
#include <QTest>

#include <array>
#include <cstring>
#include <vector>

using namespace eMule;

class tst_EncryptedDatagram : public QObject {
    Q_OBJECT

private slots:
    void clientED2K_encryptDecryptRoundtrip();
    void clientKadNodeID_encryptDecryptRoundtrip();
    void clientKadRecvKey_encryptDecryptRoundtrip();
    void serverDecrypt_recoversServerEncodedReply();
    void serverDecrypt_recoversReplyWithPadding();
    void overheadSize_ed2k();
    void overheadSize_kad();
    void nonEncryptedPassthrough_protocolMarker();
};

// ---------------------------------------------------------------------------
// Client ED2K encrypt → decrypt roundtrip
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::clientED2K_encryptDecryptRoundtrip()
{
    // Set up keys
    std::array<uint8, 16> userHash{};
    for (std::size_t i = 0; i < 16; ++i)
        userHash[i] = static_cast<uint8>(i + 1);

    uint32 publicIP = 0x0A000001; // 10.0.0.1

    // Original payload
    const char* payload = "Hello eMule!";
    const uint32 payloadLen = 12;

    // Allocate buffer with room for header
    uint32 overhead = static_cast<uint32>(EncryptedDatagramSocket::encryptOverheadSize(false));
    std::vector<uint8> buf(payloadLen + overhead + 16, 0);

    // Copy payload after overhead area
    std::memcpy(buf.data() + overhead, payload, payloadLen);

    // Encrypt
    uint32 encryptedLen = EncryptedDatagramSocket::encryptSendClient(
        buf.data(), payloadLen,
        userHash.data(), false,
        0, 0, publicIP);

    QVERIFY(encryptedLen > payloadLen);
    QCOMPARE(encryptedLen, payloadLen + overhead);

    // Decrypt
    DecryptResult result = EncryptedDatagramSocket::decryptReceivedClient(
        buf.data(), static_cast<int>(encryptedLen), publicIP,
        userHash.data(), nullptr, 0);

    QVERIFY(result.length == static_cast<int>(payloadLen));
    QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
}

// ---------------------------------------------------------------------------
// Client Kad NodeID encrypt → decrypt roundtrip
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::clientKadNodeID_encryptDecryptRoundtrip()
{
    std::array<uint8, 16> kadID{};
    for (std::size_t i = 0; i < 16; ++i)
        kadID[i] = static_cast<uint8>(0xA0 + i);

    const char* payload = "KadTest";
    const uint32 payloadLen = 7;

    uint32 overhead = static_cast<uint32>(EncryptedDatagramSocket::encryptOverheadSize(true));
    std::vector<uint8> buf(payloadLen + overhead + 16, 0);
    std::memcpy(buf.data() + overhead, payload, payloadLen);

    uint32 recvKey = 12345;
    uint32 sendKey = 67890;

    uint32 encryptedLen = EncryptedDatagramSocket::encryptSendClient(
        buf.data(), payloadLen,
        kadID.data(), true,
        recvKey, sendKey, 0);

    QVERIFY(encryptedLen > payloadLen);

    // Decrypt
    DecryptResult result = EncryptedDatagramSocket::decryptReceivedClient(
        buf.data(), static_cast<int>(encryptedLen), 0,
        nullptr, kadID.data(), 0);

    QCOMPARE(result.length, static_cast<int>(payloadLen));
    QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
    QCOMPARE(result.receiverVerifyKey, recvKey);
    QCOMPARE(result.senderVerifyKey, sendKey);
}

// ---------------------------------------------------------------------------
// Client Kad ReceiverKey encrypt → decrypt roundtrip
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::clientKadRecvKey_encryptDecryptRoundtrip()
{
    uint32 kadRecvKeyVal = 0x12345678;

    const char* payload = "RecvKey";
    const uint32 payloadLen = 7;

    uint32 overhead = static_cast<uint32>(EncryptedDatagramSocket::encryptOverheadSize(true));
    std::vector<uint8> buf(payloadLen + overhead + 16, 0);
    std::memcpy(buf.data() + overhead, payload, payloadLen);

    uint32 sendVerify = 22222;

    // Encrypt with null hash to force ReceiverKey path
    uint32 encryptedLen = EncryptedDatagramSocket::encryptSendClient(
        buf.data(), payloadLen,
        nullptr, true,
        kadRecvKeyVal, sendVerify, 0);

    QVERIFY(encryptedLen > payloadLen);

    // Decrypt — provide the kadRecvKey
    DecryptResult result = EncryptedDatagramSocket::decryptReceivedClient(
        buf.data(), static_cast<int>(encryptedLen), 0,
        nullptr, nullptr, kadRecvKeyVal);

    QCOMPARE(result.length, static_cast<int>(payloadLen));
    QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
}

// ---------------------------------------------------------------------------
// Server → client decrypt recovers a reply encoded the way a real server does
//
// decryptReceivedServer must key the RC4 on the SERVER-CLIENT magic (0xA5), not
// the CLIENT-SERVER magic (0x6B) that encryptSendServer uses. The old symmetric
// test (encryptSendServer → decryptReceivedServer, both 0x6B) could not catch a
// direction-magic error because it agreed with itself. This test is deliberately
// ASYMMETRIC: it encodes the frame independently, exactly as a real server /
// eNode-go (ed2k/udpcrypt.go Encrypt) does, then asserts our decrypt recovers it.
// ---------------------------------------------------------------------------

namespace {
/// Encode a server→client obfuscated UDP frame: RC4 keyed on
/// MD5(baseKey ‖ 0xA5 ‖ randomKeyPart), no keystream drop, carrying
/// SYNC_SERVER + zero padding + payload. Mirrors srchybrid
/// EncryptedDatagramSocket.cpp (server side) and eNode-go udpcrypt.go Encrypt.
QByteArray buildObfuscatedServerReply(const uint8* payload, uint32 payloadLen,
                                      uint32 baseKey, uint8 padLen = 0)
{
    constexpr uint8  kMagicServerClient = 0xA5;
    constexpr uint32 kSyncServer        = 0x13EF24D5u;
    const uint16 randomKeyPart = getRandomUInt16();

    uint8 keyData[7];
    pokeUInt32(keyData, baseKey);
    keyData[4] = kMagicServerClient;
    pokeUInt16(&keyData[5], randomKeyPart);
    MD5Hasher md5(keyData, sizeof keyData);
    RC4Key key = rc4CreateKey({md5.getRawHash(), 16}, /*skipDiscard=*/true);

    // [marker != OP_EDONKEYPROT][randomKeyPart:2][ RC4( sync:4 | padLen:1 | padding:padLen | payload:N ) ]
    QByteArray frame;
    frame.resize(static_cast<int>(3 + 4 + 1 + padLen + payloadLen));
    auto* buf = reinterpret_cast<uint8*>(frame.data());
    buf[0] = 0x01;
    pokeUInt16(&buf[1], randomKeyPart);
    pokeUInt32(&buf[3], kSyncServer);
    buf[7] = padLen;
    for (uint8 i = 0; i < padLen; ++i)
        buf[8 + i] = static_cast<uint8>(0xAA + i); // arbitrary padding bytes
    std::memcpy(&buf[8 + padLen], payload, payloadLen);
    rc4Crypt(&buf[3], 4 + 1 + padLen + payloadLen, key); // encrypt from the sync marker onward
    return frame;
}
} // namespace

void tst_EncryptedDatagram::serverDecrypt_recoversServerEncodedReply()
{
    const uint32 baseKey = 0x12345678;   // e.g. eNode-go's fixed udp.serverKey
    const char* payload = "ServerReply!";
    const uint32 payloadLen = 12;

    QByteArray frame = buildObfuscatedServerReply(
        reinterpret_cast<const uint8*>(payload), payloadLen, baseKey);

    auto* buf = reinterpret_cast<uint8*>(frame.data());
    DecryptResult result = EncryptedDatagramSocket::decryptReceivedServer(
        buf, static_cast<int>(frame.size()), baseKey);

    // With the 0x6B bug the sync check fails and the frame passes through
    // undecrypted (length == frame.size(), data still points at the marker).
    QCOMPARE(result.length, static_cast<int>(payloadLen));
    QVERIFY(result.data != nullptr);
    QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
}

// Regression: a server reply carrying non-zero padding must decrypt without
// crashing. decryptReceivedServer skips padding by advancing the RC4 keystream
// with a null buffer (rc4Crypt(nullptr, padLen, key)); before the fix that
// dereferenced a null pointer and SIGSEGV'd in rc4Crypt. Real servers routinely
// send padding, so the zero-padding test above never exercised this path.
void tst_EncryptedDatagram::serverDecrypt_recoversReplyWithPadding()
{
    const uint32 baseKey = 0x12345678;
    const char* payload = "ServerReply!";
    const uint32 payloadLen = 12;

    for (uint8 padLen : {uint8(1), uint8(7), uint8(15)}) {
        QByteArray frame = buildObfuscatedServerReply(
            reinterpret_cast<const uint8*>(payload), payloadLen, baseKey, padLen);

        auto* buf = reinterpret_cast<uint8*>(frame.data());
        DecryptResult result = EncryptedDatagramSocket::decryptReceivedServer(
            buf, static_cast<int>(frame.size()), baseKey);

        QCOMPARE(result.length, static_cast<int>(payloadLen));
        QVERIFY(result.data != nullptr);
        QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
    }
}

// ---------------------------------------------------------------------------
// Overhead size
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::overheadSize_ed2k()
{
    int overhead = EncryptedDatagramSocket::encryptOverheadSize(false);
    QCOMPARE(overhead, 8); // marker(1) + randomKey(2) + magic(4) + paddingLen(1)
}

void tst_EncryptedDatagram::overheadSize_kad()
{
    int overhead = EncryptedDatagramSocket::encryptOverheadSize(true);
    QCOMPARE(overhead, 16); // 8 + recvKey(4) + sendKey(4)
}

// ---------------------------------------------------------------------------
// Non-encrypted passthrough
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::nonEncryptedPassthrough_protocolMarker()
{
    // A packet starting with a known protocol marker should pass through unchanged
    std::array<uint8, 16> userHash{};
    std::vector<uint8> buf(20, 0);
    buf[0] = OP_EMULEPROT; // known protocol marker
    buf[1] = 0x01;
    buf[2] = 0x02;

    DecryptResult result = EncryptedDatagramSocket::decryptReceivedClient(
        buf.data(), 20, 0x01020304,
        userHash.data(), nullptr, 0);

    // Should pass through unchanged
    QCOMPARE(result.data, buf.data());
    QCOMPARE(result.length, 20);
}

QTEST_MAIN(tst_EncryptedDatagram)
#include "tst_EncryptedDatagram.moc"
