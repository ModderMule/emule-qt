#include "pch.h"
/// @file CorruptionBlackBox.cpp
/// @brief Per-part corruption tracking implementation — replaces MFC CCorruptionBlackBox.

#include "client/CorruptionBlackBox.h"
#include "utils/Opcodes.h"


namespace eMule {

static constexpr int kBanThreshold = 32;  // % max corrupted data

// ---------------------------------------------------------------------------
// BlockRecord
// ---------------------------------------------------------------------------

BlockRecord::BlockRecord(uint64 start, uint64 end, const Address& ip, BlockRecordStatus s)
    : startPos(start)
    , endPos(end)
    , sender(ip)
    , status(s)
{
}

bool BlockRecord::canMerge(uint64 start, uint64 end, const Address& ip, BlockRecordStatus s) const
{
    return sender == ip && status == s
        && (start == endPos + 1 || end + 1 == startPos);
}

bool BlockRecord::merge(uint64 start, uint64 end, const Address& ip, BlockRecordStatus s)
{
    if (sender != ip || status != s)
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

void CorruptionBlackBox::transferredData(uint64 startPos, uint64 endPos, const Address& senderIn)
{
    if (endPos - startPos >= PARTSIZE || startPos > endPos)
        return;

    // The overlap resolution below hands leftover ranges back to whoever owned
    // them, so the working sender changes as we go and cannot be the parameter.
    Address sender = senderIn;

    auto nPart = static_cast<std::size_t>(startPos / PARTSIZE);
    const uint64 partBase = nPart * PARTSIZE;
    uint64 relStart = startPos - partBase;
    uint64 relEnd = endPos - partBase;

    if (relEnd >= PARTSIZE) {
        // data crosses part boundary — split
        relEnd = PARTSIZE - 1;
        transferredData(partBase + PARTSIZE, endPos, sender);
    }

    if (nPart >= m_records.size())
        return;

    auto& partRecords = m_records[nPart];
    std::ptrdiff_t posMerge = -1;

    for (std::size_t i = 0; i < partRecords.size(); ++i) {
        auto& rec = partRecords[i];
        if (rec.canMerge(relStart, relEnd, sender, BlockRecordStatus::None)) {
            posMerge = static_cast<std::ptrdiff_t>(i);
        } else if (rec.status == BlockRecordStatus::None) {
            // Check for overlaps with existing pending entries
            if (rec.startPos >= relStart && rec.endPos <= relEnd) {
                // old one is included in the new one — delete
                partRecords.erase(partRecords.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
            } else if (rec.startPos < relStart && rec.endPos > relEnd) {
                // old one fully contains the new one
                if (sender != rec.sender) {
                    // different IP: split into 3 blocks
                    uint64 oldStart = rec.startPos;
                    uint64 oldEnd = rec.endPos;
                    const Address oldIP = rec.sender;

                    rec.startPos = relStart;
                    rec.endPos = relEnd;
                    rec.sender = sender;

                    partRecords.emplace_back(oldStart, relStart - 1, oldIP);
                    // prepare remaining block
                    relStart = relEnd + 1;
                    relEnd = oldEnd;
                    sender = oldIP;
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
        partRecords[static_cast<std::size_t>(posMerge)].merge(relStart, relEnd, sender, BlockRecordStatus::None);
    else
        partRecords.emplace_back(relStart, relEnd, sender);
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
            const Address ip = partRecords[i].sender;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else if (partRecords[i].startPos >= relStart && partRecords[i].startPos <= relEnd) {
            // split off tail
            uint64 oldEnd = partRecords[i].endPos;
            const Address ip = partRecords[i].sender;
            auto status = partRecords[i].status;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
        } else if (partRecords[i].endPos >= relStart && partRecords[i].endPos <= relEnd) {
            // split off head
            uint64 oldStart = partRecords[i].startPos;
            const Address ip = partRecords[i].sender;
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
            const Address ip = partRecords[i].sender;
            auto status = partRecords[i].status;
            partRecords[i].startPos = relStart;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
            partRecords.emplace_back(oldStart, relStart - 1, ip, status);
        } else if (partRecords[i].startPos >= relStart && partRecords[i].startPos <= relEnd) {
            // split off tail
            uint64 oldEnd = partRecords[i].endPos;
            const Address ip = partRecords[i].sender;
            auto status = partRecords[i].status;
            partRecords[i].endPos = relEnd;
            partRecords.emplace_back(relEnd + 1, oldEnd, ip, status);
        } else if (partRecords[i].endPos >= relStart && partRecords[i].endPos <= relEnd) {
            // split off head
            uint64 oldStart = partRecords[i].startPos;
            const Address ip = partRecords[i].sender;
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
    std::vector<Address> guiltyIPs;
    for (auto& rec : m_records[part]) {
        if (rec.status == BlockRecordStatus::Corrupted) {
            if (std::find(guiltyIPs.begin(), guiltyIPs.end(), rec.sender) == guiltyIPs.end())
                guiltyIPs.push_back(rec.sender);
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
                if (rec.sender == guiltyIPs[k]) {
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
        r.addr = guiltyIPs[k];
        r.corruptBytes = dataCorrupt[k];
        r.verifiedBytes = dataVerified[k];

        uint64 total = dataVerified[k] + dataCorrupt[k];
        r.corruptPercent = (total > 0) ? static_cast<int>((dataCorrupt[k] * 100) / total) : 0;
        r.shouldBan = r.corruptPercent > kBanThreshold;

        results.push_back(r);
    }

    return results;
}

std::size_t CorruptionBlackBox::senderCount(uint16 part) const
{
    if (static_cast<std::size_t>(part) >= m_records.size())
        return 0;

    std::vector<Address> seen;
    for (const auto& rec : m_records[part]) {
        if (std::find(seen.begin(), seen.end(), rec.sender) == seen.end())
            seen.push_back(rec.sender);
    }
    return seen.size();
}

Address CorruptionBlackBox::soleSenderOfWholePart(uint16 part, uint64 partLength) const
{
    if (static_cast<std::size_t>(part) >= m_records.size() || partLength == 0)
        return {};

    const auto& partRecords = m_records[part];
    if (partRecords.empty())
        return {};

    const Address& sender = partRecords.front().sender;
    if (sender.isNull())
        return {};

    std::vector<std::pair<uint64, uint64>> ranges;
    ranges.reserve(partRecords.size());
    for (const auto& rec : partRecords) {
        if (!(rec.sender == sender))
            return {};
        ranges.emplace_back(rec.startPos, rec.endPos);
    }

    // Contiguous cover of the whole part, allowing for overlap: two sources can be
    // told to fetch the same range, and a resumed HTTP fetch re-writes its seam.
    std::sort(ranges.begin(), ranges.end());
    if (ranges.front().first != 0)
        return {};

    uint64 reach = ranges.front().second;
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].first > reach + 1)
            return {};
        reach = std::max(reach, ranges[i].second);
    }

    return (reach + 1 >= partLength) ? sender : Address{};
}

} // namespace eMule
