#pragma once

/// @file CorruptionBlackBox.h
/// @brief Per-part corruption tracking — replaces MFC CCBBRecord + CCorruptionBlackBox.
///
/// Decoupled from UpDownClient: takes the sender's Address directly, and
/// evaluateData() returns results instead of banning anyone itself. The caller
/// decides what to do with a guilty verdict — see PartFile::punishCorruptionSenders().
///
/// Address rather than a bare uint32 on purpose. The port has two uint32 IP
/// conventions in play (ClientList::findByIP compares network order, Kademlia
/// passes host order) and a raw integer documents neither, so the type that
/// carries its own byte order is the only one that cannot be plugged in wrongly.
/// It also holds an IPv6 peer, which a uint32 cannot, and it is what
/// ClientList::addBannedClient() takes — so recording, lookup and ban all speak
/// one type.

#include "net/Address.h"
#include "utils/Types.h"

#include <cstdint>
#include <vector>

namespace eMule {

enum class BlockRecordStatus : uint8 {
    None      = 0,
    Verified  = 1,
    Corrupted = 2
};

/// Record of a data block received from a specific sender.
struct BlockRecord {
    uint64 startPos = 0;
    uint64 endPos   = 0;
    Address sender;
    BlockRecordStatus status = BlockRecordStatus::None;

    BlockRecord() = default;
    BlockRecord(uint64 start, uint64 end, const Address& ip,
                BlockRecordStatus s = BlockRecordStatus::None);

    [[nodiscard]] bool canMerge(uint64 start, uint64 end, const Address& ip,
                                BlockRecordStatus s) const;
    bool merge(uint64 start, uint64 end, const Address& ip, BlockRecordStatus s);
};

/// Result of evaluating corruption data for a specific sender.
struct EvaluationResult {
    Address addr;
    uint64 corruptBytes  = 0;
    uint64 verifiedBytes = 0;
    int    corruptPercent = 0;
    bool   shouldBan = false;
};

/// Per-part corruption tracking blackbox.
class CorruptionBlackBox {
public:
    CorruptionBlackBox() = default;

    void init(uint64 fileSize);
    void free();

    void transferredData(uint64 startPos, uint64 endPos, const Address& sender);
    void verifiedData(uint64 startPos, uint64 endPos);

    /// Mark a range bad. Like MFC's, this takes at most one 180 KB block at a
    /// time — AICH recovery is what normally calls it, once per block it found
    /// wrong. To condemn a whole part, walk it in EMBLOCKSIZE steps.
    void corruptedData(uint64 startPos, uint64 endPos);

    [[nodiscard]] std::vector<EvaluationResult> evaluateData(uint16 part) const;

    /// Distinct senders that contributed to this part.
    [[nodiscard]] std::size_t senderCount(uint16 part) const;

    /// The one sender that supplied this entire part on its own, or a null Address.
    ///
    /// Stronger than senderCount() == 1, and the difference matters: records do not
    /// survive a restart, so a part left half-finished last session has no sender for
    /// its first half. Whoever fills the remaining gap would otherwise look like the
    /// sole contributor and take the blame for bytes it never sent. Requiring the
    /// records to cover [0, partLength) is what rules that out.
    [[nodiscard]] Address soleSenderOfWholePart(uint16 part, uint64 partLength) const;

    [[nodiscard]] std::size_t partCount() const { return m_records.size(); }

private:
    std::vector<std::vector<BlockRecord>> m_records;
};

} // namespace eMule
