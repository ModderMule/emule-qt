#pragma once

/// @file CorruptionBlackBox.h
/// @brief Per-part corruption tracking — replaces MFC CCBBRecord + CCorruptionBlackBox.
///
/// Decoupled from CUpDownClient: takes uint32 senderIP directly.
/// evaluateData() returns results instead of calling theApp.clientlist.

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
    uint32 senderIP = 0;
    BlockRecordStatus status = BlockRecordStatus::None;

    BlockRecord() = default;
    BlockRecord(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s = BlockRecordStatus::None);

    [[nodiscard]] bool canMerge(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s) const;
    bool merge(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s);
};

/// Result of evaluating corruption data for a specific IP.
struct EvaluationResult {
    uint32 ip = 0;
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

    void transferredData(uint64 startPos, uint64 endPos, uint32 senderIP);
    void verifiedData(uint64 startPos, uint64 endPos);
    void corruptedData(uint64 startPos, uint64 endPos);

    [[nodiscard]] std::vector<EvaluationResult> evaluateData(uint16 part) const;

    [[nodiscard]] std::size_t partCount() const { return m_records.size(); }

private:
    std::vector<std::vector<BlockRecord>> m_records;
};

} // namespace eMule
