/// @file tst_EncryptedDatagram.cpp
/// @brief Tests for EncryptedDatagramSocket — encrypt/decrypt roundtrips.

#include "TestHelpers.h"
#include "net/EncryptedDatagramSocket.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

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
    void serverEncryptDecryptRoundtrip();
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
// Server encrypt → decrypt roundtrip
// ---------------------------------------------------------------------------

void tst_EncryptedDatagram::serverEncryptDecryptRoundtrip()
{
    uint32 baseKey = 0xCAFEBABE;

    const char* payload = "ServerPkt";
    const uint32 payloadLen = 9;

    uint32 overhead = static_cast<uint32>(EncryptedDatagramSocket::encryptOverheadSize(false));
    std::vector<uint8> buf(payloadLen + overhead + 16, 0);
    std::memcpy(buf.data() + overhead, payload, payloadLen);

    uint32 encryptedLen = EncryptedDatagramSocket::encryptSendServer(
        buf.data(), payloadLen, baseKey);

    QVERIFY(encryptedLen > payloadLen);

    DecryptResult result = EncryptedDatagramSocket::decryptReceivedServer(
        buf.data(), static_cast<int>(encryptedLen), baseKey);

    QCOMPARE(result.length, static_cast<int>(payloadLen));
    QVERIFY(std::memcmp(result.data, payload, payloadLen) == 0);
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
