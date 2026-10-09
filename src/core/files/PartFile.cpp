#include "pch.h"
/// @file PartFile.cpp
/// @brief In-progress download file — port of MFC CPartFile.
///
/// Core download file implementation: gap management, buffered I/O,
/// status machine, priority, block selection, persistence, source tracking.

#include "files/PartFile.h"
#include "files/PartFileWriteThread.h"
#include "app/AppContext.h"
#include "search/SeenFileIndex.h"
#include "utils/OtherFunctions.h"
#include "files/SharedFileList.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHHashTree.h"
#include "crypto/FileIdentifier.h"
#include "crypto/MD4Hash.h"
#include "httpcache/HttpCacheManager.h"
#include "ipfilter/IPFilter.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/FileDate.h"
#include "utils/Log.h"
#include "utils/PathUtils.h"
#include "utils/SafeFile.h"
#include "utils/StringUtils.h"
#include "utils/TimeUtils.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadSearch.h"
#include "server/ServerConnect.h"
#include "server/Server.h"


#include <QScopeGuard>
#include <QtEndian>
#include <QDir>
#include <QFileInfo>


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
    // The flush below can verify a part, and a verified part shares the file — which
    // would hand a dying object to the shared list (or call into one already gone).
    m_destroying = true;

    // A QThread must not outlive its owner's signals half-way through a file: stop it at
    // the next part and wait. It does not start the move once interrupted.
    if (m_moveThread) {
        m_moveThread->requestInterruption();
        m_moveThread->wait();
    }

    // Flush any remaining buffered data, and collect a write still with the worker
    flushBuffer(/*forceICH*/ false, /*noAICH*/ true);   // MFC srchybrid/PartFile.cpp:294

    // Close the part file handle before saving metadata
    if (m_partFileHandle.isOpen())
        m_partFileHandle.close();

    // Save .part.met with final gap list (matches MFC CPartFile destructor)
    if (!m_partMetFilename.isEmpty() && status() != PartFileStatus::Complete)
        savePartFile();

    // Null out m_reqFile in any sources still referencing this file,
    // so late socket-error callbacks don't dereference a dangling pointer.
    for (auto* client : m_srcList) {
        if (client->reqFile() == this)
            client->setReqFile(nullptr);
    }
    for (auto* client : m_a4afSrcList) {
        if (client->reqFile() == this)
            client->setReqFile(nullptr);
    }
    unlinkA4AFSources();

    // Clear requested blocks list — blocks are owned by clients'
    // Pending_Block_Struct and freed by clearPendingBlockRequest.
    m_requestedBlocks.clear();
}

// ===========================================================================
// isPreviewPossible — media file with first part complete
// ===========================================================================

bool PartFile::isPreviewPossible() const
{
    // Must be a media file (Video or Audio)
    const auto ft = getED2KFileTypeID(fileName());
    if (ft != ED2KFileType::Video && ft != ED2KFileType::Audio)
        return false;

    // Must be in a downloadable state (not error, not already complete)
    if (m_status == PartFileStatus::Error || m_status == PartFileStatus::Complete)
        return false;

    // First 9.28 MB part must be fully downloaded
    return isComplete(static_cast<uint32>(0));
}

// ===========================================================================
// initPartFile (private)
// ===========================================================================

void PartFile::initPartFile()
{
    m_status = PartFileStatus::Empty;
    m_deadSourceList.init(/*globalList*/ false);
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
    m_nextMetSaveTime = 0;
    m_dlActiveTime = 0;
    m_tLastModified = 0;
    m_tCreated = std::time(nullptr);
    m_lastPausePurge = std::time(nullptr);
    m_md4HashsetNeeded = true;
    m_aichPartHashsetNeeded = true;
    m_corruptionBlackBox.free();
}

// ===========================================================================
// Identity
// ===========================================================================

bool PartFile::isPartFile() const
{
    return m_status != PartFileStatus::Complete;
}

// ===========================================================================
// canBeShared / addToSharedFiles — MFC CPartFile::AddToSharedFiles
// ===========================================================================

bool PartFile::canBeShared() const
{
    // A part file we cannot hash-verify has nothing safe to offer: without the MD4
    // hashset a peer asking for part 3 gets bytes we cannot check, and the whole
    // ed2k trust chain for that transfer rests on us. MFC srchybrid/PartFile.cpp:4509.
    //
    // MFC also tests m_bMD4HashsetNeeded here; this asks only the question that flag is
    // an answer to. In this port the flag means "still to be requested from a peer" —
    // it is *derived* from this same predicate when a .part.met is loaded (:1507) and
    // cleared when a hashset arrives over the wire (DownloadClient.cpp:1075,1093) — so
    // testing both would ask the same thing twice, except for a hashset installed
    // programmatically, where nobody re-derives the flag and it is stale-true.
    if (!fileIdentifier().hasExpectedMD4HashCount())
        return false;

    // And at least one whole part, or there is simply nothing to serve. MFC reaches
    // the same condition from the other side: LoadPartFile only promotes PS_EMPTY to
    // PS_READY once IsCompleteBD() answers true for some part
    // (srchybrid/PartFile.cpp:1093-1105).
    for (uint32 p = 0; p < partCount(); ++p) {
        if (isComplete(p))
            return true;
    }
    return false;
}

void PartFile::addToSharedFiles()
{
    // "part files are always shared files" — srchybrid/DownloadQueue.cpp:109,127.
    // Without this the file is served only to peers that already know about us
    // (findUploadFile() falls back to the download queue), because both advertising
    // paths — OP_OFFERFILES and Kad source publishing — walk SharedFileList's map and
    // nothing else. We were invisible as a partial source.
    // MFC's shape exactly (srchybrid/PartFile.cpp:4507-4516): only Empty is promoted,
    // so this is the one-way latch. A file already Ready is in the share; the re-add
    // after a reload goes through DownloadQueue::addPartFilesToShare() instead.
    // Reads the raw member, not status(): a paused file still gets promoted and shared.
    if (m_destroying || !theApp.sharedFileList || m_status != PartFileStatus::Empty
        || !canBeShared())
        return;

    setStatus(PartFileStatus::Ready);
    theApp.sharedFileList->safeAddKFile(this);
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

    // The blackbox is sized in parts. Loading a .part.met calls this several times
    // as the size tags come in, so only re-init when the shape actually changed —
    // init() resizes and would otherwise drop records mid-download.
    if (m_corruptionBlackBox.partCount() != partCount())
        m_corruptionBlackBox.init(static_cast<uint64>(size));

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

bool PartFile::isCompleteBDSafe(uint64 start, uint64 end) const
{
    // MFC srchybrid/PartFile.cpp:1631-1651
    if (fileSize() == 0)
        return false;
    end = std::min(end, fileSize() - 1);
    if (start > end || !isComplete(start, end))
        return false;
    // Sorted by end, not start, so no early break. Data with the worker is not
    // on disk yet either.
    const auto overlaps = [&](const BufferedData& bd) { return bd.start <= end && bd.end >= start; };
    return std::ranges::none_of(m_bufferedData, overlaps)
        && std::ranges::none_of(m_flushingData, overlaps);
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

uint64 PartFile::totalGapSize() const
{
    uint64 total = 0;
    for (const auto& gap : m_gapList)
        total += gap.end - gap.start + 1;
    return total;
}

bool PartFile::isCorruptedPart(uint32 partNumber) const
{
    return std::ranges::find(m_corruptedParts, static_cast<uint16>(partNumber))
           != m_corruptedParts.end();
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

uint32 PartFile::writeToBuffer(uint64 transize, const uint8* data,
                                uint64 start, uint64 end,
                                Requested_Block_Struct* block,
                                const Address& sender)
{
    if (!data || start > end || end >= static_cast<uint64>(fileSize()))
        return 0;

    m_transferred += transize;   // MFC PartFile.cpp:3953; every caller here is a network one

    // The rehash worker is reading the .part right now; a write would race it.
    if (m_status == PartFileStatus::Hashing || m_status == PartFileStatus::WaitingForHash)
        return 0;

    // A compressed block covers more of the file than it took on the wire; the
    // difference is what compression saved (MFC: srchybrid/PartFile.cpp:3959-3963).
    // For an uncompressed block the two are equal and nothing is counted.
    if (const uint64 lenData = end - start + 1; lenData > transize) {
        const uint64 gain = lenData - transize;
        m_compressionGain += gain;
        if (theApp.statistics)
            theApp.statistics->addCompressionGain(gain);
    }

    // Duplicate (the endgame hands one block to several sources) or data aimed at a
    // part that is already complete: never write it twice, never over verified data,
    // and never blame its sender — MFC srchybrid/PartFile.cpp:3966-3985.
    if (isComplete(start, end))
        return 0;
    const auto startPart = static_cast<uint32>(start / PARTSIZE);
    const auto endPart = static_cast<uint32>(end / PARTSIZE);
    if (isComplete(startPart) || (endPart != startPart && isComplete(endPart))) {
        logDebug(QStringLiteral("Received data touches an already complete part - ignored: %1")
                     .arg(fileName()));
        return 0;
    }

    // Who to blame if this part later fails its hash (MFC: srchybrid/PartFile.cpp:3988).
    // A null sender is the "importing parts" case MFC guards the same way, plus every
    // transfer that has no peer behind it at all — see the URLClient and HttpCacheClient
    // call sites.
    if (!sender.isNull())
        m_corruptionBlackBox.transferredData(start, end, sender);

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

    // Flush immediately when the file is complete, or when it is in any state other
    // than plain downloading — an import writes whole parts through here and there is
    // nothing to gain from holding them (MFC srchybrid/PartFile.cpp:4031-4034).
    // The last block of a running download goes the way every other flush does;
    // anything else (stopping, completing, erroring) wants it on disk now.
    if (status() != PartFileStatus::Ready && status() != PartFileStatus::Empty)
        flushBuffer();
    else if (m_gapList.empty()
             || m_totalBufferData > static_cast<uint64>(thePrefs.fileBufferSize()) * 2)
        flushBufferAsync();   // twice the limit: not until the next tick (MFC :4033)

    return static_cast<uint32>(end - start + 1);
}

void PartFile::flushBufferAsync()
{
    PartFileWriteThread* writer = theApp.partFileWriter;
    if (!writer || m_destroying) {
        flushBuffer();
        return;
    }
    // One job per file: what came in since goes with the next one.
    if (m_bufferedData.empty() || m_flushToken != 0)
        return;

    if (theApp.downloadQueue && !theApp.downloadQueue->reserveForWrite(this, m_totalBufferData)) {
        if (!m_insufficient && !m_paused && !m_stopped)
            pauseFile(/*insufficient*/ true);
        return;
    }

    PartFileWriteJob job;
    job.fileHash = QByteArray(reinterpret_cast<const char*>(fileHash()), 16);
    job.token = PartFileWriteThread::nextToken();
    job.partPath = partFilePath();

    // The parts these bytes touch, plus any still waiting for a check, are read
    // back by the worker — where a check can be made at all.
    std::vector<bool> touched = m_changedParts;
    touched.resize(partCount(), false);
    job.chunks.reserve(m_bufferedData.size());
    for (auto& bd : m_bufferedData) {
        for (uint64 p = bd.start / PARTSIZE; p <= bd.end / PARTSIZE && p < touched.size(); ++p)
            touched[p] = true;
        job.chunks.push_back({bd.start, std::move(bd.data)});
        bd.data.clear();
    }
    const bool canCheck = fileIdentifier().hasExpectedMD4HashCount()
        || (fileIdentifier().hasAICHHash() && fileIdentifier().hasExpectedAICHHashCount());
    if (canCheck) {
        for (uint32 p = 0; p < touched.size(); ++p) {
            if (touched[p] && (isComplete(p) || (isCorruptedPart(p) && thePrefs.useICH())))
                job.digests.push_back(digestRequest(p));
        }
    }

    m_flushingData.splice(m_flushingData.end(), m_bufferedData);
    m_flushingBytes = m_totalBufferData;
    m_totalBufferData = 0;
    m_flushToken = job.token;

    // The worker writes through its own handle; ours must not hold stale pages.
    if (m_partFileHandle.isOpen())
        m_partFileHandle.close();

    writer->enqueue(std::move(job), this);
}

void PartFile::applyFlushResult(PartFileWriteResult& result, bool forceICH, bool noAICH)
{
    if (result.token != m_flushToken || m_flushToken == 0)
        return;                          // not ours any more
    m_flushToken = 0;

    if (!result.written) {
        // Held, not discarded: the bytes come back and the next flush tries again.
        auto chunk = result.chunks.begin();
        for (auto& bd : m_flushingData) {
            if (chunk == result.chunks.end())
                break;
            bd.data = std::move(chunk->data);
            ++chunk;
        }
        m_bufferedData.merge(m_flushingData, [](const BufferedData& a, const BufferedData& b) {
            return a.end < b.end;
        });
        m_totalBufferData += m_flushingBytes;
        m_flushingBytes = 0;
        if (!m_destroying)
            resumeIdleSources();   // nobody is held on a verdict that is not coming
        handleWriteFailure(result.error, result.diskFull);
        return;
    }

    for (const auto& bd : m_flushingData)
        markChangedParts(bd.start, bd.end);
    m_flushingData.clear();
    m_flushingBytes = 0;

    verifyChangedParts(forceICH, noAICH, &result.digests);

    // A part came back bad: the sources that were held on the verdict take it up.
    if (!m_gapList.empty() && !m_destroying)
        resumeIdleSources();

    // "Pause when preview is possible", once (MFC PartFile.cpp:4243-4246)
    if (!m_gapList.empty() && !m_destroying && isPausingOnPreview() && isPreviewPossible()) {
        m_pauseOnPreview = false;
        pauseFile();
    }

    if (m_gapList.empty() && !m_destroying) {
        // Blocks that arrived while this one was out still have to land first.
        if (m_bufferedData.empty())
            completeIfVerified();
        else if (status() == PartFileStatus::Ready || status() == PartFileStatus::Empty)
            flushBufferAsync();
        return;
    }

    const uint64 curTick = getTickCount();
    if (m_nextMetSaveTime < curTick) {
        savePartFile();
        m_nextMetSaveTime = curTick + 30000;
    }
}

void PartFile::flushBuffer(bool forceICH, bool noAICH)
{
    finishPendingFlush(forceICH, noAICH);

    if (m_bufferedData.empty())
        return;

    // Refused before the write, not after it failed: a volume at its floor stays
    // there. The buffer is kept; the file comes back when there is room again.
    if (!m_destroying && theApp.downloadQueue
        && !theApp.downloadQueue->reserveForWrite(this, m_totalBufferData)) {
        if (!m_insufficient && !m_paused && !m_stopped)
            pauseFile(/*insufficient*/ true);
        return;
    }

    // Open file if not already open
    if (!m_partFileHandle.isOpen()) {
        const QString partFilePath = m_tmpPath + QDir::separator() + m_partMetFilename;
        // Derive .part file name from .part.met
        QString partPath = partFilePath;
        if (partPath.endsWith(QStringLiteral(".met")))
            partPath.chop(4); // Remove ".met" to get ".part"

        m_partFileHandle.setFileName(partPath);
        if (!m_partFileHandle.open(QIODevice::ReadWrite)) {
            handleWriteFailure(m_partFileHandle.errorString(), false);
            return;
        }
    }

    // ⚠️ Every one of these can fail, and dropping the buffer anyway is worse
    // than any of them: the bytes are gone, the gap is filled with whatever was
    // on disk, and the part then fails its MD4 — which this file charges to the
    // *peer* that sent it (see punishCorruptionSenders below). A full disk would
    // quietly ban the people uploading to us. MFC throws diskFull here
    // (srchybrid/PartFile.cpp:4095); the Qt port has no exception path, so the
    // buffer is kept instead and the write is retried on the next flush.
    bool wrote = true;
    for (const auto& bd : m_bufferedData) {
        if (!m_partFileHandle.seek(static_cast<qint64>(bd.start))) {
            wrote = false;
            break;
        }
        const qint64 n =
            m_partFileHandle.write(reinterpret_cast<const char*>(bd.data.data()),
                                   static_cast<qint64>(bd.data.size()));
        if (n != static_cast<qint64>(bd.data.size())) {
            wrote = false;
            break;
        }
    }
    if (wrote && !m_partFileHandle.flush())
        wrote = false;

    if (!wrote) {
        // Held, not discarded: the next flush tries again.
        handleWriteFailure(m_partFileHandle.errorString(),
                           isDiskFullError(m_partFileHandle));
        return;
    }

    // Only what reached the disk is re-checked; a failed write marks nothing
    // (MFC DeleteWrittenItem, srchybrid/PartFile.cpp:4524-4526)
    for (const auto& bd : m_bufferedData)
        markChangedParts(bd.start, bd.end);

    m_bufferedData.clear();
    m_totalBufferData = 0;

    verifyChangedParts(forceICH, noAICH);

    // If no gaps remain, file is complete. Not from the destructor: completing starts a
    // move on this object; the saved .part.met completes it on the next start instead.
    if (m_gapList.empty() && !m_destroying) {
        completeIfVerified();
        return;
    }
    if (!m_destroying && isPausingOnPreview() && isPreviewPossible()) {
        m_pauseOnPreview = false;
        pauseFile();
    }

    // Periodic save of .part.met (separate timer from buffer flush — matches MFC m_nNextMetFlushTime)
    const uint64 curTick = getTickCount();
    if (m_nextMetSaveTime < curTick) {
        savePartFile();
        m_nextMetSaveTime = curTick + 30000; // save every ~30s
    }
}

// ===========================================================================
// hashsetReceived — verify parts that completed without a hashset
// ===========================================================================

void PartFile::hashsetReceived()
{
    // MFC has no equivalent and leaves such parts unverified
    if (m_status == PartFileStatus::Completing || m_status == PartFileStatus::Complete)
        return;

    bool any = false;
    for (uint32 p = 0; p < m_partsAwaitingHashset.size() && p < partCount(); ++p) {
        if (!m_partsAwaitingHashset[p])
            continue;
        // Back to awaiting via hashSinglePart if the hashset still doesn't cover it
        m_partsAwaitingHashset[p] = false;
        markChangedParts(static_cast<uint64>(p) * PARTSIZE, static_cast<uint64>(p) * PARTSIZE);
        any = true;
    }
    if (any)
        verifyChangedParts(/*forceICH*/ false, /*noAICH*/ false);

    // Persist the new hashset
    savePartFile();
}

// ===========================================================================
// Block Selection
// ===========================================================================

bool PartFile::getNextRequestedBlock(UpDownClient* sender,
                                      Requested_Block_Struct** newblocks,
                                      int& count)
{
    // MFC CPartFile::GetNextRequestedBlock (Maella Enhanced Chunk Selection).
    // Rank every part the sender can give us (rarity, preview, completion,
    // transferring clients), then take the best — random among ties, so
    // sources spread over the file instead of filling it left to right.
    if (!sender || count <= 0)
        return false;

    const auto& partStatus = sender->partStatus();
    if (partStatus.empty() && !sender->completeSource())
        return false;

    const uint16 pc = partCount();
    const uint64 fs = static_cast<uint64>(fileSize());
    int blocksFound = 0;

    // Near the end a block may get a second source — but only when its holder is not
    // getting on with it (see EndPhase). Everyone racing every block, as before, had
    // the slowest source set the pace of the last blocks and wasted the others' data.
    // A duplicate completion is harmless: writeToBuffer refuses data for a filled
    // range, and processBlockPacket then drops the loser's block.
    const EndPhase phase = endPhase();

    auto senderHasPart = [&](uint32 p) {
        return sender->completeSource() || (p < partStatus.size() && partStatus[p] != 0);
    };

    // Next block of a part not yet requested by anyone, shrunk around requested
    // ranges — or, late in the download, one whose holder may be doubled up on.
    auto nextFreeBlock = [&](uint32 partNum, uint64& searchFrom, uint64& start, uint64& end) {
        Requested_Block_Struct probe;
        while (getNextEmptyBlockInPart(partNum, &probe, searchFrom)) {
            start = probe.startOffset;
            end = probe.endOffset;
            if (shrinkToAvoidAlreadyRequested(start, end)) {
                searchFrom = end + 1;
                return true;
            }
            if (phase != EndPhase::Normal
                && maySecondSourceTake(sender, probe.startOffset, probe.endOffset, phase)) {
                start = probe.startOffset;
                end = probe.endOffset;
                searchFrom = end + 1;
                return true;
            }
            searchFrom = probe.endOffset + 1;
        }
        return false;
    };

    // Late in the download, a source much slower than another that has the same part
    // reserves only what it can deliver soon, one piece at a time, so the fast one is
    // not left waiting for it. 0: no limit. A rate of 0 is "not measured yet".
    auto slowReservation = [&](uint32 partNum) -> uint64 {
        if (phase == EndPhase::Normal)
            return 0;
        uint32 fastest = 0;
        for (const auto* other : m_downloadingSources) {
            if (other != sender && (other->completeSource() || other->isPartAvailable(partNum)))
                fastest = std::max(fastest, other->downDatarate());
        }
        const uint64 mine = sender->downDatarate();
        if (fastest == 0 || mine * kSlowSourceFactor > fastest)
            return 0;
        return mine == 0 ? uint64{EMBLOCKSIZE}
                         : std::max(mine * kSlowReservationSecs, kMinSlowReservation);
    };

    const uint64 nowTick = getTickCount();
    auto allocateFromPart = [&](uint32 partNum) {
        uint64 searchFrom = 0;
        uint64 start = 0;
        uint64 end = 0;
        const uint64 limit = slowReservation(partNum);
        while (blocksFound < count && nextFreeBlock(partNum, searchFrom, start, end)) {
            if (limit > 0 && end - start + 1 > limit)
                end = start + limit - 1;
            auto* reqBlock = new Requested_Block_Struct;
            reqBlock->startOffset = start;
            reqBlock->endOffset = end;
            reqBlock->holder = sender;
            reqBlock->lastProgressTick = nowTick;
            std::memcpy(reqBlock->fileID.data(), fileHash(), 16);
            newblocks[blocksFound++] = reqBlock;
            m_requestedBlocks.push_back(reqBlock);
            if (limit > 0) {
                count = blocksFound;   // one piece per round
                break;
            }
        }
    };

    // Continue the previous chunk; MFC does so only for eMule >= 0.43.1
    uint16 lastPart = sender->lastPartAsked();
    if (lastPart != UINT16_MAX
        && (sender->clientSoft() != ClientSoftware::eMule
            || sender->clientVersion() < makeClientVersion(0, 43, 1)))
        lastPart = UINT16_MAX;
    if (lastPart < pc && senderHasPart(lastPart)) {
        allocateFromPart(lastPart);
        if (blocksFound >= count) {
            count = blocksFound;
            return true;
        }
    }
    if (lastPart != UINT16_MAX)
        sender->setLastPartAsked(UINT16_MAX);

    struct PartCandidate {
        uint32 part;
        uint32 rank; // lower is better
    };
    std::vector<PartCandidate> candidates;

    // Zone bounds, more with more sources
    const int srcCount = sourceCount();
    const uint16 veryRareBound = static_cast<uint16>(std::max((srcCount + 9) / 10, 3));
    const uint16 rareBound = static_cast<uint16>(2 * veryRareBound);
    const uint16 almostRareBound = static_cast<uint16>(4 * veryRareBound);
    const bool a4afHeavy = srcCount <= static_cast<int>(m_a4afSrcList.size());

    // Preview chunks: first + last — for every file, or for this one in advanced mode
    // (MFC PartFile.cpp:4680-4682)
    const bool isPreviewEnable =
        (thePrefs.previewPrio() || (m_previewPrio && thePrefs.showExtControls()))
        && fs > 2 * PARTSIZE && isPreviewableFileType();

    static thread_local std::mt19937 rng(std::random_device{}());

    for (uint32 p = 0; p < pc; ++p) {
        if (p == lastPart || !senderHasPart(p))
            continue;
        uint64 probeFrom = 0;
        uint64 probeStart = 0;
        uint64 probeEnd = 0;
        if (!nextFreeBlock(p, probeFrom, probeStart, probeEnd))
            continue;

        // Criterion 1: frequency
        const uint16 freq = p < m_srcPartFrequency.size() ? m_srcPartFrequency[p] : 0;

        const uint64 partStart = static_cast<uint64>(p) * PARTSIZE;
        const uint64 partEnd = std::min(partStart + PARTSIZE, fs) - 1;

        // Criterion 2: preview parts; a tiny last part pulls in the one before
        const bool critPreview = isPreviewEnable
            && (p == 0 || p == pc - 1u || (p == pc - 2u && fs - partEnd < PARTSIZE / 3));

        // Criterion 3+4: completion, requested blocks counted as downloaded
        uint64 partSize = (partEnd - partStart + 1) - totalGapSizeInPart(p);
        bool critRequested = false;
        for (const auto* reqBlock : m_requestedBlocks) {
            if (reqBlock->startOffset > partEnd || reqBlock->endOffset < partStart)
                continue;
            partSize += std::min(reqBlock->endOffset, partEnd)
                      - std::max(reqBlock->startOffset, partStart) + 1;
            critRequested = true;
        }
        partSize = std::min<uint64>(partSize, PARTSIZE);
        // Against PARTSIZE, so a short last part gets no head start
        const uint16 critCompletion = static_cast<uint16>(
            std::min<uint64>((partSize * 100 + PARTSIZE - 1) / PARTSIZE, 100));

        // Criterion 5: same chunk
        const bool sameChunk = (p == sender->lastPartAsked());

        // Criterion 6+7: transferring clients with this part, time to complete
        uint16 transferringClientsScore = static_cast<uint16>(m_downloadingSources.size());
        uint16 bandwidthScore = 2000;
        if (transferringClientsScore > 1) {
            uint64 totalRate = 1;
            for (const auto* dlClient : m_downloadingSources) {
                if (dlClient->isPartAvailable(p)) {
                    --transferringClientsScore;
                    totalRate += dlClient->downDatarate() + 500;
                }
            }
            bandwidthScore = static_cast<uint16>(
                std::min<uint64>((PARTSIZE - partSize) / (totalRate * 5), 2000));
        }

        uint32 rank;
        if (partSize > 0 && a4afHeavy) {
            // Too many A4AF sources: finishing started chunks comes first
            rank = freq
                 + (critPreview ? 0u : 200u)
                 + static_cast<uint32>(!critRequested)
                 + (100u - critCompletion)
                 + static_cast<uint32>(!sameChunk)
                 + bandwidthScore;
        } else if (freq <= veryRareBound) {
            rank = 75u * freq
                 + static_cast<uint32>(!critRequested)
                 + (critRequested ? 3000u : 3001u)
                 + (100u - critCompletion)
                 + static_cast<uint32>(!sameChunk)
                 + transferringClientsScore;
        } else if (critPreview) {
            rank = ((critRequested && !sameChunk) ? 20000u : 10000u)
                 + (100u - critCompletion);
        } else if (freq <= rareBound) {
            rank = 25u * freq
                 + (critRequested ? 10101u : 10102u)
                 + (100u - critCompletion)
                 + static_cast<uint32>(!sameChunk)
                 + transferringClientsScore;
        } else if (freq <= almostRareBound) {
            // Slightly lessens the weight of frequency; MFC's 1..(almostRare-rare)+1
            std::uniform_int_distribution<uint32> dist(1, 1u + almostRareBound - rareBound);
            rank = freq
                 + (critRequested ? 20101u : (20201u + almostRareBound - rareBound))
                 + (partSize > 0 ? 0u : 500u)
                 + 5u * (100u - critCompletion)
                 + (sameChunk ? 0u : dist(rng))
                 + bandwidthScore;
        } else {
            rank = (critRequested ? 30000u : 30001u)
                 + (100u - critCompletion)
                 + static_cast<uint32>(!sameChunk)
                 + bandwidthScore;
        }
        candidates.push_back({p, rank});
    }

    // Random among equal ranks, as MFC's pick from aBest
    std::ranges::shuffle(candidates, rng);
    std::ranges::stable_sort(candidates, {}, &PartCandidate::rank);

    for (const auto& cand : candidates) {
        if (blocksFound >= count)
            break;
        const int before = blocksFound;
        allocateFromPart(cand.part);
        if (blocksFound > before)
            sender->setLastPartAsked(static_cast<uint16>(cand.part));
    }

    count = blocksFound;
    return blocksFound > 0;
}

bool PartFile::getNextEmptyBlockInPart(uint32 partNumber,
                                        Requested_Block_Struct* reqBlock,
                                        uint64 searchFrom) const
{
    if (!reqBlock)
        return false;

    const uint64 partStart = static_cast<uint64>(partNumber) * PARTSIZE;
    uint64 partEnd = partStart + PARTSIZE - 1;
    const uint64 fs = static_cast<uint64>(fileSize());
    if (partEnd >= fs)
        partEnd = fs - 1;

    // Effective search start: at least partStart, at least searchFrom
    const uint64 effectiveStart = std::max(partStart, searchFrom);
    if (effectiveStart > partEnd)
        return false;

    // Find first gap within this part's byte range, starting from effectiveStart
    for (const auto& gap : m_gapList) {
        if (gap.start > partEnd)
            break;
        if (gap.end < effectiveStart)
            continue;

        // Found a gap that overlaps [effectiveStart, partEnd]
        const uint64 blockStart = std::max(gap.start, effectiveStart);
        uint64 blockEnd = std::min(gap.end, partEnd);

        // Cut at the next part-relative EMBLOCKSIZE boundary, so requests line up
        // with AICH blocks (MFC GetNextEmptyBlockInPart)
        const uint64 blockLimit = partStart + ((blockStart - partStart) / EMBLOCKSIZE + 1) * EMBLOCKSIZE - 1;
        blockEnd = std::min(blockEnd, blockLimit);

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
            // Only remove from list — the block is still alive in the
            // client's Pending_Block_Struct and freed by clearPendingBlockRequest.
            m_requestedBlocks.erase(it);
            return true;
        }
    }
    return false;
}

bool PartFile::removeBlockFromList(const Requested_Block_Struct* block)
{
    const auto it = std::ranges::find(m_requestedBlocks, block);
    if (it == m_requestedBlocks.end())
        return false;
    m_requestedBlocks.erase(it);
    return true;
}

PartFile::EndPhase PartFile::endPhase() const
{
    const uint64 size = static_cast<uint64>(fileSize());
    const uint64 left = totalGapSize();
    if (size == 0 || left == 0)
        return EndPhase::Normal;

    if (left <= ENDGAME_BLOCK_THRESHOLD * uint64{EMBLOCKSIZE} || left <= size / 1000
        || left <= uint64{datarate()} * 30)
        return EndPhase::Endgame;
    return left <= size / 10 ? EndPhase::Late : EndPhase::Normal;
}

bool PartFile::maySecondSourceTake(const UpDownClient* sender, uint64 start, uint64 end,
                                   EndPhase phase) const
{
    const Requested_Block_Struct* held = nullptr;
    for (const auto* block : m_requestedBlocks) {
        if (block->startOffset > end || block->endOffset < start)
            continue;
        if (held || block->holder == sender)
            return false;   // two on it already, or the sender itself
        held = block;
    }
    // Nobody holds it: the range is buffered data waiting for its flush.
    if (!held || !held->holder)
        return false;

    // Reserved but never asked for, by a source that has delivered nothing so far.
    if (!held->requested && held->holder->sessionDown() == 0)
        return true;
    // The holder has gone quiet on it.
    if (getTickCount() - held->lastProgressTick >= kStalledBlockMs)
        return true;
    // In the last stretch a much faster source need not wait for a slow one.
    const uint64 holderRate = held->holder->downDatarate();
    return phase == EndPhase::Endgame && sender->downDatarate() > 0
           && sender->downDatarate() >= holderRate * kSlowSourceFactor;
}

void PartFile::removeAllRequestedBlocks()
{
    // Only clear the list — blocks are owned by the clients'
    // Pending_Block_Struct and freed by clearPendingBlockRequest.
    m_requestedBlocks.clear();
}

uint64 PartFile::reservePartForExternalTransfer(uint32 partNumber,
                                                std::vector<Requested_Block_Struct*>& out)
{
    out.clear();

    if (partNumber >= partCount() || isComplete(partNumber))
        return 0;

    uint64 covered = 0;
    uint64 searchFrom = 0;

    while (true) {
        auto* reqBlock = new Requested_Block_Struct;
        if (!getNextEmptyBlockInPart(partNumber, reqBlock, searchFrom)) {
            delete reqBlock;
            break;
        }
        searchFrom = reqBlock->endOffset + 1;

        // Somebody is already pulling this range over ed2k. Leave it to them —
        // we still fetch the whole part over HTTP, and writeToBuffer discards
        // whatever arrives second, so the overlap is wasteful but harmless.
        if (isAlreadyRequested(reqBlock->startOffset, reqBlock->endOffset)) {
            delete reqBlock;
            continue;
        }

        covered += reqBlock->endOffset - reqBlock->startOffset + 1;
        m_requestedBlocks.push_back(reqBlock);
        out.push_back(reqBlock);
    }

    return covered;
}

void PartFile::releaseReservedBlocks(std::vector<Requested_Block_Struct*>& blocks)
{
    for (auto* block : blocks) {
        if (!block)
            continue;

        // Unlike the client-owned blocks the rest of this list holds, these have
        // no Pending_Block_Struct behind them: unregister *and* free.
        removeBlockFromList(block->startOffset, block->endOffset);
        delete block;
    }

    blocks.clear();
}

// ===========================================================================
// Rehash — recover a .part whose contents no longer match the .part.met
// ===========================================================================

QString PartFile::partDataPath() const
{
    QString path = m_fullName;
    if (path.endsWith(QStringLiteral(".met")))
        path.chop(4);
    return path;
}

void PartFile::applyRehashResult(const QByteArray& partOk)
{
    // Only a part we believed complete can be proven wrong. A half-downloaded part never
    // matches its hash, and its gaps already say what is missing — clearing them would
    // throw the received blocks away (MFC srchybrid/PartFile.cpp:1486-1528).
    const uint32 parts = partCount();
    const auto total = static_cast<uint64>(fileSize());
    uint32 good = 0;

    for (uint32 part = 0; part < parts; ++part) {
        const uint64 start = static_cast<uint64>(part) * PARTSIZE;
        if (start >= total)
            break;
        if (!isComplete(part))
            continue;

        // A part the worker could not read stays trusted: no evidence, no verdict.
        const auto state = part < static_cast<uint32>(partOk.size())
            ? static_cast<PartVerdict>(partOk[static_cast<qsizetype>(part)])
            : PartVerdict::Unread;
        if (state == PartVerdict::Bad) {
            logWarning(QStringLiteral("Rehash found corrupted part %1 in %2")
                           .arg(part).arg(fileName()));
            addGap(start, std::min(start + PARTSIZE, total) - 1);
        } else {
            ++good;
        }
    }

    updateCompletedInfos();

    // Re-latch: Ready the moment one part survived, matching loadPartFile().
    setStatus(m_gapList.empty()   ? PartFileStatus::Completing
              : good > 0          ? PartFileStatus::Ready
                                  : PartFileStatus::Empty);
    m_fileOp = PartFileOp::None;

    logInfo(QStringLiteral("Rehash finished: %1 — %2/%3 parts intact")
                .arg(fileName()).arg(good).arg(parts));

    // Stamps m_tLastModified from the .part we just read, so the next load agrees
    // with it and does not rehash again.
    savePartFile();

    // Not addToSharedFiles(): that only promotes an Empty file, and we have just
    // latched Ready ourselves. MFC does the same at srchybrid/PartFile.cpp:1570.
    if (m_status == PartFileStatus::Ready && theApp.sharedFileList && canBeShared())
        theApp.sharedFileList->safeAddKFile(this);

    // Nothing left to download: deliver it. Every part read and matched means the
    // final check has just been done; an unread one leaves it to completeFile().
    if (m_gapList.empty() && !m_destroying) {
        const bool allRead = !partOk.isEmpty()
            && !partOk.left(static_cast<qsizetype>(parts)).contains(static_cast<char>(PartVerdict::Unread))
            && static_cast<uint32>(partOk.size()) >= parts;
        completeFile(allRead);
    }
}

QByteArray PartFile::verifyPartData(
    const QString& partPath, uint64 fileSize, const QByteArray& fileHash,
    const std::vector<std::array<uint8, 16>>& partHashes,
    const std::function<bool(uint32, uint32)>& keepGoing,
    AICHRecoveryHashSet* aichOut)
{
    // A file below PARTSIZE has no part hashes: its one part is checked against the
    // file hash (MFC srchybrid/PartFile.cpp:1493-1495).
    const bool singlePart = partHashes.empty();
    const auto partCount = static_cast<uint32>(
        std::max<uint64>(1, (fileSize + PARTSIZE - 1) / PARTSIZE));

    // Unread until proven either way: a part we could not read says nothing about the data.
    QByteArray partOk(static_cast<qsizetype>(partCount), static_cast<char>(PartVerdict::Unread));

    QFile file(partPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("Rehash: cannot open %1: %2").arg(partPath, file.errorString()));
        return partOk;
    }

    DiskLoadLimiter diskLoad;
    for (uint32 part = 0; part < partCount; ++part) {
        const uint64 start = static_cast<uint64>(part) * PARTSIZE;
        if (start >= fileSize || (!singlePart && part >= partHashes.size()))
            break;
        const uint64 len = std::min<uint64>(PARTSIZE, fileSize - start);

        if (!file.seek(static_cast<qint64>(start)))
            break;
        QByteArray data;
        {
            const DiskLoadLimiter::Read timed(diskLoad);
            data = file.read(static_cast<qint64>(len));
        }
        if (static_cast<uint64>(data.size()) != len) {
            logWarning(QStringLiteral("Rehash: short read in %1 at part %2").arg(partPath).arg(part));
            break;   // the rest stays unread
        }

        std::array<uint8, 16> actual{};
        AICHHashTree* aichPart = aichOut ? aichOut->m_hashTree.findHash(start, len) : nullptr;
        KnownFile::createHashFromMemory(reinterpret_cast<const uint8*>(data.constData()),
                                        static_cast<uint32>(len), actual.data(), aichPart);

        const bool ok = singlePart
            ? std::memcmp(actual.data(), fileHash.constData(), 16) == 0
            : actual == partHashes[part];
        partOk[static_cast<qsizetype>(part)] =
            static_cast<char>(ok ? PartVerdict::Ok : PartVerdict::Bad);

        if (keepGoing && !keepGoing(part + 1, partCount))
            break;
    }
    return partOk;
}

// ===========================================================================
// Status Machine
// ===========================================================================

void PartFile::setStatus(PartFileStatus s)
{
    // Paused/Insufficient are what status() synthesises from m_paused/m_insufficient;
    // storing them would overwrite the real state and lose the shareability latch.
    // MFC asserts exactly this in _SetStatus (srchybrid/PartFile.cpp:4546-4550); here
    // it is a plain refusal rather than an assert, so the invariant holds the same way
    // in a release build as in a debug one, and can be tested.
    if (s == PartFileStatus::Paused || s == PartFileStatus::Insufficient) {
        logWarning(QStringLiteral("PartFile::setStatus: refusing overlay state %1 for %2")
                       .arg(static_cast<int>(s)).arg(fileName()));
        return;
    }

    if (m_status == s)
        return;
    m_status = s;
    emit m_partNotifier.statusChanged(status());
}

uint32 PartFile::dlActiveTime() const
{
    uint32 active = m_dlActiveTime;
    if (m_activated != 0)
        active += static_cast<uint32>(std::time(nullptr) - m_activated);
    return active;
}

int64 PartFile::timeRemaining() const
{
    const uint64 done = completedSize();
    return estimateTimeRemaining(fileSize() > done ? fileSize() - done : 0, done, datarate(),
                                 dlActiveTime(), thePrefs.useAdvancedCalcRemainingTime());
}

int64 PartFile::estimateTimeRemaining(uint64 left, uint64 done, uint32 rate, uint32 activeSecs,
                                      bool advanced)
{
    const int64 simple = rate ? static_cast<int64>(left / rate) : -1;
    if (!advanced)
        return simple;

    // Average over the whole active time, once there is enough to average
    const int64 estimate = (activeSecs && done >= 512000)
        ? static_cast<int64>(static_cast<double>(left) / (static_cast<double>(done) / activeSecs))
        : -1;
    if (estimate == -1 || (simple > 0 && simple < estimate))
        return simple;
    return estimate < DAY2S(15) ? estimate : -1;
}

void PartFile::setActive(bool active)
{
    const time_t now = std::time(nullptr);
    if (active) {
        if (theApp.isConnected() && m_activated == 0)
            m_activated = now;
    } else if (m_activated != 0) {
        m_dlActiveTime += static_cast<uint32>(now - m_activated);
        m_activated = 0;
    }
}

void PartFile::pauseFile(bool insufficient)
{
    const bool wasPaused = m_paused;

    // Start of the idle hour after which stopPausedFile() lets the sources go.
    if (!m_paused && !m_insufficient)
        m_lastPausePurge = std::time(nullptr);

    // Only a real pause sets m_paused. Running out of disk space is a different
    // condition — the user did not pause anything — and conflating the two makes
    // status() report Paused for it, since m_paused wins the overlay.
    // MFC srchybrid/PartFile.cpp:3351-3355.
    if (!insufficient)
        m_paused = true;
    m_insufficient = insufficient;
    setActive(false);

    // The stored status is left alone — status() now reports Paused/Insufficient on
    // top of it, and the file keeps whatever shareability it had latched.
    emit m_partNotifier.statusChanged(status());

    if (kadFileSearchID()) {
        kad::SearchManager::stopSearch(kadFileSearchID(), true);
        setKadFileSearchID(0);
    }
    m_lastSearchTimeKad = 0;
    m_lastSearchTimeServer = 0;
    if (theApp.downloadQueue)
        theApp.downloadQueue->removeLocalServerRequest(this);

    // Tell uploading sources to stop, or they keep sending into a paused file.
    // MFC srchybrid/PartFile.cpp:3343-3349.
    for (auto* client : std::vector(m_srcList)) {
        if (client->downloadState() == DownloadState::Downloading) {
            client->sendCancelTransfer();
            client->setDownloadState(DownloadState::OnQueue);
        }
    }

    m_datarate = 0;

    // m_stopped is already set when stopFile() routes through here — it logs its own
    // line, so don't announce the intermediate pause.
    if (!wasPaused && !m_stopped) {
        logInfo(insufficient
                    ? QStringLiteral("Download paused (insufficient disk space): %1").arg(fileName())
                    : QStringLiteral("Download paused: %1").arg(fileName()));
    }

    savePartFile();
}

void PartFile::resumeFile()
{
    // m_insufficient too: an out-of-space file has m_paused false (see pauseFile), and
    // would otherwise be unresumable.
    if (!m_paused && !m_stopped && !m_insufficient && !m_completionError)
        return;

    m_paused = false;
    m_stopped = false;
    m_insufficient = false;
    // A failed write: the buffer is still held, so resuming is the retry. (MFC keeps
    // PS_ERROR until a restart.)
    if (m_writeError) {
        m_writeError = false;
        setStatus(m_statusBeforeWriteError);
    }
    setActive(theApp.isConnected());

    // Nothing to re-derive: dropping the flags is what un-pauses it, and the stored
    // status has been carrying the real state all along.
    emit m_partNotifier.statusChanged(status());

    logInfo(QStringLiteral("Download resumed: %1 — status=%2 gaps=%3")
                .arg(fileName())
                .arg(static_cast<int>(status()))
                .arg(m_gapList.size()));

    // If we had a completion error but no gaps, retry completion
    if (m_completionError && m_gapList.empty()) {
        m_completionError = false;
        // A part that could not be read back last time gets its check now; it may
        // turn out corrupt and reopen the file.
        setStatus(PartFileStatus::Ready);
        verifyChangedParts(/*forceICH*/ false, /*noAICH*/ false);
        completeIfVerified();
    }

    savePartFile();
}

void PartFile::stopFile(bool cancel)
{
    // MFC CPartFile::StopFile() starts with PauseFile() so a stop inherits the pause
    // teardown — most importantly stopping the running Kad source search and clearing
    // its ID. Doing it by hand here used to leave the search dangling, so Stop/Start
    // did not recover a file whose search ID had gone stale (Pause/Resume did).
    m_stopped = true;
    pauseFile(false);
    removeAllSources(true);

    logInfo(QStringLiteral("Download %1: %2")
                .arg(cancel ? QStringLiteral("cancelled") : QStringLiteral("stopped"))
                .arg(fileName()));

    // Flush any buffered data
    if (!m_bufferedData.empty())
        flushBuffer();

    m_lastSearchTimeKad = 0;
    m_totalSearchesKad = 0;
    m_lastSearchTimeServer = 0;

    m_datarate = 0;

    if (cancel) {
        // Close file handle before deleting
        if (m_partFileHandle.isOpen())
            m_partFileHandle.close();

        // Delete temp files: .part, .part.met, .part.met.bak
        const QString metPath = m_tmpPath + QDir::separator() + m_partMetFilename;
        QString partPath = metPath;
        if (partPath.endsWith(QStringLiteral(".met")))
            partPath.chop(4);

        QFile::remove(partPath);                              // NNN.part
        QFile::remove(metPath);                               // NNN.part.met
        QFile::remove(metPath + QStringLiteral(".bak"));      // NNN.part.met.bak

        // Must happen before m_partMetFilename is cleared below — the list path cannot be
        // derived without it (MorphXT CPartFile::Delete, PartFile.cpp:4841).
        SourceSaver::removeFile(m_tmpPath, m_partMetFilename);

        // Prevent destructor from saving to deleted files
        m_partMetFilename.clear();
        setStatus(PartFileStatus::Error);
    } else {
        // m_stopped/m_paused are already set by the pauseFile() this routes through;
        // the status itself stays as it was.
        savePartFile();
    }
}

// MFC CPartFile::StopPausedFile (srchybrid/PartFile.cpp:3299-3307): a file idle for an
// hour does not keep its old sources.
void PartFile::stopPausedFile()
{
    if (m_stopped)
        return;
    const PartFileStatus st = status();
    if (st != PartFileStatus::Paused && st != PartFileStatus::Insufficient
        && st != PartFileStatus::Error)
        return;
    const time_t now = std::time(nullptr);
    if (now < m_lastPausePurge + static_cast<time_t>(HR2S(1)))
        return;

    if (st == PartFileStatus::Paused) {
        stopFile();
    } else {
        // Not stopFile(): it would drop the insufficient flag the auto-resume waits
        // on, and an errored file must keep its state for the retry.
        m_lastPausePurge = now;
        removeAllSources(true);
    }
}

// MFC CPartFile::RemoveAllSources (srchybrid/PartFile.cpp:3040-3066).
void PartFile::removeAllSources(bool tryToSwap)
{
    for (auto* client : std::vector(m_srcList)) {
        if (tryToSwap
            && client->swapToAnotherFile(QStringLiteral("Removing source. removeAllSources()"),
                                         true, true, true, nullptr, false, false))
            continue;
        if (theApp.downloadQueue) {
            theApp.downloadQueue->removeSource(client);
        } else {
            removeSource(client);
            client->setDownloadState(DownloadState::None);
            client->setReqFile(nullptr);
        }
    }
    updatePartsInfo();
    unlinkA4AFSources();
    updateFileRatingCommentAvail();
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

    // MFC PartFile.cpp:4417-4430
    const auto srcCount = m_srcList.size();
    uint8 newPriority;
    if (srcCount > 100)
        newPriority = kPrLow;
    else if (srcCount > 20)
        newPriority = kPrNormal;
    else
        newPriority = kPrHigh;

    if (newPriority == m_downPriority)
        return;
    m_downPriority = newPriority;
    if (theApp.downloadQueue)
        theApp.downloadQueue->requestPrioritySort();
}

void PartFile::setSwapForSourceExchangeTick()
{
    m_lastSwapForSourceExchangeTick = getTickCount();
}

bool PartFile::rightFileHasHigherPrio(const PartFile* left, const PartFile* right)
{
    if (!right)
        return false;
    if (!left)
        return true;

    // The *category's* priority, not its index. This used to compare the index
    // itself, which ranked downloads by the arbitrary order the user happened to
    // create their categories in. MFC ranks by Category_Struct::prio — the
    // a4af priority the category dialog edits (srchybrid/PartFile.cpp:5159-5165).
    const auto leftCat = thePrefs.category(static_cast<int>(left->category()));
    const auto rightCat = thePrefs.category(static_cast<int>(right->category()));
    if (leftCat.prio != rightCat.prio)
        return rightCat.prio > leftCat.prio;

    // Higher download priority first; by ordinal, Very Low is 4 as a constant
    if (left->downPriority() != right->downPriority())
        return realPriority(left->downPriority()) < realPriority(right->downPriority());

    // Within one non-default category the user may ask for alphabetical order,
    // which is the point of the setting: a series downloads in episode order
    // instead of whichever part happened to find sources first
    // (srchybrid/PartFile.cpp:5167-5173).
    if (left->category() != 0 && left->category() == right->category()
        && leftCat.downloadInAlphabeticalOrder && thePrefs.showExtControls()
        && !left->fileName().isEmpty()
        && !right->fileName().isEmpty())
    {
        const int cmp = right->fileName().compare(left->fileName(), Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0;
    }

    // Older file first (earlier creation time)
    return left->m_tCreated > right->m_tCreated;
}

// ===========================================================================
// Source Tracking
// ===========================================================================

int PartFile::availableSourceCount() const
{
    // MFC keeps a cached per-state histogram; walking the list is cheap enough here
    // because the only caller is the Save/Load Sources tick, once per file per 10 minutes.
    return static_cast<int>(std::ranges::count_if(m_srcList, [](const UpDownClient* client) {
        const DownloadState state = client->downloadState();
        return state == DownloadState::OnQueue || state == DownloadState::Downloading;
    }));
}

// The per-file half of the Kad source search conditions.
bool PartFile::wantsKadSourceSearch(uint64 curTick) const
{
    if (m_paused || m_stopped || kadFileSearchID() || curTick < m_lastSearchTimeKad)
        return false;
    const PartFileStatus st = status();
    if (st != PartFileStatus::Ready && st != PartFileStatus::Empty)
        return false;
    return static_cast<int>(maxSourcePerFileUDP()) > sourceCount();
}

int PartFile::validSourcesCount() const
{
    return static_cast<int>(std::ranges::count_if(m_srcList, [](const UpDownClient* client) {
        switch (client->downloadState()) {
        case DownloadState::OnQueue:
        case DownloadState::Downloading:
        case DownloadState::Connected:
        case DownloadState::RemoteQueueFull:
            return true;
        default:
            return false;
        }
    }));
}

uint32 PartFile::maxSources() const
{
    // A limit set in advanced mode is not applied outside it: it could be neither
    // seen nor changed there (MFC PartFile.cpp:5342-5347).
    if (!thePrefs.showExtControls() || m_privateMaxSources == 0)
        return thePrefs.maxSourcesPerFile();
    return m_privateMaxSources;
}

uint32 PartFile::maxSourcePerFileSoft() const
{
    return std::min<uint32>((maxSources() * 9) / 10, MAX_SOURCES_FILE_SOFT);
}

uint32 PartFile::maxSourcePerFileUDP() const
{
    return std::min<uint32>((maxSources() * 3) / 4, MAX_SOURCES_FILE_UDP);
}

bool PartFile::isPreviewableFileType() const
{
    const ED2KFileType type = getED2KFileTypeID(fileName());
    return type == ED2KFileType::Video || type == ED2KFileType::Archive
        || fileName().endsWith(QStringLiteral(".iso"), Qt::CaseInsensitive);
}

bool PartFile::isPausingOnPreview() const
{
    if (!m_pauseOnPreview || !isPreviewableFileType())
        return false;
    switch (status()) {   // MFC CanPauseFile
    case PartFileStatus::Paused:
    case PartFileStatus::Error:
    case PartFileStatus::Complete:
    case PartFileStatus::Completing:
        return false;
    default:
        return true;
    }
}

void PartFile::updatePartsInfo()
{
    if (!isPartFile()) {
        KnownFile::updatePartsInfo();
        return;
    }

    const time_t now = std::time(nullptr);
    const bool refresh = completeSourcesDue(now);

    // Rebuilt from scratch: addSource()/removeSource() keep the frequencies up to date
    // incrementally, but a source that reports a *new* part status (every OP_FILESTATUS
    // and every reask) was only ever added, never subtracted, so the counts drifted up.
    m_srcPartFrequency.assign(partCount(), 0);

    std::vector<uint16> peerCounts;
    for (const auto* src : m_srcList) {
        const auto& status = src->partStatus();
        const bool complete = src->completeSource();
        if (!complete && status.empty())
            continue;   // hasn't told us what it holds — MFC PartFile.cpp:2576

        for (uint16 i = 0; i < partCount(); ++i) {
            if (complete || (i < status.size() && status[i] != 0))
                ++m_srcPartFrequency[i];
        }
        if (refresh)
            peerCounts.push_back(src->upCompleteSourcesCount());
    }

    if (refresh) {
        uint16 seen = 0;
        if (partCount() > 0) {
            seen = *std::min_element(m_srcPartFrequency.begin(),
                                     m_srcPartFrequency.end());
        }
        updateCompleteSourceCounts(peerCounts, seen, /*blend*/ true);
    }

    // Every part has a source right now (MFC UpdateAvailablePartsCount)
    if (partCount() > 0
        && std::ranges::none_of(m_srcPartFrequency, [](uint16 n) { return n == 0; }))
        m_lastSeenComplete = now;

    emit notifier()->fileUpdated();
    noteChanged();
}

void PartFile::addSource(UpDownClient* client)
{
    if (!client)
        return;
    if (m_srcIndex.contains(client))
        return;
    m_srcList.push_back(client);
    m_srcIndex.add(client);

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

UpDownClient* PartFile::findSourceLike(const UpDownClient* candidate) const
{
    if (!candidate || m_srcList.empty())
        return nullptr;
    if (m_srcIndex.contains(candidate))
        return const_cast<UpDownClient*>(candidate);

    // The index names who could match; the old rule still decides.
    static thread_local std::vector<UpDownClient*> maybe;
    m_srcIndex.candidates(candidate, maybe);
    for (UpDownClient* cur : maybe) {
        ++s_sourceCompares;
        if (isSamePeer(cur, candidate))
            return cur;
    }
    return nullptr;
}

UpDownClient* PartFile::findSourceLikeByScan(const UpDownClient* candidate) const
{
    for (UpDownClient* cur : m_srcList) {
        if (isSamePeer(cur, candidate))
            return cur;
    }
    return nullptr;
}

bool PartFile::isSamePeer(const UpDownClient* source, const UpDownClient* candidate)
{
    if (source == candidate)
        return true;
    // compare() matches a pre-hello source by userIDHybrid + port. A v6-only source and a
    // dual-stack one carrying the same v6 hint share no IPv4 key, hence the last test.
    return source->compare(candidate, /*ignoreUserHash*/ true)
        || source->compare(candidate, /*ignoreUserHash*/ false)
        || (!source->userIPv6().isNull() && source->userIPv6() == candidate->userIPv6()
            && source->userPort() != 0 && source->userPort() == candidate->userPort());
}

void PartFile::forgetAllSources()
{
    m_srcList.clear();
    m_srcIndex.clear();
}

void PartFile::removeSource(UpDownClient* client)
{
    if (!m_srcIndex.contains(client))
        return;
    m_srcIndex.remove(client);
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

    // A departed peer's opinion stops counting — MFC DownloadQueue.cpp:659-662.
    // Only worth recomputing when it actually had one.
    if (client->fileRating() > 0 || !client->fileComment().isEmpty())
        updateFileRatingCommentAvail();

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

    // Generate unique NNN.part filename — skip existing files to avoid
    // overwriting downloads from previous sessions (counter resets to 0 on restart)
    static std::atomic<uint32> counter{0};
    uint32 num;
    QString partMetFilename;
    QString partPath;
    do {
        num = counter.fetch_add(1);
        partMetFilename = QStringLiteral("%1.part.met").arg(num, 3, 10, QChar(u'0'));
        partPath = tempDir + QDir::separator()
                   + QStringLiteral("%1.part").arg(num, 3, 10, QChar(u'0'));
    } while (QFile::exists(tempDir + QDir::separator() + partMetFilename)
             || QFile::exists(partPath));

    m_partMetFilename = partMetFilename;
    m_fullName = tempDir + QDir::separator() + m_partMetFilename;
    m_partFileHandle.setFileName(partPath);
    if (!m_partFileHandle.open(QIODevice::ReadWrite)) {
        logError(QStringLiteral("PartFile::createPartFile: failed to create %1").arg(partPath));
        return false;
    }

    // Resize file to target size. A hole elsewhere; on Windows a plain file would
    // take all of it on disk here, so it grows with the writes instead, as MFC.
    const uint64 fs = static_cast<uint64>(fileSize());
#ifdef Q_OS_WIN
    // "Create new part files as sparse" (MFC CreatePartFile, PartFile.cpp:410). Before
    // the resize: NTFS claims every cluster with it otherwise.
    const bool sparse = thePrefs.sparsePartFiles() && markFileSparse(m_partFileHandle);
    if (thePrefs.sparsePartFiles() && !sparse)
        logDebug(QStringLiteral("Could not make %1 a sparse file").arg(partPath));   // FAT etc.
    const bool sizeNow = sparse || thePrefs.allocFullFile();
#else
    const bool sparse = false;   // the option is Windows only
    const bool sizeNow = true;
#endif
    if (fs > 0 && sizeNow)
        m_partFileHandle.resize(static_cast<qint64>(fs));

    // "Allocate full file size": claim the blocks now (MFC does it with the first
    // flush, PartFile.cpp:4063-4078; the file has its size from here on). Not below
    // the free-space floor, and never fatal.
    if (fs > 0 && thePrefs.allocFullFile() && !sparse) {
        const std::optional<uint64> free = tryFreeDiskSpace(tempDir);
        const uint64 floor = thePrefs.checkDiskspace() ? thePrefs.minFreeDiskSpace() : 0;
        if (free.has_value() && *free < fs + floor)
            logWarning(QStringLiteral("Not enough free space to allocate %1 in full").arg(fileName()));
        else if (!preallocateFile(m_partFileHandle, fs))
            logWarning(QStringLiteral("Could not allocate %1 in full").arg(fileName()));
    }

    // Init gap covering entire file
    m_gapList.clear();
    if (fs > 0)
        m_gapList.push_back({0, fs - 1});

    // Init part frequency
    m_srcPartFrequency.resize(partCount(), 0);

    m_tCreated = std::time(nullptr);
    m_status = PartFileStatus::Empty;
    setActive(theApp.isConnected());   // MFC CreatePartFile

    // "Auto cleanup file names of new downloads" — MFC CreatePartFile,
    // srchybrid/PartFile.cpp:449. Here, so every intake route gets it.
    if (thePrefs.autoCleanupFilenames()) {
        const QString cleaned = cleanupFilename(fileName(), thePrefs.filenameCleanups());
        if (!cleaned.isEmpty())
            setFileName(cleaned);
    }

    // Save initial .part.met
    savePartFile();

    // MFC CreatePartFile: clear hashset flags for files that don't need them
    if (fileIdentifier().getTheoreticalMD4PartHashCount() == 0)
        m_md4HashsetNeeded = false;
    if (fileIdentifier().getTheoreticalAICHPartHashCount() == 0)
        m_aichPartHashsetNeeded = false;

    return true;
}

// ===========================================================================
// Persistence — LoadPartFile
// ===========================================================================

PartFileLoadResult PartFile::loadPartFile(const QString& directory,
                                           const QString& filename,
                                           PartFileFormat* checkFormat)
{
    m_tmpPath = directory;
    m_partMetFilename = filename;
    m_fullName = directory + QDir::separator() + filename;

    const QString metPath = m_fullName;
    SafeFile file(metPath, QIODevice::ReadOnly);

    // The eDonkey layouts an import can meet (MFC PartFile.cpp:739-771): "new style"
    // files have no priorities worth keeping, carry their hashes at the end, and
    // their .part date says nothing.
    bool newStyle = false;
    PartFileFormat format = PartFileFormat::DefaultOld;

    // The destructor saves the .part.met it knows. A file that was only identified,
    // or that did not load, must not be written back from this object.
    bool loaded = false;
    const auto forgetMet = qScopeGuard([this, &loaded] {
        if (!loaded) {
            m_partMetFilename.clear();
            m_fullName.clear();
        }
    });

    try {
        // Read version byte
        const uint8 version = file.readUInt8();
        if (version != PARTFILE_VERSION &&
            version != PARTFILE_VERSION_LARGEFILE &&
            version != PARTFILE_SPLITTEDVERSION)
        {
            if (version == 'S') {   // "SDL…": a Shareaza download
                file.close();       // it is rewritten in place; Windows refuses while open
                const PartFileLoadResult result =
                    importShareazaTempFile(directory, filename, checkFormat);
                loaded = result == PartFileLoadResult::LoadSuccess;
                return result;
            }
            logWarning(QStringLiteral("PartFile::loadPartFile: unknown version 0x%1 in %2")
                           .arg(version, 2, 16, QChar(u'0'))
                           .arg(metPath));
            return PartFileLoadResult::FailedCorrupt;
        }

        newStyle = version == PARTFILE_SPLITTEDVERSION;
        if (newStyle) {
            format = PartFileFormat::Splitted;
        } else if (file.length() >= 28) {
            file.seek(24, 0);
            if (file.readUInt32() == 0x01020000u) {   // eDonkey's "old part style"
                newStyle = true;
                format = PartFileFormat::NewOld;
            }
            file.seek(1, 0);
        }

        const auto readHashSet = [this, &file] {
            uint8 hash[16];
            file.readHash16(hash);
            setFileHash(hash);

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
        };

        if (!newStyle) {
            // Timestamp, MD4 hash, part hashes
            m_tLastModified = static_cast<time_t>(file.readUInt32());
            readHashSet();
        } else if (file.readUInt32() == 0) {
            readHashSet();                            // 0.48 part.met: different again
        } else {
            file.seek(2, 0);
            m_tLastModified = static_cast<time_t>(file.readUInt32());
            uint8 hash[16];
            file.readHash16(hash);
            setFileHash(hash);
            fileIdentifier().getRawMD4HashSet().clear();
        }

        // Read tag count and iterate
        const uint32 tagCount = readTagCount(file, kMaxFileTags);
        m_gapList.clear();

        // Temporary gap storage (pairs of start/end)
        std::vector<std::pair<uint64, uint64>> gapPairs;
        uint64 pendingGapStart = UINT64_MAX;
        struct NamedGap { uint64 first = UINT64_MAX; uint64 second = UINT64_MAX; };
        std::map<uint32, NamedGap> namedGaps;   // by index; second = exclusive end

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
            case FT_LASTSEENCOMPLETE:
                if (tag.isInt())
                    m_lastSeenComplete = static_cast<time_t>(tag.intValue());
                break;
            case FT_MAXSOURCES:
                if (tag.isInt())
                    m_privateMaxSources = static_cast<uint32>(tag.intValue());
                break;
            case FT_DL_PREVIEW:
                if (tag.isInt()) {
                    m_previewPrio = (tag.intValue() & 0x01) != 0;
                    m_pauseOnPreview = (tag.intValue() & 0x02) != 0;
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
            case FT_ULPRIORITY:
                if (tag.isInt() && !newStyle)
                    setUpPriorityFromTag(tag.intValue());
                break;
            case FT_DLPRIORITY:
                if (tag.isInt() && !newStyle) {
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
            case FT_KADNOTECACHE:
                // Consume here so it isn't re-added to the extra-tags list.
                if (tag.isBlob())
                    deserializeKadNotes(tag.blobValue());
                break;
            default: {
                // Gap tags. eMule names them with a string: the FT_GAPSTART/FT_GAPEND
                // byte, then the gap's index in decimal; the end is the first byte
                // *after* the gap (MFC PartFile.cpp:960-979). Older builds of this
                // port wrote bare numeric IDs with an inclusive end instead.
                const auto gapValue = [&tag]() -> uint64 {
                    if (tag.isInt())
                        return tag.intValue();
                    return tag.isInt64(false) ? tag.int64Value() : 0;
                };
                const uint8 nameByte = tag.name().isEmpty()
                    ? uint8{0} : static_cast<uint8>(tag.name().at(0));
                if (tag.nameId() == 0
                    && (nameByte == FT_GAPSTART || nameByte == FT_GAPEND)) {
                    if (tag.isInt() || tag.isInt64(false)) {
                        auto& gap = namedGaps[tag.name().mid(1).toUInt()];
                        if (nameByte == FT_GAPSTART)
                            gap.first = gapValue();
                        else
                            gap.second = gapValue();   // exclusive
                    }
                } else if (tag.nameId() == FT_GAPSTART) {
                    pendingGapStart = gapValue();
                } else if (tag.nameId() == FT_GAPEND) {
                    if (pendingGapStart != UINT64_MAX) {
                        gapPairs.push_back({pendingGapStart, gapValue()});
                        pendingGapStart = UINT64_MAX;
                    }
                } else {
                    addTagUnique(std::move(tag));
                }
                break;
            }
            }
        }

        // eMule re-saving one of our old files keeps the numeric tags as unknown ones
        // next to its own fresh gaps, so named gaps always win.
        if (!namedGaps.empty()) {
            gapPairs.clear();
            for (const auto& [index, gap] : namedGaps) {
                if (gap.first != UINT64_MAX && gap.second != UINT64_MAX && gap.second > 0)
                    gapPairs.push_back({gap.first, gap.second - 1});
            }
        }

        // Replace auto-generated gap with actual gaps from file, clamped to the file and
        // merged: overlapping entries would count the same missing bytes twice.
        m_gapList.clear();
        const uint64 fs = static_cast<uint64>(fileSize());
        std::ranges::sort(gapPairs);
        for (auto [gStart, gEnd] : gapPairs) {
            if (fs == 0 || gStart >= fs)
                continue;
            if (gEnd >= fs)
                gEnd = fs - 1;
            if (gStart > gEnd)
                continue;
            if (!m_gapList.empty() && gStart <= m_gapList.back().end + 1)
                m_gapList.back().end = std::max(m_gapList.back().end, gEnd);
            else
                m_gapList.push_back({gStart, gEnd});
        }

        // The hybrid style keeps its part hashes after the tags (MFC :994-1006)
        if (newStyle && !checkFormat && file.position() < file.length()) {
            file.readUInt8();
            auto& md4HashSet = fileIdentifier().getRawMD4HashSet();
            for (uint32 i = 0; i < partCount() && file.position() + 16 < file.length(); ++i) {
                std::array<uint8, 16> partHash{};
                file.readHash16(partHash.data());
                md4HashSet.push_back(partHash);
            }
            fileIdentifier().calculateMD4HashByHashSet(true, true);
        }

    } catch (const FileException& ex) {
        logError(QStringLiteral("PartFile::loadPartFile: error reading %1: %2")
                     .arg(metPath, QString::fromStdString(ex.what())));
        return PartFileLoadResult::FailedCorrupt;
    }

    if (static_cast<uint64>(fileSize()) > MAX_EMULE_FILE_SIZE) {
        logError(QStringLiteral("PartFile::loadPartFile: %1: file size exceeds the supported limit")
                     .arg(metPath));
        return PartFileLoadResult::FailedOther;
    }

    if (checkFormat) {
        *checkFormat = format;
        return PartFileLoadResult::CheckSuccess;
    }

    // Open .part file and verify size
    QString partPath = metPath;
    if (partPath.endsWith(QStringLiteral(".met")))
        partPath.chop(4);

    if (!QFile::exists(partPath)) {
        logError(QStringLiteral("PartFile::loadPartFile: .part file missing: %1").arg(partPath));
        return PartFileLoadResult::FailedNoAccess;
    }

    // MFC safety: if .part file is shorter than expected, add gap for missing tail.
    // Normal for a file that grows with its writes (Windows), so only data lost is news.
    {
        const uint64 partFileLen = static_cast<uint64>(QFileInfo(partPath).size());
        const uint64 fs = static_cast<uint64>(fileSize());
        if (fs > 0 && partFileLen < fs && !isPureGap(partFileLen, fs - 1)) {
            logWarning(QStringLiteral("PartFile::loadPartFile: .part file truncated (%1 < %2), adding gap for tail")
                           .arg(partFileLen).arg(fs));
            addGap(partFileLen, fs - 1);
        }
    }

    // Init part frequency array
    m_srcPartFrequency.resize(partCount(), 0);

    // Update completed infos
    updateCompletedInfos();

    // MFC LoadPartFile: final hashset-needed check based on actual hash counts
    m_md4HashsetNeeded = !fileIdentifier().hasExpectedMD4HashCount();

    // Parts finished in an earlier session without a hashset were never checked
    if (m_md4HashsetNeeded) {
        m_partsAwaitingHashset.assign(partCount(), false);
        for (uint32 p = 0; p < partCount(); ++p)
            m_partsAwaitingHashset[p] = isComplete(p);
    }

    // Set status. MFC's latch (srchybrid/PartFile.cpp:1093-1105): start Empty, and
    // promote to Ready as soon as one part verifies complete — that, not "is it
    // running", is what Ready means. Pause is not stored; status() overlays it.
    if (m_gapList.empty()) {
        m_status = PartFileStatus::Completing;
    } else {
        m_status = PartFileStatus::Empty;
        if (!m_md4HashsetNeeded) {
            for (uint32 p = 0; p < partCount(); ++p) {
                if (isComplete(p)) {
                    m_status = PartFileStatus::Ready;
                    break;
                }
            }
        }
    }

    // "Added On" is when the .part came to be, not when this run began
    // (MFC srchybrid/PartFile.cpp LoadPartFile: m_tCreated = st_ctime).
    if (const QDateTime born = QFileInfo(partPath).birthTime(); born.isValid())
        m_tCreated = static_cast<time_t>(born.toSecsSinceEpoch());

    // Did the .part change behind our back — an unclean shutdown, or something else
    // writing to it? Then the gap list we just loaded describes bytes that may no
    // longer be there, and the only honest answer is to re-verify every part.
    // MFC srchybrid/PartFile.cpp:1129-1145.
    if (m_status != PartFileStatus::Completing && !m_md4HashsetNeeded && !newStyle) {
        const QFileInfo partInfo(partPath);
        const qint64 onDisk = partInfo.lastModified().toSecsSinceEpoch();
        if (onDisk > 0
            && !sameFileDate(m_tLastModified, static_cast<time_t>(onDisk),
                             isLocalTimeVolume(partInfo.absolutePath()))) {
            logWarning(QStringLiteral("Part file changed since last run, rehashing: %1")
                           .arg(fileName()));
            m_status = PartFileStatus::WaitingForHash;
            if (theApp.sharedFileList && theApp.sharedFileList->enqueuePartFileRehash(this)) {
                m_fileOp = PartFileOp::Hashing;
                m_status = PartFileStatus::Hashing;
            } else {
                // Nothing can move it on from WaitingForHash, so say so rather than
                // leaving a download wedged in a state nobody will clear.
                m_status = PartFileStatus::Error;
            }
        }
    }

    loaded = true;
    return PartFileLoadResult::LoadSuccess;
}

// ===========================================================================
// Persistence — SavePartFile
// ===========================================================================

bool PartFile::savePartFile()
{
    if (m_fullName.isEmpty())
        return false;

    // Writing the .met mid-rehash would persist a gap list the worker is about to
    // replace (MFC srchybrid/PartFile.cpp:1162).
    if (m_status == PartFileStatus::WaitingForHash || m_status == PartFileStatus::Hashing)
        return false;

    // Stamp the date of the .part we are describing. This is what loadPartFile()
    // compares against to notice the data was changed behind our back; without it the
    // field written below is meaningless (MFC srchybrid/PartFile.cpp:1174-1180).
    {
        // A file open for writing has no final date yet: NTFS settles the last-write
        // time when the handle closes, so a stamp taken before that differs from what
        // the next start finds, and every such file was rehashed in full. Whoever
        // needs the handle next reopens it.
        if (m_partFileHandle.isOpen())
            m_partFileHandle.close();
        const QFileInfo partInfo(partDataPath());
        if (partInfo.exists()) {
            const qint64 mtime = partInfo.lastModified().toSecsSinceEpoch();
            m_tLastModified = mtime > 0 ? static_cast<time_t>(mtime) : static_cast<time_t>(-1);
        }
    }

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

        // Tag count — written as placeholder, patched at the end after all tags
        // (including buffered-data-as-gaps which we don't know the count of upfront)
        uint32 tagCount = 0;
        const auto tagCountPos = file.position();
        file.writeUInt32(0); // placeholder

        // -- Write tags --

        // Filename
        Tag(FT_FILENAME, fileName()).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        tagCount++;

        // File size
        if (largeFile) {
            Tag(FT_FILESIZE, static_cast<uint32>(static_cast<uint64>(fileSize()) & 0xFFFFFFFFu))
                .writeNewEd2kTag(file);
            tagCount++;
            Tag(FT_FILESIZE_HI, static_cast<uint32>(static_cast<uint64>(fileSize()) >> 32))
                .writeNewEd2kTag(file);
            tagCount++;
        } else {
            Tag(FT_FILESIZE, static_cast<uint32>(fileSize()))
                .writeNewEd2kTag(file);
            tagCount++;
        }

        // Transferred
        if (m_transferred > UINT32_MAX) {
            Tag(FT_TRANSFERRED, static_cast<uint64>(m_transferred))
                .writeNewEd2kTag(file);
        } else {
            Tag(FT_TRANSFERRED, static_cast<uint32>(m_transferred))
                .writeNewEd2kTag(file);
        }
        tagCount++;

        // Priority
        Tag(FT_DLPRIORITY,
            static_cast<uint32>(m_autoDownPriority ? kPrAuto : m_downPriority))
            .writeNewEd2kTag(file);
        tagCount++;

        // Upload priority (MFC PartFile.cpp:1250)
        Tag(FT_ULPRIORITY, upPriorityTagValue()).writeNewEd2kTag(file);
        tagCount++;

        // Status (paused)
        Tag(FT_STATUS, static_cast<uint32>(m_paused ? 1u : 0u))
            .writeNewEd2kTag(file);
        tagCount++;

        // Category
        Tag(FT_CATEGORY, m_category).writeNewEd2kTag(file);
        tagCount++;

        // Corruption loss
        if (m_corruptionLoss > 0) {
            Tag(FT_CORRUPTED, static_cast<uint32>(m_corruptionLoss))
                .writeNewEd2kTag(file);
            tagCount++;
        }

        // Compression gain
        if (m_compressionGain > 0) {
            Tag(FT_COMPRESSION, static_cast<uint32>(m_compressionGain))
                .writeNewEd2kTag(file);
            tagCount++;
        }

        // Active download time
        if (const uint32 activeTime = dlActiveTime(); activeTime > 0) {
            Tag(FT_DL_ACTIVE_TIME, activeTime).writeNewEd2kTag(file);
            tagCount++;
        }

        if (m_lastSeenComplete > 0) {
            Tag(FT_LASTSEENCOMPLETE, static_cast<uint32>(m_lastSeenComplete)).writeNewEd2kTag(file);
            tagCount++;
        }

        if (m_privateMaxSources > 0) {
            Tag(FT_MAXSOURCES, m_privateMaxSources).writeNewEd2kTag(file);
            tagCount++;
        }

        // Bit 0 preview priority, bit 1 pause on preview (MFC PartFile.cpp:1284-1289)
        if (m_previewPrio || m_pauseOnPreview) {
            Tag(FT_DL_PREVIEW, static_cast<uint32>((m_pauseOnPreview ? 2 : 0) | (m_previewPrio ? 1 : 0)))
                .writeNewEd2kTag(file);
            tagCount++;
        }

        // Corrupted parts list
        if (!m_corruptedParts.empty()) {
            QString partList;
            for (size_t i = 0; i < m_corruptedParts.size(); ++i) {
                if (i > 0) partList += u',';
                partList += QString::number(m_corruptedParts[i]);
            }
            Tag(FT_CORRUPTEDPARTS, partList).writeNewEd2kTag(file, UTF8Mode::Raw);
            tagCount++;
        }

        // AICH hash (base32 encoded)
        if (fileIdentifier().hasAICHHash()) {
            Tag(FT_AICH_HASH, fileIdentifier().getAICHHash().getString())
                .writeNewEd2kTag(file, UTF8Mode::Raw);
            tagCount++;
        }

        // AICH part hashset (binary blob)
        if (fileIdentifier().hasExpectedAICHHashCount()) {
            SafeMemFile aichFile;
            fileIdentifier().writeAICHHashsetToFile(aichFile);
            Tag(FT_AICHHASHSET, aichFile.buffer()).writeNewEd2kTag(file);
            tagCount++;
        }

        // Cached Kad notes (filenames/comments) — eMuleQt private blob tag
        if (!kadNotes().empty()) {
            Tag(FT_KADNOTECACHE, serializeKadNotes()).writeNewEd2kTag(file);
            tagCount++;
        }

        // Gaps, the way eMule and aMule read them: a string name made of the
        // FT_GAPSTART/FT_GAPEND byte and the gap's index, and an end that is the first
        // byte after the gap (MFC PartFile.cpp:1371-1391).
        uint32 gapIndex = 0;
        const auto writeGap = [&](uint64 start, uint64 end) {
            const QByteArray index = QByteArray::number(gapIndex++);
            const QByteArray startName = QByteArray(1, static_cast<char>(FT_GAPSTART)) + index;
            const QByteArray endName = QByteArray(1, static_cast<char>(FT_GAPEND)) + index;
            if (largeFile) {
                Tag(startName, start).writeTagToFile(file);
                Tag(endName, end + 1).writeTagToFile(file);
            } else {
                Tag(startName, static_cast<uint32>(start)).writeTagToFile(file);
                Tag(endName, static_cast<uint32>(end + 1)).writeTagToFile(file);
            }
            tagCount += 2;
        };
        for (const auto& gap : m_gapList)
            writeGap(gap.start, gap.end);

        // Write buffered (not-yet-flushed) data ranges as gaps too.
        // This data exists only in RAM — if we crash before flushing,
        // the reload must re-request these ranges. (MFC PartFile.cpp:1392-1433)
        // Data handed to the disk worker is no further along: same treatment.
        for (const auto* pending : {&m_bufferedData, &m_flushingData}) {
            auto it = pending->begin();
            while (it != pending->end()) {
                uint64 bStart = it->start;
                uint64 bEnd   = it->end;
                ++it;
                // Merge contiguous entries
                while (it != pending->end() && it->start == bEnd + 1) {
                    bEnd = it->end;
                    ++it;
                }
                writeGap(bStart, bEnd);
            }
        }

        // Patch the tag count at the recorded position
        const auto endPos = file.position();
        file.seek(tagCountPos, SEEK_SET);
        file.writeUInt32(tagCount);
        file.seek(endPos, SEEK_SET);

        // A full disk only shows up here; a short .part.met must never replace a good one.
        commitAndReplace(file, tempPath, m_fullName, theApp.commitFilesNow());

    } catch (const FileException& ex) {
        logError(QStringLiteral("PartFile::savePartFile: error writing %1: %2")
                     .arg(tempPath, QString::fromStdString(ex.what())));
        QFile::remove(tempPath);
        return false;
    }

    return true;
}

// ===========================================================================
// completeFile (private) — initiates async file move
// ===========================================================================

void PartFile::completeFile(bool alreadyVerified)
{
    // The flush below can land here again, and so can a late caller.
    if (m_completionRunning)
        return;
    m_completionRunning = true;

    if (theApp.downloadQueue)
        theApp.downloadQueue->removeLocalServerRequest(this);

    setStatus(PartFileStatus::Completing);

    // MFC PartFile.cpp:2975
    m_changedParts.clear();
    m_partsAwaitingHashset.clear();

    // A completed file needs no more sources. MFC PartFile.cpp:4334-4335.
    if (kadFileSearchID()) {
        kad::SearchManager::stopSearch(kadFileSearchID(), false);
        setKadFileSearchID(0);
    }

    // Flush any remaining buffer, and wait out a write still with the worker
    flushBuffer();

    // Close the part file
    if (m_partFileHandle.isOpen())
        m_partFileHandle.close();

    // Derive source path (.part file)
    QString partPath = m_fullName;
    if (partPath.endsWith(QStringLiteral(".met")))
        partPath.chop(4);

    // The category picks the folder; category 0 and a category with no folder
    // of its own both resolve to the global incoming dir. MFC decides this at
    // the same moment and the same way (srchybrid/PartFile.cpp:2840-2843) —
    // deliberately *now*, not when the download was created, so a category
    // whose folder was set after the download started still gets its file.
    const QString incomingDir = thePrefs.incomingDirForCategory(static_cast<int>(m_category));
    const QString destPath = incomingDir + QDir::separator() + fileName();

    // Re-read the whole file against its hashes before it is delivered and shared under
    // them — MFC CompleteFile(false), srchybrid/PartFile.cpp:2685-2694. Without a hashset
    // there is nothing to check against.
    const bool canVerify = fileIdentifier().hasExpectedMD4HashCount();
    if (!alreadyVerified && !canVerify)
        logWarning(QStringLiteral("Completing '%1' without a final check: no part hashes")
                       .arg(fileName()));

    // Verify and move, both off the main thread
    performFileMove(partPath, destPath, !alreadyVerified && canVerify);
}

void PartFile::finishLoadedDownload()
{
    if (m_status == PartFileStatus::Completing && !m_completionRunning)
        completeIfVerified();
}

// ===========================================================================
// Process — periodic tick
// ===========================================================================

uint32 PartFile::process(uint32 reduceDownload, uint32 counter)
{
    if (m_paused || m_stopped)
        return 0;

    const uint64 curTick = getTickCount();

    // Flush buffer if size or time threshold exceeded
    const uint32 bufferSize = thePrefs.fileBufferSize();
    const uint32 bufferTimeLimit = thePrefs.fileBufferTimeLimit();

    if (m_totalBufferData > 0) {
        const bool sizeExceeded = m_totalBufferData >= bufferSize;
        const bool timeExceeded = (m_lastBufferFlushTime > 0) &&
                                   ((curTick - m_lastBufferFlushTime) > bufferTimeLimit * 1000);

        if (sizeExceeded || timeExceeded || m_lastBufferFlushTime == 0) {
            flushBufferAsync();
            m_lastBufferFlushTime = curTick;
        }
    }

    // Calculate datarate and apply per-client proportional download limits
    // (MFC PartFile.cpp lines 2201-2229).
    m_datarate = 0;
    const auto downloadingCopy = m_downloadingSources; // copy: checkDownloadTimeout may modify vector
    for (auto* client : downloadingCopy) {
        if (client->downloadState() == DownloadState::Downloading && client->socket()) {
            client->checkDownloadTimeout();
            const uint32 curDatarate = client->calculateDownloadRate();
            m_datarate += curDatarate;

            if (auto* sock = client->socket()) { // re-check: checkDownloadTimeout may disconnect
                if (reduceDownload > 0) {
                    // Proportional limit with graduated floors (MFC lines 2211-2225)
                    uint32 limit = reduceDownload * curDatarate / 1000;
                    if (limit < 1000 && reduceDownload == 200)
                        limit += 1000;
                    else if (limit < 200 && curDatarate == 0 && reduceDownload >= 100)
                        limit = 200;
                    else if (limit < 60 && curDatarate < 600 && reduceDownload >= 97)
                        limit = 60;
                    else if (limit < 20 && curDatarate < 200 && reduceDownload >= 93)
                        limit = 20;
                    else if (limit < 1)
                        limit = 1;
                    sock->setDownloadLimit(limit);
                } else {
                    sock->disableDownloadLimit();
                }
            }
        }
    }

    // Retry connections to idle sources — MFC PartFile.cpp Process() source loop.
    // The per-cycle cap has no MFC counterpart; it stays as a cheap backstop behind the
    // two time gates below.
    // Index-based loop because NNP purge can call removeSource() which invalidates iterators.
    int connectAttempts = 0;
    static constexpr int kMaxConnectAttemptsPerCycle = 3;
    for (size_t i = 0; i < m_srcList.size(); ) {
        auto* client = m_srcList[i];
        const auto ds = client->downloadState();

        // A source whose queue is full is the first to go near the cap — MFC
        // PartFile.cpp:2330-2336.
        if (ds == DownloadState::OnQueue && client->remoteQueueFull()
            && curTick >= m_lastPurgeTime + MIN2MS(1)
            && sourceCount() >= static_cast<int>(maxSources()) * 4 / 5)
        {
            m_lastPurgeTime = curTick;
            if (theApp.downloadQueue) {
                theApp.downloadQueue->removeSource(client);
                continue; // client removed, don't increment i
            }
        }

        if (connectAttempts < kMaxConnectAttemptsPerCycle) {
            // OnQueue with or without a socket: a held connection gets the in-place
            // re-ask below, else the remote purges us after an hour. MFC falls through
            // from DS_ONQUEUE (srchybrid/PartFile.cpp:2327-2356).
            const bool onQueue = (ds == DownloadState::OnQueue);

            if ((ds == DownloadState::None || ds == DownloadState::TooManyConns
                 || onQueue)
                && client->connectingState() == ConnectingState::None)
            {
                // MFC's re-ask window applies to every candidate, not just a source
                // that lost its socket: nothing is asked again before FILEREASKTIME
                // (~29 min, doubled for NNP) has passed. srchybrid/PartFile.cpp:2345.
                //
                // Same line gates on theApp.IsConnected(): offline, no source is asked or
                // dialled — else sources restored by SourceSaver download with no network up.
                if (!theApp.isConnected() || client->timeUntilReask(this) > 0) {
                    ++i;
                    continue;
                }

                // Still connected and past the window: MFC re-asks in place rather than
                // dialling again (srchybrid/PartFile.cpp:2347-2351). Reachable for a
                // non-mule peer only since checkHandshakeFinished() stopped demanding an
                // eMule info packet.
                if (client->socket() && client->socket()->isConnected()
                    && client->checkHandshakeFinished()
                    && client->uploadState() != UploadState::Banned)
                {
                    client->setDownloadState(DownloadState::Connected);
                    client->setLastTriedToConnectNow();
                    client->sendFileRequest();
                    ++i;
                    continue;
                }

                // Otherwise dial, but no more than once every 20 minutes per source —
                // MFC's second gate (srchybrid/PartFile.cpp:2352).
                if (curTick < client->lastTriedToConnect() + MIN2MS(20)) {
                    ++i;
                    continue;
                }

                // Reset OnQueue → None: askForDownload() returns early on OnQueue (the
                // peer is expected to re-ask us), which is not what a source due for
                // a dial needs.
                if (onQueue)
                    client->setDownloadState(DownloadState::None);

                // askForDownload(), not tryToConnect(): MFC calls it from exactly here
                // (srchybrid/PartFile.cpp:2354) and it owns the whole re-ask preamble —
                // charging an unanswered UDP re-ask, the TooManyConns backoff, the LowID
                // delays that keep a source instead of burning a callback on it, and the
                // A4AF swap. It sets Connecting and calls tryToConnect() itself. Calling
                // tryToConnect() directly left all of that unreachable — askForDownload()
                // had no production caller at all.
                if (client->askForDownload())
                    ++connectAttempts;

                // askForDownload() → swapToAnotherFile() → doSwap() calls
                // m_reqFile->removeSource(this), so this source may have just left
                // m_srcList and another one slid into slot i. Re-examine that slot
                // instead of stepping over it.
                if (i < m_srcList.size() && m_srcList[i] != client)
                    continue;
            }
        }

        // LowID<->LowID source handling — MFC srchybrid/PartFile.cpp:2294-2306.
        //
        // Deliberately not removed on sight: these sources pop in and out and cost more
        // churn than they save. Kept until either side's ID changes, and only thinned out
        // when we are near the source cap.
        if (ds == DownloadState::LowToLowIP) {
            if (client->hasLowID() && !theApp.canDoCallback(client)) {
                if (curTick >= m_lastPurgeTime + SEC2MS(30)
                    && sourceCount() >= static_cast<int>(maxSources()) * 4 / 5)
                {
                    m_lastPurgeTime = curTick;
                    if (theApp.downloadQueue)
                        theApp.downloadQueue->removeSource(client);
                    continue; // client removed, don't increment i
                }
            } else {
                // Our ID or theirs changed — the source is reachable again.
                client->setDownloadState(DownloadState::OnQueue);
            }
        }

        // NNP source handling — MFC PartFile.cpp:2309-2326
        if (ds == DownloadState::NoNeededParts) {
            // Purge NNP sources when at 80% capacity (40s interval)
            if (curTick >= m_lastPurgeTime + SEC2MS(40)) {
                m_lastPurgeTime = curTick;
                if (sourceCount() >= static_cast<int>(maxSources()) * 4 / 5) {
                    if (theApp.downloadQueue)
                        theApp.downloadQueue->removeSource(client);
                    continue; // client removed, don't increment i
                }
            }
            // Re-ask when doubled reask time (58 min) elapses
            if (client->timeUntilReask(this) == 0) {
                client->swapToAnotherFile(
                    QStringLiteral("A4AF for NNP file. PartFile::process()"),
                    true, false, false, nullptr, true, true);
                client->setDownloadState(DownloadState::OnQueue);
                // A successful swap removed the client from m_srcList — same slot-shift
                // hazard as the connect branch above.
                if (i < m_srcList.size() && m_srcList[i] != client)
                    continue;
            }
        }

        ++i;
    }

    // -- Save/Load Sources (MorphXT CPartFile::Process, PartFile.cpp:3564-3567) --
    // MorphXT runs this every third of its own ticks; ours arrive at 10 Hz, so once a second
    // is the equivalent cadence. The real gating is SourceSaver's own 10-minute resave timer.
    if (counter == 3 && thePrefs.useSaveLoadSources())
        m_sourceSaver.process(this);

    // -- Server TCP source request (MFC PartFile.cpp:2383-2389) --
    // Only queued here. DownloadQueue sends up to 15 per TCP frame and then waits,
    // so a long download list cannot burn through the server's request credits.
    if (!m_localSrcReqQueued && theApp.downloadQueue
        && (m_lastSearchTimeServer == 0 || curTick >= m_lastSearchTimeServer + SERVERREASKTIME)
        && theApp.serverConnect && theApp.serverConnect->isConnected()
        && static_cast<int>(maxSourcePerFileSoft()) > sourceCount()
        && !m_stopped
        && (!isLargeFile() || (theApp.serverConnect->currentServer()
                               && theApp.serverConnect->currentServer()->supportsLargeFilesTCP())))
    {
        m_localSrcReqQueued = true;
        theApp.downloadQueue->sendLocalSrcRequest(this);
    }

    // -- Kad source search (MFC PartFile.cpp:2363-2380) --
    const int maxSrcUDP = static_cast<int>(maxSourcePerFileUDP());

    if (maxSrcUDP > sourceCount()) {
        auto* kad = kad::Kademlia::instance();

        // Name the first unmet precondition so a file that never asks Kad for sources
        // can be diagnosed from the log instead of guessed at. Throttled per file.
        const char* skipReason = nullptr;
        QString skipDetail;   // optional suffix; only reasons with a deadline fill this in
        if (!kad || !theApp.downloadQueue)
            skipReason = "kad/queue unavailable";
        else if (!kad->isConnected())
            skipReason = "kad not connected";
        else if (kad::SearchManager::getTotalResponsesReceived() == 0)
            skipReason = "no kad responses yet";
        else if (m_stopped)
            skipReason = "file stopped";
        else if (curTick < m_lastSearchTimeKad) {
            // The reask window is 1-7 hours, so a bare "reask timer" reads the same as a file
            // that is stuck. Print what is left: a countdown that shrinks between two of these
            // lines is a healthy wait, one that does not is a bug.
            skipReason = "reask timer";
            skipDetail = QStringLiteral(" (%1 left)")
                             .arg(formatDuration(std::chrono::seconds(
                                 (m_lastSearchTimeKad - curTick) / 1000)));
        }
        else if (kad->getTotalFile() >= KADEMLIATOTALFILE)
            skipReason = "KADEMLIATOTALFILE reached";
        else if (kadFileSearchID())
            skipReason = "search id already set";
        else if (!theApp.downloadQueue->doKademliaFileRequest())
            skipReason = "queue ask throttle";
        else if (!theApp.downloadQueue->isKadSearchTurn(this))
            skipReason = "a file with fewer sources goes first";

        if (!skipReason) {
            // MFC stamps the queue-wide throttle before the search-ID check
            // (PartFile.cpp:2365). Doing that lets a file which can never start a
            // search — one holding a stale ID — burn the one-per-KADEMLIAASKTIME slot
            // every second and starve every file behind it in the queue. Stamp it only
            // when a lookup is actually attempted; the ordering above already
            // guarantees kadFileSearchID() is 0 here.
            theApp.downloadQueue->setLastKademliaFileRequest();

            auto* search = kad::SearchManager::prepareLookup(
                kad::SearchType::File, true, kad::UInt128(fileHash()));
            if (search) {
                if (m_totalSearchesKad < 7)
                    ++m_totalSearchesKad;
                m_lastSearchTimeKad = curTick + (KADEMLIAREASKTIME * m_totalSearchesKad);
                search->setGUIName(fileName());
                setKadFileSearchID(search->getSearchID());
            } else {
                setKadFileSearchID(0);
                logKadSourceSearchSkipped(curTick, QStringLiteral("prepareLookup failed"));
            }
        } else {
            logKadSourceSearchSkipped(curTick, QString::fromLatin1(skipReason) + skipDetail);
        }
    } else if (kadFileSearchID()) {
        kad::SearchManager::stopSearch(kadFileSearchID(), true);
        setKadFileSearchID(0);
    }

    return m_datarate;
}

// ===========================================================================
// Protocol helpers
// ===========================================================================

void PartFile::writePartStatus(SafeMemFile& file) const
{
    // The wire count is the ED2K one (size/PARTSIZE + 1), not our data part count
    // (MFC PartFile.cpp:2097). They differ only for a size that is an exact multiple
    // of PARTSIZE, where the ED2K count has one extra zero-length part — and there
    // the two disagreeing made every MFC peer reject our file status and extended
    // info with "wrong part number".
    const uint16 pc = ed2kPartCount();
    file.writeUInt16(pc);

    if (pc == 0)
        return;

    const uint16 byteCount = (pc + 7) / 8;
    std::vector<uint8> bitmap(byteCount, 0);

    for (uint32 i = 0; i < pc; ++i) {
        // The trailing zero-length part is always complete: MFC asks IsCompleteBDSafe
        // over an empty range, which no gap can intersect (PartFile.cpp:1606-1628).
        if (i >= partCount() || isComplete(i))
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

void PartFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

    // Aggregate from the sources first — MFC PartFile.cpp:4370-4402. These are the
    // ratings peers hand over with OP_FILEDESC; leaving them out (as this did) is
    // why the rating stayed 0 for almost every download and the indicator looked
    // broken rather than empty.
    for (const UpDownClient* src : m_srcList) {
        if (!src)
            continue;
        if (!hasNewComment && !src->fileComment().isEmpty())
            hasNewComment = true;
        if (src->fileRating() > 0 && src->fileRating() <= 5) {
            ratingSum += src->fileRating();
            ++ratingCount;
        }
    }

    // Aggregate from Kad notes cache
    for (const auto& [publisherId, note] : m_kadNotesCache) {
        if (!note.comment.isEmpty())
            hasNewComment = true;
        if (note.rating > 0 && note.rating <= 5) {
            ratingSum += note.rating;
            ++ratingCount;
        }
    }

    bool changed = false;

    if (hasNewComment != m_hasComment) {
        m_hasComment = hasNewComment;
        changed = true;
    }

    // Rounded, not truncated (MFC ROUND()). Over a 1-5 scale truncation drags
    // every average down a notch: a 2 and a 5 average to 4, not 3.
    const uint32 newRating =
        (ratingCount > 0)
            ? static_cast<uint32>((ratingSum + ratingCount / 2) / ratingCount)
            : 0;
    if (newRating != m_userRating) {
        m_userRating = newRating;
        changed = true;
    }

    if (changed)
        emit m_partNotifier.progressUpdated(m_percentCompleted);
}

const FakeFileVerdict& PartFile::fakeVerdict() const
{
    constexpr qint64 kRefreshSecs = 20;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // The head check is the strongest signal: do not sit on a verdict made without it.
    const ContainerCheck& container = containerCheck();
    const bool hasHead = containerCheckResolved();
    if (m_fakeVerdictAt != 0 && now - m_fakeVerdictAt < kRefreshSecs && hasHead == m_fakeVerdictHadHead)
        return m_fakeVerdict;

    FakeFileInput in;
    in.name = fileName();
    in.size = static_cast<uint64>(fileSize());
    in.observedNames = m_observedNames;
    in.userRating = userRating();
    in.mediaLengthSec = getIntTagValue(FT_MEDIA_LENGTH);
    in.mediaBitrateKbps = getIntTagValue(FT_MEDIA_BITRATE);
    in.artist = getStrTagValue(FT_MEDIA_ARTIST);
    in.album = getStrTagValue(FT_MEDIA_ALBUM);
    in.title = getStrTagValue(FT_MEDIA_TITLE);
    in.container = container;

    if (theApp.seenFileIndex)
        in.observedNames += theApp.seenFileIndex->lookup(fileHash()).nameList;
    for (const UpDownClient* source : m_srcList) {
        if (!source)
            continue;
        if (!source->clientFilename().isEmpty())
            in.observedNames.push_back(source->clientFilename());
        if (!source->fileComment().isEmpty())
            in.comments.push_back(source->fileComment());
    }
    in.observedNames.removeDuplicates();
    for (const auto& [publisher, note] : kadNotesCache()) {
        in.kadNoteRatedFake = in.kadNoteRatedFake || note.rating == 1;
        if (!note.comment.isEmpty())
            in.comments.push_back(note.comment);
    }

    m_fakeVerdict = assessFile(in, activeFakeFileRules());
    m_fakeVerdictAt = now;
    m_fakeVerdictHadHead = hasHead;
    return m_fakeVerdict;
}

void PartFile::addObservedNames(const QStringList& names)
{
    for (const QString& name : names) {
        if (!name.isEmpty() && !m_observedNames.contains(name) && m_observedNames.size() < 32)
            m_observedNames.push_back(name);
    }
    m_fakeVerdictAt = 0;
}

bool PartFile::readContainerHead(QByteArray& head) const
{
    // Finished: the bytes sit in the destination file like any other known file.
    if (m_status == PartFileStatus::Complete)
        return ShareableFile::readContainerHead(head);

    // Still downloading: only the .part file has them, and only once the first
    // chunk landed. isComplete() is a gap-list scan, far cheaper than an open --
    // and an open here would hand back a hole full of zeroes, which reads as a
    // fake. Saying "not yet" is the honest answer until the bytes are real.
    if (!isComplete(0, static_cast<uint64>(kContainerHeadBytes) - 1))
        return false;

    // The data file: m_fullName is the .part.met, whose first bytes are never a container
    QFile file(partFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    head = file.read(kContainerHeadBytes);
    return true;
}

// ===========================================================================
// FileMoveThread — async file move for completed downloads
// ===========================================================================

FileMoveThread::FileMoveThread(const QString& srcPath, const QString& destPath,
                               std::optional<Verify> verify, uint64 aichFileSize, QObject* parent)
    : QThread(parent)
    , m_srcPath(srcPath)
    , m_destPath(destPath)
    , m_verify(std::move(verify))
    , m_aichFileSize(aichFileSize)
{
}

void FileMoveThread::run()
{
    // The recovery set comes out of the same read as the final check; without it the
    // next start has to read the whole file again just for this.
    const bool wantAICH = m_aichFileSize > 0 && AICHRecoveryHashSet::hasKnown2MetPath();
    AICHRecoveryHashSet aich(m_aichFileSize);
    bool haveAICH = false;

    if (m_verify) {
        const QByteArray partOk = PartFile::verifyPartData(
            m_srcPath, m_verify->fileSize, m_verify->fileHash, m_verify->partHashes,
            [this](uint32, uint32) { return !isInterruptionRequested(); },
            wantAICH ? &aich : nullptr);
        if (isInterruptionRequested())
            return;
        if (partOk.count(static_cast<char>(PartFile::PartVerdict::Ok)) != partOk.size()) {
            emit verifyFailed(partOk);
            return;
        }
        if (wantAICH) {
            aich.reCalculateHash(false);
            haveAICH = aich.verifyHashTree(true);
            if (haveAICH)
                aich.setStatus(EAICHStatus::HashSetComplete);
        }
    } else if (wantAICH) {
        // Already verified by a rehash, which builds no tree
        haveAICH = KnownFile::buildAICHHashSet(m_srcPath, m_aichFileSize, aich);
    }
    if (isInterruptionRequested())
        return;
    if (haveAICH) {
        const QByteArray master(reinterpret_cast<const char*>(aich.getMasterHash().getRawHash()),
                                kAICHHashSize);
        if (aich.saveHashSet())   // frees the set
            emit aichHashSetStored(master);
    }
    emit moveStarted();

    // Ensure destination directory exists
    QDir destDir(QFileInfo(m_destPath).absolutePath());
    if (!destDir.exists())
        destDir.mkpath(QStringLiteral("."));

    const QString finalDest = uniqueDestination(
        m_destPath, [](const QString& path) { return QFile::exists(path); });
    if (finalDest != m_destPath)
        logInfo(QStringLiteral("A file named %1 exists already: saved as %2")
                    .arg(QFileInfo(m_destPath).fileName(), QFileInfo(finalDest).fileName()));

    // Try rename first (instant on same filesystem)
    if (QFile::rename(m_srcPath, finalDest)) {
        m_destPath = finalDest;
        emit moveFinished(true, finalDest);
        return;
    }

    // Rename failed (cross-filesystem)
    const bool ok = copyThenRename(m_srcPath, finalDest,
                                   [this] { return !isInterruptionRequested(); });
    if (!ok && isInterruptionRequested())
        return;
    if (ok)
        m_destPath = finalDest;
    emit moveFinished(ok, finalDest);
}

QString FileMoveThread::uniqueDestination(const QString& path,
                                          const std::function<bool(const QString&)>& exists)
{
    if (!exists(path))
        return path;

    // MFC PartFile.cpp:2856-2894: "name(N).ext", going on from an (N) the name
    // already ends in. A name without extension gets no trailing dot here.
    const qsizetype slash = path.lastIndexOf(u'/');
    const QString dir = path.left(slash + 1);
    QString stem = path.mid(slash + 1);
    QString ext;
    if (const qsizetype dot = stem.lastIndexOf(u'.'); dot > 0) {
        ext = stem.mid(dot);
        stem.truncate(dot);
    }

    int count = 0;
    if (stem.endsWith(u')')) {
        const qsizetype open = stem.lastIndexOf(u'(');
        const QStringView digits = QStringView(stem).sliced(open + 1, stem.size() - open - 2);
        const bool numeric = open > 0 && !digits.isEmpty()
            && std::ranges::all_of(digits, [](QChar c) { return c.isDigit(); });
        if (numeric) {
            count = digits.toInt();
            stem.truncate(open);
        }
    }

    QString candidate;
    do {
        candidate = QStringLiteral("%1%2(%3)%4").arg(dir, stem).arg(++count).arg(ext);
    } while (exists(candidate));
    return candidate;
}

bool FileMoveThread::copyThenRename(const QString& srcPath, const QString& finalDest,
                                    const std::function<bool()>& keepGoing)
{
    // Never a half-written file under the final name: a crash leaves only the staged
    // sibling, which the next attempt replaces.
    const QString staged = finalDest + Preferences::kCompletingSuffix;
    QFile::remove(staged);

    QFile srcFile(srcPath);
    if (!srcFile.open(QIODevice::ReadOnly))
        return false;
    QFile destFile(staged);
    if (!destFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    const qint64 total = srcFile.size();
    qint64 copied = 0;
    bool ok = true;
    std::vector<char> buf(256 * 1024);
    while (ok && copied < total) {
        if (keepGoing && !keepGoing()) {
            ok = false;
            break;
        }
        const qint64 got = srcFile.read(buf.data(), static_cast<qint64>(buf.size()));
        // A read error is not the end of the file
        if (got <= 0 || destFile.write(buf.data(), got) != got)
            ok = false;
        else
            copied += got;
    }
    ok = ok && copied == total && flushToDisk(destFile);
    destFile.close();
    srcFile.close();

    if (!ok || destFile.error() != QFileDevice::NoError || !QFile::rename(staged, finalDest)) {
        QFile::remove(staged);
        return false;
    }
    QFile::remove(srcPath);
    return true;
}

// ===========================================================================
// performFileMove (private) — launches async file move thread
// ===========================================================================

void PartFile::performFileMove(const QString& srcPath, const QString& destPath, bool verify)
{
    std::optional<FileMoveThread::Verify> check;
    if (verify) {
        check.emplace();
        check->fileHash = QByteArray(reinterpret_cast<const char*>(fileHash()), 16);
        check->fileSize = static_cast<uint64>(fileSize());
        check->partHashes = fileIdentifier().getRawMD4HashSet();
    }

    // Not a child of the notifier: a running QThread must never be destroyed. It deletes
    // itself when done, and ~PartFile() interrupts and waits for it.
    auto* thread = new FileMoveThread(srcPath, destPath, std::move(check),
                                      static_cast<uint64>(fileSize()));
    m_moveThread = thread;
    m_completedAICHMaster.clear();

    QObject::connect(thread, &FileMoveThread::aichHashSetStored, &m_partNotifier,
                     [this](const QByteArray& masterHash) { m_completedAICHMaster = masterHash; });
    m_fileOp = verify ? PartFileOp::Hashing : PartFileOp::Copying;

    QObject::connect(thread, &FileMoveThread::moveStarted, &m_partNotifier, [this] {
        m_fileOp = PartFileOp::Copying;
    });

    QObject::connect(thread, &FileMoveThread::verifyFailed,
                     &m_partNotifier, [this](const QByteArray& partOk) {
        m_completionRunning = false;
        m_fileOp = PartFileOp::None;

        if (partOk.contains(static_cast<char>(PartVerdict::Bad))) {
            // Damaged on disk after it was verified. Those parts are downloaded again;
            // the file goes back to being an ordinary download.
            logWarning(QStringLiteral("Final check of '%1' found damaged parts — not delivered, "
                                      "downloading them again").arg(fileName()));
            applyRehashResult(partOk);
        } else {
            logError(QStringLiteral("Final check of '%1' could not read the file — not "
                                    "delivered, resume to try again").arg(fileName()));
            m_completionError = true;
            setStatus(PartFileStatus::Error);
        }
        emit m_partNotifier.fileMoveFinished(false);
    });

    QObject::connect(thread, &FileMoveThread::moveFinished,
                     &m_partNotifier, [this](bool success, const QString& finalPath) {
        m_completionRunning = false;
        m_fileOp = PartFileOp::None;
        if (success) {
            // Delete .part.met and .bak
            QFile::remove(m_fullName);
            QFile::remove(m_fullName + QStringLiteral(".bak"));
            QFile::remove(m_fullName + QStringLiteral(".backup"));

            // The download is finished, so its saved source list is dead weight
            // (MorphXT CPartFile::PerformFileComplete, PartFile.cpp:4659).
            SourceSaver::removeFile(m_tmpPath, m_partMetFilename);

            setActive(false);   // done: stop the active-time clock
            ensureOneCompleteSource();   // ourselves (MFC PartFile.cpp:2990-2992)
            setStatus(PartFileStatus::Complete);
            setFilePath(finalPath);
            setPath(QFileInfo(finalPath).absolutePath());
            if (theApp.sharedFileList)
                theApp.sharedFileList->refreshDirectoryOf(this);   // filed under the temp dir until now
            // The date known.met matches the file by at the next scan; without it the
            // file is rehashed in full (MFC srchybrid/PartFile.cpp:2930-2939).
            if (const qint64 mtime = QFileInfo(finalPath).lastModified().toSecsSinceEpoch();
                mtime > 0) {
                m_tLastModified = static_cast<time_t>(mtime);
                setUtcFileDate(m_tLastModified);
            }
            adoptCompletedAICHHashSet();

            // Tags that came with the download are anyone's guess; publish only
            // what the finished file says itself (MFC PartFile.cpp:3011-3014).
            updateMetaDataTags();

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

PartFile::PartVerdict PartFile::hashSinglePart(uint32 partNumber, bool* aichAgreed,
                                               const PartDigest* given)
{
    if (aichAgreed)
        *aichAgreed = false;

    if (partNumber >= partCount())
        return PartVerdict::Ok; // out of range, nothing to verify

    // We demand that MD4 and AICH agree, exactly as MFC does — a part is only good if
    // neither algorithm objects (srchybrid/PartFile.cpp:3124-3129).
    const bool haveMD4 = fileIdentifier().hasExpectedMD4HashCount();
    const bool haveAICH = fileIdentifier().hasAICHHash()
                          && fileIdentifier().hasExpectedAICHHashCount();

    if (!haveMD4 && !haveAICH) {
        // Nothing to compare against. Ask for both hash sets rather than blessing the
        // data (MFC PartFile.cpp:3132-3137).
        logError(QStringLiteral("PartFile: cannot verify part %1 of '%2' — no hashset")
                     .arg(partNumber).arg(fileName()));
        setMD4HashsetNeeded(true);
        setAICHPartHashsetNeeded(true);
        // Checked once more when a hashset arrives (hashsetReceived)
        if (m_partsAwaitingHashset.size() < partCount())
            m_partsAwaitingHashset.resize(partCount(), false);
        m_partsAwaitingHashset[partNumber] = true;
        return PartVerdict::Ok;
    }

    // Read the part back and hash it — unless the disk worker already did. A failed
    // read is no verdict: MFC throws here and the file goes to error
    // (srchybrid/PartFile.cpp:4276-4313) — it never blesses the part.
    PartDigest own;
    if (!given) {
        if (!m_partFileHandle.isOpen()) {
            m_partFileHandle.setFileName(partFilePath());
            if (!m_partFileHandle.open(QIODevice::ReadWrite))
                return PartVerdict::Unread;
        }
        own = digestPart(m_partFileHandle, digestRequest(partNumber));
        given = &own;
    }
    if (!given->read)
        return PartVerdict::Unread;
    const uint8* computedHash = given->md4.data();

    // MD4 — MFC PartFile.cpp:3155-3171
    bool md4Error = false;
    if (haveMD4) {
        if (partCount() > 1 || fileSize() == PARTSIZE) {
            if (const uint8* storedHash = fileIdentifier().getMD4PartHash(partNumber))
                md4Error = !md4equ(computedHash, storedHash);
            else {
                logWarning(QStringLiteral("PartFile: MD4 part hash %1 missing for '%2'")
                               .arg(partNumber).arg(fileName()));
                setMD4HashsetNeeded(true);
            }
        } else {
            // A file of one part has no part hashes at all — its file hash is the part
            // hash, and without this arm such a file was never verified.
            md4Error = !md4equ(computedHash, fileIdentifier().getMD4Hash());
        }
    } else {
        logWarning(QStringLiteral("PartFile: MD4 hashset missing while verifying part %1 of '%2'")
                       .arg(partNumber).arg(fileName()));
        setMD4HashsetNeeded(true);
    }

    // AICH — MFC PartFile.cpp:3173-3184
    bool aichError = false;
    const bool aichChecked = haveAICH && given->aichValid;
    if (aichChecked) {
        if (partCount() > 1) {
            if (fileIdentifier().getAvailableAICHPartHashCount() > partNumber)
                aichError = fileIdentifier().getRawAICHHashSet()[partNumber] != given->aich;
            else {
                logWarning(QStringLiteral("PartFile: AICH part hash %1 missing for '%2'")
                               .arg(partNumber).arg(fileName()));
                setAICHPartHashsetNeeded(true);
            }
        } else
            aichError = fileIdentifier().getAICHHash() != given->aich;
    }

    // Only a check that actually ran may report agreement: the caller skips the AICH
    // recovery request on the strength of it (flushBuffer, above).
    if (aichAgreed && aichChecked)
        *aichAgreed = !aichError;

    if (haveMD4 && aichChecked && md4Error != aichError) {
        logError(QStringLiteral("PartFile: MD4 and AICH disagree on part %1 of '%2' — "
                                "MD4: %3, AICH: %4")
                     .arg(partNumber).arg(fileName(),
                          md4Error ? QStringLiteral("corrupt") : QStringLiteral("ok"),
                          aichError ? QStringLiteral("corrupt") : QStringLiteral("ok")));
    }

    return (!md4Error && !aichError) ? PartVerdict::Ok : PartVerdict::Bad;
}

// ===========================================================================
// requestAICHRecovery — request AICH recovery data from a source
// ===========================================================================

void PartFile::seedAICHRecoveryMasterHash()
{
    if (!fileIdentifier().hasAICHHash())
        return;

    const EAICHStatus status = m_aichRecoveryHashSet.getStatus();
    if (m_aichRecoveryHashSet.hasValidMasterHash()
        && (status == EAICHStatus::Verified || status == EAICHStatus::Trusted))
    {
        return; // already trusted, don't downgrade a set that peers voted on
    }

    // An AICH root hash that came with the ed2k link or the search result is as
    // trustworthy as the file hash beside it. MFC seeds the recovery set the same way
    // (srchybrid/PartFile.cpp:97, :188, DownloadQueue.cpp:283). Without this,
    // requestAICHRecovery() bails on every freshly added download — the set stays Empty
    // until the .part.met is reloaded on the next start, so recovery is effectively off
    // for exactly the downloads most likely to need it.
    m_aichRecoveryHashSet.setMasterHash(fileIdentifier().getAICHHash(), EAICHStatus::Verified);
}

void PartFile::voteAICHRoot(const AICHHash& root, const Address& from)
{
    m_aichRecoveryHashSet.untrustedHashReceived(root, from);

    if (m_aichRecoveryHashSet.getStatus() == EAICHStatus::Trusted
        && m_aichRecoveryHashSet.hasValidMasterHash() && !fileIdentifier().hasAICHHash())
    {
        fileIdentifier().setAICHHash(m_aichRecoveryHashSet.getMasterHash());
        logInfo(QStringLiteral("AICH root of %1 is trusted by its sources").arg(fileName()));
        savePartFile();
    }
}

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

    // Somebody is already fetching this part for us. MFC PartFile.cpp:5186.
    if (AICHRecoveryHashSet::isClientRequestPending(this, static_cast<uint16>(partNumber)))
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
    flushBuffer(/*forceICH*/ true, /*noAICH*/ true);

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
            m_corruptionBlackBox.verifiedData(partStart + pos, partStart + pos + blockSize - 1);
        } else {
            m_corruptionBlackBox.corruptedData(partStart + pos, partStart + pos + blockSize - 1);
        }
    }

    // AICH has just narrowed the damage to individual blocks, which is the whole
    // point of it and the only place MFC ever bans for corruption
    // (srchybrid/PartFile.cpp:5303).
    punishCorruptionSenders(static_cast<uint16>(partNumber));

    // Adjust corruption loss accounting (MFC: srchybrid/PartFile.cpp:5305-5308).
    // No ICH credit here — MFC counts parts saved by ICH only on the re-hash path.
    if (m_corruptionLoss >= recovered)
        m_corruptionLoss -= recovered;
    if (theApp.statistics)
        theApp.statistics->subCorruptionLoss(recovered);

    // Sanity check: if the part became complete, verify with MD4 too
    if (isComplete(partStart, partStart + partLen - 1)) {
        const PartVerdict verdict = hashSinglePart(partNumber);
        if (verdict == PartVerdict::Unread) {
            logError(QStringLiteral("PartFile AICH recovery: cannot read part %1 of '%2' back")
                         .arg(partNumber).arg(fileName()));
            markChangedParts(partStart, partStart);   // checked again on the next flush
            return;
        }
        if (verdict == PartVerdict::Bad) {
            logWarning(QStringLiteral("PartFile AICH recovery: part %1 completed but MD4 disagrees — marking corrupt")
                           .arg(partNumber));
            if (!fileIdentifier().hasAICHHash())
                m_aichRecoveryHashSet.setStatus(EAICHStatus::Error);
            addGap(partStart, partStart + partLen - 1);
            return;
        }

        logInfo(QStringLiteral("PartFile AICH recovery: part %1 completed and MD4 verified").arg(partNumber));

        // Remove from corrupted list
        dropCorruptedPart(partNumber);

        // A recovered part is a part we can serve. MFC srchybrid/PartFile.cpp:5329.
        addToSharedFiles();

        // Check if entire file is now complete
        if (m_bufferedData.empty() && m_flushingData.empty())
            completeIfVerified();
    }

    savePartFile();
    logInfo(QStringLiteral("PartFile AICH recovery: recovered %1 of %2 bytes from part %3 of '%4'")
                .arg(recovered).arg(partLen).arg(partNumber).arg(fileName()));
}

// ===========================================================================
// createServerSourceRequestPacket — OP_GETSOURCES(_OBFU) to the connected server
// ===========================================================================

std::unique_ptr<Packet> PartFile::createServerSourceRequestPacket(bool obfuscated) const
{
    // Large-file size encoding (MFC DownloadQueue.cpp:1354-1357): a zero uint32
    // marks that a uint64 size follows. Sending the bare uint64 (no marker) makes a
    // standard server read the low 32 bits as the whole size, so the (hash,size)
    // lookup misses and no sources come back.
    const uint64 fsize     = static_cast<uint64>(fileSize());
    const bool   largeFile = isLargeFile();   // same test as the queue's server guard
    const uint32 pktSize   = largeFile ? 28u : 20u;

    auto pkt = std::make_unique<Packet>(
        obfuscated ? OP_GETSOURCES_OBFU : OP_GETSOURCES,
        pktSize, OP_EDONKEYPROT);
    md4cpy(pkt->pBuffer, fileHash());
    if (largeFile) {
        const uint32 zero = 0;
        std::memcpy(pkt->pBuffer + 16, &zero, 4);
        std::memcpy(pkt->pBuffer + 20, &fsize, 8);
    } else {
        const uint32 fsize32 = static_cast<uint32>(fsize);
        std::memcpy(pkt->pBuffer + 16, &fsize32, 4);
    }
    return pkt;
}

// ===========================================================================
// createSrcInfoPacket — SX2 override for PartFile (uses srcList)
// ===========================================================================

std::unique_ptr<Packet> PartFile::createSrcInfoPacket(
    const UpDownClient* forClient, uint8 version, uint16 /*options*/) const
{
    if (!forClient)
        return nullptr;

    // With no sources of our own, the clients uploading this part file are still worth
    // handing out — that is the base implementation, and it carries the part-count guard.
    // MFC srchybrid/PartFile.cpp:3642-3643.
    if (m_srcList.empty())
        return KnownFile::createSrcInfoPacket(forClient, version, 0);

    // The answer is built against the part status the requester reported for *this* file,
    // so it has to be the file it last set. A client that asks out of context gets
    // nothing rather than a source set filtered through a stale bitmap.
    // MFC srchybrid/PartFile.cpp:3645-3650.
    if (!md4equ(forClient->reqUpFileId(), fileHash())) {
        logDebug(QStringLiteral("createSrcInfoPacket: requester's upload file is not %1")
                     .arg(fileName()));
        return nullptr;
    }

    // Same shape test the base class applies. MFC srchybrid/PartFile.cpp:3652-3659.
    if (!(forClient->upPartCount() == 0 && forClient->upPartStatus().empty()) &&
        !(forClient->upPartCount() == partCount() && !forClient->upPartStatus().empty()))
    {
        logDebug(QStringLiteral("createSrcInfoPacket: requester part count %1 does not "
                                "match file part count %2 for %3")
                     .arg(forClient->upPartCount()).arg(partCount()).arg(fileName()));
        return nullptr;
    }

    // Sources are only worth exchanging for a file we are actually downloading. A
    // paused, erroring, completing or rehashing file has no business handing its
    // source list around (MFC srchybrid/PartFile.cpp:3662).
    if (status() != PartFileStatus::Ready && status() != PartFileStatus::Empty)
        return nullptr;

    // Download side: candidates are this file's sources, judged on their *download*
    // part status (what they hold of the file we want) — the upload side reads
    // upPartStatus() instead, which is the only reason the two sides need separate
    // predicates. Everything about the packet format itself lives in the base.
    //
    // Only sources we reached and that told us what they hold go out, and only when they
    // have a part the requester lacks — MFC srchybrid/PartFile.cpp:3692-3706. A source
    // that reported the whole file keeps no bitmap here (completeSource()).
    const auto& clientParts = forClient->upPartStatus();
    const size_t parts = partCount();
    return buildSrcInfoPacket(
        forClient, version, m_srcList,
        [&clientParts, parts](const UpDownClient* src) {
            if (!src->isValidSource() || src->partCount() != parts)
                return false;
            const auto& srcParts = src->partStatus();
            const bool complete = srcParts.empty() && src->completeSource();
            if (!complete && srcParts.size() != parts)
                return false;
            for (size_t p = 0; p < parts; ++p) {
                if ((complete || srcParts[p] != 0)
                    && (clientParts.empty() || clientParts[p] == 0))
                    return true;
            }
            return false;
        });
}

// ===========================================================================
// addClientSources — process SX2 source answer for this PartFile
// ===========================================================================

void PartFile::addClientSources(SafeMemFile& data, uint8 clientSXVersion, bool isSX2,
                                const UpDownClient* sender)
{
    if (isStopped())
        return;

    const uint16 srcCount = data.readUInt16();
    const qint64 dataSize = data.length() - data.position();
    if (thePrefs.wantsSourceExchangeLog())
        logDebug(QStringLiteral("SXRecv: Client source response; SX2=%1, Ver=%2, Count=%3, File=\"%4\"")
                     .arg(isSX2 ? QStringLiteral("Yes") : QStringLiteral("No"))
                     .arg(clientSXVersion).arg(srcCount).arg(fileName()));

    // Byte size of one source record for a given SX version.
    const auto recordSize = [](uint8 v) -> qint64 {
        qint64 n = 4 + 2 + 4 + 2;    // userId, port, serverIP, serverPort
        if (v >= 2) n += 16;         // userHash
        if (v >= 4) n += 1;          // cryptOptions
        return n;
    };

    // Extended SX (variable-length tag-block records) is used only when the sender
    // advertised the capability AND sent the extended version byte. The fixed-size
    // integrity check below does not apply then; records are parsed defensively.
    const bool extSX = isSX2 && sender && sender->supportsExtendedXS()
                       && clientSXVersion == SOURCEEXCHANGEEXT_VERSION;

    // The version actually used to lay out the records, which is not necessarily the
    // one the peer announced.
    uint8 version;

    if (extSX) {
        version = SOURCEEXCHANGEEXT_VERSION;   // 1 — tag-block layout, no fixed record size
    } else if (!isSX2) {
        // SX1 has no version byte, so infer the layout from the record size and only
        // then check the peer announced at least that much. Trusting the announced
        // version instead would mis-slice the records and yield garbage user hashes.
        if (srcCount != 0 && dataSize == srcCount * recordSize(1))
            version = 1;
        else if (srcCount != 0 && dataSize == srcCount * recordSize(2))
            version = (clientSXVersion == 2) ? 2 : 3;
        else if (srcCount != 0 && dataSize == srcCount * recordSize(4))
            version = 4;
        else {
            logWarning(QStringLiteral("Ignoring invalid SX packet (v%1, count=%2, size=%3) for %4")
                           .arg(clientSXVersion).arg(srcCount).arg(dataSize).arg(fileName()));
            return;
        }

        if (clientSXVersion < version) {
            logWarning(QStringLiteral("Ignoring SX packet claiming v%1 but shaped as v%2 for %3")
                           .arg(clientSXVersion).arg(version).arg(fileName()));
            return;
        }
    } else {
        // SX2 states its version, so a mismatch can't be a misunderstanding — drop it.
        if (clientSXVersion == 0 || clientSXVersion > SOURCEEXCHANGE2_VERSION) {
            logWarning(QStringLiteral("Ignoring SX2 packet with unknown version v%1 for %2")
                           .arg(clientSXVersion).arg(fileName()));
            return;
        }
        version = clientSXVersion;

        if (dataSize != srcCount * recordSize(version)) {
            logWarning(QStringLiteral("Ignoring corrupt SX2 packet (v%1, count=%2, size=%3, "
                                      "expected %4) for %5")
                           .arg(version).arg(srcCount).arg(dataSize)
                           .arg(srcCount * recordSize(version)).arg(fileName()));
            return;
        }
    }

    // The ExtSX per-source record is variable length and carries no size check, so a
    // truncated packet is caught here (FileException) rather than over-reading.
    try {
    for (uint16 i = 0; i < srcCount; ++i) {
        uint32 userId = data.readUInt32();
        uint16 port = data.readUInt16();
        uint32 serverIP = 0;
        uint16 serverPort = 0;
        std::array<uint8, 16> userHash{};
        uint8 cryptFlags = 0;
        bool haveUserHash = false;
        bool haveCryptFlags = false;
        Address userIPv6;   // set only when a valid CT_MOD_IP_V6 tag is present

        if (extSX) {
            // Variable tag block: serverIP/port ride as tags; a reachable public IPv6
            // as CT_MOD_IP_V6. Unknown tags are skipped by type (forward-compatible).
            const uint8 tagCount = data.readUInt8();
            for (uint8 t = 0; t < tagCount; ++t) {
                Tag tag(data, true);
                switch (tag.nameId()) {
                case CT_EMULE_SERVERIP:
                    if (tag.isInt()) serverIP = tag.intValue();
                    break;
                case CT_EMULE_SERVERTCP:
                    if (tag.isInt()) serverPort = static_cast<uint16>(tag.intValue());
                    break;
                case CT_MOD_IP_V6:
                    if (tag.isHash()) {
                        const Address a = Address::fromIPv6Bytes(tag.hashValue());
                        if (a.isPublicIP()) userIPv6 = a;
                    }
                    break;
                case CT_EMULE_USERHASH:
                    // The classic record's version >= 2 field, as a tag. Only a peer that
                    // set MODMISC_EXTXS_SKIPTAGS is ever sent these, but accept them from
                    // anyone — the tag block is self-describing either way.
                    if (tag.isHash()) {
                        std::memcpy(userHash.data(), tag.hashValue(), 16);
                        haveUserHash = true;
                    }
                    break;
                case CT_EMULE_CONOPTS:
                    // The classic record's version >= 4 field, as a tag.
                    if (tag.isInt()) {
                        cryptFlags = static_cast<uint8>(tag.intValue());
                        haveCryptFlags = true;
                    }
                    break;
                default:
                    break;   // skip unknown tag by type
                }
            }
        } else {
            serverIP = data.readUInt32();
            serverPort = data.readUInt16();
            if (version >= 2)
                data.readHash16(userHash.data());
            if (version >= 4)
                cryptFlags = data.readUInt8();
        }

        // v3+ clients send IDs in hybrid (host order) format so high-ID clients
        // with an address ending in .0 aren't falsely switched to a low-ID.
        // Validation needs the ED2K (network order) representation; the raw
        // userId is handed to UpDownClient along with the ed2kID flag below,
        // which does its own conversion — so don't mutate userId here.
        const uint32 userIdEd2k = (version < 3) ? userId : htonl(userId);

        // The hybrid form is what identifies a low ID; for v<3 the id arrives in
        // ED2K order and only high IDs get converted.
        const uint32 userIdHybrid = (version < 3)
            ? (isLowID(userId) ? userId : htonl(userId))
            : userId;

        // If we're firewalled too, neither side can accept the other's connection,
        // so a low-ID source is dead weight — unless it has a reachable public IPv6,
        // which we can connect to directly.
        if (isLowID(userIdHybrid) && theApp.isFirewalled() && userIPv6.isNull())
            continue;

        // Reject an IPv6 we can't use before it can rescue an unusable v4 below.
        if (!userIPv6.isNull()) {
            const bool v6Bad =
                !isGoodIP(userIPv6)
                || (theApp.clientList && theApp.clientList->isBannedClient(userIPv6));
            if (v6Bad)
                userIPv6 = Address{};
        }

        // Whether the v4 field alone is enough to reach this source. A source can carry
        // a deliberately unusable v4 — the reference marks an IPv6-only peer with the
        // HighID sentinel 0xFFFFFFFF, which fails isGoodIP as a broadcast address.
        bool v4Usable = true;
        if (!isLowID(userIdEd2k)) {
            // For high-ID clients, userId == IP: validate it, IP-filter it, and don't
            // re-admit a peer we've already banned via another path.
            v4Usable = isGoodIP(userIdEd2k)
                       && !(theApp.ipFilter && theApp.ipFilter->isFiltered(userIdEd2k))
                       && !(theApp.clientList
                            && theApp.clientList->isBannedClient(
                                   Address::fromNetworkOrder(userIdEd2k)));
        }

        // Drop only when neither family can reach the source. Previously an unusable v4
        // discarded the record outright, throwing away an IPv6 that had already been
        // parsed — which is why IPv6-only sources never propagated.
        if (!v4Usable && userIPv6.isNull())
            continue;

        // Max sources check
        if (sourceCount() >= static_cast<int>(maxSources()))
            break;

        // A source reachable only over IPv6 is constructed as a LowID client: its v4
        // field is meaningless, and feeding it through would have us dialing garbage
        // (0xFFFFFFFF becomes 255.255.255.255).
        const uint32 ctorId = v4Usable ? userId : 1u;
        auto* client = new UpDownClient(port, ctorId, serverIP, serverPort, this,
                                        v4Usable && version < 3);
        client->setSourceFrom(SourceFrom::SourceExchange);

        if (!userIPv6.isNull()) {
            client->setUserIPv6(userIPv6);
            client->setOpenIPv6(true);
        }

        // Classic SX carries these as version-gated fixed fields; ExtSX carries them as
        // optional tags, so on that path presence is what decides, not the version.
        if (extSX ? haveUserHash : (version >= 2))
            client->setUserHash(userHash.data());

        if (extSX ? haveCryptFlags : (version >= 4))
            client->setConnectOptions(cryptFlags, true, false);

        // Queued, not dialled — MFC PartFile.cpp:3927. process() reaches it through
        // askForDownload(), which owns the socket cap, the re-ask throttle, the LowID
        // handling and the A4AF swap that a direct tryToConnect() skipped.
        // A result other than `client` means it was rejected or folded into a known one.
        if (!theApp.downloadQueue || theApp.downloadQueue->checkAndAddSource(this, client) != client)
            delete client;
    }
    } catch (...) {
        logWarning(QStringLiteral("Truncated or corrupt source-exchange packet for %1").arg(fileName()));
    }
}

// ===========================================================================
// dropCorruptedPart (private)
// ===========================================================================

bool PartFile::dropCorruptedPart(uint32 partNumber)
{
    const auto it = std::ranges::find(m_corruptedParts, static_cast<uint16>(partNumber));
    if (it == m_corruptedParts.end())
        return false;
    m_corruptedParts.erase(it);
    return true;
}

// ===========================================================================
// logKadSourceSearchSkipped (private)
// ===========================================================================

void PartFile::logKadSourceSearchSkipped(uint64 curTick, const QString& reason)
{
    // process() runs at 10 Hz, so the interesting part is *which* condition is
    // blocking, not how often. One line per file per interval keeps the Kad tab usable.
    static constexpr uint32 kKadSkipLogInterval = SEC2MS(30);

    if (m_lastKadSkipLogTime != 0 && curTick - m_lastKadSkipLogTime < kKadSkipLogInterval)
        return;
    m_lastKadSkipLogTime = curTick;

    kad::logKad(QStringLiteral("Kad: no source search for %1 — %2 (sources=%3 searchId=%4)")
                    .arg(fileName())
                    .arg(reason)
                    .arg(sourceCount())
                    .arg(kadFileSearchID()));
}

// ===========================================================================
// markPartCorrupted (private)
// ===========================================================================

void PartFile::markPartCorrupted(uint32 partNumber)
{
    const uint64 partStart = static_cast<uint64>(partNumber) * PARTSIZE;
    const uint64 partEnd = std::min(partStart + PARTSIZE - 1,
                                    static_cast<uint64>(fileSize()) - 1);

    // corruptedData() takes one 180 KB block at a time, the granularity AICH works
    // at, so a whole part has to be walked rather than handed over in one call.
    for (uint64 pos = partStart; pos <= partEnd; pos += EMBLOCKSIZE) {
        const uint64 blockEnd = std::min(pos + EMBLOCKSIZE - 1, partEnd);
        m_corruptionBlackBox.corruptedData(pos, blockEnd);
    }
}

// ===========================================================================
// punishCorruptionSenders (private)
// ===========================================================================

void PartFile::punishCorruptionSenders(uint16 part)
{
    if (!theApp.clientList)
        return;

    for (const auto& guilty : m_corruptionBlackBox.evaluateData(part)) {
        if (!guilty.shouldBan || guilty.addr.isNull())
            continue;

        // Already banned: the ban list is the state, and re-banning would only
        // restart the two-hour clock on an old offence (MFC skips these too,
        // srchybrid/CorruptionBlackBox.cpp:276-279).
        if (theApp.clientList->isBannedClient(guilty.addr))
            continue;

        const QString reason =
            QStringLiteral("Identified as a sender of corrupt data (%1% of %2 bytes attributed)")
                .arg(guilty.corruptPercent)
                .arg(guilty.corruptBytes + guilty.verifiedBytes);

        logWarning(QStringLiteral("PartFile: banning %1 — part %2 of '%3': %4")
                       .arg(guilty.addr.toString())
                       .arg(part)
                       .arg(fileName(), reason));

        // Prefer the client object: ban() also takes the peer off the upload queue
        // and marks its state, which banning a bare address cannot do. Falling back
        // to the address covers a peer that has already disconnected, exactly as MFC
        // does (srchybrid/CorruptionBlackBox.cpp:316-322).
        if (UpDownClient* client = theApp.clientList->findByConnAddress(guilty.addr))
            client->ban(reason);
        else
            theApp.clientList->addBannedClient(guilty.addr);
    }
}


// ===========================================================================
// markChangedParts (private)
// ===========================================================================

void PartFile::markChangedParts(uint64 start, uint64 end)
{
    if (partCount() == 0)
        return;
    if (m_changedParts.size() < partCount())
        m_changedParts.resize(partCount(), false);

    // A range can span parts (MFC SafeHash loop)
    const uint64 last = std::min<uint64>(end / PARTSIZE, partCount() - 1);
    for (uint64 p = start / PARTSIZE; p <= last; ++p)
        m_changedParts[p] = true;
}

// ===========================================================================
// Disk-worker helpers (private)
// ===========================================================================

void PartFile::finishPendingFlush(bool forceICH, bool noAICH)
{
    if (m_flushToken == 0)
        return;
    std::optional<PartFileWriteResult> result;
    if (theApp.partFileWriter)
        result = theApp.partFileWriter->waitFor(m_flushToken);
    if (result) {
        applyFlushResult(*result, forceICH, noAICH);
        return;
    }
    // No worker and no result: the job cannot have run. Its bytes are gone with it,
    // so the ranges go back to being gaps rather than being taken for written.
    logError(QStringLiteral("PartFile: lost a pending write of '%1'").arg(fileName()));
    m_flushToken = 0;
    for (const auto& bd : m_flushingData)
        addGap(bd.start, bd.end);
    m_flushingData.clear();
    m_flushingBytes = 0;
}

void PartFile::resumeIdleSources()
{
    // A copy: a source with nothing to get leaves the list from inside the call.
    const std::vector<UpDownClient*> sources = m_downloadingSources;
    for (UpDownClient* client : sources) {
        if (std::ranges::find(m_downloadingSources, client) != m_downloadingSources.end()
            && client->downloadState() == DownloadState::Downloading
            && client->pendingBlocks().empty())
            client->sendBlockRequests();
    }
}

QString PartFile::partFilePath() const
{
    QString path = m_fullName.isEmpty() ? m_tmpPath + QDir::separator() + m_partMetFilename
                                        : m_fullName;
    if (path.endsWith(QStringLiteral(".met")))
        path.chop(4);
    return path;
}

PartDigestRequest PartFile::digestRequest(uint32 partNumber)
{
    PartDigestRequest request;
    request.part = partNumber;
    request.start = static_cast<uint64>(partNumber) * PARTSIZE;
    const uint64 end = std::min(request.start + PARTSIZE - 1, static_cast<uint64>(fileSize()) - 1);
    request.length = end - request.start + 1;

    // The geometry of the recovery set's node for this part: the root computed with
    // it compares with the stored part hash (MFC PartFile.cpp:3139-3147).
    if (fileIdentifier().hasAICHHash() && fileIdentifier().hasExpectedAICHHashCount()) {
        if (const AICHHashTree* node =
                m_aichRecoveryHashSet.findPartHash(static_cast<uint16>(partNumber))) {
            request.wantAICH = true;
            request.aichDataSize = node->m_dataSize;
            request.aichLeftBranch = node->m_isLeftBranch;
            request.aichBaseSize = node->getBaseSize();
        }
    }
    return request;
}

// ===========================================================================
// completeIfVerified (private)
// ===========================================================================

void PartFile::completeIfVerified()
{
    if (!m_gapList.empty() || m_destroying)
        return;

    if (std::ranges::find(m_changedParts, true) != m_changedParts.end()) {
        logError(QStringLiteral("PartFile: '%1' is fully downloaded but could not be read back "
                                "to verify it — resume to try again").arg(fileName()));
        m_completionError = true;
        setStatus(PartFileStatus::Error);
        return;
    }
    completeFile();
}

// ===========================================================================
// verifyChangedParts (private)
// ===========================================================================

void PartFile::verifyChangedParts(bool forceICH, bool noAICH,
                                  const std::map<uint32, PartDigest>* digests)
{
    const auto digestOf = [digests](uint32 part) -> const PartDigest* {
        if (!digests)
            return nullptr;
        const auto it = digests->find(part);
        return it != digests->end() ? &it->second : nullptr;
    };

    // Hash verification per changed part (MD4 + AICH), mirroring MFC's
    // CPartFile::FlushBuffer (srchybrid/PartFile.cpp:4154-4220). Flag cleared first.
    for (uint32 p = 0; p < partCount(); ++p) {
        if (p >= m_changedParts.size() || !m_changedParts[p])
            continue;
        m_changedParts[p] = false;

        const uint64 partStart = static_cast<uint64>(p) * PARTSIZE;
        const uint64 partEnd = std::min(partStart + PARTSIZE - 1,
                                        static_cast<uint64>(fileSize()) - 1);

        if (isComplete(p)) {
            // Complete by the gap list, but some of it still in memory or with the
            // disk worker: what is on disk is not the part yet. Judged when it is
            // (MFC tests IsCompleteBD here, srchybrid/PartFile.cpp:4163).
            if (!isCompleteBDSafe(partStart, partEnd)) {
                m_changedParts[p] = true;
                continue;
            }
            bool aichAgreed = false;
            const PartVerdict verdict = hashSinglePart(p, &aichAgreed, digestOf(p));
            if (verdict == PartVerdict::Unread) {
                // No verdict: neither blessed nor condemned, and looked at again on
                // the next flush. completeIfVerified() keeps the file from finishing.
                logError(QStringLiteral("PartFile: cannot read part %1 of '%2' back to verify it")
                             .arg(p).arg(fileName()));
                m_changedParts[p] = true;
                continue;
            }
            if (verdict == PartVerdict::Ok) {
                // The part verified, so it is no longer a candidate for recovery.
                m_corruptionBlackBox.verifiedData(partStart, partEnd);
                dropCorruptedPart(p);

                // We now hold a whole verified part, which is MFC's condition for a
                // part file becoming a shared file (srchybrid/PartFile.cpp:4194).
                // Idempotent: a part is re-checked whenever it is rewritten or a
                // late hashset arrives.
                addToSharedFiles();

                // A chunk we fetched over the HTTP Cache may now be handed on to other
                // peers — but only because MD4 actually matched, which is a stronger
                // claim than hashSinglePart() answering Ok. That is also its answer for
                // a part it had no hash to check against (it then waits for the
                // hashset), and its md4 error flag is only raised when a per-part hash
                // exists to compare with. Relaying
                // means vouching for the bytes, so demand the hash really existed.
                if (theApp.httpCache && fileIdentifier().getMD4PartHash(p) != nullptr) {
                    std::array<uint8, 16> hash{};
                    std::memcpy(hash.data(), fileHash(), 16);
                    theApp.httpCache->reportPartVerified(hash, p);
                }
                continue;
            }

            logWarning(QStringLiteral("PartFile: hash mismatch for part %1 of '%2' — re-downloading")
                           .arg(p).arg(fileName()));
            addGap(partStart, partEnd);

            // Add part to corrupted list, if not already there
            if (std::ranges::find(m_corruptedParts, static_cast<uint16>(p)) == m_corruptedParts.end())
                m_corruptedParts.push_back(static_cast<uint16>(p));

            // Request AICH recovery data if AICH didn't already agree. noAICH is
            // separate from forceICH because the destructor flushes with noAICH: a part
            // failing MD4 there must not start a request against a file being destroyed.
            if (!noAICH && !aichAgreed)
                requestAICHRecovery(p);

            // Track corruption loss
            const uint64 lost = partEnd - partStart + 1;
            m_corruptionLoss += lost;
            if (theApp.statistics)
                theApp.statistics->addCorruptionLoss(lost);

            // MFC only ever assigns blame from AICH recovery, which needs a second
            // source to supply the reference hash and so may never arrive. When one
            // sender supplied the whole part that detour is unnecessary: nobody else
            // could have contributed the bad bytes. This is the ordinary case for an
            // HTTP Cache chunk, which is always one whole part from one peer.
            if (!m_corruptionBlackBox
                     .soleSenderOfWholePart(static_cast<uint16>(p), lost)
                     .isNull()) {
                markPartCorrupted(p);
                punishCorruptionSenders(static_cast<uint16>(p));
            }

            // Tell the peer that offered this part over the HTTP Cache, so its own
            // three-strike counter retires the chunk instead of handing it to the next
            // downloader. A no-op for a part we did not fetch that way.
            if (theApp.httpCache) {
                std::array<uint8, 16> hash{};
                std::memcpy(hash.data(), fileHash(), 16);
                theApp.httpCache->reportPartCorrupt(hash, p);
            }

        } else if (isCorruptedPart(p) && (thePrefs.useICH() || forceICH)) {
            // Intelligent Corruption Handling: the part still has gaps, but the
            // bytes behind them were never erased — only distrusted. If MD4 over
            // the whole part matches, they were fine all along and re-downloading
            // them would be wasted traffic.
            // Unread included: filling the gaps of a part nobody could read is the
            // worst outcome this function has.
            if (hashSinglePart(p, nullptr, digestOf(p)) != PartVerdict::Ok)
                continue;

            m_corruptionBlackBox.verifiedData(partStart, partEnd);

            const uint64 recovered = totalGapSizeInPart(p);
            fillGap(partStart, partEnd);
            removeBlockFromList(partStart, partEnd);
            dropCorruptedPart(p);

            // Recovery just produced a whole good part — MFC shares here too
            // (srchybrid/PartFile.cpp:4227).
            addToSharedFiles();

            m_corruptionLoss = (m_corruptionLoss >= recovered) ? m_corruptionLoss - recovered : 0;
            if (theApp.statistics) {
                theApp.statistics->subCorruptionLoss(recovered);
                theApp.statistics->addIchPartSaved();
            }

            logInfo(QStringLiteral("PartFile: ICH recovered %1 bytes of part %2 of '%3'")
                        .arg(recovered).arg(p).arg(fileName()));
        }
    }

}

bool PartFile::shrinkToAvoidAlreadyRequested(uint64& start, uint64& end) const
{
    // MFC ShrinkToAvoidAlreadyRequested: keep the free sub-range, requested
    // blocks and buffered data both count as taken
    auto shrink = [&](uint64 takenStart, uint64 takenEnd) {
        if (takenStart > end || takenEnd < start)
            return true;
        if (takenStart > start)
            end = takenStart - 1;
        else if (takenEnd < end)
            start = takenEnd + 1;
        else
            return false;
        return start <= end;
    };
    for (const auto* block : m_requestedBlocks)
        if (!shrink(block->startOffset, block->endOffset))
            return false;
    for (const auto& bd : m_bufferedData)
        if (!shrink(bd.start, bd.end))
            return false;
    for (const auto& bd : m_flushingData)
        if (!shrink(bd.start, bd.end))
            return false;
    return true;
}

// ===========================================================================
// adoptCompletedAICHHashSet (private) — MFC srchybrid/PartFile.cpp:1543-1550
// ===========================================================================

void PartFile::adoptCompletedAICHHashSet()
{
    if (m_completedAICHMaster.size() != kAICHHashSize) {
        logDebug(QStringLiteral("No AICH recovery set stored for completed file %1").arg(fileName()));
        return;
    }
    const AICHHash master(reinterpret_cast<const uint8*>(m_completedAICHMaster.constData()));
    m_completedAICHMaster.clear();

    // The data matched its MD4 hashes, so a different AICH root (from a link or a
    // source) was the wrong one.
    if (fileIdentifier().hasAICHHash() && fileIdentifier().getAICHHash() != master)
        logWarning(QStringLiteral("AICH hash of '%1' replaced by the one of the delivered data")
                       .arg(fileName()));
    fileIdentifier().setAICHHash(master);

    AICHRecoveryHashSet stored(fileSize());
    stored.setMasterHash(master, EAICHStatus::HashSetComplete);
    if (!stored.loadHashSet() || !fileIdentifier().setAICHHashSet(stored))
        logDebug(QStringLiteral("Failed to create AICH part hashset for %1").arg(fileName()));
    setAICHRecoverHashSetAvailable(true);
}

// Leaves no client holding this file as an A4AF alternative.
void PartFile::unlinkA4AFSources()
{
    // Copy: removeFileFromOtherLists() edits m_a4afSrcList.
    for (auto* client : std::vector(m_a4afSrcList))
        client->removeFileFromOtherLists(this);
}

namespace {

/// The little of MFC's CArchive an .sd file needs.
class ArchiveReader {
public:
    explicit ArchiveReader(QFile& file) : m_file(file) {}

    void read(void* dest, qint64 len)
    {
        if (m_file.read(static_cast<char*>(dest), len) != len)
            throw FileException("unexpected end of file");
    }
    template <typename T> T value()
    {
        T v{};
        read(&v, sizeof v);
        return qFromLittleEndian(v);
    }
    bool flag() { return value<qint32>() != 0; }   // BOOL

    /// CString: a length that widens on 0xFF / 0xFFFF, with FF FE FF marking UTF-16.
    QString string()
    {
        bool wide = false;
        quint32 len = value<quint8>();
        if (len == 0xFF) {
            len = value<quint16>();
            if (len == 0xFFFE) {
                wide = true;
                len = value<quint8>();
                if (len == 0xFF)
                    len = value<quint16>();
            }
            if (len == 0xFFFF)
                len = value<quint32>();
        }
        if (len > 4096)
            throw FileException("implausible string length");
        QByteArray raw(static_cast<qsizetype>(len) * (wide ? 2 : 1), '\0');
        read(raw.data(), raw.size());
        return wide ? QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()),
                                         static_cast<qsizetype>(len))
                    : QString::fromLocal8Bit(raw);
    }

private:
    QFile& m_file;
};

/// Position @p file just behind the next occurrence of @p needle (MFC gotostring).
bool seekBehind(QFile& file, const QByteArray& needle)
{
    const qint64 start = file.pos();
    const QByteArray rest = file.readAll();
    const qsizetype at = rest.indexOf(needle);
    if (at < 0) {
        file.seek(start);
        return false;
    }
    file.seek(start + at + needle.size());
    return true;
}

} // namespace

void PartFile::resetForImportLoad()
{
    fileIdentifier().deleteMD4Hashset();
    m_gapList.clear();
    m_corruptedParts.clear();
    clearTags();
    closeDataFile();
}

void PartFile::handleWriteFailure(const QString& error, bool diskFull)
{
    // MFC FlushBuffersExceptionHandler (PartFile.cpp:4285-4329). The buffer stays as
    // it is and nobody is blamed; the file stops asking for more until someone acts.
    if (m_destroying) {
        logError(QStringLiteral("Could not write %1: %2").arg(fileName(), error));
        return;
    }
    if (diskFull) {
        logError(QStringLiteral("Out of disk space while writing %1").arg(fileName()));
        // With the check on the file waits and the free-space sweep brings it back;
        // with it off that sweep un-parks everything, so it is a plain pause.
        if (!m_insufficient && !m_paused) {
            pauseFile(/*insufficient*/ thePrefs.checkDiskspace());
            emit m_partNotifier.outOfDiskSpace();   // once per stop, not per failed flush
        }
        return;
    }
    if (m_writeError)
        return;
    logError(QStringLiteral("Could not write %1: %2 — download paused").arg(fileName(), error));
    m_writeError = true;
    m_statusBeforeWriteError = m_status;
    setStatus(PartFileStatus::Error);
    pauseFile();
}

// MFC CPartFile::ImportShareazaTempfile (srchybrid/PartFile.cpp:457-702). The head of
// an .sd is read in order; the fragment list and the eD2K hash set sit behind data
// this does not parse and are found by searching for the file size and the file hash.
PartFileLoadResult PartFile::importShareazaTempFile(const QString& directory,
                                                    const QString& filename,
                                                    PartFileFormat* checkFormat)
{
    const QString sdPath = directory + QDir::separator() + filename;
    QFile sd(sdPath);
    if (!sd.open(QIODevice::ReadOnly)) {
        logError(QStringLiteral("Failed to open %1: %2").arg(sdPath, sd.errorString()));
        return PartFileLoadResult::FailedNoAccess;
    }

    try {
        ArchiveReader ar(sd);

        char id[3];
        ar.read(id, 3);
        if (std::memcmp(id, "SDL", 3) != 0) {
            if (checkFormat)
                *checkFormat = PartFileFormat::Unknown;
            return PartFileLoadResult::FailedOther;
        }

        const qint32 version = ar.value<qint32>();
        setFileName(ar.string(), true);
        const quint64 size = ar.value<quint64>();
        if (size == 0 || size > MAX_EMULE_FILE_SIZE)
            return PartFileLoadResult::FailedOther;
        setFileSize(size);

        // SHA1, Tiger, MD5, eD2K — each a BOOL, the hash if set, and from v31 a
        // "trusted" BOOL
        const auto skipHash = [&](qint64 len) {
            char skipped[24];
            if (ar.flag())
                ar.read(skipped, len);
        };
        skipHash(20);
        if (version >= 31) ar.flag();
        skipHash(24);
        if (version >= 31) ar.flag();
        if (version >= 22)
            skipHash(16);
        if (version >= 31) ar.flag();
        uint8 ed2k[16];
        const bool hasEd2k = version >= 13 && ar.flag();
        if (hasEd2k)
            ar.read(ed2k, sizeof ed2k);
        if (version >= 31) ar.flag();

        if (!hasEd2k) {
            logError(QStringLiteral("%1 has no eD2K hash and cannot be imported").arg(filename));
            return PartFileLoadResult::FailedOther;
        }
        setFileHash(ed2k);

        if (checkFormat) {
            *checkFormat = PartFileFormat::Shareaza;
            return PartFileLoadResult::CheckSuccess;
        }

        const qint64 basePos = sd.pos();
        m_gapList.clear();
        fileIdentifier().getRawMD4HashSet().clear();

        // Fragment list: total, remaining, count, then (begin, length) of what is missing
        const bool wideList = version >= 29;
        const quint64 sizeLE = qToLittleEndian(size);
        if (seekBehind(sd, QByteArray(reinterpret_cast<const char*>(&sizeLE), wideList ? 8 : 4))) {
            sd.seek(sd.pos() - (wideList ? 8 : 4));
            const auto word = [&]() -> quint64 {
                return wideList ? ar.value<quint64>() : ar.value<quint32>();
            };
            const quint64 total = word();
            const quint64 remaining = word();
            quint32 fragments = ar.value<quint32>();
            bool bad = total < remaining;
            while (!bad && fragments-- > 0) {
                const quint64 begin = word();
                const quint64 length = word();
                if (length == 0 || begin + length > total || begin >= size)
                    bad = true;
                else
                    addGap(begin, std::min(begin + length - 1, size - 1));
            }
            if (bad) {
                m_gapList.clear();
                logWarning(QStringLiteral("%1: fragment list is corrupt").arg(filename));
            }
        } else {
            logWarning(QStringLiteral("%1: no fragment list found").arg(filename));
            sd.seek(basePos);
        }

        // eD2K hash set: count, the file hash again, then one hash per part
        if (seekBehind(sd, QByteArray(reinterpret_cast<const char*>(ed2k), 16))) {
            sd.seek(sd.pos() - 16 - 4);
            const quint32 count = ar.value<quint32>();
            uint8 again[16];
            ar.read(again, sizeof again);
            auto& hashSet = fileIdentifier().getRawMD4HashSet();
            if (count <= partCount() + 1u) {
                for (quint32 i = 0; i < count; ++i) {
                    std::array<uint8, 16> partHash{};
                    ar.read(partHash.data(), 16);
                    hashSet.push_back(partHash);
                }
            }
            const bool ok = hashSet.size() > 1
                ? fileIdentifier().calculateMD4HashByHashSet(true, true)
                : fileIdentifier().hasExpectedMD4HashCount();
            if (!ok) {
                logWarning(QStringLiteral("%1: hash set is corrupt").arg(filename));
                fileIdentifier().deleteMD4Hashset();
            }
        } else {
            logWarning(QStringLiteral("%1: no hash set found").arg(filename));
        }
    } catch (const FileException&) {
        logError(QStringLiteral("%1 is corrupt").arg(filename));
        return PartFileLoadResult::FailedOther;
    }
    sd.close();

    // The rest would repeat loadPartFile(): write it in our format and load that.
    m_tmpPath = directory;
    m_partMetFilename = filename;
    m_fullName = sdPath;
    m_status = PartFileStatus::Empty;
    if (!savePartFile())
        return PartFileLoadResult::FailedOther;

    fileIdentifier().deleteMD4Hashset();
    m_gapList.clear();
    return loadPartFile(directory, filename);
}

} // namespace eMule
