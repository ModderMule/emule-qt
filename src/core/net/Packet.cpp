/// @file Packet.cpp
/// @brief ED2K network packet container implementation.

#include "net/Packet.h"
#include "utils/SafeFile.h"

#if __has_include(<zlib.h>)
#include <zlib.h>
#define HAVE_ZLIB 1
#else
#define HAVE_ZLIB 0
#endif

#include <algorithm>
#include <cstring>
#include <new>

namespace eMule {

// ---------------------------------------------------------------------------
// Packet — constructors
// ---------------------------------------------------------------------------

Packet::Packet(uint8 protocol)
    : prot(protocol)
{
}

Packet::Packet(const char* header)
{
    const auto* h = reinterpret_cast<const HeaderStruct*>(header);
    size = h->packetLength - 1;
    opcode = h->command;
    prot = h->eDonkeyID;
}

Packet::Packet(SafeMemFile& datafile, uint8 protocol, uint8 opcodeVal)
    : size(static_cast<uint32>(datafile.length()))
    , opcode(opcodeVal)
    , prot(protocol)
{
    m_completeBuffer = new char[size + kPacketHeaderSize + 4];
    pBuffer = m_completeBuffer + kPacketHeaderSize;
    QByteArray buf = datafile.takeBuffer();
    std::memcpy(pBuffer, buf.constData(), size);
}

Packet::Packet(uint8 opcodeVal, uint32 payloadSize, uint8 protocol, bool fromPartFile)
    : size(payloadSize)
    , opcode(opcodeVal)
    , prot(protocol)
    , m_fromPartFile(fromPartFile)
{
    if (payloadSize > 0) {
        m_completeBuffer = new char[payloadSize + kPacketHeaderSize + 4]{};
        pBuffer = m_completeBuffer + kPacketHeaderSize;
    }
}

Packet::Packet(const Packet& other)
    : size(other.size)
    , statsPayload(other.statsPayload)
    , opcode(other.opcode)
    , prot(other.prot)
    , m_packed(other.m_packed)
    , m_fromPartFile(other.m_fromPartFile)
{
    std::memcpy(m_head, other.m_head, kPacketHeaderSize);
    m_completeBuffer = new char[size + kPacketHeaderSize + 4];
    pBuffer = m_completeBuffer + kPacketHeaderSize;
    if (other.m_completeBuffer)
        std::memcpy(m_completeBuffer, other.m_completeBuffer, size + kPacketHeaderSize);
    else if (other.pBuffer)
        std::memcpy(pBuffer, other.pBuffer, size);
}

// ---------------------------------------------------------------------------
// Packet — destructor
// ---------------------------------------------------------------------------

Packet::~Packet()
{
    if (m_completeBuffer)
        delete[] m_completeBuffer;
    else
        delete[] pBuffer;
    delete[] m_tempBuffer;
}

// ---------------------------------------------------------------------------
// Packet — header generation
// ---------------------------------------------------------------------------

char* Packet::getHeader()
{
    auto* h = reinterpret_cast<HeaderStruct*>(m_head);
    h->eDonkeyID = prot;
    h->packetLength = size + 1;
    h->command = opcode;
    return m_head;
}

char* Packet::getUDPHeader()
{
    auto* h = reinterpret_cast<UDPHeaderStruct*>(m_head);
    h->eDonkeyID = prot;
    h->command = opcode;
    return m_head;
}

// ---------------------------------------------------------------------------
// Packet — complete packet access
// ---------------------------------------------------------------------------

char* Packet::getPacket()
{
    if (m_completeBuffer) {
        std::memcpy(m_completeBuffer, getHeader(), kPacketHeaderSize);
        return m_completeBuffer;
    }
    delete[] m_tempBuffer;
    m_tempBuffer = nullptr;
    m_tempBuffer = new char[size + kPacketHeaderSize + 4];
    std::memcpy(m_tempBuffer, getHeader(), kPacketHeaderSize);
    if (pBuffer)
        std::memcpy(m_tempBuffer + kPacketHeaderSize, pBuffer, size);
    return m_tempBuffer;
}

char* Packet::detachPacket()
{
    if (m_completeBuffer) {
        std::memcpy(m_completeBuffer, getHeader(), kPacketHeaderSize);
        char* result = m_completeBuffer;
        m_completeBuffer = nullptr;
        pBuffer = nullptr;
        return result;
    }
    delete[] m_tempBuffer;
    m_tempBuffer = nullptr;
    char* result = new char[size + kPacketHeaderSize + 4];
    std::memcpy(result, getHeader(), kPacketHeaderSize);
    if (pBuffer)
        std::memcpy(result + kPacketHeaderSize, pBuffer, size);
    return result;
}

// ---------------------------------------------------------------------------
// Packet — compression
// ---------------------------------------------------------------------------

void Packet::packPacket()
{
#if HAVE_ZLIB
    uLongf newsize = size + 300;
    auto* output = new Bytef[newsize];
    int result = compress2(output, &newsize, reinterpret_cast<const Bytef*>(pBuffer), size, Z_BEST_COMPRESSION);
    if (result == Z_OK && newsize < size) {
        prot = (prot == OP_KADEMLIAHEADER) ? OP_KADEMLIAPACKEDPROT : OP_PACKEDPROT;
        std::memcpy(pBuffer, output, newsize);
        size = static_cast<uint32>(newsize);
        m_packed = true;
    }
    delete[] output;
#endif
}

bool Packet::unPackPacket(uint32 maxDecompressedSize)
{
#if HAVE_ZLIB
    uint32 nNewSize = size * 10 + 300;
    if (nNewSize > maxDecompressedSize)
        nNewSize = maxDecompressedSize;

    Bytef* unpack = nullptr;
    uLongf unpackedSize = 0;
    int result = Z_OK;
    do {
        delete[] unpack;
        unpack = new Bytef[nNewSize];
        unpackedSize = nNewSize;
        result = uncompress(unpack, &unpackedSize, reinterpret_cast<const Bytef*>(pBuffer), size);
        nNewSize *= 2;
    } while (result == Z_BUF_ERROR && nNewSize < maxDecompressedSize);

    if (result == Z_OK) {
        size = static_cast<uint32>(unpackedSize);
        // After unpack, we switch to standalone pBuffer (no complete buffer)
        if (m_completeBuffer) {
            delete[] m_completeBuffer;
            m_completeBuffer = nullptr;
        } else {
            delete[] pBuffer;
        }
        pBuffer = reinterpret_cast<char*>(unpack);
        prot = (prot == OP_KADEMLIAPACKEDPROT) ? OP_KADEMLIAHEADER : OP_EMULEPROT;
        return true;
    }
    delete[] unpack;
#else
    Q_UNUSED(maxDecompressedSize);
#endif
    return false;
}

// ---------------------------------------------------------------------------
// RawPacket
// ---------------------------------------------------------------------------

RawPacket::RawPacket(const char* data, uint32 dataSize, bool fromPartFile)
{
    prot = 0x00;
    size = dataSize;
    pBuffer = new char[size];
    std::memcpy(pBuffer, data, size);
    m_fromPartFile = fromPartFile;
}

RawPacket::~RawPacket()
{
    // m_completeBuffer should always be null for RawPacket
}

char* RawPacket::getHeader()
{
    return nullptr;
}

char* RawPacket::getUDPHeader()
{
    return nullptr;
}

void RawPacket::attachPacket(char* packetData, uint32 packetSize, bool fromPartFile)
{
    delete[] pBuffer;
    pBuffer = packetData;
    size = packetSize;
    m_fromPartFile = fromPartFile;
}

char* RawPacket::detachPacket()
{
    char* buf = pBuffer;
    pBuffer = nullptr;
    return buf;
}

} // namespace eMule
