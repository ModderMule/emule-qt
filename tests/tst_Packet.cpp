/// @file tst_Packet.cpp
/// @brief Tests for net/Packet — construction, header serialization, pack/unpack.

#include "TestHelpers.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <cstring>
#include <memory>

using namespace eMule;

class tst_Packet : public QObject {
    Q_OBJECT

private slots:
    // Construction
    void defaultConstruction();
    void fromHeader();
    void fromOpcodeAndSize();
    void copyConstruction();
    void fromSafeMemFile();

    // Header serialization
    void tcpHeader();
    void udpHeader();

    // Complete packet
    void getPacket_withPayload();
    void detachPacket_transfersOwnership();
    void realPacketSize();

    // Compression
    void packUnpack_roundtrip();
    void unPack_oversizedRejection();

    // RawPacket
    void rawPacket_noHeader();
    void rawPacket_attachDetach();
};

// ---------------------------------------------------------------------------
// Construction tests
// ---------------------------------------------------------------------------

void tst_Packet::defaultConstruction()
{
    Packet pkt;
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(pkt.size, 0u);
    QCOMPARE(pkt.opcode, static_cast<uint8>(0));
    QVERIFY(pkt.pBuffer == nullptr);
}

void tst_Packet::fromHeader()
{
    // Build a raw 6-byte header
    HeaderStruct hdr;
    hdr.eDonkeyID = OP_EMULEPROT;
    hdr.packetLength = 42 + 1; // payload size + 1 for opcode
    hdr.command = 0xAB;

    Packet pkt(reinterpret_cast<const char*>(&hdr));
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_EMULEPROT));
    QCOMPARE(pkt.size, 42u);
    QCOMPARE(pkt.opcode, static_cast<uint8>(0xAB));
    QVERIFY(pkt.pBuffer == nullptr); // caller must allocate
}

void tst_Packet::fromOpcodeAndSize()
{
    Packet pkt(0x46, 100, OP_EDONKEYPROT, true);
    QCOMPARE(pkt.opcode, static_cast<uint8>(0x46));
    QCOMPARE(pkt.size, 100u);
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_EDONKEYPROT));
    QVERIFY(pkt.pBuffer != nullptr);
    QVERIFY(pkt.isFromPF());

    // Buffer should be zeroed
    for (uint32 i = 0; i < pkt.size; ++i)
        QCOMPARE(pkt.pBuffer[i], '\0');
}

void tst_Packet::copyConstruction()
{
    Packet original(0x01, 10, OP_EMULEPROT);
    std::memset(original.pBuffer, 0x42, 10);

    Packet copy(original);
    QCOMPARE(copy.opcode, original.opcode);
    QCOMPARE(copy.size, original.size);
    QCOMPARE(copy.prot, original.prot);
    QVERIFY(copy.pBuffer != original.pBuffer);
    QVERIFY(std::memcmp(copy.pBuffer, original.pBuffer, 10) == 0);
}

void tst_Packet::fromSafeMemFile()
{
    SafeMemFile memFile;
    memFile.writeUInt32(0xDEADBEEF);
    memFile.writeUInt16(0x1234);

    Packet pkt(memFile, OP_EMULEPROT, 0x99);
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_EMULEPROT));
    QCOMPARE(pkt.opcode, static_cast<uint8>(0x99));
    QCOMPARE(pkt.size, 6u); // 4 + 2 bytes
}

// ---------------------------------------------------------------------------
// Header serialization tests
// ---------------------------------------------------------------------------

void tst_Packet::tcpHeader()
{
    Packet pkt(0x46, 100, OP_EDONKEYPROT);
    char* hdr = pkt.getHeader();
    QVERIFY(hdr != nullptr);

    auto* h = reinterpret_cast<const HeaderStruct*>(hdr);
    QCOMPARE(h->eDonkeyID, static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(h->packetLength, 101u); // size + 1
    QCOMPARE(h->command, static_cast<uint8>(0x46));
}

void tst_Packet::udpHeader()
{
    Packet pkt(0x90, 50, OP_EMULEPROT);
    char* hdr = pkt.getUDPHeader();
    QVERIFY(hdr != nullptr);

    auto* h = reinterpret_cast<const UDPHeaderStruct*>(hdr);
    QCOMPARE(h->eDonkeyID, static_cast<uint8>(OP_EMULEPROT));
    QCOMPARE(h->command, static_cast<uint8>(0x90));
}

// ---------------------------------------------------------------------------
// Complete packet tests
// ---------------------------------------------------------------------------

void tst_Packet::getPacket_withPayload()
{
    Packet pkt(0x01, 4, OP_EDONKEYPROT);
    std::memcpy(pkt.pBuffer, "TEST", 4);

    char* full = pkt.getPacket();
    QVERIFY(full != nullptr);

    // Header at front
    auto* h = reinterpret_cast<const HeaderStruct*>(full);
    QCOMPARE(h->eDonkeyID, static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(h->command, static_cast<uint8>(0x01));

    // Payload after header
    QVERIFY(std::memcmp(full + kPacketHeaderSize, "TEST", 4) == 0);
}

void tst_Packet::detachPacket_transfersOwnership()
{
    Packet pkt(0x01, 4, OP_EDONKEYPROT);
    std::memcpy(pkt.pBuffer, "ABCD", 4);

    char* detached = pkt.detachPacket();
    QVERIFY(detached != nullptr);

    // After detach, pBuffer should be null
    QVERIFY(pkt.pBuffer == nullptr);

    // Detached buffer should have header + payload
    auto* h = reinterpret_cast<const HeaderStruct*>(detached);
    QCOMPARE(h->command, static_cast<uint8>(0x01));
    QVERIFY(std::memcmp(detached + kPacketHeaderSize, "ABCD", 4) == 0);

    delete[] detached;
}

void tst_Packet::realPacketSize()
{
    Packet pkt(0x01, 100, OP_EDONKEYPROT);
    QCOMPARE(pkt.getRealPacketSize(), 100u + static_cast<uint32>(kPacketHeaderSize));
}

// ---------------------------------------------------------------------------
// Compression tests
// ---------------------------------------------------------------------------

void tst_Packet::packUnpack_roundtrip()
{
    // Create a packet with repetitive data (compresses well)
    Packet pkt(0x40, 200, OP_EMULEPROT);
    std::memset(pkt.pBuffer, 'A', 200);

    uint32 originalSize = pkt.size;

    pkt.packPacket();
    // After packing, size should be smaller and protocol changed
    QVERIFY(pkt.size < originalSize);
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_PACKEDPROT));

    // Now unpack
    bool ok = pkt.unPackPacket(50000);
    QVERIFY(ok);
    QCOMPARE(pkt.size, originalSize);
    // Protocol should be restored to OP_EMULEPROT
    QCOMPARE(pkt.prot, static_cast<uint8>(OP_EMULEPROT));

    // Verify data integrity
    for (uint32 i = 0; i < pkt.size; ++i)
        QCOMPARE(pkt.pBuffer[i], 'A');
}

void tst_Packet::unPack_oversizedRejection()
{
    // Create a packed packet
    Packet pkt(0x40, 200, OP_EMULEPROT);
    std::memset(pkt.pBuffer, 'B', 200);
    pkt.packPacket();

    // Try to unpack with tiny max size — should fail
    bool ok = pkt.unPackPacket(5);
    QVERIFY(!ok);
}

// ---------------------------------------------------------------------------
// RawPacket tests
// ---------------------------------------------------------------------------

void tst_Packet::rawPacket_noHeader()
{
    RawPacket raw("Hello", 5);
    QCOMPARE(raw.size, 5u);
    QVERIFY(raw.getHeader() == nullptr);
    QVERIFY(raw.getUDPHeader() == nullptr);
    QCOMPARE(raw.getRealPacketSize(), 5u); // no header overhead
    QVERIFY(raw.getPacket() == raw.pBuffer);
}

void tst_Packet::rawPacket_attachDetach()
{
    RawPacket raw("init", 4);

    // Detach
    char* buf = raw.detachPacket();
    QVERIFY(buf != nullptr);
    QVERIFY(raw.pBuffer == nullptr);

    // Attach new data
    char* newData = new char[3];
    std::memcpy(newData, "XYZ", 3);
    raw.attachPacket(newData, 3, true);
    QCOMPARE(raw.size, 3u);
    QVERIFY(raw.isFromPF());

    delete[] buf;
}

QTEST_MAIN(tst_Packet)
#include "tst_Packet.moc"
