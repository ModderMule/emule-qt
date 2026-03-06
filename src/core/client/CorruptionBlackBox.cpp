#include "pch.h"
/// @file CorruptionBlackBox.cpp
/// @brief Per-part corruption tracking implementation — replaces MFC CCorruptionBlackBox.

#include "client/CorruptionBlackBox.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <algorithm>

namespace eMule {

static constexpr int kBanThreshold = 32;  // % max corrupted data

// ---------------------------------------------------------------------------
// BlockRecord
// ---------------------------------------------------------------------------

BlockRecord::BlockRecord(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s)
    : startPos(start)
    , endPos(end)
    , senderIP(ip)
    , status(s)
{
}

bool BlockRecord::canMerge(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s) const
{
    return senderIP == ip && status == s
        && (start == endPos + 1 || end + 1 == startPos);
}

bool BlockRecord::merge(uint64 start, uint64 end, uint32 ip, BlockRecordStatus s)
{
    if (senderIP != ip || status != s)
        return false;
    if (start == endPos + 1)
        endPos = end;
    else if (end + 1 == startPos)
        startPos = start;
    else
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// CorruptionBlackBox
// ---------------------------------------------------------------------------

void CorruptionBlackBox::init(uint64 fileSize)
{
    m_records.resize(static_cast<std::size_t>((fileSize + PARTSIZE - 1) / PARTSIZE));
}

void CorruptionBlackBox::free()
{
    m_records.clear();
}

void CorruptionBlackBox::transferredData(uint64 startPos, uint64 endPos, uint32 senderIP)
{
    if (endPos - startPos >= PARTSIZE || startPos > endPos)
        return;

    auto nPart = static_cast<std::size_t>(startPos / PARTSIZE);
    const uint64 partBase = nPart * PARTSIZE;
    uint64 relStart = startPos - partBase;
    uint64 relEnd = endPos - partBase;

    if (relEnd >= PARTSIZE) {
        // data crosses part boundary — split
        relEnd = PARTSIZE - 1;
        transferredData(partBase + PARTSIZE, endPos, senderIP);
    }

    if (nPart >= m_records.size())
        return;

    auto& partRecords = m_records[nPart];
    std::ptrdiff_t posMerge = -1;

    for (std::size_t i = 0; i < partRecords.size(); ++i) {
        auto& rec = partRecords[i];
        if (rec.canMerge(relStart, relEnd, senderIP, BlockRecordStatus::None)) {
            posMerge = static_cast<std::ptrdiff_t>(i);
        } else if (rec.status == BlockRecordStatus::None) {
            // Check for overlaps with existing pending entries
            if (rec.startPos >= relStart && rec.endPos <= relEnd) {
                // old one is included in the new one — delete
                partRecords.erase(partRecords.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
            } else if (rec.startPos < relStart && rec.endPos > relEnd) {
                // old one fully contains the new one
                if (senderIP != rec.senderIP) {
                    // different IP: split into 3 blocks
                    uint64 oldStart = rec.startPos;
                    uint64 oldEnd = rec.endPos;
                    uint32 oldIP = rec.senderIP;

                    rec.startPos = relStart;
                    rec.endPos = relEnd;
                    rec.senderIP = senderIP;

                    partRecords.emplace_back(oldStart, relStart - 1, oldIP);
                    // prepare remaining block
                    relStart = relEnd + 1;
                    relEnd = oldEnd;
                    senderIP = oldIP;
                    break;
                }
            } else if (rec.startPos >= relStart && rec.startPos <= relEnd) {
                // old overlaps on the right side
                rec.startPos = relEnd + 1;
            } else if (rec.endPos >= relStart && rec.endPos <= relEnd) {
                // old overlaps on the left side
                rec.endPos = relStart - 1;
            }
        }
    }

    if (posMerge >= 0)
        partRecords[static_cast<std::size_t>(posMerge)].merge(relStart, relEnd, senderIP, BlockRecordStatus::None);
    else
        partRecords.emplace_back(relStart, relEnd, senderIP);
}

void CorruptionBlackBox::verifiedData(uint64 startPos, uint64 endPos)
{
    if (endPos >= startPos + PARTSIZE)
        return;

    auto nPart = static_cast<std::size_t>(startPos / PARTSIZE);
    uint64 relStart = startPos - nPart * PARTSIZE;
    uint64 relEnd = endPos - nPart * PARTSIZE;
    if (relEnd >= PARTSIZE || nPart >= m_records.size())
        return;

    auto& partRecords = m_records[nPart];
    for (std::size_t i = 0; i < partRecords.size(); ++i) {
        if (partRecords[i].status != BlockRecordStatus::None
            && partRecords[i].status != BlockRecordStatus::Verified)
            continue;

        if (partRecords[i].startPos >= relStart && partRecords[i].endPos <= relEnd) {
            // entire block is within verified range
        } else if (partRecords[i].startPos < relStart && partRecords[i].endPos > relEnd) {
            // split into 3
            uint64 oldStart = partRecords[i].startPos;
            uint64 oldEnd = partRecords[i].endPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else if (partRecords[i].startPos >= relStart && partRecords[i].startPos <= relEnd) {
            // split off tail
            uint64 oldEnd = partRecords[i].endPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
        } else if (partRecords[i].endPos >= relStart && partRecords[i].endPos <= relEnd) {
            // split off head
            uint64 oldStart = partRecords[i].startPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else {
            continue;
        }
        partRecords[i].status = BlockRecordStatus::Verified;
    }
}

void CorruptionBlackBox::corruptedData(uint64 startPos, uint64 endPos)
{
    if (endPos - startPos >= EMBLOCKSIZE)
        return;

    auto nPart = static_cast<std::size_t>(startPos / PARTSIZE);
    uint64 relStart = startPos - nPart * PARTSIZE;
    uint64 relEnd = endPos - nPart * PARTSIZE;
    if (relEnd >= PARTSIZE || nPart >= m_records.size())
        return;

    auto& partRecords = m_records[nPart];
    for (std::size_t i = 0; i < partRecords.size(); ++i) {
        if (partRecords[i].status != BlockRecordStatus::None)
            continue;

        if (partRecords[i].startPos >= relStart && partRecords[i].endPos <= relEnd) {
            // entire block within corrupted range
        } else if (partRecords[i].startPos < relStart && partRecords[i].endPos > relEnd) {
            // split into 3
            uint64 oldStart = partRecords[i].startPos;
            uint64 oldEnd = partRecords[i].endPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else if (partRecords[i].startPos >= relStart && partRecords[i].startPos <= relEnd) {
            // split off tail
            uint64 oldEnd = partRecords[i].endPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
        } else if (partRecords[i].endPos >= relStart && partRecords[i].endPos <= relEnd) {
            // split off head
            uint64 oldStart = partRecords[i].startPos;
            uint32 ip = partRecords[i].senderIP;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else {
            continue;
        }
        partRecords[i].status = BlockRecordStatus::Corrupted;
    }
}

std::vector<EvaluationResult> CorruptionBlackBox::evaluateData(uint16 part) const
{
    if (static_cast<std::size_t>(part) >= m_records.size())
        return {};

    // Collect unique guilty IPs from this part
    std::vector<uint32> guiltyIPs;
    for (auto& rec : m_records[part]) {
        if (rec.status == BlockRecordStatus::Corrupted) {
            if (std::find(guiltyIPs.begin(), guiltyIPs.end(), rec.senderIP) == guiltyIPs.end())
                guiltyIPs.push_back(rec.senderIP);
        }
    }

    if (guiltyIPs.empty())
        return {};

    // Accumulate stats across ALL parts for the guilty IPs
    std::vector<uint64> dataCorrupt(guiltyIPs.size(), 0);
    std::vector<uint64> dataVerified(guiltyIPs.size(), 0);

    for (auto& partRecords : m_records) {
        for (auto& rec : partRecords) {
            for (std::size_t k = 0; k < guiltyIPs.size(); ++k) {
                if (rec.senderIP == guiltyIPs[k]) {
                    if (rec.status == BlockRecordStatus::Corrupted)
                        dataCorrupt[k] += std::max(rec.endPos - rec.startPos + 1, static_cast<uint64>(EMBLOCKSIZE));
                    else if (rec.status == BlockRecordStatus::Verified)
                        dataVerified[k] += rec.endPos - rec.startPos + 1;
                }
            }
        }
    }

    std::vector<EvaluationResult> results;
    results.reserve(guiltyIPs.size());

    for (std::size_t k = 0; k < guiltyIPs.size(); ++k) {
        EvaluationResult r;
        r.ip = guiltyIPs[k];
        r.corruptBytes = dataCorrupt[k];
        r.verifiedBytes = dataVerified[k];

        uint64 total = dataVerified[k] + dataCorrupt[k];
        r.corruptPercent = (total > 0) ? static_cast<int>((dataCorrupt[k] * 100) / total) : 0;
        r.shouldBan = r.corruptPercent > kBanThreshold;

        results.push_back(r);
    }

    return results;
}

} // namespace eMule
