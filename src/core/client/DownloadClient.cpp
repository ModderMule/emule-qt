/// @file DownloadClient.cpp
/// @brief UpDownClient download methods — file requests, block transfer, source swapping.
///
/// Ported from MFC srchybrid/DownloadClient.cpp.
/// Methods of UpDownClient related to download functionality.

#include "client/UpDownClient.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/DeadSourceList.h"
#include "app/AppContext.h"
#include "crypto/AICHData.h"
#include "crypto/AICHHashSet.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/ClientUDPSocket.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"



#include <algorithm>
#include <cstring>

#if __has_include(<zlib.h>)
#include <zlib.h>
#define HAVE_ZLIB 1
#else
#define HAVE_ZLIB 0
#endif

namespace eMule {

// ===========================================================================
// askForDownload
// ===========================================================================

bool UpDownClient::askForDownload()
{
    if (!m_reqFile) {
        logDebug(QStringLiteral("askForDownload: no reqFile for %1").arg(userName()));
        return false;
    }

    if (m_downloadState == DownloadState::Downloading) {
        logDebug(QStringLiteral("askForDownload: already downloading from %1").arg(userName()));
        return false;
    }

    // Check socket limit
    if (theApp.listenSocket && theApp.listenSocket->tooManySockets())
        return false;

    // Check if already waiting on queue
    if (m_downloadState == DownloadState::OnQueue)
        return false;

    return tryToConnect();
}

// ===========================================================================
// isSourceRequestAllowed
// ===========================================================================

bool UpDownClient::isSourceRequestAllowed() const
{
    return isSourceRequestAllowed(m_reqFile);
}

bool UpDownClient::isSourceRequestAllowed(PartFile* partFile, bool sourceExchangeCheck) const
{
    Q_UNUSED(partFile);

    if (m_sourceExchange1Ver == 0)
        return false;

    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Don't request sources too often
    if (m_lastAskedForSources != 0 && (curTick - m_lastAskedForSources) < SOURCECLIENTREASKS)
        return false;

    if (sourceExchangeCheck) {
        if (partFile && partFile->sourceCount() >= thePrefs.maxSourcesPerFile())
            return false;
    }

    return true;
}

// ===========================================================================
// sendFileRequest — MFC DownloadClient.cpp
// ===========================================================================

void UpDownClient::sendFileRequest()
{
    if (!m_socket || !m_reqFile)
        return;

    // MFC: Mark the time of this file request so PartFile::process() and UDP
    // reask logic know when we last asked this source.
    setLastAskedTime();

    logDebug(QStringLiteral("sendFileRequest: reqFile=%1")
                 .arg(m_reqFile ? m_reqFile->fileName() : QStringLiteral("null")));

    // OP_SETREQFILEID — set the file context on the remote (MFC DownloadClient.cpp)
    {
        SafeMemFile data;
        data.writeHash16(m_reqUpFileId.data());
        auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_SETREQFILEID);
        sendPacket(std::move(packet));
    }

    // OP_REQUESTFILENAME — MFC DownloadClient.cpp SendFileRequest()
    // We advertise extReqVer=2 in CT_EMULE_MISCOPTIONS1 (our HELLO).
    // The remote uses OUR extReqVer to parse this packet, so we must
    // include the extended fields the remote expects:
    //   extReqVer >= 1: 16-byte file hash
    //   extReqVer >= 2: + partStatus bitmap + uint16 completeSources
    {
        SafeMemFile fnData;
        fnData.writeHash16(m_reqUpFileId.data());

        // extReqVer >= 2: include our part status + complete sources
        if (m_reqFile) {
            m_reqFile->writePartStatus(fnData);
            fnData.writeUInt16(static_cast<uint16>(
                m_reqFile->completeSourcesCount()));
        }

        auto fnPacket = std::make_unique<Packet>(fnData, OP_EDONKEYPROT, OP_REQUESTFILENAME);
        sendPacket(std::move(fnPacket));
    }

    // Source Exchange 2: request sources from this peer
    // MFC uses OP_REQUESTSOURCES (0x81) in eMule proto for SX2 when
    // source exchange version >= 4. The data format is:
    //   [16-byte hash][1-byte options][optional 8-byte filesize]
    if (isSourceRequestAllowed()) {
        SafeMemFile sxData;
        sxData.writeHash16(m_reqUpFileId.data());

        uint8 options = 0;
        // TODO: set options bits as needed (available sources, etc.)
        sxData.writeUInt8(options);

        auto sxPacket = std::make_unique<Packet>(sxData, OP_EMULEPROT, OP_REQUESTSOURCES);
        sendPacket(std::move(sxPacket));
        setLastAskedForSourcesTime();
    }
}

// ===========================================================================
// sendStartupLoadReq
// ===========================================================================

void UpDownClient::sendStartupLoadReq()
{
    if (!m_socket)
        return;

    auto packet = std::make_unique<Packet>(OP_STARTUPLOADREQ, 16);
    std::memcpy(packet->pBuffer, m_reqUpFileId.data(), 16);
    packet->prot = OP_EDONKEYPROT;
    sendPacket(std::move(packet));
}

// ===========================================================================
// processFileInfo
// ===========================================================================

void UpDownClient::processFileInfo(SafeMemFile& data, PartFile* file)
{
    // Read filename from peer response
    const QString filename = data.readString(true);
    m_clientFilename = filename;

    if (file && file->fileName().isEmpty())
        file->setFileName(filename, true);
}

// ===========================================================================
// processFileStatus
// ===========================================================================

void UpDownClient::processFileStatus(bool udpPacket, SafeMemFile& data, PartFile* file)
{
    Q_UNUSED(udpPacket);

    const uint16 partCount = data.readUInt16();
    m_partCount = partCount;

    if (partCount == 0) {
        // Complete file — all parts available
        m_completeSource = true;
        m_partStatus.clear();
        return;
    }

    m_completeSource = false;
    m_partStatus.resize(partCount);

    // Read availability bitmap
    const uint16 byteCount = (partCount + 7) / 8;
    if (data.length() - data.position() < byteCount) {
        // Malformed packet — not enough data for bitmap
        m_completeSource = true;   // assume complete if bitmap missing
        m_partStatus.clear();
        m_partCount = 0;
        return;
    }
    std::vector<uint8> bitmap(byteCount);
    data.read(bitmap.data(), byteCount);

    bool allAvailable = true;
    for (uint16 i = 0; i < partCount; ++i) {
        m_partStatus[i] = (bitmap[i / 8] & (1 << (i % 8))) ? 1 : 0;
        if (!m_partStatus[i])
            allAvailable = false;
    }

    if (allAvailable)
        m_completeSource = true;

    // Update PartFile source part frequency
    if (file) {
        auto& freq = file->srcPartFrequency();
        if (freq.size() == partCount) {
            for (uint16 i = 0; i < partCount; ++i) {
                if (m_partStatus[i])
                    freq[i]++;
            }
        }

        // MFC: Check if this source has any parts we still need
        bool partsNeeded = m_completeSource;
        if (!partsNeeded) {
            for (uint16 i = 0; i < partCount; ++i) {
                if (m_partStatus[i] && !file->isComplete(i)) {
                    partsNeeded = true;
                    break;
                }
            }
        }

        if (!partsNeeded) {
            setDownloadState(DownloadState::NoNeededParts);
            swapToAnotherFile(
                QStringLiteral("A4AF for NNP file. processFileStatus() TCP"),
                true, false, false, nullptr, true, true);
        }
    }
}

// ===========================================================================
// processHashSet
// ===========================================================================

void UpDownClient::processHashSet(const uint8* data, uint32 size, bool fileIdentifiers)
{
    if (!data || size < 16 || !m_reqFile) {
        m_hashsetRequestingMD4 = false;
        m_hashsetRequestingAICH = false;
        return;
    }

    SafeMemFile file(data, size);

    if (fileIdentifiers) {
        // New-style: read via FileIdentifier with MD4 + AICH hashsets
        auto& ident = m_reqFile->fileIdentifier();
        bool md4 = false;
        bool aich = false;

        if (!ident.readHashSetsFromPacket(file, md4, aich)) {
            logDebug(QStringLiteral("processHashSet: readHashSetsFromPacket failed from %1").arg(userName()));
            m_hashsetRequestingMD4 = false;
            m_hashsetRequestingAICH = false;
            return;
        }

        if (md4) {
            // Verify MD4 hashset against file hash
            if (!ident.calculateMD4HashByHashSet(true, true)) {
                logDebug(QStringLiteral("processHashSet: MD4 hashset verification failed from %1").arg(userName()));
            }
            m_hashsetRequestingMD4 = false;
        }

        if (aich) {
            if (!ident.verifyAICHHashSet()) {
                logDebug(QStringLiteral("processHashSet: AICH hashset verification failed from %1").arg(userName()));
            }
            m_hashsetRequestingAICH = false;
        }
    } else {
        // Legacy: read file hash + part hashes
        uint8 fileHash[16];
        file.readHash16(fileHash);

        // Verify this is the file we requested
        if (!md4equ(fileHash, m_reqFile->fileHash())) {
            logDebug(QStringLiteral("processHashSet: hash mismatch from %1").arg(userName()));
            m_hashsetRequestingMD4 = false;
            return;
        }

        auto& ident = m_reqFile->fileIdentifier();
        if (!ident.loadMD4HashsetFromFile(file, true)) {
            logDebug(QStringLiteral("processHashSet: loadMD4HashsetFromFile failed from %1").arg(userName()));
            m_hashsetRequestingMD4 = false;
            return;
        }

        // Verify the hashset
        if (!ident.calculateMD4HashByHashSet(true, true)) {
            logDebug(QStringLiteral("processHashSet: MD4 verification failed from %1").arg(userName()));
        }

        m_hashsetRequestingMD4 = false;
    }

    // If hashset obtained, proceed with download
    if (m_downloadState == DownloadState::ReqHashSet) {
        sendStartupLoadReq();
    }
}

// ===========================================================================
// processAcceptUpload
// ===========================================================================

void UpDownClient::processAcceptUpload()
{
    m_remoteQueueFull = false;
    logDebug(QStringLiteral("processAcceptUpload: downloadState=%1 from %2")
                 .arg(static_cast<int>(m_downloadState)).arg(userName()));

    // MFC: Check file is valid and ready for download
    if (m_reqFile && !m_reqFile->isStopped() &&
        (m_reqFile->status() == PartFileStatus::Ready ||
         m_reqFile->status() == PartFileStatus::Empty))
    {
        m_sentCancelTransfer = false;  // MFC: SetSentCancelTransfer(0)

        if (m_downloadState == DownloadState::OnQueue) {
            setDownloadState(DownloadState::Downloading);
            m_downStartTime = static_cast<uint32>(getTickCount());

            logDebug(QStringLiteral("processAcceptUpload: state → Downloading, sending block requests"));
            sendBlockRequests();
        }
    } else {
        // File is stopped or invalid — cancel the transfer
        logDebug(QStringLiteral("processAcceptUpload: file not ready, sending cancel"));
        sendCancelTransfer();
        setDownloadState(m_reqFile == nullptr || m_reqFile->isStopped()
                             ? DownloadState::None : DownloadState::OnQueue);
    }
}

// ===========================================================================
// addRequestForAnotherFile
// ===========================================================================

bool UpDownClient::addRequestForAnotherFile(PartFile* file)
{
    if (!file)
        return false;

    // Check if already in other requests list
    for (const auto* f : m_otherRequests) {
        if (f == file)
            return false;
    }

    m_otherRequests.push_back(file);
    return true;
}

// ===========================================================================
// clearDownloadBlockRequests
// ===========================================================================

void UpDownClient::clearDownloadBlockRequests()
{
    for (auto* pending : m_pendingBlocks) {
        // MFC: CPartFile::RemoveBlockFromList — return block range to the
        // PartFile so getNextRequestedBlock() can re-issue it to another source.
        if (m_reqFile && pending->block) {
            m_reqFile->removeBlockFromList(pending->block->startOffset,
                                           pending->block->endOffset);
        }
        clearPendingBlockRequest(pending);
        delete pending;
    }
    m_pendingBlocks.clear();
}

// ===========================================================================
// createBlockRequests
// ===========================================================================

void UpDownClient::createBlockRequests(int blockCount)
{
    if (!m_reqFile || blockCount <= 0)
        return;

    // Don't exceed 3 pending blocks total
    auto currentPending = static_cast<int>(m_pendingBlocks.size());
    int toRequest = std::min(blockCount, 3 - currentPending);
    if (toRequest <= 0)
        return;

    Requested_Block_Struct* blocks[3] = {};
    int count = toRequest;

    if (m_reqFile->getNextRequestedBlock(this, blocks, count)) {
        for (int i = 0; i < count; ++i) {
            auto* pending = new Pending_Block_Struct;
            pending->block = blocks[i];
            pending->queued = 0; // not yet sent, sendBlockRequests will mark as sent
            m_pendingBlocks.push_back(pending);
        }
    }
}

// ===========================================================================
// sendBlockRequests
// ===========================================================================

void UpDownClient::sendBlockRequests()
{
    if (!m_socket) {
        logDebug(QStringLiteral("sendBlockRequests: no socket"));
        return;
    }

    if (m_downloadState != DownloadState::Downloading) {
        logDebug(QStringLiteral("sendBlockRequests: wrong state %1").arg(static_cast<int>(m_downloadState)));
        return;
    }

    // MFC resets the block-receive timer here to prevent download timeout
    // before the first block arrives (especially on slow connections)
    m_lastBlockReceived = static_cast<uint32>(getTickCount());

    // Create new block requests if needed
    createBlockRequests(3);

    logDebug(QStringLiteral("sendBlockRequests: pendingBlocks=%1 from %2")
                 .arg(m_pendingBlocks.size()).arg(userName()));

    if (m_pendingBlocks.empty()) {
        logDebug(QStringLiteral("sendBlockRequests: no blocks available — NoNeededParts"));
        sendCancelTransfer();
        setDownloadState(DownloadState::NoNeededParts);
        return;
    }

    // Collect unsent blocks (up to 3) — MFC collects pointers first, then
    // marks as queued before writing offsets so that a subsequent call never
    // re-requests the same blocks.
    Pending_Block_Struct* pblock[3] = {};
    int nr = 0;
    for (auto* pending : m_pendingBlocks) {
        if (nr >= 3)
            break;
        if (!pending->block || pending->queued)
            continue;
        pblock[nr++] = pending;
        pending->queued = 1;
    }

    if (nr == 0) {
        logDebug(QStringLiteral("sendBlockRequests: all blocks already queued"));
        return;
    }

    // Build request packet: hash + 3 start offsets + 3 end offsets
    SafeMemFile data;
    data.writeHash16(m_reqUpFileId.data());

    for (int i = 0; i < 3; ++i) {
        const uint64 start = (i < nr) ? pblock[i]->block->startOffset : 0;
        if (m_supportsLargeFiles)
            data.writeUInt64(start);
        else
            data.writeUInt32(static_cast<uint32>(start));
    }

    for (int i = 0; i < 3; ++i) {
        // MFC sends exclusive end offset on the wire: endOffset + 1
        const uint64 end = (i < nr) ? pblock[i]->block->endOffset + 1 : 0;
        if (m_supportsLargeFiles)
            data.writeUInt64(end);
        else
            data.writeUInt32(static_cast<uint32>(end));
    }

    // OP_REQUESTPARTS uses OP_EDONKEYPROT, OP_REQUESTPARTS_I64 uses OP_EMULEPROT
    const uint8 opcode = m_supportsLargeFiles ? OP_REQUESTPARTS_I64 : OP_REQUESTPARTS;
    const uint8 proto  = m_supportsLargeFiles ? OP_EMULEPROT : OP_EDONKEYPROT;
    auto packet = std::make_unique<Packet>(data, proto, opcode);

    for (int i = 0; i < nr; ++i) {
        logDebug(QStringLiteral("sendBlockRequests: block[%1] start=%2 end=%3 to %4")
                     .arg(i).arg(pblock[i]->block->startOffset)
                     .arg(pblock[i]->block->endOffset).arg(userName()));
    }

    sendPacket(std::move(packet));
}

// ===========================================================================
// processBlockPacket — MFC DownloadClient.cpp ProcessBlockPacket
// ===========================================================================

void UpDownClient::processBlockPacket(const uint8* data, uint32 size,
                                       bool packed, bool i64Offsets)
{
    if (!data || size == 0)
        return;

    // Ignore if not in a downloading state
    if (m_downloadState != DownloadState::Downloading &&
        m_downloadState != DownloadState::NoNeededParts) {
        return;
    }

    m_lastBlockReceived = static_cast<uint32>(getTickCount());

    // Parse the packet header using SafeMemFile, matching MFC approach.
    // Header layout depends on packed vs. unpacked and 32-bit vs. 64-bit offsets:
    //   hash(16) + startOffset(4|8) + [compressedSize(4) | endOffset(4|8)]
    SafeMemFile packet(data, size);

    uint8 fileHash[16];
    packet.readHash16(fileHash);
    uint32 nHeaderSize = 16;

    // Verify this data is for the correct file
    if (!m_reqFile || !md4equ(data, m_reqFile->fileHash())) {
        logDebug(QStringLiteral("processBlockPacket: wrong file ID from %1").arg(userName()));
        return;
    }

    // Read start position
    uint64 nStartPos;
    if (i64Offsets) {
        nStartPos = packet.readUInt64();
        nHeaderSize += 8;
    } else {
        nStartPos = packet.readUInt32();
        nHeaderSize += 4;
    }

    // Read end position — differs for packed vs. unpacked
    uint64 nEndPos;
    if (packed) {
        // For compressed packets: next 4 bytes are compressed size, skip them.
        // The actual end position is calculated from remaining packet data.
        packet.seek(sizeof(uint32), SEEK_CUR);
        nHeaderSize += 4;
        nEndPos = nStartPos + (size - nHeaderSize);
    } else if (i64Offsets) {
        nEndPos = packet.readUInt64();
        nHeaderSize += 8;
    } else {
        nEndPos = packet.readUInt32();
        nHeaderSize += 4;
    }

    const uint32 uTransferredFileDataSize = size - nHeaderSize;

    // Validate: end must be > start and data size must match
    if (nEndPos <= nStartPos || uTransferredFileDataSize != (nEndPos - nStartPos)) {
        logDebug(QStringLiteral("processBlockPacket: bad data block from %1").arg(userName()));
        return;
    }

    // Update transfer statistics
    m_transferredDown += uTransferredFileDataSize;
    m_curSessionDown += uTransferredFileDataSize;

    // Add to rate averaging
    TransferredData td;
    td.dataLen = uTransferredFileDataSize;
    td.timestamp = m_lastBlockReceived;
    m_averageDDR.push_back(td);
    m_sumForAvgDownDataRate += uTransferredFileDataSize;

    // Move end back by one (MFC uses inclusive end offset)
    --nEndPos;

    // Find the matching pending block for this data range
    Pending_Block_Struct* curBlock = nullptr;
    auto itPos = m_pendingBlocks.end();
    for (auto it = m_pendingBlocks.begin(); it != m_pendingBlocks.end(); ++it) {
        if ((*it)->block &&
            (*it)->block->startOffset <= nStartPos &&
            (*it)->block->endOffset >= nStartPos)
        {
            curBlock = *it;
            itPos = it;
            break;
        }
    }

    if (!curBlock) {
        // No matching pending block — drop packet
        return;
    }

    if (curBlock->zStreamError) {
        // Previous decompression error — discard and remove block
        m_reqFile->removeBlockFromList(curBlock->block->startOffset, curBlock->block->endOffset);
        return;
    }

    m_lastBlockOffset = nStartPos;

    uint32 lenWritten = 0;

    if (!packed) {
        // --- Uncompressed data ---
        // Security check: received end must not exceed requested end
        if (nEndPos > curBlock->block->endOffset) {
            logDebug(QStringLiteral("processBlockPacket: block exceeds requested boundary from %1").arg(userName()));
            m_reqFile->removeBlockFromList(curBlock->block->startOffset, curBlock->block->endOffset);
            return;
        }

        m_reqFile->writeToBuffer(uTransferredFileDataSize,
                                  data + nHeaderSize,
                                  nStartPos, nEndPos,
                                  curBlock->block);
        lenWritten = uTransferredFileDataSize;
    } else {
        // --- Compressed data ---
#if HAVE_ZLIB
        // Allocate initial decompression buffer
        uint32 lenUnzipped = std::min(size * 2, static_cast<uint32>(EMBLOCKSIZE + 300));
        uint8* unzipped = new uint8[lenUnzipped];

        int result = unzip(curBlock, data + nHeaderSize, uTransferredFileDataSize,
                           &unzipped, &lenUnzipped);

        if (result == Z_OK && static_cast<int>(lenUnzipped) >= 0) {
            if (lenUnzipped > 0) {
                // Calculate write positions from cumulative decompression progress
                // (MFC: nStartPos = block->StartOffset + totalUnzipped - lenUnzipped)
                uint64 writeStart = curBlock->block->startOffset
                                  + curBlock->totalUnzipped - lenUnzipped;
                uint64 writeEnd = curBlock->block->startOffset
                                + curBlock->totalUnzipped - 1;

                if (writeStart > curBlock->block->endOffset ||
                    writeEnd > curBlock->block->endOffset)
                {
                    logDebug(QStringLiteral("processBlockPacket: decompressed data exceeds block boundary from %1").arg(userName()));
                    m_reqFile->removeBlockFromList(curBlock->block->startOffset, curBlock->block->endOffset);
                } else {
                    m_reqFile->writeToBuffer(uTransferredFileDataSize,
                                              unzipped,
                                              writeStart, writeEnd,
                                              curBlock->block);
                    lenWritten = lenUnzipped;
                }
            }
        } else {
            logDebug(QStringLiteral("processBlockPacket: decompression error %1 from %2").arg(result).arg(userName()));
            m_reqFile->removeBlockFromList(curBlock->block->startOffset, curBlock->block->endOffset);

            // Clean up the failed zstream
            if (curBlock->zStream) {
                inflateEnd(curBlock->zStream);
                delete curBlock->zStream;
                curBlock->zStream = nullptr;
            }
            curBlock->zStreamError = true;
            curBlock->totalUnzipped = 0;
        }
        delete[] unzipped;
#else
        logError(QStringLiteral("Received compressed block but zlib not available"));
        return;
#endif
    }

    // If data was written, check for block completion and request more
    if (lenWritten > 0 && !m_pendingBlocks.empty() && curBlock->block) {
        curBlock->block->transferredByClient += lenWritten;

        // Check if block is complete (end of decompressed/uncompressed data matches block end)
        bool complete = false;
        if (packed) {
            // For compressed: complete when zStream ended (set to null by unzip on Z_STREAM_END)
            complete = (curBlock->zStream == nullptr && !curBlock->zStreamError);
        } else {
            complete = (nEndPos >= curBlock->block->endOffset);
        }

        if (complete) {
            m_pendingBlocks.erase(itPos);

            // Remove from PartFile's requested-blocks list so the range
            // is no longer considered "already requested" by other sources.
            if (m_reqFile && curBlock->block) {
                m_reqFile->removeBlockFromList(curBlock->block->startOffset,
                                               curBlock->block->endOffset);
            }

            clearPendingBlockRequest(curBlock);
            delete curBlock;

            // Request next blocks
            sendBlockRequests();
        }
    }
}

// ===========================================================================
// clearPendingBlockRequest (private)
// ===========================================================================

void UpDownClient::clearPendingBlockRequest(const Pending_Block_Struct* pending)
{
    if (!pending)
        return;

#if HAVE_ZLIB
    if (pending->zStream) {
        inflateEnd(pending->zStream);
        delete pending->zStream;
    }
#endif

    delete pending->block;
}

// ===========================================================================
// sendCancelTransfer
// ===========================================================================

void UpDownClient::sendCancelTransfer()
{
    if (!m_sentCancelTransfer) {
        auto packet = std::make_unique<Packet>(OP_CANCELTRANSFER, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
        m_sentCancelTransfer = true;
    }
}

// ===========================================================================
// startDownload
// ===========================================================================

void UpDownClient::startDownload()
{
    setDownloadState(DownloadState::Downloading);
    m_downStartTime = static_cast<uint32>(getTickCount());
    m_sentCancelTransfer = false;
    sendBlockRequests();
}

// ===========================================================================
// sendHashSetRequest
// ===========================================================================

void UpDownClient::sendHashSetRequest()
{
    if (!m_socket)
        return;

    if (m_hashsetRequestingMD4 || m_hashsetRequestingAICH)
        return;

    m_hashsetRequestingMD4 = true;
    setDownloadState(DownloadState::ReqHashSet);

    SafeMemFile data;
    data.writeHash16(m_reqUpFileId.data());

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HASHSETREQUEST);
    sendPacket(std::move(packet));
}

// ===========================================================================
// calculateDownloadRate
// ===========================================================================

uint32 UpDownClient::calculateDownloadRate()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Remove old entries (older than 10 seconds)
    while (!m_averageDDR.empty() && (curTick - m_averageDDR.front().timestamp) > 10000) {
        m_sumForAvgDownDataRate -= m_averageDDR.front().dataLen;
        m_averageDDR.pop_front();
    }

    if (!m_averageDDR.empty()) {
        const uint32 elapsed = curTick - m_averageDDR.front().timestamp;
        if (elapsed > 0)
            m_downDatarate = static_cast<uint32>((m_sumForAvgDownDataRate * 1000) / elapsed);
        else
            m_downDatarate = 0;
    } else {
        m_downDatarate = 0;
    }

    return m_downDatarate;
}

// ===========================================================================
// checkDownloadTimeout
// ===========================================================================

void UpDownClient::checkDownloadTimeout()
{
    if (m_downloadState != DownloadState::Downloading)
        return;

    const uint32 curTick = static_cast<uint32>(getTickCount());

    if (m_lastBlockReceived == 0)
        m_lastBlockReceived = curTick;

    if ((curTick - m_lastBlockReceived) > DOWNLOADTIMEOUT) {
        logDebug(QStringLiteral("Download timeout for %1").arg(userName()));
        disconnected(QStringLiteral("Download timeout"));
    }
}

// ===========================================================================
// availablePartCount
// ===========================================================================

uint16 UpDownClient::availablePartCount() const
{
    uint16 count = 0;
    for (const auto status : m_partStatus) {
        if (status != 0)
            ++count;
    }
    return count;
}

// ===========================================================================
// isPartAvailable
// ===========================================================================

bool UpDownClient::isPartAvailable(uint32 part) const
{
    if (part >= m_partStatus.size())
        return false;
    return m_partStatus[part] != 0;
}

// ===========================================================================
// setRemoteQueueRank
// ===========================================================================

void UpDownClient::setRemoteQueueRank(uint32 rank, bool updateDisplay)
{
    Q_UNUSED(updateDisplay);
    m_remoteQueueRank = rank;
    m_remoteQueueFull = false;
}

// ===========================================================================
// UDP reask methods
// ===========================================================================

void UpDownClient::udpReaskACK(uint16 newQR)
{
    m_reaskPending = false;
    setRemoteQueueRank(newQR);
}

void UpDownClient::udpReaskFNF()
{
    m_reaskPending = false;

    // File not found — remove source
    if (theApp.downloadQueue)
        theApp.downloadQueue->removeSource(this);
    setDownloadState(DownloadState::None);
}

void UpDownClient::udpReaskForDownload()
{
    if (!m_reqFile || !supportsUDP())
        return;

    if (m_reaskPending)
        return;

    // Check UDP packet success rate — abort if failure rate > 30%
    if (m_totalUDPPackets > 5 && m_failedUDPPackets > 0) {
        if ((m_failedUDPPackets * 100 / m_totalUDPPackets) > 30)
            return;
    }

    m_reaskPending = true;
    m_totalUDPPackets++;

    // Build OP_REASKFILEPING packet: file hash + part status
    SafeMemFile data;
    data.writeHash16(m_reqFile->fileHash());

    // If source exchange v3+, include our part status for better source matching
    if (m_sourceExchange1Ver >= 3 && m_reqFile->partCount() > 0) {
        const uint16 parts = m_reqFile->partCount();
        data.writeUInt16(parts);

        const uint16 byteCount = (parts + 7) / 8;
        std::vector<uint8> bitmap(byteCount, 0);
        for (uint16 i = 0; i < parts; ++i) {
            if (m_reqFile->isComplete(i))
                bitmap[i / 8] |= (1 << (i % 8));
        }
        data.write(bitmap.data(), byteCount);
    }

    // Include complete source count if extended requests
    if (m_extendedRequestsVer >= 2) {
        data.writeUInt16(m_reqFile->sourceCount());
    }

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_REASKFILEPING);

    // Send via client UDP socket
    if (theApp.clientUDP) {
        const bool encrypt = supportsCryptLayer() && thePrefs.cryptLayerSupported();
        theApp.clientUDP->sendPacket(std::move(packet), m_connectIP, m_udpPort,
                                     encrypt, m_userHash.data(), false, 0);
    }
}

// ===========================================================================
// isValidSource
// ===========================================================================

bool UpDownClient::isValidSource() const
{
    return m_downloadState != DownloadState::None &&
           m_downloadState != DownloadState::Error;
}

// ===========================================================================
// Source swapping
// ===========================================================================

bool UpDownClient::swapToAnotherFile(const QString& reason, bool ignoreNoNeeded,
                                      bool ignoreSuspensions, bool removeCompletely,
                                      PartFile* toFile, bool allowSame, bool isAboutToAsk)
{
    Q_UNUSED(isAboutToAsk);

    if (!m_reqFile)
        return false;

    // Determine aggressive swapping mode
    const bool aggressiveSwapping = (removeCompletely || ignoreNoNeeded);

    // If specific target file given, try to swap directly
    if (toFile) {
        if (toFile == m_reqFile && !allowSame)
            return false;
        if (toFile != m_reqFile) {
            bool skipped = false;
            if (swapToRightFile(toFile, m_reqFile, ignoreSuspensions,
                                isInNoNeededList(toFile),
                                m_downloadState == DownloadState::NoNeededParts,
                                skipped, aggressiveSwapping))
            {
                return doSwap(toFile, removeCompletely, reason);
            }
        }
        return false;
    }

    // Find best file to swap to from our other-requests lists
    PartFile* bestFile = nullptr;
    bool bestSkippedSrcExch = false;

    // Check m_otherRequests list
    for (auto* otherFile : m_otherRequests) {
        if (otherFile == m_reqFile)
            continue;
        bool skipped = false;
        if (swapToRightFile(otherFile, bestFile ? bestFile : m_reqFile,
                            ignoreSuspensions, false,
                            (m_downloadState == DownloadState::NoNeededParts),
                            skipped, aggressiveSwapping))
        {
            bestFile = otherFile;
            bestSkippedSrcExch = skipped;
        }
    }

    // Check m_otherNoNeeded list if ignoring no-needed
    if (ignoreNoNeeded) {
        for (auto* otherFile : m_otherNoNeeded) {
            if (otherFile == m_reqFile)
                continue;
            bool skipped = false;
            if (swapToRightFile(otherFile, bestFile ? bestFile : m_reqFile,
                                ignoreSuspensions, true,
                                (m_downloadState == DownloadState::NoNeededParts),
                                skipped, aggressiveSwapping))
            {
                bestFile = otherFile;
                bestSkippedSrcExch = skipped;
            }
        }
    }

    if (bestFile) {
        if (bestSkippedSrcExch)
            setSwapForSourceExchangeTick();
        return doSwap(bestFile, removeCompletely, reason);
    }

    return false;
}

bool UpDownClient::doSwap(PartFile* swapTo, bool removeCompletely, const QString& reason)
{
    if (!swapTo || !m_reqFile || swapTo == m_reqFile)
        return false;

    PartFile* oldFile = m_reqFile;

    // Remove this client from old file's source list
    oldFile->removeSource(this);

    // Remove from new file's A4AF source list
    auto& a4afList = swapTo->a4afSrcList();
    a4afList.erase(std::remove(a4afList.begin(), a4afList.end(), this), a4afList.end());

    // If not removing completely, add to old file's A4AF list
    if (!removeCompletely) {
        oldFile->a4afSrcList().push_back(this);

        // Add to appropriate other-requests list
        if (m_downloadState == DownloadState::NoNeededParts) {
            m_otherNoNeeded.push_back(oldFile);
        } else {
            m_otherRequests.push_back(oldFile);
        }
    }

    // Remove old file from our other-requests/no-needed lists
    m_otherRequests.remove(swapTo);
    m_otherNoNeeded.remove(swapTo);

    // Set the new request file
    m_reqFile = swapTo;
    if (m_reqFile->fileHash()) {
        md4cpy(m_reqUpFileId.data(), m_reqFile->fileHash());
    }

    // Add to new file's source list
    swapTo->addSource(this);

    // Reset download state for new file
    resetFileStatusInfo();
    m_sentCancelTransfer = false;

    logDebug(QStringLiteral("Source swap: %1 from %2 to %3 reason: %4").arg(userName(), oldFile->fileName(), swapTo->fileName(), reason));

    return true;
}

bool UpDownClient::swapToRightFile(PartFile* swapTo, PartFile* curFile, bool ignoreSuspensions,
                                    bool swapToIsNNP, bool curFileIsNNP,
                                    bool& wasSkippedDueToSrcExch,
                                    bool aggressiveSwapping)
{
    wasSkippedDueToSrcExch = false;

    if (!swapTo || !curFile)
        return false;

    // Don't swap if suspended (unless ignoring suspensions)
    if (!ignoreSuspensions && isSwapSuspended(swapTo))
        return false;

    // Source count check — prefer files needing more sources
    const int swapToSrcCount = swapTo->sourceCount();
    const int curFileSrcCount = curFile->sourceCount();
    const int maxSources = static_cast<int>(thePrefs.maxSourcesPerFile());

    // If swapTo already has max sources, don't swap
    if (swapToSrcCount >= maxSources)
        return false;

    // NNP (No Needed Parts) handling:
    // Prefer swapping away from NNP files to non-NNP files
    if (curFileIsNNP && !swapToIsNNP)
        return true;  // Current is NNP, target is not — swap

    if (!curFileIsNNP && swapToIsNNP) {
        if (!aggressiveSwapping)
            return false;  // Current is not NNP, target is — don't swap unless aggressive
    }

    // Source exchange: avoid swapping too frequently for source exchange
    if (!aggressiveSwapping && recentlySwappedForSourceExchange()) {
        wasSkippedDueToSrcExch = true;
        return false;
    }

    // Prefer the file with fewer sources (needs us more)
    if (swapToSrcCount < curFileSrcCount)
        return true;

    // Equal source count: prefer higher priority file
    if (swapToSrcCount == curFileSrcCount) {
        if (swapTo->upPriority() > curFile->upPriority())
            return true;
    }

    return false;
}

void UpDownClient::dontSwapTo(PartFile* file)
{
    if (!file)
        return;

    FileStamp stamp;
    stamp.file = file;
    stamp.timestamp = static_cast<uint32>(getTickCount());
    m_dontSwap.push_back(stamp);
}

bool UpDownClient::isSwapSuspended(const PartFile* file, bool allowShortReaskTime,
                                    bool fileIsNNP) const
{
    Q_UNUSED(allowShortReaskTime);
    Q_UNUSED(fileIsNNP);

    if (!file)
        return false;

    const uint32 curTick = static_cast<uint32>(getTickCount());

    for (const auto& stamp : m_dontSwap) {
        if (stamp.file == file) {
            return (curTick - stamp.timestamp) < PURGESOURCESWAPSTOP;
        }
    }

    return false;
}

bool UpDownClient::isInNoNeededList(const PartFile* file) const
{
    for (const auto* f : m_otherNoNeeded) {
        if (f == file)
            return true;
    }
    return false;
}

bool UpDownClient::recentlySwappedForSourceExchange() const
{
    if (m_lastSwapForSourceExchangeTick == 0)
        return false;
    const uint32 curTick = static_cast<uint32>(getTickCount());
    return (curTick - m_lastSwapForSourceExchangeTick) < SEC2MS(30);
}

void UpDownClient::setSwapForSourceExchangeTick()
{
    m_lastSwapForSourceExchangeTick = static_cast<uint32>(getTickCount());
}

// ===========================================================================
// timeUntilReask
// ===========================================================================

uint32 UpDownClient::timeUntilReask() const
{
    return timeUntilReask(m_reqFile);
}

uint32 UpDownClient::timeUntilReask(const PartFile* file) const
{
    const uint32 lastAsk = lastAskedTime(file);
    if (lastAsk == 0)
        return 0;

    // MFC: NNP sources get doubled reask time to save connections and traffic
    uint32 reaskTime = FILEREASKTIME;
    if ((file == m_reqFile && m_downloadState == DownloadState::NoNeededParts)
        || (file != m_reqFile && isInNoNeededList(file)))
    {
        reaskTime = FILEREASKTIME * 2;
    }

    const uint32 curTick = static_cast<uint32>(getTickCount());
    const uint32 elapsed = curTick - lastAsk;

    if (elapsed >= reaskTime)
        return 0;
    return reaskTime - elapsed;
}

uint32 UpDownClient::lastAskedTime(const PartFile* file) const
{
    if (file) {
        auto it = m_fileReaskTimes.find(file);
        if (it != m_fileReaskTimes.end())
            return it->second;
    }
    return m_lastAskedForSources;
}

void UpDownClient::setLastAskedTime()
{
    m_lastAskedForSources = static_cast<uint32>(getTickCount());
    if (m_reqFile)
        m_fileReaskTimes[m_reqFile] = m_lastAskedForSources;
}

// ===========================================================================
// updateDisplayedInfo
// ===========================================================================

void UpDownClient::updateDisplayedInfo(bool force)
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    if (!force) {
        // Rate-limit display updates
        if ((curTick - m_lastRefreshedDLDisplay) < MIN2MS(1) + m_randomUpdateWait)
            return;
    }

    m_lastRefreshedDLDisplay = curTick;
    emit updateDisplayedInfoRequested();
}

// ===========================================================================
// AICH
// ===========================================================================

const AICHHash* UpDownClient::reqFileAICHHash() const
{
    if (!m_reqFile)
        return nullptr;
    if (!m_reqFile->aichRecoveryHashSet().hasValidMasterHash())
        return nullptr;
    return &m_reqFile->aichRecoveryHashSet().getMasterHash();
}

void UpDownClient::sendAICHRequest(PartFile* forFile, uint16 part)
{
    if (!forFile || !m_socket)
        return;

    m_aichRequested = true;

    // Build OP_AICHREQUEST: file hash (16) + part number (uint16) + master hash (20)
    SafeMemFile data;
    data.writeHash16(forFile->fileHash());
    data.writeUInt16(part);
    forFile->aichRecoveryHashSet().getMasterHash().write(data);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_AICHREQUEST);
    safeConnectAndSendPacket(std::move(packet));
}

void UpDownClient::processAICHAnswer(const uint8* data, uint32 size)
{
    if (!data || size < 16 + 2 + kAICHHashSize) {
        m_aichRequested = false;
        return;
    }

    if (!m_aichRequested) {
        logDebug(QStringLiteral("processAICHAnswer: unrequested AICH answer from %1").arg(userName()));
        return;
    }
    m_aichRequested = false;

    SafeMemFile file(data, size);

    // Read file hash and part number
    uint8 fileHash[16];
    file.readHash16(fileHash);
    const uint16 partNumber = file.readUInt16();

    // Read and verify the master hash
    AICHHash masterHash(file);

    // Find the PartFile this belongs to
    PartFile* partFile = nullptr;
    if (theApp.downloadQueue)
        partFile = theApp.downloadQueue->fileByID(fileHash);

    if (!partFile) {
        logDebug(QStringLiteral("processAICHAnswer: file not found for AICH answer"));
        return;
    }

    auto& recoveryHashSet = partFile->aichRecoveryHashSet();

    // Verify master hash matches
    if (!recoveryHashSet.hasValidMasterHash() ||
        recoveryHashSet.getMasterHash() != masterHash)
    {
        logDebug(QStringLiteral("processAICHAnswer: master hash mismatch from %1").arg(userName()));
        return;
    }

    // Read recovery data
    if (!recoveryHashSet.readRecoveryData(
            static_cast<uint64>(partNumber) * PARTSIZE, file))
    {
        logDebug(QStringLiteral("processAICHAnswer: readRecoveryData failed from %1").arg(userName()));
        return;
    }

    // Notify PartFile that AICH recovery data is available
    partFile->aichRecoveryDataAvailable(partNumber);
}

void UpDownClient::processAICHRequest(const uint8* data, uint32 size)
{
    if (!data || size < 16 + 2 + kAICHHashSize || !m_socket)
        return;

    SafeMemFile file(data, size);

    // Read file hash, part number, and master hash
    uint8 fileHash[16];
    file.readHash16(fileHash);
    const uint16 partNumber = file.readUInt16();
    AICHHash masterHash(file);

    // Look up file in shared files
    KnownFile* knownFile = nullptr;
    if (theApp.sharedFileList)
        knownFile = theApp.sharedFileList->getFileByID(fileHash);

    if (!knownFile || !knownFile->isAICHRecoverHashSetAvailable()) {
        logDebug(QStringLiteral("processAICHRequest: file not found or AICH not available"));
        return;
    }

    // Verify the file has an AICH hash in its identifier
    const auto& ident = knownFile->fileIdentifier();
    if (!ident.hasAICHHash() || ident.getAICHHash() != masterHash) {
        logDebug(QStringLiteral("processAICHRequest: master hash mismatch"));
        return;
    }

    // Validate part number
    const uint64 fileSize = knownFile->fileSize();
    if (static_cast<uint64>(partNumber) * PARTSIZE >= fileSize) {
        logDebug(QStringLiteral("processAICHRequest: invalid part number %1").arg(partNumber));
        return;
    }

    // Create a temporary recovery hash set to generate recovery data.
    // createPartRecoveryData will load the hash tree from known2_64.met.
    AICHRecoveryHashSet recoveryHashSet(fileSize);
    recoveryHashSet.setMasterHash(masterHash, EAICHStatus::Verified);

    SafeMemFile response;
    response.writeHash16(fileHash);
    response.writeUInt16(partNumber);
    masterHash.write(response);

    if (!recoveryHashSet.createPartRecoveryData(
            static_cast<uint64>(partNumber) * PARTSIZE, response))
    {
        logDebug(QStringLiteral("processAICHRequest: createPartRecoveryData failed"));
        return;
    }

    auto packet = std::make_unique<Packet>(response, OP_EMULEPROT, OP_AICHANSWER);
    sendPacket(std::move(packet));
}

void UpDownClient::processAICHFileHash(SafeMemFile& data, PartFile* file)
{
    if (!file)
        return;

    // Read the AICH master hash from the peer
    AICHHash masterHash(data);

    auto& recoveryHashSet = file->aichRecoveryHashSet();

    if (recoveryHashSet.hasValidMasterHash() &&
        recoveryHashSet.getStatus() == EAICHStatus::Verified)
    {
        // We already have a verified hash — check if it matches
        if (recoveryHashSet.getMasterHash() != masterHash) {
            logDebug(QStringLiteral("processAICHFileHash: hash mismatch from %1 for %2").arg(userName(), file->fileName()));
            // Add to dead source list — this source has wrong AICH hash
            if (theApp.clientList) {
                DeadSourceKey key;
                key.hash = m_userHash;
                key.serverIP = m_serverIP;
                key.userID = m_userIDHybrid;
                key.port = m_userPort;
                key.kadPort = m_kadPort;
                theApp.clientList->globalDeadSourceList.addDeadSource(key, hasLowID());
            }
            return;
        }
    }

    // Report hash to trust system for consensus building
    recoveryHashSet.untrustedHashReceived(masterHash, m_connectIP);
}

// ===========================================================================
// unzip — zlib decompression — faithful port of MFC CUpDownClient::unzip
//
// Called once per compressed sub-packet (~10 KB).  The z_stream persists
// across calls in block->zStream until Z_STREAM_END is reached for the
// full 180 KB block.  block->totalUnzipped tracks cumulative output via
// zS->total_out so that the caller can compute write offsets.
// ===========================================================================

#if HAVE_ZLIB
int UpDownClient::unzip(Pending_Block_Struct* block, const uint8* zipped,
                         uint32 lenZipped, uint8** unzipped, uint32* lenUnzipped,
                         int iRecursion)
{
    int err = Z_DATA_ERROR;

    if (!block || !zipped || !unzipped || !lenUnzipped)
        return err;

    z_stream* zS = block->zStream;

    // First call for this block — create and initialise the z_stream
    if (zS == nullptr) {
        block->zStream = new z_stream;
        zS = block->zStream;

        zS->zalloc = nullptr;
        zS->zfree = nullptr;
        zS->opaque = nullptr;

        // Set output pointers here to avoid overwriting on recursive calls
        zS->next_out = *unzipped;
        zS->avail_out = *lenUnzipped;

        err = inflateInit(zS);
        if (err != Z_OK)
            return err;
    }

    // Feed input data
    zS->next_in = const_cast<Bytef*>(zipped);
    zS->avail_in = lenZipped;

    // Only set output pointers on non-recursive calls
    if (iRecursion == 0) {
        zS->next_out = *unzipped;
        zS->avail_out = *lenUnzipped;
    }

    err = inflate(zS, Z_SYNC_FLUSH);

    if (err == Z_STREAM_END) {
        // Stream completed — finish up
        err = inflateEnd(zS);
        if (err != Z_OK)
            return err;

        // Output = bytes produced in this call sequence
        *lenUnzipped = static_cast<uint32>(zS->total_out - block->totalUnzipped);
        block->totalUnzipped = static_cast<uint32>(zS->total_out);

        // zStream is done — null it so caller knows block is complete
        delete block->zStream;
        block->zStream = nullptr;

    } else if (err == Z_OK && zS->avail_out == 0 && zS->avail_in != 0) {
        // Output buffer was too small — expand and recurse
        uint32 newLength = *lenUnzipped * 2;
        if (newLength == 0)
            newLength = lenZipped * 2;

        // Copy successfully unzipped data so far to a larger buffer
        uint8* temp = new uint8[newLength];
        uint32 alreadyOut = static_cast<uint32>(zS->total_out - block->totalUnzipped);
        std::memcpy(temp, *unzipped, alreadyOut);
        delete[] *unzipped;
        *unzipped = temp;
        *lenUnzipped = newLength;

        // Reposition stream output into new buffer
        zS->next_out = *unzipped + alreadyOut;
        zS->avail_out = *lenUnzipped - alreadyOut;

        // Recurse with remaining input
        err = unzip(block, zS->next_in, zS->avail_in,
                    unzipped, lenUnzipped, iRecursion + 1);

    } else if (err == Z_OK && zS->avail_in == 0) {
        // All input consumed, output OK
        *lenUnzipped = static_cast<uint32>(zS->total_out - block->totalUnzipped);
        block->totalUnzipped = static_cast<uint32>(zS->total_out);

    } else {
        // Unexpected error — corrupt data
        logDebug(QStringLiteral("unzip error: %1 %2").arg(err).arg(QString::fromUtf8(zS->msg ? zS->msg : zError(err))));
    }

    if (err != Z_OK)
        *lenUnzipped = 0;

    return err;
}
#else
int UpDownClient::unzip(Pending_Block_Struct* /*block*/, const uint8* /*zipped*/,
                         uint32 /*lenZipped*/, uint8** unzipped, uint32* lenUnzipped,
                         int /*recursion*/)
{
    if (unzipped) *unzipped = nullptr;
    if (lenUnzipped) *lenUnzipped = 0;
    return -1; // Z_DATA_ERROR equivalent — zlib not available
}
#endif

} // namespace eMule
