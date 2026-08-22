#pragma once

/// @file MockPeerSocket.h
/// @brief An ed2k peer that records what our upload pipeline sends it.
///
/// A real EMSocket — RC4 obfuscation included — driven from the test rather than
/// by a UpDownClient, so a test can perform a handshake by hand and then assert
/// on the packets that come back. Data packets are reassembled and, where the
/// uploader chose to compress them, inflated, so a test can compare the bytes
/// against the file on disk.
///
/// No Q_OBJECT: it declares no signals or slots of its own, only overrides two
/// virtuals, which is what lets it live in a header.

#include "net/EMSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"

#include <QByteArray>

#include <zlib.h>

#include <cstring>
#include <map>
#include <vector>

namespace eMule::testing {

class MockPeerSocket : public EMSocket {
public:
    using EMSocket::EMSocket;

    struct ReceivedPacket {
        uint8 opcode;
        uint8 prot;
        std::vector<char> data;
    };

    struct DataBlock {
        uint64 start;
        uint64 end;
        QByteArray data;
    };

    std::vector<ReceivedPacket> receivedPackets;
    int lastErrorCode = 0;

    bool hasOpcode(uint8 op) const
    {
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op)
                return true;
        }
        return false;
    }

    bool hasOpcode(uint8 op, uint8 proto) const
    {
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op && rp.prot == proto)
                return true;
        }
        return false;
    }

    /// How many packets of this opcode arrived. hasOpcode() answers "any"; a
    /// test for a dedup rule needs "exactly one".
    int countOpcode(uint8 op, uint8 proto) const
    {
        int count = 0;
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op && rp.prot == proto)
                ++count;
        }
        return count;
    }

    /// Payloads of every packet with this opcode, in arrival order.
    std::vector<QByteArray> payloadsFor(uint8 op, uint8 proto) const
    {
        std::vector<QByteArray> out;
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == op && rp.prot == proto)
                out.emplace_back(rp.data.data(), static_cast<qsizetype>(rp.data.size()));
        }
        return out;
    }

    /// Total raw payload bytes across all data packets (header bytes excluded).
    uint64 totalDataBytes() const
    {
        uint64 total = 0;
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == OP_SENDINGPART && rp.data.size() > 24)
                total += rp.data.size() - 24;
            else if (rp.opcode == OP_SENDINGPART_I64 && rp.data.size() > 32)
                total += rp.data.size() - 32;
            else if (rp.opcode == OP_COMPRESSEDPART && rp.data.size() > 24)
                total += rp.data.size() - 24;
            else if (rp.opcode == OP_COMPRESSEDPART_I64 && rp.data.size() > 28)
                total += rp.data.size() - 28;
        }
        return total;
    }

    /// Count of data packets received (OP_SENDINGPART* or OP_COMPRESSEDPART*).
    int dataPacketCount() const
    {
        int count = 0;
        for (const auto& rp : receivedPackets) {
            if (rp.opcode == OP_SENDINGPART || rp.opcode == OP_SENDINGPART_I64
                || rp.opcode == OP_COMPRESSEDPART || rp.opcode == OP_COMPRESSEDPART_I64)
                ++count;
        }
        return count;
    }

    /// Extract received data blocks from OP_SENDINGPART* and OP_COMPRESSEDPART* packets.
    /// Compressed blocks are decompressed with zlib before returning.
    std::vector<DataBlock> receivedDataBlocks() const
    {
        std::vector<DataBlock> blocks;

        // Collect compressed packet chunks keyed by start offset.
        // Each OP_COMPRESSEDPART has: hash(16) + start(4) + totalCompressedLen(4) + chunk.
        // Multiple packets with the same start offset form one block.
        struct CompressedEntry {
            uint64 start;
            uint32 totalCompressedLen;
            QByteArray compressedData;
        };
        std::map<uint64, CompressedEntry> compressedMap;

        for (const auto& rp : receivedPackets) {
            // --- Uncompressed ---
            if (rp.opcode == OP_SENDINGPART && rp.prot == OP_EDONKEYPROT && rp.data.size() > 24) {
                uint32 start = 0, end = 0;
                std::memcpy(&start, rp.data.data() + 16, 4);
                std::memcpy(&end, rp.data.data() + 20, 4);
                DataBlock block;
                block.start = start;
                block.end = end;
                block.data = QByteArray(rp.data.data() + 24,
                                        static_cast<qsizetype>(rp.data.size() - 24));
                blocks.push_back(std::move(block));
            } else if (rp.opcode == OP_SENDINGPART_I64 && rp.prot == OP_EMULEPROT && rp.data.size() > 32) {
                uint64 start = 0, end = 0;
                std::memcpy(&start, rp.data.data() + 16, 8);
                std::memcpy(&end, rp.data.data() + 24, 8);
                DataBlock block;
                block.start = start;
                block.end = end;
                block.data = QByteArray(rp.data.data() + 32,
                                        static_cast<qsizetype>(rp.data.size() - 32));
                blocks.push_back(std::move(block));
            }
            // --- Compressed (standard 32-bit offsets) ---
            else if (rp.opcode == OP_COMPRESSEDPART && rp.prot == OP_EMULEPROT && rp.data.size() > 24) {
                uint32 start32 = 0, compLen = 0;
                std::memcpy(&start32, rp.data.data() + 16, 4);
                std::memcpy(&compLen, rp.data.data() + 20, 4);
                auto& entry = compressedMap[start32];
                entry.start = start32;
                entry.totalCompressedLen = compLen;
                entry.compressedData.append(rp.data.data() + 24,
                                            static_cast<qsizetype>(rp.data.size() - 24));
            }
            // --- Compressed (64-bit offsets) ---
            else if (rp.opcode == OP_COMPRESSEDPART_I64 && rp.prot == OP_EMULEPROT && rp.data.size() > 28) {
                uint64 start64 = 0;
                uint32 compLen = 0;
                std::memcpy(&start64, rp.data.data() + 16, 8);
                std::memcpy(&compLen, rp.data.data() + 24, 4);
                auto& entry = compressedMap[start64];
                entry.start = start64;
                entry.totalCompressedLen = compLen;
                entry.compressedData.append(rp.data.data() + 28,
                                            static_cast<qsizetype>(rp.data.size() - 28));
            }
        }

        // Decompress collected compressed blocks
        for (const auto& [offset, entry] : compressedMap) {
            // Allocate generous output buffer (original block ≤ PARTSIZE)
            uLongf destLen = entry.totalCompressedLen * 10 + 300;
            if (destLen < 65536)
                destLen = 65536;
            std::vector<uint8> decompressed(destLen);

            int zResult = uncompress(decompressed.data(), &destLen,
                                     reinterpret_cast<const uint8*>(entry.compressedData.constData()),
                                     static_cast<uLong>(entry.compressedData.size()));
            if (zResult == Z_OK) {
                DataBlock block;
                block.start = entry.start;
                block.end = entry.start + destLen;
                block.data = QByteArray(reinterpret_cast<const char*>(decompressed.data()),
                                        static_cast<qsizetype>(destLen));
                blocks.push_back(std::move(block));
            } else {
                qWarning("zlib uncompress failed for block at offset %llu: error %d",
                         static_cast<unsigned long long>(entry.start), zResult);
            }
        }

        return blocks;
    }

protected:
    bool packetReceived(Packet* packet) override
    {
        ReceivedPacket rp;
        rp.opcode = packet->opcode;
        rp.prot = packet->prot;
        if (packet->pBuffer && packet->size > 0)
            rp.data.assign(packet->pBuffer, packet->pBuffer + packet->size);
        receivedPackets.push_back(std::move(rp));
        return true;
    }

    void onError(int errorCode) override
    {
        lastErrorCode = errorCode;
    }
};

} // namespace eMule::testing
