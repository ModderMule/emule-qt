#pragma once

/// @file Packet.h
/// @brief ED2K network packet container — replaces MFC Packet / CRawPacket from Packets.h.
///
/// Provides the basic packet container used for all ED2K protocol communication.
/// Protocol-specific packet construction/parsing (Tags, etc.) belongs in Module 7.

#include "utils/Types.h"
#include "utils/Opcodes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace eMule {

class SafeMemFile;

// ---------------------------------------------------------------------------
// Wire-format header structures (packed, little-endian on x86)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

/// TCP packet header: protocol byte + 4-byte payload length + opcode byte.
struct HeaderStruct {
    uint8 eDonkeyID;
    uint32 packetLength;
    uint8 command;
};

/// UDP packet header: protocol byte + opcode byte.
struct UDPHeaderStruct {
    uint8 eDonkeyID;
    uint8 command;
};

#pragma pack(pop)

inline constexpr std::size_t kPacketHeaderSize = sizeof(HeaderStruct); // 6

// ---------------------------------------------------------------------------
// Packet
// ---------------------------------------------------------------------------

/// ED2K protocol packet container.
///
/// Manages a payload buffer and generates TCP/UDP headers on demand.
/// Supports zlib compression (packPacket/unPackPacket).
class Packet {
public:
    /// Create an empty packet with the given protocol byte.
    explicit Packet(uint8 protocol = OP_EDONKEYPROT);

    /// Create from a received TCP header (6 bytes). Only header is parsed;
    /// caller must allocate pBuffer and fill payload separately.
    explicit Packet(const char* header);

    /// Create from a SafeMemFile (takes ownership of its data).
    explicit Packet(SafeMemFile& datafile, uint8 protocol = OP_EDONKEYPROT, uint8 opcode = 0x00);

    /// Create with a given opcode and payload size (buffer allocated, zeroed).
    Packet(uint8 opcode, uint32 payloadSize, uint8 protocol = OP_EDONKEYPROT, bool fromPartFile = true);

    /// Copy constructor.
    Packet(const Packet& other);

    virtual ~Packet();

    Packet& operator=(const Packet&) = delete;

    /// Get the 6-byte TCP header for this packet.
    virtual char* getHeader();

    /// Get the 2-byte UDP header for this packet.
    virtual char* getUDPHeader();

    /// Get the complete packet (header + payload). Valid until next call or destruction.
    virtual char* getPacket();

    /// Detach and return the complete packet buffer. Caller owns the memory.
    virtual char* detachPacket();

    /// Total packet size including header.
    [[nodiscard]] virtual uint32 getRealPacketSize() const { return size + static_cast<uint32>(kPacketHeaderSize); }

    /// Whether this packet originated from a part file.
    [[nodiscard]] bool isFromPF() const { return m_fromPartFile; }

    /// Compress the payload with zlib. Changes protocol byte to packed variant.
    void packPacket();

    /// Decompress the payload. Returns false on failure.
    bool unPackPacket(uint32 maxDecompressedSize = 50000u);

    // --- Public data members (directly accessed by EMSocket for packet framing) ---
    char* pBuffer = nullptr;    ///< Payload data (after header).
    uint32 size = 0;            ///< Payload size in bytes.
    uint32 statsPayload = 0;    ///< For statistics only, not used by class itself.
    uint8 opcode = 0;           ///< Command/opcode byte.
    uint8 prot = 0;             ///< Protocol byte (OP_EDONKEYPROT, OP_EMULEPROT, etc.).

protected:
    char* m_completeBuffer = nullptr;   ///< Header + payload in contiguous allocation.
    char* m_tempBuffer = nullptr;       ///< Temporary buffer for getPacket() when no complete buffer.
    char m_head[kPacketHeaderSize]{};   ///< Scratch space for header generation.
    bool m_packed = false;
    bool m_fromPartFile = false;
};

// ---------------------------------------------------------------------------
// RawPacket — no ED2K header framing (for HTTP/raw data mode)
// ---------------------------------------------------------------------------

/// Raw data packet without ED2K protocol headers.
class RawPacket : public Packet {
public:
    /// Create from raw data (copies the data).
    RawPacket(const char* data, uint32 dataSize, bool fromPartFile = false);

    ~RawPacket() override;

    char* getHeader() override;
    char* getUDPHeader() override;
    char* getPacket() override { return pBuffer; }
    char* detachPacket() override;
    [[nodiscard]] uint32 getRealPacketSize() const override { return size; }

    /// Attach an externally-allocated buffer. Takes ownership.
    void attachPacket(char* packetData, uint32 packetSize, bool fromPartFile = false);
};

} // namespace eMule
