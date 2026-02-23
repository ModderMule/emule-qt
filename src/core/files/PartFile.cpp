/// @file PartFile.cpp
/// @brief In-progress download file — port of MFC CPartFile.
///
/// Core download file implementation: gap management, buffered I/O,
/// status machine, priority, block selection, persistence, source tracking.

#include "files/PartFile.h"
#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHHashTree.h"
#include "crypto/FileIdentifier.h"
#include "crypto/MD4Hash.h"
#include "crypto/SHAHash.h"
#include "ipfilter/IPFilter.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>

namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

PartFile::PartFile(uint32 category)
    : m_category(category)
{
    initPartFile();
}

PartFile::~PartFile()
{
    // Flush any remaining buffered data
    if (!m_bufferedData.empty()) {
        flushBuffer();
    }

    // Close the part file handle
    if (m_partFileHandle.isOpen())
        m_partFileHandle.close();

    // Clean up requested blocks
    for (auto* block : m_requestedBlocks)
        delete block;
    m_requestedBlocks.clear();
}

// ===========================================================================
// initPartFile (private)
// ===========================================================================

void PartFile::initPartFile()
{
    m_status = PartFileStatus::Empty;
    m_fileOp = PartFileOp::None;
    m_downPriority = kPrNormal;
    m_autoDownPriority = thePrefs.autoDownloadPriority();
    m_paused = false;
    m_stopped = false;
    m_insufficient = false;
    m_completionError = false;
    m_transferred = 0;
    m_corruptionLoss = 0;
    m_compressionGain = 0;
    m_datarate = 0;
    m_completedSize = 0;
    m_percentCompleted = 0.0f;
    m_totalBufferData = 0;
    m_lastBufferFlushTime = 0;
    m_dlActiveTime = 0;
    m_tLastModified = 0;
    m_tCreated = std::time(nullptr);
    m_lastPausePurge = 0;
    m_md4HashsetNeeded = true;
    m_aichPartHashsetNeeded = true;
    m_anStates.fill(0);
}

// ===========================================================================
// Identity
// ===========================================================================

bool PartFile::isPartFile() const
{
    return m_status != PartFileStatus::Complete;
}

// ===========================================================================
// setFileSize — also inits gap and frequency arrays
// ===========================================================================

void PartFile::setFileSize(EMFileSize size)
{
    KnownFile::setFileSize(size);
    m_aichRecoveryHashSet.setFileSize(size);

    if (size > 0 && m_gapList.empty()) {
        // Initialize single gap covering entire file
        m_gapList.push_back({0, static_cast<uint64>(size) - 1});
    }

    // Initialize source part frequency array
    m_srcPartFrequency.resize(partCount(), 0);

    updateCompletedInfos();
}

// ===========================================================================
// Gap Management
// ===========================================================================

void PartFile::addGap(uint64 start, uint64 end)
{
    if (start > end)
        return;

    // Clamp to file size
    const uint64 fs = static_cast<uint64>(fileSize());
    if (fs == 0)
        return;
    if (end >= fs)
        end = fs - 1;

    // Merge overlapping/adjacent gaps
    auto it = m_gapList.begin();
    while (it != m_gapList.end()) {
        if (it->start > end + 1) {
            // No more overlaps possible — insert before this gap
            m_gapList.insert(it, {start, end});
            updateCompletedInfos();
            return;
        }
        if (it->end + 1 >= start) {
            // Overlap or adjacent — merge
            start = std::min(start, it->start);
            end = std::max(end, it->end);
            it = m_gapList.erase(it);
        } else {
            ++it;
        }
    }
    // Append at end
    m_gapList.push_back({start, end});
    updateCompletedInfos();
}

void PartFile::fillGap(uint64 start, uint64 end)
{
    if (start > end)
        return;

    auto it = m_gapList.begin();
    while (it != m_gapList.end()) {
        if (it->start > end)
            break; // past the filled range

        if (it->end < start) {
            ++it;
            continue; // before the filled range
        }

        // Overlap detected
        if (start <= it->start && end >= it->end) {
            // Gap fully contained — remove entirely
            it = m_gapList.erase(it);
        } else if (start <= it->start) {
            // Trim head
            it->start = end + 1;
            ++it;
        } else if (end >= it->end) {
            // Trim tail
            it->end = start - 1;
            ++it;
        } else {
            // Split: gap spans the filled range
            const uint64 origEnd = it->end;
            it->end = start - 1;
            ++it;
            m_gapList.insert(it, {end + 1, origEnd});
            break;
        }
    }

    updateCompletedInfos();
}

bool PartFile::isComplete(uint64 start, uint64 end) const
{
    for (const auto& gap : m_gapList) {
        if (gap.start > end)
            break;
        if (gap.end >= start)
            return false; // Gap intersects the range
    }
    return true;
}

bool PartFile::isComplete(uint32 part) const
{
    if (part >= partCount())
        return false;

    const uint64 partStart = static_cast<uint64>(part) * PARTSIZE;
    uint64 partEnd = partStart + PARTSIZE - 1;
    const uint64 fs = static_cast<uint64>(fileSize());
    if (partEnd >= fs)
        partEnd = fs - 1;

    return isComplete(partStart, partEnd);
}

bool PartFile::isPureGap(uint64 start, uint64 end) const
{
    for (const auto& gap : m_gapList) {
        if (gap.start <= start && gap.end >= end)
            return true;
        if (gap.start > start)
            break;
    }
    return false;
}

bool PartFile::isAlreadyRequested(uint64 start, uint64 end) const
{
    for (const auto* block : m_requestedBlocks) {
        if (block->startOffset <= end && block->endOffset >= start)
            return true;
    }
    return false;
}

uint64 PartFile::totalGapSizeInRange(uint64 start, uint64 end) const
{
    uint64 total = 0;
    for (const auto& gap : m_gapList) {
        if (gap.start > end)
            break;
        if (gap.end < start)
            continue;

        const uint64 overlapStart = std::max(gap.start, start);
        const uint64 overlapEnd = std::min(gap.end, end);
        total += overlapEnd - overlapStart + 1;
    }
    return total;
}

uint64 PartFile::totalGapSizeInPart(uint32 part) const
{
    const uint64 partStart = static_cast<uint64>(part) * PARTSIZE;
    uint64 partEnd = partStart + PARTSIZE - 1;
    const uint64 fs = static_cast<uint64>(fileSize());
    if (partEnd >= fs)
        partEnd = fs - 1;

    return totalGapSizeInRange(partStart, partEnd);
}

void PartFile::updateCompletedInfos()
{
    const uint64 fs = static_cast<uint64>(fileSize());
    if (fs == 0) {
        m_completedSize = 0;
        m_percentCompleted = 0.0f;
        return;
    }

    uint64 totalGaps = 0;
    for (const auto& gap : m_gapList)
        totalGaps += gap.end - gap.start + 1;

    m_completedSize = (fs > totalGaps) ? fs - totalGaps : 0;
    m_percentCompleted = static_cast<float>(
        static_cast<double>(m_completedSize) * 100.0 / static_cast<double>(fs));

    emit m_partNotifier.progressUpdated(m_percentCompleted);
}

// ===========================================================================
// Buffered I/O
// ===========================================================================

void PartFile::writeToBuffer(uint64 transize, const uint8* data,
                              uint64 start, uint64 end,
                              Requested_Block_Struct* block)
{
    Q_UNUSED(transize);

    if (!data || start > end)
        return;

    // Create buffered data entry with copy of data
    BufferedData bd;
    bd.start = start;
    bd.end = end;
    bd.data.assign(data, data + (end - start + 1));
    bd.block = block;

    // Insert sorted by end offset
    auto it = m_bufferedData.begin();
    while (it != m_bufferedData.end() && it->end < bd.end)
        ++it;
    m_bufferedData.insert(it, std::move(bd));

    m_totalBufferData += (end - start + 1);

    // Fill the gap for this range
    fillGap(start, end);

    // If file is complete (no gaps), flush immediately
    if (m_gapList.empty())
        flushBuffer();
}

void PartFile::flushBuffer(bool forceICH)
{
    if (m_bufferedData.empty())
        return;

    // Open file if not already open
    if (!m_partFileHandle.isOpen()) {
        const QString partFilePath = m_tmpPath + QDir::separator() + m_partMetFilename;
        // Derive .part file name from .part.met
        QString partPath = partFilePath;
        if (partPath.endsWith(QStringLiteral(".met")))
            partPath.chop(4); // Remove ".met" to get ".part"

        m_partFileHandle.setFileName(partPath);
        if (!m_partFileHandle.open(QIODevice::ReadWrite)) {
            logError(QStringLiteral("PartFile::flushBuffer: failed to open %1")
                         .arg(partPath));
            return;
        }
    }

    // Write each buffered entry to disk
    for (const auto& bd : m_bufferedData) {
        m_partFileHandle.seek(static_cast<qint64>(bd.start));
        m_partFileHandle.write(reinterpret_cast<const char*>(bd.data.data()),
                                static_cast<qint64>(bd.data.size()));
    }

    m_partFileHandle.flush();
    m_bufferedData.clear();
    m_totalBufferData = 0;

    // Hash verification per changed part (MD4 + AICH)
    for (uint32 p = 0; p < partCount(); ++p) {
        if (!isComplete(p))
            continue;

        bool aichAgreed = false;
        if (!hashSinglePart(p, &aichAgreed)) {
            const uint64 partStart = static_cast<uint64>(p) * PARTSIZE;
            const uint64 partEnd = std::min(partStart + PARTSIZE - 1,
                                             static_cast<uint64>(fileSize()) - 1);

            logWarning(QStringLiteral("PartFile: hash mismatch for part %1 of '%2' — re-downloading")
                           .arg(p).arg(fileName()));
            addGap(partStart, partEnd);

            // Add part to corrupted list, if not already there
            if (std::ranges::find(m_corruptedParts, static_cast<uint16>(p)) == m_corruptedParts.end())
                m_corruptedParts.push_back(static_cast<uint16>(p));

            // Request AICH recovery data if AICH didn't already agree
            if (!forceICH && !aichAgreed)
                requestAICHRecovery(p);

            // Track corruption loss
            const uint64 lost = partEnd - partStart + 1;
            m_corruptionLoss += lost;
        }
    }

    // If no gaps remain, file is complete
    if (m_gapList.empty()) {
        completeFile();
        return;
    }

    // Periodic save of .part.met
    const uint32 curTick = static_cast<uint32>(getTickCount());
    if (m_lastBufferFlushTime == 0 || (curTick - m_lastBufferFlushTime) > 30000) {
        savePartFile();
        m_lastBufferFlushTime = curTick;
    }
}

// ===========================================================================
// Block Selection
// ===========================================================================

bool PartFile::getNextRequestedBlock(UpDownClient* sender,
                                      Requested_Block_Struct** newblocks,
                                      int& count)
{
    if (!sender || count <= 0)
        return false;

    const auto& partStatus = sender->partStatus();
    const uint16 senderPartCount = sender->partCount();

    if (senderPartCount == 0 && !sender->completeSource())
        return false;

    // Build list of candidate parts (sender has it AND we need it)
    struct PartCandidate {
        uint32 part;
        int score;
    };
    std::vector<PartCandidate> candidates;

    const uint16 pc = partCount();
    for (uint32 p = 0; p < pc; ++p) {
        // Check if sender has this part
        bool senderHasPart = sender->completeSource();
        if (!senderHasPart && p < partStatus.size())
            senderHasPart = (partStatus[p] != 0);

        if (!senderHasPart)
            continue;

        // Check if we need this part (has gaps)
        if (isComplete(p))
            continue;

        // Score the part
        int score = 0;

        // Rarity bonus
        if (p < m_srcPartFrequency.size()) {
            uint16 freq = m_srcPartFrequency[p];
            if (freq <= 3)
                score += 50;    // very rare
            else if (freq <= 7)
                score += 25;    // rare
            else if (freq <= 15)
                score += 10;    // somewhat rare
        }

        // Completion bonus — prefer nearly-complete parts
        const uint64 partStart = static_cast<uint64>(p) * PARTSIZE;
        uint64 partEnd = partStart + PARTSIZE - 1;
        const uint64 fs = static_cast<uint64>(fileSize());
        if (partEnd >= fs)
            partEnd = fs - 1;
        const uint64 partSize = partEnd - partStart + 1;
        const uint64 gapInPart = totalGapSizeInPart(p);
        if (partSize > 0) {
            const int completion = static_cast<int>((partSize - gapInPart) * 100 / partSize);
            score += completion / 5;
        }

        // Preview priority — first and last parts
        if (p == 0 || p == pc - 1)
            score += 30;

        candidates.push_back({p, score});
    }

    if (candidates.empty())
        return false;

    // Sort by score descending
    std::ranges::sort(candidates, [](const PartCandidate& a, const PartCandidate& b) {
        return a.score > b.score;
    });

    int blocksFound = 0;
    for (const auto& cand : candidates) {
        if (blocksFound >= count)
            break;

        auto* reqBlock = new Requested_Block_Struct;
        if (getNextEmptyBlockInPart(cand.part, reqBlock)) {
            // Check not already requested
            if (!isAlreadyRequested(reqBlock->startOffset, reqBlock->endOffset)) {
                newblocks[blocksFound] = reqBlock;
                m_requestedBlocks.push_back(reqBlock);
                ++blocksFound;
            } else {
                delete reqBlock;
            }
        } else {
            delete reqBlock;
        }
    }

    count = blocksFound;
    return blocksFound > 0;
}

bool PartFile::getNextEmptyBlockInPart(uint32 partNumber,
                                        Requested_Block_Struct* reqBlock) const
{
    if (!reqBlock)
        return false;

    const uint64 partStart = static_cast<uint64>(partNumber) * PARTSIZE;
    uint64 partEnd = partStart + PARTSIZE - 1;
    const uint64 fs = static_cast<uint64>(fileSize());
    if (partEnd >= fs)
        partEnd = fs - 1;

    // Find first gap within this part's byte range
    for (const auto& gap : m_gapList) {
        if (gap.start > partEnd)
            break;
        if (gap.end < partStart)
            continue;

        // Found a gap in this part
        const uint64 blockStart = std::max(gap.start, partStart);
        uint64 blockEnd = std::min(gap.end, partEnd);

        // Align to EMBLOCKSIZE
        if (blockEnd - blockStart + 1 > EMBLOCKSIZE)
            blockEnd = blockStart + EMBLOCKSIZE - 1;

        reqBlock->startOffset = blockStart;
        reqBlock->endOffset = blockEnd;
        std::memcpy(reqBlock->fileID.data(), fileHash(), 16);
        return true;
    }

    return false;
}

bool PartFile::removeBlockFromList(uint64 start, uint64 end)
{
    for (auto it = m_requestedBlocks.begin(); it != m_requestedBlocks.end(); ++it) {
        if ((*it)->startOffset == start && (*it)->endOffset == end) {
            delete *it;
            m_requestedBlocks.erase(it);
            return true;
        }
    }
    return false;
}

void PartFile::removeAllRequestedBlocks()
{
    for (auto* block : m_requestedBlocks)
        delete block;
    m_requestedBlocks.clear();
}

// ===========================================================================
// Status Machine
// ===========================================================================

void PartFile::setStatus(PartFileStatus s)
{
    if (m_status == s)
        return;
    m_status = s;
    emit m_partNotifier.statusChanged(s);
}

void PartFile::pauseFile(bool insufficient)
{
    m_paused = true;
    m_insufficient = insufficient;

    if (insufficient)
        setStatus(PartFileStatus::Insufficient);
    else
        setStatus(PartFileStatus::Paused);

    m_datarate = 0;
    savePartFile();
}

void PartFile::resumeFile()
{
    if (!m_paused && !m_stopped)
        return;

    m_paused = false;
    m_stopped = false;
    m_insufficient = false;

    setStatus(m_gapList.empty() ? PartFileStatus::Completing : PartFileStatus::Ready);

    // If we had a completion error but no gaps, retry completion
    if (m_completionError && m_gapList.empty()) {
        m_completionError = false;
        completeFile();
    }

    savePartFile();
}

void PartFile::stopFile(bool cancel)
{
    Q_UNUSED(cancel);

    m_paused = true;
    m_stopped = true;

    // Flush any buffered data
    if (!m_bufferedData.empty())
        flushBuffer();

    m_datarate = 0;
    setStatus(PartFileStatus::Paused);
    savePartFile();
}

// ===========================================================================
// Priority
// ===========================================================================

void PartFile::setDownPriority(uint8 priority)
{
    switch (priority) {
    case kPrVeryLow:
    case kPrLow:
    case kPrNormal:
    case kPrHigh:
    case kPrVeryHigh:
        m_downPriority = priority;
        break;
    default:
        m_downPriority = kPrNormal;
        break;
    }
}

void PartFile::updateAutoDownPriority()
{
    if (!m_autoDownPriority)
        return;

    const auto srcCount = m_srcList.size();
    uint8 newPriority;
    if (srcCount <= 3)
        newPriority = kPrHigh;
    else if (srcCount <= 20)
        newPriority = kPrNormal;
    else
        newPriority = kPrLow;

    m_downPriority = newPriority;
}

bool PartFile::rightFileHasHigherPrio(const PartFile* left, const PartFile* right)
{
    if (!left || !right)
        return false;

    // Higher category first
    if (left->category() != right->category())
        return left->category() < right->category();

    // Higher download priority first (kPrVeryHigh=3 > kPrHigh=2 > etc.)
    if (left->downPriority() != right->downPriority())
        return left->downPriority() < right->downPriority();

    // Older file first (earlier creation time)
    return left->m_tCreated > right->m_tCreated;
}

// ===========================================================================
// Source Tracking
// ===========================================================================

void PartFile::addSource(UpDownClient* client)
{
    if (!client)
        return;
    if (std::ranges::find(m_srcList, client) != m_srcList.end())
        return;
    m_srcList.push_back(client);

    // Update part frequency
    if (client->completeSource()) {
        for (auto& freq : m_srcPartFrequency)
            ++freq;
    } else {
        const auto& status = client->partStatus();
        for (uint16 i = 0; i < std::min<size_t>(status.size(), m_srcPartFrequency.size()); ++i) {
            if (status[i])
                ++m_srcPartFrequency[i];
        }
    }

    updateAutoDownPriority();
    emit m_partNotifier.sourceAdded(client);
}

void PartFile::removeSource(UpDownClient* client)
{
    auto it = std::ranges::find(m_srcList, client);
    if (it == m_srcList.end())
        return;
    m_srcList.erase(it);

    // Update part frequency
    if (client->completeSource()) {
        for (auto& freq : m_srcPartFrequency)
            if (freq > 0) --freq;
    } else {
        const auto& status = client->partStatus();
        for (uint16 i = 0; i < std::min<size_t>(status.size(), m_srcPartFrequency.size()); ++i) {
            if (status[i] && m_srcPartFrequency[i] > 0)
                --m_srcPartFrequency[i];
        }
    }

    // Also remove from downloading sources
    removeDownloadingSource(client);

    updateAutoDownPriority();
    emit m_partNotifier.sourceRemoved(client);
}

void PartFile::addDownloadingSource(UpDownClient* client)
{
    if (!client)
        return;
    if (std::ranges::find(m_downloadingSources, client) == m_downloadingSources.end())
        m_downloadingSources.push_back(client);
}

void PartFile::removeDownloadingSource(UpDownClient* client)
{
    auto it = std::ranges::find(m_downloadingSources, client);
    if (it != m_downloadingSources.end())
        m_downloadingSources.erase(it);
}

// ===========================================================================
// Persistence — CreatePartFile
// ===========================================================================

bool PartFile::createPartFile(const QString& tempDir)
{
    m_tmpPath = tempDir;

    // Ensure temp directory exists
    QDir dir(tempDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    // Generate unique NNN.part filename
    static std::atomic<uint32> counter{0};
    const uint32 num = counter.fetch_add(1);
    m_partMetFilename = QStringLiteral("%1.part.met").arg(num, 3, 10, QChar(u'0'));
    m_fullName = tempDir + QDir::separator() + m_partMetFilename;

    // Create the .part file
    const QString partPath = tempDir + QDir::separator()
                              + QStringLiteral("%1.part").arg(num, 3, 10, QChar(u'0'));
    m_partFileHandle.setFileName(partPath);
    if (!m_partFileHandle.open(QIODevice::ReadWrite)) {
        logError(QStringLiteral("PartFile::createPartFile: failed to create %1").arg(partPath));
        return false;
    }

    // Resize file to target size
    const uint64 fs = static_cast<uint64>(fileSize());
    if (fs > 0)
        m_partFileHandle.resize(static_cast<qint64>(fs));

    // Init gap covering entire file
    m_gapList.clear();
    if (fs > 0)
        m_gapList.push_back({0, fs - 1});

    // Init part frequency
    m_srcPartFrequency.resize(partCount(), 0);

    m_tCreated = std::time(nullptr);
    m_status = PartFileStatus::Empty;

    // Save initial .part.met
    savePartFile();

    return true;
}

// ===========================================================================
// Persistence — LoadPartFile
// ===========================================================================

PartFileLoadResult PartFile::loadPartFile(const QString& directory,
                                           const QString& filename)
{
    m_tmpPath = directory;
    m_partMetFilename = filename;
    m_fullName = directory + QDir::separator() + filename;

    const QString metPath = m_fullName;
    SafeFile file(metPath, QIODevice::ReadOnly);

    try {
        // Read version byte
        const uint8 version = file.readUInt8();
        if (version != PARTFILE_VERSION &&
            version != PARTFILE_VERSION_LARGEFILE &&
            version != PARTFILE_SPLITTEDVERSION)
        {
            logWarning(QStringLiteral("PartFile::loadPartFile: unknown version 0x%1 in %2")
                           .arg(version, 2, 16, QChar(u'0'))
                           .arg(metPath));
            return PartFileLoadResult::FailedCorrupt;
        }

        // Read timestamp
        m_tLastModified = static_cast<time_t>(file.readUInt32());

        // Read MD4 hash
        uint8 hash[16];
        file.readHash16(hash);
        setFileHash(hash);

        // Read hashset (part hashes) and store in FileIdentifier
        const uint16 hashCount = file.readUInt16();
        auto& md4HashSet = fileIdentifier().getRawMD4HashSet();
        md4HashSet.clear();
        md4HashSet.reserve(hashCount);
        for (uint16 i = 0; i < hashCount; ++i) {
            std::array<uint8, 16> partHash{};
            file.readHash16(partHash.data());
            md4HashSet.push_back(partHash);
        }
        if (hashCount > 0)
            m_md4HashsetNeeded = false;

        // Read tag count and iterate
        const uint32 tagCount = file.readUInt32();
        m_gapList.clear();

        // Temporary gap storage (pairs of start/end)
        std::vector<std::pair<uint64, uint64>> gapPairs;
        uint64 pendingGapStart = UINT64_MAX;

        for (uint32 i = 0; i < tagCount; ++i) {
            Tag tag(file, true);

            switch (tag.nameId()) {
            case FT_FILENAME:
                if (tag.isStr())
                    setFileName(tag.strValue(), true);
                break;
            case FT_FILESIZE:
                if (tag.isInt())
                    setFileSize(tag.intValue());
                else if (tag.isInt64(false))
                    setFileSize(tag.int64Value());
                break;
            case FT_FILESIZE_HI:
                if (tag.isInt()) {
                    auto hi = static_cast<uint64>(tag.intValue());
                    setFileSize((hi << 32) | static_cast<uint64>(fileSize()));
                }
                break;
            case FT_TRANSFERRED:
                if (tag.isInt())
                    m_transferred = tag.intValue();
                else if (tag.isInt64(false))
                    m_transferred = tag.int64Value();
                break;
            case FT_CORRUPTED:
                if (tag.isInt())
                    m_corruptionLoss = tag.intValue();
                else if (tag.isInt64(false))
                    m_corruptionLoss = tag.int64Value();
                break;
            case FT_COMPRESSION:
                if (tag.isInt())
                    m_compressionGain = tag.intValue();
                else if (tag.isInt64(false))
                    m_compressionGain = tag.int64Value();
                break;
            case FT_DLPRIORITY:
                if (tag.isInt()) {
                    auto val = static_cast<uint8>(tag.intValue());
                    if (val == kPrAuto) {
                        m_autoDownPriority = true;
                        m_downPriority = kPrNormal;
                    } else {
                        m_autoDownPriority = false;
                        if (val <= kPrVeryLow)
                            m_downPriority = val;
                        else
                            m_downPriority = kPrNormal;
                    }
                }
                break;
            case FT_STATUS:
                if (tag.isInt())
                    m_paused = (tag.intValue() != 0);
                break;
            case FT_CATEGORY:
                if (tag.isInt())
                    m_category = tag.intValue();
                break;
            case FT_DL_ACTIVE_TIME:
                if (tag.isInt())
                    m_dlActiveTime = tag.intValue();
                break;
            case FT_AICH_HASH:
                if (tag.isStr()) {
                    AICHHash aichHash;
                    if (decodeBase32(tag.strValue(), aichHash.getRawHash(), kAICHHashSize) == kAICHHashSize) {
                        fileIdentifier().setAICHHash(aichHash);
                        m_aichRecoveryHashSet.setMasterHash(aichHash, EAICHStatus::Verified);
                    }
                }
                break;
            case FT_AICHHASHSET:
                if (tag.isBlob()) {
                    const auto& blob = tag.blobValue();
                    SafeMemFile aichFile(reinterpret_cast<const uint8*>(blob.constData()),
                                         static_cast<qint64>(blob.size()));
                    bool loadedAICHHashSet = fileIdentifier().loadAICHHashsetFromFile(aichFile, false);
                    if (loadedAICHHashSet) {
                        if (fileIdentifier().verifyAICHHashSet())
                            m_aichPartHashsetNeeded = false;
                        else
                            logWarning(QStringLiteral("Failed to verify AICH hashset for '%1'")
                                           .arg(fileName()));
                    }
                }
                break;
            case FT_CORRUPTEDPARTS:
                if (tag.isStr()) {
                    // Parse comma-separated corrupted part list
                    const auto parts = tag.strValue().split(u',');
                    for (const auto& ps : parts) {
                        bool ok = false;
                        const uint16 partNum = ps.toUShort(&ok);
                        if (ok)
                            m_corruptedParts.push_back(partNum);
                    }
                }
                break;
            default: {
                // Handle gap tags — either by numeric nameId (new format)
                // or by string name starting with FT_GAPSTART/FT_GAPEND byte
                // followed by gap index digits (original eMule format).
                const bool isGapStart =
                    tag.nameId() == FT_GAPSTART ||
                    (!tag.name().isEmpty() &&
                     static_cast<uint8>(tag.name().at(0)) == FT_GAPSTART);
                const bool isGapEnd =
                    tag.nameId() == FT_GAPEND ||
                    (!tag.name().isEmpty() &&
                     static_cast<uint8>(tag.name().at(0)) == FT_GAPEND);

                if (isGapStart) {
                    if (tag.isInt())
                        pendingGapStart = tag.intValue();
                    else if (tag.isInt64(false))
                        pendingGapStart = tag.int64Value();
                } else if (isGapEnd) {
                    uint64 gapEnd = 0;
                    if (tag.isInt())
                        gapEnd = tag.intValue();
                    else if (tag.isInt64(false))
                        gapEnd = tag.int64Value();

                    if (pendingGapStart != UINT64_MAX) {
                        gapPairs.push_back({pendingGapStart, gapEnd});
                        pendingGapStart = UINT64_MAX;
                    }
                } else {
                    addTagUnique(std::move(tag));
                }
                break;
            }
            }
        }

        // Replace auto-generated gap with actual gaps from file.
        // Clamp end to fileSize-1 (inclusive range) — some clients
        // store gap end == fileSize for the trailing gap.
        m_gapList.clear();
        const uint64 fs = static_cast<uint64>(fileSize());
        for (auto [gStart, gEnd] : gapPairs) {
            if (fs == 0 || gStart >= fs)
                continue;
            if (gEnd >= fs)
                gEnd = fs - 1;
            if (gStart <= gEnd)
                m_gapList.push_back({gStart, gEnd});
        }

        // Sort gaps
        m_gapList.sort([](const Gap& a, const Gap& b) {
            return a.start < b.start;
        });

    } catch (const FileException& ex) {
        logError(QStringLiteral("PartFile::loadPartFile: error reading %1: %2")
                     .arg(metPath, QString::fromStdString(ex.what())));
        return PartFileLoadResult::FailedCorrupt;
    }

    // Open .part file and verify size
    QString partPath = metPath;
    if (partPath.endsWith(QStringLiteral(".met")))
        partPath.chop(4);

    if (!QFile::exists(partPath)) {
        logError(QStringLiteral("PartFile::loadPartFile: .part file missing: %1").arg(partPath));
        return PartFileLoadResult::FailedNoAccess;
    }

    // Init part frequency array
    m_srcPartFrequency.resize(partCount(), 0);

    // Update completed infos
    updateCompletedInfos();

    // Set status
    if (m_paused)
        m_status = PartFileStatus::Paused;
    else if (m_gapList.empty())
        m_status = PartFileStatus::Completing;
    else
        m_status = PartFileStatus::Ready;

    return PartFileLoadResult::LoadSuccess;
}

// ===========================================================================
// Persistence — SavePartFile
// ===========================================================================

bool PartFile::savePartFile()
{
    if (m_fullName.isEmpty())
        return false;

    // Write to temp file first, then atomic rename
    const QString tempPath = m_fullName + QStringLiteral(".backup");

    try {
        SafeFile file(tempPath, QIODevice::WriteOnly);

        // Version
        const bool largeFile = isLargeFile();
        file.writeUInt8(largeFile ? PARTFILE_VERSION_LARGEFILE : PARTFILE_VERSION);

        // Timestamp
        file.writeUInt32(static_cast<uint32>(m_tLastModified));

        // MD4 hash
        file.writeHash16(fileHash());

        // Part hashes (hashset)
        const uint16 hashCount = fileIdentifier().getAvailableMD4PartHashCount();
        file.writeUInt16(hashCount);
        for (uint16 i = 0; i < hashCount; ++i) {
            const uint8* partHash = fileIdentifier().getMD4PartHash(i);
            if (partHash)
                file.writeHash16(partHash);
            else {
                uint8 zeroHash[16] = {};
                file.writeHash16(zeroHash);
            }
        }

        // Count tags
        uint32 tagCount = 0;
        tagCount += 1; // FT_FILENAME
        tagCount += 1; // FT_FILESIZE
        if (largeFile)
            tagCount += 1; // FT_FILESIZE_HI
        tagCount += 1; // FT_TRANSFERRED
        tagCount += 1; // FT_DLPRIORITY
        tagCount += 1; // FT_STATUS
        tagCount += 1; // FT_CATEGORY
        if (m_corruptionLoss > 0)
            tagCount += 1; // FT_CORRUPTED
        if (m_compressionGain > 0)
            tagCount += 1; // FT_COMPRESSION
        if (m_dlActiveTime > 0)
            tagCount += 1; // FT_DL_ACTIVE_TIME
        if (!m_corruptedParts.empty())
            tagCount += 1; // FT_CORRUPTEDPARTS
        if (fileIdentifier().hasAICHHash())
            tagCount += 1; // FT_AICH_HASH
        if (fileIdentifier().hasExpectedAICHHashCount())
            tagCount += 1; // FT_AICHHASHSET

        // Gap pairs
        tagCount += static_cast<uint32>(m_gapList.size()) * 2; // start + end per gap

        file.writeUInt32(tagCount);

        // -- Write tags --

        // Filename
        Tag(FT_FILENAME, fileName()).writeNewEd2kTag(file, UTF8Mode::OptBOM);

        // File size
        if (largeFile) {
            Tag(FT_FILESIZE, static_cast<uint32>(static_cast<uint64>(fileSize()) & 0xFFFFFFFFu))
                .writeNewEd2kTag(file);
            Tag(FT_FILESIZE_HI, static_cast<uint32>(static_cast<uint64>(fileSize()) >> 32))
                .writeNewEd2kTag(file);
        } else {
            Tag(FT_FILESIZE, static_cast<uint32>(fileSize()))
                .writeNewEd2kTag(file);
        }

        // Transferred
        if (m_transferred > UINT32_MAX) {
            Tag(FT_TRANSFERRED, static_cast<uint64>(m_transferred))
                .writeNewEd2kTag(file);
        } else {
            Tag(FT_TRANSFERRED, static_cast<uint32>(m_transferred))
                .writeNewEd2kTag(file);
        }

        // Priority
        Tag(FT_DLPRIORITY,
            static_cast<uint32>(m_autoDownPriority ? kPrAuto : m_downPriority))
            .writeNewEd2kTag(file);

        // Status (paused)
        Tag(FT_STATUS, static_cast<uint32>(m_paused ? 1u : 0u))
            .writeNewEd2kTag(file);

        // Category
        Tag(FT_CATEGORY, m_category).writeNewEd2kTag(file);

        // Corruption loss
        if (m_corruptionLoss > 0) {
            Tag(FT_CORRUPTED, static_cast<uint32>(m_corruptionLoss))
                .writeNewEd2kTag(file);
        }

        // Compression gain
        if (m_compressionGain > 0) {
            Tag(FT_COMPRESSION, static_cast<uint32>(m_compressionGain))
                .writeNewEd2kTag(file);
        }

        // Active download time
        if (m_dlActiveTime > 0) {
            Tag(FT_DL_ACTIVE_TIME, m_dlActiveTime).writeNewEd2kTag(file);
        }

        // Corrupted parts list
        if (!m_corruptedParts.empty()) {
            QString partList;
            for (size_t i = 0; i < m_corruptedParts.size(); ++i) {
                if (i > 0) partList += u',';
                partList += QString::number(m_corruptedParts[i]);
            }
            Tag(FT_CORRUPTEDPARTS, partList).writeNewEd2kTag(file, UTF8Mode::Raw);
        }

        // AICH hash (base32 encoded)
        if (fileIdentifier().hasAICHHash()) {
            Tag(FT_AICH_HASH, fileIdentifier().getAICHHash().getString())
                .writeNewEd2kTag(file, UTF8Mode::Raw);
        }

        // AICH part hashset (binary blob)
        if (fileIdentifier().hasExpectedAICHHashCount()) {
            SafeMemFile aichFile;
            fileIdentifier().writeAICHHashsetToFile(aichFile);
            Tag(FT_AICHHASHSET, aichFile.buffer()).writeNewEd2kTag(file);
        }

        // Gap pairs as FT_GAPSTART/FT_GAPEND tags
        for (const auto& gap : m_gapList) {
            if (largeFile) {
                Tag(FT_GAPSTART, static_cast<uint64>(gap.start)).writeNewEd2kTag(file);
                Tag(FT_GAPEND, static_cast<uint64>(gap.end)).writeNewEd2kTag(file);
            } else {
                Tag(FT_GAPSTART, static_cast<uint32>(gap.start)).writeNewEd2kTag(file);
                Tag(FT_GAPEND, static_cast<uint32>(gap.end)).writeNewEd2kTag(file);
            }
        }

    } catch (const FileException& ex) {
        logError(QStringLiteral("PartFile::savePartFile: error writing %1: %2")
                     .arg(tempPath, QString::fromStdString(ex.what())));
        return false;
    }

    // Atomic rename: temp → original
    if (QFile::exists(m_fullName))
        QFile::remove(m_fullName);
    if (!QFile::rename(tempPath, m_fullName)) {
        logError(QStringLiteral("PartFile::savePartFile: rename failed %1 → %2")
                     .arg(tempPath, m_fullName));
        return false;
    }

    // Create .bak backup copy
    const QString bakPath = m_fullName + QStringLiteral(".bak");
    QFile::copy(m_fullName, bakPath);

    return true;
}

// ===========================================================================
// completeFile (private) — initiates async file move
// ===========================================================================

void PartFile::completeFile()
{
    setStatus(PartFileStatus::Completing);

    // Flush any remaining buffer
    if (!m_bufferedData.empty())
        flushBuffer();

    // Verify AICH master hash if available
    if (m_aichRecoveryHashSet.hasValidMasterHash()
        && m_aichRecoveryHashSet.getStatus() == EAICHStatus::HashSetComplete)
    {
        // Save AICH hashset to known2_64.met
        m_aichRecoveryHashSet.saveHashSet();

        // Set AICH hash on file identifier if not already set
        if (!fileIdentifier().hasAICHHash())
            fileIdentifier().setAICHHash(m_aichRecoveryHashSet.getMasterHash());

        // Extract AICH part hashes into FileIdentifier
        if (!fileIdentifier().hasExpectedAICHHashCount())
            fileIdentifier().setAICHHashSet(m_aichRecoveryHashSet);
    }

    // Close the part file
    if (m_partFileHandle.isOpen())
        m_partFileHandle.close();

    // Derive source path (.part file)
    QString partPath = m_fullName;
    if (partPath.endsWith(QStringLiteral(".met")))
        partPath.chop(4);

    const QString incomingDir = thePrefs.incomingDir();
    const QString destPath = incomingDir + QDir::separator() + fileName();

    // Perform the file move asynchronously
    performFileMove(partPath, destPath);
}

// ===========================================================================
// Process — periodic tick
// ===========================================================================

uint32 PartFile::process(uint32 reduceDownload, uint32 counter)
{
    Q_UNUSED(reduceDownload);
    Q_UNUSED(counter);

    if (m_paused || m_stopped)
        return 0;

    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Flush buffer if size or time threshold exceeded
    const uint32 bufferSize = thePrefs.fileBufferSize();
    const uint32 bufferTimeLimit = thePrefs.fileBufferTimeLimit();

    if (m_totalBufferData > 0) {
        const bool sizeExceeded = m_totalBufferData >= bufferSize;
        const bool timeExceeded = (m_lastBufferFlushTime > 0) &&
                                   ((curTick - m_lastBufferFlushTime) > bufferTimeLimit * 1000);

        if (sizeExceeded || timeExceeded || m_lastBufferFlushTime == 0) {
            flushBuffer();
            m_lastBufferFlushTime = curTick;
        }
    }

    // Calculate datarate from downloading sources
    m_datarate = 0;
    for (auto* client : m_downloadingSources) {
        m_datarate += client->calculateDownloadRate();
    }

    // Retry connections to idle sources — MFC PartFile.cpp Process() source loop.
    // Only attempt a few per cycle to avoid socket flooding.
    int connectAttempts = 0;
    static constexpr int kMaxConnectAttemptsPerCycle = 3;
    for (auto* client : m_srcList) {
        if (connectAttempts >= kMaxConnectAttemptsPerCycle)
            break;

        const auto ds = client->downloadState();
        const bool disconnectedOnQueue =
            (ds == DownloadState::OnQueue) && !client->socket();

        if ((ds == DownloadState::None || disconnectedOnQueue)
            && client->connectingState() == ConnectingState::None)
        {
            // Reset OnQueue → None so tryToConnect sets Connecting and
            // connectionEstablished defers the file request properly.
            if (disconnectedOnQueue)
                client->setDownloadState(DownloadState::None);

            if (client->tryToConnect())
                ++connectAttempts;
        }
    }

    return m_datarate;
}

// ===========================================================================
// Protocol helpers
// ===========================================================================

void PartFile::writePartStatus(SafeMemFile& file) const
{
    const uint16 pc = partCount();
    file.writeUInt16(pc);

    if (pc == 0)
        return;

    const uint16 byteCount = (pc + 7) / 8;
    std::vector<uint8> bitmap(byteCount, 0);

    for (uint32 i = 0; i < pc; ++i) {
        if (isComplete(i))
            bitmap[i / 8] |= static_cast<uint8>(1 << (i % 8));
    }

    file.write(bitmap.data(), byteCount);
}

void PartFile::writeCompleteSourcesCount(SafeMemFile& file) const
{
    file.writeUInt16(completeSourcesCount());
}

void PartFile::getFilledArray(std::vector<Gap>& filled) const
{
    filled.clear();
    const uint64 fs = static_cast<uint64>(fileSize());
    if (fs == 0)
        return;

    uint64 pos = 0;
    for (const auto& gap : m_gapList) {
        if (gap.start > pos)
            filled.push_back({pos, gap.start - 1});
        pos = gap.end + 1;
    }
    if (pos < fs)
        filled.push_back({pos, fs - 1});
}

// ===========================================================================
// Stubs
// ===========================================================================

void PartFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

    // Aggregate from Kad notes cache
    for (const auto& [rating, comment] : m_kadNotesCache) {
        if (!comment.isEmpty())
            hasNewComment = true;
        if (rating > 0 && rating <= 5) {
            ratingSum += rating;
            ++ratingCount;
        }
    }

    bool changed = false;

    if (hasNewComment != m_hasComment) {
        m_hasComment = hasNewComment;
        changed = true;
    }

    uint32 newRating = (ratingCount > 0) ? (ratingSum / ratingCount) : 0;
    if (newRating != m_userRating) {
        m_userRating = newRating;
        changed = true;
    }

    if (changed)
        emit m_partNotifier.progressUpdated(m_percentCompleted);
}

// ===========================================================================
// FileMoveThread — async file move for completed downloads
// ===========================================================================

FileMoveThread::FileMoveThread(const QString& srcPath, const QString& destPath,
                               QObject* parent)
    : QThread(parent)
    , m_srcPath(srcPath)
    , m_destPath(destPath)
{
}

void FileMoveThread::run()
{
    // Ensure destination directory exists
    QDir destDir(QFileInfo(m_destPath).absolutePath());
    if (!destDir.exists())
        destDir.mkpath(QStringLiteral("."));

    // Handle duplicate filenames — append (N) suffix
    QString finalDest = m_destPath;
    if (QFile::exists(finalDest)) {
        const QFileInfo fi(m_destPath);
        const QString baseName = fi.completeBaseName();
        const QString suffix = fi.suffix();
        const QString dir = fi.absolutePath();
        int counter = 1;
        do {
            if (suffix.isEmpty())
                finalDest = QStringLiteral("%1/%2 (%3)").arg(dir, baseName).arg(counter);
            else
                finalDest = QStringLiteral("%1/%2 (%3).%4").arg(dir, baseName).arg(counter).arg(suffix);
            ++counter;
        } while (QFile::exists(finalDest));
    }

    // Try rename first (instant on same filesystem)
    if (QFile::rename(m_srcPath, finalDest)) {
        m_destPath = finalDest;
        emit moveFinished(true, finalDest);
        return;
    }

    // Rename failed (cross-filesystem) — perform copy + delete
    QFile srcFile(m_srcPath);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        emit moveFinished(false, finalDest);
        return;
    }

    QFile destFile(finalDest);
    if (!destFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit moveFinished(false, finalDest);
        return;
    }

    constexpr qint64 kBufSize = 256 * 1024;
    char buf[kBufSize];
    while (true) {
        const qint64 got = srcFile.read(buf, kBufSize);
        if (got <= 0)
            break;
        if (destFile.write(buf, got) != got) {
            destFile.close();
            QFile::remove(finalDest);
            emit moveFinished(false, finalDest);
            return;
        }
    }

    srcFile.close();
    destFile.close();

    // Verify copy size
    if (QFileInfo(finalDest).size() != QFileInfo(m_srcPath).size()) {
        QFile::remove(finalDest);
        emit moveFinished(false, finalDest);
        return;
    }

    // Remove source
    QFile::remove(m_srcPath);
    m_destPath = finalDest;
    emit moveFinished(true, finalDest);
}

// ===========================================================================
// performFileMove (private) — launches async file move thread
// ===========================================================================

void PartFile::performFileMove(const QString& srcPath, const QString& destPath)
{
    auto* thread = new FileMoveThread(srcPath, destPath, &m_partNotifier);

    QObject::connect(thread, &FileMoveThread::moveFinished,
                     &m_partNotifier, [this](bool success, const QString& finalPath) {
        if (success) {
            // Delete .part.met and .bak
            QFile::remove(m_fullName);
            QFile::remove(m_fullName + QStringLiteral(".bak"));

            setStatus(PartFileStatus::Complete);
            setFilePath(finalPath);
            setPath(QFileInfo(finalPath).absolutePath());
            m_tLastModified = std::time(nullptr);

            // DownloadQueue handles SharedFileList/KnownFileList integration
            // via the downloadCompleted() signal connection
            emit m_partNotifier.downloadCompleted();
        } else {
            logError(QStringLiteral("PartFile::completeFile: file move failed %1 → %2")
                         .arg(m_fullName, finalPath));
            m_completionError = true;
            setStatus(PartFileStatus::Error);
        }
        emit m_partNotifier.fileMoveFinished(success);
    });

    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// ===========================================================================
// hashSinglePart (private) — verify MD4 + AICH for one part
// ===========================================================================

bool PartFile::hashSinglePart(uint32 partNumber, bool* aichAgreed)
{
    if (aichAgreed)
        *aichAgreed = false;

    if (partNumber >= partCount())
        return true; // out of range, nothing to verify

    const uint64 partStart = static_cast<uint64>(partNumber) * PARTSIZE;
    const uint64 partEnd = std::min(partStart + PARTSIZE - 1,
                                     static_cast<uint64>(fileSize()) - 1);
    const uint64 partLen = partEnd - partStart + 1;

    // Open file if needed
    if (!m_partFileHandle.isOpen()) {
        QString partPath = m_fullName;
        if (partPath.endsWith(QStringLiteral(".met")))
            partPath.chop(4);
        m_partFileHandle.setFileName(partPath);
        if (!m_partFileHandle.open(QIODevice::ReadWrite))
            return true; // can't verify, assume OK
    }

    // Read part data from disk
    m_partFileHandle.seek(static_cast<qint64>(partStart));
    QByteArray partData = m_partFileHandle.read(static_cast<qint64>(partLen));
    if (static_cast<uint64>(partData.size()) != partLen)
        return true; // can't read, assume OK

    // AICH verification (block-level, more precise than MD4)
    if (m_aichRecoveryHashSet.hasValidMasterHash()
        && (m_aichRecoveryHashSet.getStatus() == EAICHStatus::Verified
            || m_aichRecoveryHashSet.getStatus() == EAICHStatus::Trusted
            || m_aichRecoveryHashSet.getStatus() == EAICHStatus::HashSetComplete))
    {
        // Build AICH hash tree from actual part data
        AICHHashTree aichPartTree(partLen, true, EMBLOCKSIZE);
        KnownFile::createHashFromMemory(
            reinterpret_cast<const uint8*>(partData.constData()),
            static_cast<uint32>(partLen), nullptr, &aichPartTree);

        if (aichPartTree.m_hashValid) {
            // Compare with the stored part hash from the recovery hashset
            const AICHHashTree* storedPartNode =
                m_aichRecoveryHashSet.m_hashTree.findExistingHash(partStart, partLen);
            if (storedPartNode && storedPartNode->m_hashValid) {
                if (aichPartTree.m_hash == storedPartNode->m_hash) {
                    if (aichAgreed)
                        *aichAgreed = true;
                }
            }
        }
    }

    // MD4 verification
    bool md4OK = true;
    const uint8* storedHash = fileIdentifier().getMD4PartHash(partNumber);
    if (storedHash) {
        uint8 computedHash[16]{};
        KnownFile::createHashFromMemory(
            reinterpret_cast<const uint8*>(partData.constData()),
            static_cast<uint32>(partLen), computedHash, nullptr);

        if (!md4equ(computedHash, storedHash))
            md4OK = false;
    }

    // Both must agree for the part to be considered valid
    // If AICH was verified and agrees, we trust it even if MD4 can't verify
    // If MD4 verifies OK, the part is valid
    // If MD4 fails, the part is corrupt regardless of AICH
    if (!md4OK)
        return false;

    // If AICH was checked and disagrees while MD4 was OK, trust MD4
    return true;
}

// ===========================================================================
// requestAICHRecovery — request AICH recovery data from a source
// ===========================================================================

void PartFile::requestAICHRecovery(uint32 partNumber)
{
    if (!m_aichRecoveryHashSet.hasValidMasterHash()
        || (m_aichRecoveryHashSet.getStatus() != EAICHStatus::Trusted
            && m_aichRecoveryHashSet.getStatus() != EAICHStatus::Verified))
    {
        logDebug(QStringLiteral("PartFile: unable to request AICH recovery — no trusted master hash"));
        return;
    }

    if (static_cast<uint64>(fileSize()) <= static_cast<uint64>(partNumber) * PARTSIZE + EMBLOCKSIZE)
        return;

    // Check if recovery data is already available in memory
    if (m_aichRecoveryHashSet.isPartDataAvailable(
            static_cast<uint64>(partNumber) * PARTSIZE, fileSize()))
    {
        logInfo(QStringLiteral("PartFile: found AICH recovery data in memory for part %1").arg(partNumber));
        aichRecoveryDataAvailable(partNumber);
        return;
    }

    // Find a random client that supports AICH with matching master hash
    uint32 aichClients = 0;
    uint32 aichLowIDClients = 0;
    for (const auto* client : m_srcList) {
        if (client->isSupportingAICH()
            && client->reqFileAICHHash()
            && !client->isAICHReqPending()
            && *client->reqFileAICHHash() == m_aichRecoveryHashSet.getMasterHash())
        {
            if (client->hasLowID())
                ++aichLowIDClients;
            else
                ++aichClients;
        }
    }

    if ((aichClients | aichLowIDClients) == 0) {
        logDebug(QStringLiteral("PartFile: no AICH-supporting client found for recovery"));
        return;
    }

    // Select a random client
    static std::mt19937 rng(std::random_device{}());
    const uint32 pool = (aichClients > 0) ? aichClients : aichLowIDClients;
    const uint32 selected = std::uniform_int_distribution<uint32>(1, pool)(rng);
    uint32 count = 0;

    for (auto* client : m_srcList) {
        if (client->isSupportingAICH()
            && client->reqFileAICHHash()
            && !client->isAICHReqPending()
            && *client->reqFileAICHHash() == m_aichRecoveryHashSet.getMasterHash())
        {
            if (aichClients > 0 && client->hasLowID())
                continue;
            ++count;
            if (count == selected) {
                logInfo(QStringLiteral("PartFile: requesting AICH recovery for part %1 from %2")
                            .arg(partNumber).arg(client->hasLowID()
                                ? QStringLiteral("LowID") : QStringLiteral("HighID")));
                client->sendAICHRequest(this, static_cast<uint16>(partNumber));
                return;
            }
        }
    }
}

// ===========================================================================
// aichRecoveryDataAvailable — process received AICH recovery data
// ===========================================================================

void PartFile::aichRecoveryDataAvailable(uint32 partNumber)
{
    if (partNumber >= partCount())
        return;

    // Flush any pending data first (without requesting AICH again)
    flushBuffer(true);

    const uint64 partStart = static_cast<uint64>(partNumber) * PARTSIZE;
    const uint64 partLen = std::min<uint64>(PARTSIZE, static_cast<uint64>(fileSize()) - partStart);

    // If the part is already complete, nothing to recover
    if (isComplete(partStart, partStart + partLen - 1)) {
        logDebug(QStringLiteral("PartFile AICH recovery: part %1 is already complete").arg(partNumber));
        return;
    }

    // Get the verified hash subtree for this part
    const AICHHashTree* verifiedHash =
        m_aichRecoveryHashSet.m_hashTree.findExistingHash(partStart, partLen);
    if (!verifiedHash || !verifiedHash->m_hashValid) {
        logWarning(QStringLiteral("PartFile AICH recovery: no verified hash for part %1").arg(partNumber));
        return;
    }

    // Open file and read part data
    if (!m_partFileHandle.isOpen()) {
        QString partPath = m_fullName;
        if (partPath.endsWith(QStringLiteral(".met")))
            partPath.chop(4);
        m_partFileHandle.setFileName(partPath);
        if (!m_partFileHandle.open(QIODevice::ReadWrite))
            return;
    }

    m_partFileHandle.seek(static_cast<qint64>(partStart));
    QByteArray partData = m_partFileHandle.read(static_cast<qint64>(partLen));
    if (static_cast<uint64>(partData.size()) != partLen)
        return;

    // Build our own AICH hash tree from the actual data on disk
    AICHHashTree ourHash(verifiedHash->m_dataSize, verifiedHash->m_isLeftBranch,
                         verifiedHash->getBaseSize());
    KnownFile::createHashFromMemory(
        reinterpret_cast<const uint8*>(partData.constData()),
        static_cast<uint32>(partLen), nullptr, &ourHash);

    if (!ourHash.m_hashValid) {
        logWarning(QStringLiteral("PartFile AICH recovery: failed to hash part %1 data").arg(partNumber));
        return;
    }

    // Compare block-by-block: recover good blocks, discard bad ones
    uint64 recovered = 0;
    for (uint64 pos = 0; pos < partLen; pos += EMBLOCKSIZE) {
        const uint64 blockSize = std::min<uint64>(EMBLOCKSIZE, partLen - pos);
        const AICHHashTree* verifiedBlock = verifiedHash->findExistingHash(pos, blockSize);
        const AICHHashTree* ourBlock = ourHash.findExistingHash(pos, blockSize);

        if (!verifiedBlock || !ourBlock || !verifiedBlock->m_hashValid || !ourBlock->m_hashValid)
            continue;

        if (ourBlock->m_hash == verifiedBlock->m_hash) {
            // This block is valid — mark as filled
            fillGap(partStart + pos, partStart + pos + blockSize - 1);
            removeBlockFromList(partStart + pos, partStart + pos + blockSize - 1);
            recovered += blockSize;
        }
    }

    // Adjust corruption loss accounting
    if (m_corruptionLoss >= recovered)
        m_corruptionLoss -= recovered;

    // Sanity check: if the part became complete, verify with MD4 too
    if (isComplete(partStart, partStart + partLen - 1)) {
        if (!hashSinglePart(partNumber)) {
            logWarning(QStringLiteral("PartFile AICH recovery: part %1 completed but MD4 disagrees — marking corrupt")
                           .arg(partNumber));
            if (!fileIdentifier().hasAICHHash())
                m_aichRecoveryHashSet.setStatus(EAICHStatus::Error);
            addGap(partStart, partStart + partLen - 1);
            return;
        }

        logInfo(QStringLiteral("PartFile AICH recovery: part %1 completed and MD4 verified").arg(partNumber));

        // Remove from corrupted list
        auto corruptIt = std::ranges::find(m_corruptedParts, static_cast<uint16>(partNumber));
        if (corruptIt != m_corruptedParts.end())
            m_corruptedParts.erase(corruptIt);

        // Check if entire file is now complete
        if (m_gapList.empty() && m_bufferedData.empty())
            completeFile();
    }

    savePartFile();
    logInfo(QStringLiteral("PartFile AICH recovery: recovered %1 of %2 bytes from part %3 of '%4'")
                .arg(recovered).arg(partLen).arg(partNumber).arg(fileName()));
}

// ===========================================================================
// createSrcInfoPacket — SX2 override for PartFile (uses srcList)
// ===========================================================================

std::unique_ptr<Packet> PartFile::createSrcInfoPacket(
    const UpDownClient* forClient, uint8 version, uint16 /*options*/) const
{
    if (m_srcList.empty() || !forClient)
        return nullptr;

    // Negotiate version down to our max supported
    const uint8 usedVersion = std::min(version, static_cast<uint8>(SOURCEEXCHANGE2_VERSION));

    SafeMemFile data;

    // SX2 header: version byte
    data.writeUInt8(usedVersion);

    // File hash (16 bytes)
    data.writeHash16(fileHash());

    // Placeholder for source count (will seek back to fill in)
    const auto countPos = data.position();
    data.writeUInt16(0);

    const uint16 maxSources = (usedVersion >= 4) ? 500 : 50;
    uint16 count = 0;

    // Get forClient's part status for filtering
    const auto& clientParts = forClient->upPartStatus();

    for (const auto* src : m_srcList) {
        if (count >= maxSources)
            break;

        // Skip low-ID clients (they can't be contacted directly)
        if (src->hasLowID())
            continue;

        // Skip invalid IPs
        if (src->userIP() == 0)
            continue;

        // Skip the requesting client itself
        if (src == forClient)
            continue;

        // Check if this source has parts the requester needs
        const auto& srcParts = src->partStatus();
        if (!srcParts.empty() && !clientParts.empty() &&
            srcParts.size() == clientParts.size())
        {
            bool hasNeededPart = false;
            for (size_t p = 0; p < srcParts.size(); ++p) {
                if (srcParts[p] != 0 && clientParts[p] == 0) {
                    hasNeededPart = true;
                    break;
                }
            }
            if (!hasNeededPart)
                continue;
        }

        // Write per-source data
        data.writeUInt32(src->userIP());      // userId (4 bytes)
        data.writeUInt16(src->userPort());     // port (2 bytes)
        data.writeUInt32(src->serverIP());     // serverIP (4 bytes)
        data.writeUInt16(src->serverPort());   // serverPort (2 bytes)

        if (usedVersion >= 2)
            data.writeHash16(src->userHash()); // userHash (16 bytes)

        if (usedVersion >= 4) {
            // Crypt options byte
            uint8 cryptOpts = 0;
            if (src->supportsCryptLayer())
                cryptOpts |= 0x01;
            if (src->requestsCryptLayer())
                cryptOpts |= 0x02;
            if (src->requiresCryptLayer())
                cryptOpts |= 0x04;
            if (src->supportsDirectUDPCallback())
                cryptOpts |= 0x08;
            data.writeUInt8(cryptOpts);
        }

        ++count;
    }

    if (count == 0)
        return nullptr;

    // Seek back and write actual count
    const auto endPos = data.position();
    data.seek(static_cast<int>(countPos), SEEK_SET);
    data.writeUInt16(count);
    data.seek(static_cast<int>(endPos), SEEK_SET);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_ANSWERSOURCES2);
    if (packet->size > 354)
        packet->packPacket();

    return packet;
}

// ===========================================================================
// addClientSources — process SX2 source answer for this PartFile
// ===========================================================================

void PartFile::addClientSources(SafeMemFile& data, uint8 version, const UpDownClient* sender)
{
    Q_UNUSED(sender);

    if (version == 0 || version > SOURCEEXCHANGE2_VERSION)
        return;

    const uint16 srcCount = data.readUInt16();

    for (uint16 i = 0; i < srcCount; ++i) {
        uint32 userId = data.readUInt32();
        uint16 port = data.readUInt16();
        uint32 serverIP = data.readUInt32();
        uint16 serverPort = data.readUInt16();

        std::array<uint8, 16> userHash{};
        if (version >= 2)
            data.readHash16(userHash.data());

        uint8 cryptFlags = 0;
        if (version >= 4)
            cryptFlags = data.readUInt8();

        // v1/v2: IDs were in network byte order for high-ID detection
        if (version < 3)
            userId = ntohl(userId);

        // Validate IP (for high-ID clients, userId == IP)
        if (!isLowID(userId) && !isGoodIP(userId))
            continue;

        // IPFilter check
        if (!isLowID(userId) && theApp.ipFilter && theApp.ipFilter->isFiltered(userId))
            continue;

        // Max sources check
        if (sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
            break;

        auto* client = new UpDownClient(port, userId, serverIP, serverPort, this, version < 3);
        client->setSourceFrom(SourceFrom::SourceExchange);

        if (version >= 2)
            client->setUserHash(userHash.data());

        if (version >= 4)
            client->setConnectOptions(cryptFlags, true, false);

        if (theApp.downloadQueue) {
            if (theApp.downloadQueue->checkAndAddSource(this, client)) {
                client->tryToConnect();
            } else {
                delete client;
            }
        } else {
            delete client;
        }
    }
}

} // namespace eMule
