#include "pch.h"
/// @file UploadClient.cpp
/// @brief UpDownClient upload methods — scoring, block management, upload statistics.
///
/// Ported from MFC srchybrid/UploadClient.cpp.
/// Methods of UpDownClient related to upload functionality.

#include "client/UpDownClient.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "app/AppContext.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "stats/Statistics.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/TimeUtils.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"


namespace eMule {

// ===========================================================================
// score — MFC UploadClient.cpp:182-233
// ===========================================================================

uint32 UpDownClient::score(bool sysValue, bool isDownloading, bool onlyBaseValue) const
{
    Q_UNUSED(isDownloading);

    if (!m_uploadFile)
        return 0;

    // Verify upload file is still shared
    if (theApp.sharedFileList && !theApp.sharedFileList->getFileByID(m_reqUpFileId.data()))
        return 0;

    if (m_uploadState == UploadState::Banned)
        return 0;

    // Base score from wait time
    const uint32 curTick = static_cast<uint32>(getTickCount());
    const uint32 waitTime = m_credits ? m_credits->secureWaitStartTime(m_connectAddress.toNetworkUint32()) : 0;
    float score = (waitTime != 0) ? static_cast<float>(curTick - waitTime) : 0.0f;

    // Apply file priority and credit multiplier
    score *= getCombinedFilePrioAndCredit();

    if (!onlyBaseValue && !sysValue) {
        // Friend slot bonus
        if (m_friendSlot)
            score *= 2000.0f;

        // Boost if this client is also downloading from us
        if (m_downloadState == DownloadState::Downloading)
            score += 1.0f;
    }

    if (score > static_cast<float>(UINT32_MAX))
        return UINT32_MAX;

    return static_cast<uint32>(score);
}

// ===========================================================================
// getCombinedFilePrioAndCredit
// ===========================================================================

float UpDownClient::getCombinedFilePrioAndCredit() const
{
    const float prioNum = static_cast<float>(filePrioAsNumber());
    if (!m_credits)
        return prioNum;

    return prioNum * m_credits->scoreRatio(m_connectAddress.toNetworkUint32());
}

// ===========================================================================
// processExtendedInfo — MFC UploadClient.cpp:235-279
// ===========================================================================

bool UpDownClient::processExtendedInfo(SafeMemFile& data, KnownFile* file)
{
    const uint16 partCount = data.readUInt16();
    if (!file)
        return false;

    if (partCount != file->partCount()) {
        m_upPartStatus.clear();
        m_upPartCount = 0;
        return false;
    }

    m_upPartCount = partCount;
    m_upPartStatus.resize(partCount);

    if (partCount == 0) {
        // MFC: still consume complete source count if present
        if (m_extendedRequestsVer > 1 && (data.length() - data.position()) >= 2)
            data.readUInt16();
        return true;
    }

    // Read part availability bitmap
    const uint16 byteCount = (partCount + 7) / 8;
    std::vector<uint8> bitmap(byteCount);
    data.read(bitmap.data(), byteCount);

    for (uint16 i = 0; i < partCount; ++i) {
        m_upPartStatus[i] = (bitmap[i / 8] & (1 << (i % 8))) ? 1 : 0;
    }

    // MFC ProcessExtendedInfo: consume complete source count for ExtendedRequests v2+
    if (m_extendedRequestsVer > 1 && (data.length() - data.position()) >= 2)
        data.readUInt16();

    return true;
}

// ===========================================================================
// setUploadFileID — MFC UploadClient.cpp:281-319
// ===========================================================================

void UpDownClient::setUploadFileID(KnownFile* newReqFile)
{
    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("setUploadFileID: old=%1 new=%2 for %3")
                     .arg(m_uploadFile ? m_uploadFile->fileName() : QStringLiteral("null"))
                     .arg(newReqFile ? newReqFile->fileName() : QStringLiteral("null"))
                     .arg(userName()));
    if (m_uploadFile == newReqFile)
        return;

    // Flush pending block requests from old file before switching
    flushSendBlocks();

    // Remove from old file's uploading list
    if (m_uploadFile) {
        m_uploadFile->removeUploadingClient(this);
    }

    m_uploadFile = newReqFile;

    if (m_uploadFile) {
        // Copy file hash to reqUpFileId
        if (m_uploadFile->fileHash()) {
            md4cpy(m_reqUpFileId.data(), m_uploadFile->fileHash());
        }

        m_uploadFile->addUploadingClient(this);

        // Reset part status for new file
        m_upPartStatus.clear();
        m_upPartCount = 0;
    } else {
        md4clr(m_reqUpFileId.data());
        m_upPartStatus.clear();
        m_upPartCount = 0;
    }
}

// ===========================================================================
// addReqBlock — MFC UploadClient.cpp:322-368
// ===========================================================================

void UpDownClient::addReqBlock(Requested_Block_Struct* reqBlock)
{
    if (!reqBlock)
        return;

    // Validate block range
    if (reqBlock->startOffset >= reqBlock->endOffset) {
        delete reqBlock;
        return;
    }

    // Add to block request queue
    m_blockRequests.push_back(reqBlock);

    // Signal disk IO thread to start reading the block from disk
    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("addReqBlock: start=%1 end=%2 uploadFile=%3 uploadQueue=%4")
                     .arg(reqBlock->startOffset).arg(reqBlock->endOffset)
                     .arg(m_uploadFile ? m_uploadFile->fileName() : QStringLiteral("null"))
                     .arg(theApp.uploadQueue != nullptr));
    if (theApp.uploadQueue && m_uploadFile) {
        if (auto* diskIO = theApp.uploadQueue->diskIOThread()) {
            BlockReadRequest readReq;
            readReq.file = m_uploadFile;
            readReq.client = this;
            readReq.startOffset = reqBlock->startOffset;
            readReq.endOffset = reqBlock->endOffset;
            readReq.disableCompression = (m_dataCompVer == 0);
            diskIO->queueBlockRead(std::move(readReq));
        }
    }
}

// ===========================================================================
// updateUploadingStatisticsData — MFC UploadClient.cpp:425-484
// ===========================================================================

void UpDownClient::updateUploadingStatisticsData()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    uint32 sentBytesCompleteFile = 0;
    uint32 sentBytesPartFile = 0;

    if (m_socket) {
        sentBytesCompleteFile = static_cast<uint32>(m_socket->getSentBytesCompleteFileSinceLastCallAndReset());
        sentBytesPartFile = static_cast<uint32>(m_socket->getSentBytesPartFileSinceLastCallAndReset());
        const auto sentPayload = sentBytesCompleteFile + sentBytesPartFile;

        m_transferredUp += sentPayload;
        m_curQueueSessionPayloadUp += sentPayload;

        // Per-client/port/source breakdown tracking
        if (sentPayload > 0 && theApp.statistics) {
            if (sentBytesCompleteFile > 0)
                theApp.statistics->addTransferData(clientSoft(), userPort(),
                                                   false, true, sentBytesCompleteFile);
            if (sentBytesPartFile > 0)
                theApp.statistics->addTransferData(clientSoft(), userPort(),
                                                   true, true, sentBytesPartFile);
        }
    }

    const uint32 sentBytesFile = sentBytesCompleteFile + sentBytesPartFile;

    // MFC gating: only add sample when data was sent, list is empty, or 1s gap
    if (sentBytesFile > 0 || m_averageUDR.empty() ||
        curTick >= m_averageUDR.back().timestamp + 1000)
    {
        m_averageUDR.push_back({sentBytesFile, curTick});
        m_sumForAvgUpDataRate += sentBytesFile;
    }

    // Remove entries older than 10 seconds
    while (!m_averageUDR.empty() && (curTick - m_averageUDR.front().timestamp) > 10000) {
        m_sumForAvgUpDataRate -= m_averageUDR.front().dataLen;
        m_averageUDR.pop_front();
    }

    // Calculate rate — require 2s since upload start (MFC guard)
    if (!m_averageUDR.empty() &&
        curTick > m_averageUDR.front().timestamp &&
        getUpStartTimeDelay() > 2000)
    {
        const uint32 elapsed = curTick - m_averageUDR.front().timestamp;
        m_upDatarate = static_cast<uint32>((m_sumForAvgUpDataRate * 1000) / elapsed);
    } else {
        m_upDatarate = 0;
    }
}

// ===========================================================================
// sendOutOfPartReqsAndAddToWaitingQueue
// ===========================================================================

void UpDownClient::sendOutOfPartReqsAndAddToWaitingQueue()
{
    if (m_sentOutOfPartReqs)
        return;

    if (m_socket) {
        auto packet = std::make_unique<Packet>(OP_OUTOFPARTREQS, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
    }

    // MFC: set flag AFTER sending packet
    m_sentOutOfPartReqs = true;

    if (theApp.uploadQueue)
        theApp.uploadQueue->addClientToQueue(this);
}

// ===========================================================================
// flushSendBlocks
// ===========================================================================

void UpDownClient::flushSendBlocks()
{
    // Clean up pending block requests
    for (auto* block : m_blockRequests)
        delete block;
    m_blockRequests.clear();

    for (auto* block : m_doneBlocks)
        delete block;
    m_doneBlocks.clear();
}

// ===========================================================================
// sendHashsetPacket — MFC UploadClient.cpp:512-558
// ===========================================================================

void UpDownClient::sendHashsetPacket(const uint8* data, uint32 size, bool fileIdentifiers)
{
    if (!m_socket || !data || size < 16)
        return;

    // OP_HASHSETREQUEST2 uses a FileIdentifier (descriptor + MD4 + optional
    // size/AICH) whereas OP_HASHSETREQUEST is just a raw 16-byte MD4 hash.
    uint8 fileHash[16]{};
    uint8 requestedOptions = 0;
    if (fileIdentifiers) {
        SafeMemFile io(data, size);
        FileIdentifierSA ident;
        if (!ident.readIdentifier(io))
            return;
        md4cpy(fileHash, ident.getMD4Hash());
        if (io.length() - io.position() >= 1)
            requestedOptions = io.readUInt8();
    } else {
        md4cpy(fileHash, data);
    }

    // Look up file in shared files by hash
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(fileHash);

    SafeMemFile response;

    if (fileIdentifiers) {
        // OP_HASHSETANSWER2: FileIdentifier + hashset blob via writeHashSetsToPacket
        if (file) {
            file->fileIdentifier().writeIdentifier(response);
            const bool sendMD4  = (requestedOptions & 0x01) != 0;
            const bool sendAICH = (requestedOptions & 0x02) != 0;
            file->fileIdentifier().writeHashSetsToPacket(response, sendMD4, sendAICH);
        } else {
            // File not found — write a minimal identifier so the client can match it
            response.writeHash16(fileHash);
        }
        auto packet = std::make_unique<Packet>(response, OP_EMULEPROT, OP_HASHSETANSWER2);
        sendPacket(std::move(packet));
    } else {
        // OP_HASHSETANSWER: hash(16) + count(2) + N×hash(16)
        response.writeHash16(fileHash);
        if (file) {
            const uint16 hashCount = file->fileIdentifier().getAvailableMD4PartHashCount();
            response.writeUInt16(hashCount);
            for (uint16 i = 0; i < hashCount; ++i) {
                const uint8* partHash = file->fileIdentifier().getMD4PartHash(i);
                if (partHash)
                    response.writeHash16(partHash);
            }
        } else {
            response.writeUInt16(0);
        }
        auto packet = std::make_unique<Packet>(response, OP_EDONKEYPROT, OP_HASHSETANSWER);
        sendPacket(std::move(packet));
    }
}

// ===========================================================================
// sendRankingInfo
// ===========================================================================

void UpDownClient::sendRankingInfo()
{
    if (!m_socket || !extProtocolAvailable())
        return;

    uint16 rank = 0;
    if (theApp.uploadQueue)
        rank = static_cast<uint16>(theApp.uploadQueue->waitingPosition(this));
    if (!rank)
        return;

    // MFC: fixed 12-byte packet: uint16(rank) + 10 bytes padding
    auto packet = std::make_unique<Packet>(OP_QUEUERANKING, 12, OP_EMULEPROT);
    pokeUInt16(reinterpret_cast<uint8*>(packet->pBuffer), rank);
    std::memset(packet->pBuffer + 2, 0, 10);
    sendPacket(std::move(packet));
}

// ===========================================================================
// sendCommentInfo
// ===========================================================================

void UpDownClient::sendCommentInfo(const KnownFile* file)
{
    if (!m_commentDirty || !m_socket || !file || m_acceptCommentVer < 1)
        return;
    m_commentDirty = false;

    const uint8 rating = static_cast<uint8>(const_cast<KnownFile*>(file)->getFileRating());
    const QString& comment = const_cast<KnownFile*>(file)->getFileComment();
    if (rating == 0 && comment.isEmpty())
        return;

    SafeMemFile data;
    data.writeUInt8(rating);
    data.writeLongString(comment, m_unicodeSupport ? UTF8Mode::Raw : UTF8Mode::None);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_FILEDESC);
    sendPacket(std::move(packet));
}

// ===========================================================================
// addRequestCount — MFC UploadClient.cpp:370-395
// ===========================================================================

void UpDownClient::addRequestCount(const uint8* fileID)
{
    if (!fileID)
        return;

    for (auto* req : m_requestedFiles) {
        if (md4equ(req->fileID.data(), fileID)) {
            req->badRequests++;
            if (req->badRequests > BADCLIENTBAN) {
                ban(QStringLiteral("Too many file requests"));
            }
            return;
        }
    }

    // New file request
    auto* newReq = new Requested_File_Struct;
    md4cpy(newReq->fileID.data(), fileID);
    newReq->lastAsked = static_cast<uint32>(getTickCount());
    newReq->badRequests = 1;
    m_requestedFiles.push_back(newReq);
}

// ===========================================================================
// ban / unBan
// ===========================================================================

void UpDownClient::ban(const QString& reason)
{
    if (m_uploadState != UploadState::Banned) {
        logDebug(QStringLiteral("Banning client: %1 reason: %2").arg(userName(), reason));
        setUploadState(UploadState::Banned);
        if (theApp.clientList)
            theApp.clientList->addBannedClient(m_connectAddress);
    }
}

void UpDownClient::unBan()
{
    if (m_uploadState == UploadState::Banned) {
        setUploadState(UploadState::None);
        if (theApp.clientList)
            theApp.clientList->removeBannedClient(m_connectAddress);
    }
}

// ===========================================================================
// Wait time management — delegates to ClientCredits
// ===========================================================================

uint32 UpDownClient::waitStartTime() const
{
    if (!m_credits)
        return 0;
    return m_credits->secureWaitStartTime(m_connectAddress.toNetworkUint32());
}

uint32 UpDownClient::getWaitTimeDelay() const
{
    if (!m_credits)
        return 0;
    uint32 wst = m_credits->secureWaitStartTime(m_connectAddress.toNetworkUint32());
    if (wst == 0)
        return 0;
    // MFC: freeze waited time once upload starts (GetWaitTime = m_dwUploadTime - GetWaitStartTime)
    if (m_uploadTime > 0 && m_uploadTime >= wst)
        return m_uploadTime - wst;
    uint32 curTick = static_cast<uint32>(getTickCount());
    return (curTick >= wst) ? (curTick - wst) : 0;
}

void UpDownClient::setWaitStartTime()
{
    if (m_credits)
        m_credits->setSecWaitStartTime(m_connectAddress.toNetworkUint32());
}

void UpDownClient::clearWaitStartTime()
{
    if (m_credits)
        m_credits->clearWaitStartTime();
}

// ===========================================================================
// getFileUploadSocket
// ===========================================================================

EMSocket* UpDownClient::getFileUploadSocket() const
{
    return m_socket;
}

// ===========================================================================
// isUpPartAvailable
// ===========================================================================

bool UpDownClient::isUpPartAvailable(uint32 part) const
{
    if (part >= m_upPartStatus.size())
        return false;
    return m_upPartStatus[part] != 0;
}

// ===========================================================================
// filePrioAsNumber (private)
// ===========================================================================

int UpDownClient::filePrioAsNumber() const
{
    if (!m_uploadFile)
        return 0;

    switch (m_uploadFile->upPriority()) {
    case kPrVeryLow:  return 2;   // 0.2 * 10
    case kPrLow:      return 6;   // 0.6 * 10
    case kPrNormal:   return 7;   // 0.7 * 10
    case kPrHigh:     return 9;   // 0.9 * 10
    case kPrVeryHigh: return 10;  // 1.0 * 10
    default:          return 7;   // Normal default
    }
}

uint32 UpDownClient::getUpStartTimeDelay() const
{
    if (m_uploadTime == 0)
        return 0;
    uint32 curTick = static_cast<uint32>(getTickCount());
    return (curTick >= m_uploadTime) ? (curTick - m_uploadTime) : 0;
}

// ===========================================================================
// findUploadFile — look up a file by hash for upload purposes
// ===========================================================================

KnownFile* UpDownClient::findUploadFile(const uint8* fileHash) const
{
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(fileHash);

    if (!file && theApp.downloadQueue) {
        auto* partFile = theApp.downloadQueue->fileByID(fileHash);
        if (partFile && static_cast<uint64>(partFile->completedSize()) >= PARTSIZE)
            file = partFile;
    }
    return file;
}

// ===========================================================================
// sendFileNotFound — send OP_FILEREQANSNOFIL
// ===========================================================================

void UpDownClient::sendFileNotFound(const uint8* fileHash)
{
    auto packet = std::make_unique<Packet>(OP_FILEREQANSNOFIL, 16);
    md4cpy(reinterpret_cast<uint8*>(packet->pBuffer), fileHash);
    sendPacket(std::move(packet));
}

// ===========================================================================
// sendFileStatus — send OP_FILESTATUS for a file we share
// ===========================================================================

void UpDownClient::sendFileStatus(const uint8* fileHash, KnownFile* file)
{
    SafeMemFile response;
    response.writeHash16(fileHash);

    if (file->isPartFile()) {
        static_cast<PartFile*>(file)->writePartStatus(response);
    } else {
        response.writeUInt16(0); // 0 = complete file
    }

    auto packet = std::make_unique<Packet>(response, OP_EDONKEYPROT, OP_FILESTATUS);
    sendPacket(std::move(packet));
}

// ===========================================================================
// processRequestParts — handle OP_REQUESTPARTS / OP_REQUESTPARTS_I64
// ===========================================================================

void UpDownClient::processRequestParts(const uint8* data, uint32 size, bool i64Offsets)
{
    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("processRequestParts: size=%1 i64=%2 uploadFile=%3 from %4")
                     .arg(size).arg(i64Offsets)
                     .arg(m_uploadFile ? m_uploadFile->fileName() : QStringLiteral("null"))
                     .arg(userName()));

    const uint32 expectedSize = i64Offsets ? 64u : 40u; // 16 + 3*(4 or 8) + 3*(4 or 8)
    if (size < expectedSize)
        return;

    SafeMemFile io(data, size);

    // Read file hash (16 bytes)
    uint8 fileHash[16];
    io.readHash16(fileHash);

    // Read 3 start offsets, then 3 end offsets
    std::array<uint64, 3> starts{};
    std::array<uint64, 3> ends{};

    for (size_t i = 0; i < 3; ++i)
        starts[i] = i64Offsets ? io.readUInt64() : io.readUInt32();
    for (size_t i = 0; i < 3; ++i)
        ends[i] = i64Offsets ? io.readUInt64() : io.readUInt32();

    for (size_t i = 0; i < 3; ++i) {
        if (starts[i] < ends[i]) {
            auto* reqBlock = new Requested_Block_Struct;
            reqBlock->startOffset = starts[i];
            reqBlock->endOffset = ends[i];
            md4cpy(reqBlock->fileID.data(), fileHash);
            reqBlock->transferredByClient = 0;
            addReqBlock(reqBlock);
        }
    }
}

// ===========================================================================
// processSetReqFileID — handle OP_SETREQFILEID (upload side)
// ===========================================================================

void UpDownClient::processSetReqFileID(const uint8* data, uint32 size)
{
    if (size < 16)
        return;

    setWaitStartTime();

    KnownFile* file = findUploadFile(data);
    if (!file) {
        checkFailedFileIdReqs(data);
        return;
    }

    if (file->isLargeFile() && !supportsLargeFiles()) {
        sendFileNotFound(data);
        return;
    }

    if (!md4equ(data, m_reqUpFileId.data()))
        setCommentDirty(true);

    setUploadFileID(file);
    sendFileStatus(data, file);
}

// ===========================================================================
// processRequestFileName — handle OP_REQUESTFILENAME (upload side)
// ===========================================================================

void UpDownClient::processRequestFileName(const uint8* data, uint32 size)
{
    if (size < 16)
        return;

    setWaitStartTime();

    SafeMemFile io(data, size);
    uint8 fileHash[16];
    io.readHash16(fileHash);

    KnownFile* file = findUploadFile(fileHash);
    if (!file) {
        checkFailedFileIdReqs(fileHash);
        return;
    }

    if (file->isLargeFile() && !supportsLargeFiles()) {
        sendFileNotFound(fileHash);
        return;
    }

    // Process extended info (part status) if available
    if (m_extendedRequestsVer > 0 && (io.length() - io.position()) >= 2) {
        if (!processExtendedInfo(io, file)) {
            sendFileNotFound(fileHash);
            return;
        }
    }

    if (!md4equ(fileHash, m_reqUpFileId.data()))
        setCommentDirty(true);

    setUploadFileID(file);

    // Send OP_REQFILENAMEANSWER: hash + filename
    SafeMemFile response;
    response.writeHash16(fileHash);
    response.writeString(file->fileName(), UTF8Mode::Raw);

    auto packet = std::make_unique<Packet>(response, OP_EDONKEYPROT, OP_REQFILENAMEANSWER);
    sendPacket(std::move(packet));

    sendCommentInfo(file);
}

// ===========================================================================
// processMultiPacketExt2 — handle OP_MULTIPACKET_EXT2 (upload side)
// ===========================================================================

void UpDownClient::processMultiPacketExt2(const uint8* data, uint32 size)
{
    (void)checkHandshakeFinished();

    if (size < 1)
        return;

    SafeMemFile dataIn(data, size);

    // Read file identifier
    FileIdentifierSA fileIdent;
    if (!fileIdent.readIdentifier(dataIn)) {
        logDebug(QStringLiteral("MultiPacketExt2: failed to read file identifier"));
        return;
    }

    // Look up the file
    KnownFile* reqFile = findUploadFile(fileIdent.getMD4Hash());
    if (!reqFile || !reqFile->fileIdentifier().compareRelaxed(fileIdent)) {
        sendFileNotFound(fileIdent.getMD4Hash());
        return;
    }

    if (reqFile->isLargeFile() && !supportsLargeFiles()) {
        sendFileNotFound(fileIdent.getMD4Hash());
        return;
    }

    setWaitStartTime();

    if (!md4equ(fileIdent.getMD4Hash(), m_reqUpFileId.data()))
        setCommentDirty(true);

    setUploadFileID(reqFile);

    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("processMultiPacketExt2: file=%1 hash=%2 from %3")
                     .arg(reqFile->fileName())
                     .arg(md4str(fileIdent.getMD4Hash()))
                     .arg(userName()));

    // Build response
    SafeMemFile dataOut;
    reqFile->fileIdentifier().writeIdentifier(dataOut);
    bool hasResponse = false;
    bool answerFNF = false;

    // Process sub-opcodes
    while ((dataIn.length() - dataIn.position()) > 0 && !answerFNF) {
        const uint8 subOpcode = dataIn.readUInt8();

        switch (subOpcode) {
        case OP_REQUESTFILENAME: {
            // Read extended info if available
            if (m_extendedRequestsVer > 0 && (dataIn.length() - dataIn.position()) >= 2) {
                if (!processExtendedInfo(dataIn, reqFile)) {
                    sendFileNotFound(fileIdent.getMD4Hash());
                    answerFNF = true;
                    break;
                }
            }
            // Write filename answer
            dataOut.writeUInt8(OP_REQFILENAMEANSWER);
            dataOut.writeString(reqFile->fileName(), UTF8Mode::Raw);
            hasResponse = true;
            break;
        }

        case OP_SETREQFILEID:
            // Write file status
            dataOut.writeUInt8(OP_FILESTATUS);
            if (reqFile->isPartFile()) {
                static_cast<PartFile*>(reqFile)->writePartStatus(dataOut);
            } else {
                dataOut.writeUInt16(0); // complete file
            }
            hasResponse = true;
            break;

        default:
            logDebug(QStringLiteral("MultiPacketExt2: unknown sub-opcode 0x%1")
                         .arg(subOpcode, 2, 16, QLatin1Char('0')));
            break;
        }
    }

    if (hasResponse && !answerFNF) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("processMultiPacketExt2: sending OP_MULTIPACKETANSWER_EXT2 size=%1 for %2")
                         .arg(dataOut.length()).arg(reqFile->fileName()));
        auto packet = std::make_unique<Packet>(dataOut, OP_EMULEPROT, OP_MULTIPACKETANSWER_EXT2);
        sendPacket(std::move(packet));
        sendCommentInfo(reqFile);
    }
}

// ===========================================================================
// processMultiPacketLegacy — handle OP_MULTIPACKET / OP_MULTIPACKET_EXT
//                            (upload side, deprecated opcodes)
// MFC ListenSocket.cpp ProcessExtPacket OP_MULTIPACKET / OP_MULTIPACKET_EXT
// ===========================================================================

void UpDownClient::processMultiPacketLegacy(const uint8* data, uint32 size, bool hasFileSize)
{
    (void)checkHandshakeFinished();

    if (size < 16)
        return;

    SafeMemFile dataIn(data, size);

    // Legacy header: hash16 [+ filesize64 for OP_MULTIPACKET_EXT]
    uint8 fileHash[16];
    dataIn.readHash16(fileHash);

    uint64 fileSize = 0;
    if (hasFileSize)
        fileSize = dataIn.readUInt64();

    // Look up the file
    KnownFile* reqFile = findUploadFile(fileHash);
    if (!reqFile) {
        sendFileNotFound(fileHash);
        return;
    }

    if (hasFileSize && static_cast<uint64>(reqFile->fileSize()) != fileSize) {
        sendFileNotFound(fileHash);
        return;
    }

    if (reqFile->isLargeFile() && !supportsLargeFiles()) {
        sendFileNotFound(fileHash);
        return;
    }

    setWaitStartTime();

    if (!md4equ(fileHash, m_reqUpFileId.data()))
        setCommentDirty(true);

    setUploadFileID(reqFile);

    // Build response — legacy answer uses hash16 prefix
    SafeMemFile dataOut;
    dataOut.writeHash16(fileHash);
    bool hasResponse = false;
    bool answerFNF = false;

    // Process sub-opcodes (same as EXT2)
    bool stopParsing = false;
    while ((dataIn.length() - dataIn.position()) > 0 && !answerFNF && !stopParsing) {
        const uint8 subOpcode = dataIn.readUInt8();

        switch (subOpcode) {
        case OP_REQUESTFILENAME: {
            if (m_extendedRequestsVer > 0 && (dataIn.length() - dataIn.position()) >= 2) {
                if (!processExtendedInfo(dataIn, reqFile)) {
                    sendFileNotFound(fileHash);
                    answerFNF = true;
                    break;
                }
            }
            dataOut.writeUInt8(OP_REQFILENAMEANSWER);
            dataOut.writeString(reqFile->fileName(), UTF8Mode::Raw);
            hasResponse = true;
            break;
        }

        case OP_SETREQFILEID:
            dataOut.writeUInt8(OP_FILESTATUS);
            if (reqFile->isPartFile()) {
                static_cast<PartFile*>(reqFile)->writePartStatus(dataOut);
            } else {
                dataOut.writeUInt16(0); // complete file
            }
            hasResponse = true;
            break;

        default:
            // Unknown sub-opcode with unknown length — stop parsing
            logDebug(QStringLiteral("MultiPacketLegacy: unknown sub-opcode 0x%1")
                         .arg(subOpcode, 2, 16, QLatin1Char('0')));
            stopParsing = true;
            break;
        }
    }

    if (hasResponse && !answerFNF) {
        auto packet = std::make_unique<Packet>(dataOut, OP_EMULEPROT, OP_MULTIPACKETANSWER);
        sendPacket(std::move(packet));
        sendCommentInfo(reqFile);
    }
}

// ===========================================================================
// processMultiPacketAnswerLegacy — handle OP_MULTIPACKETANSWER (download side)
// MFC DownloadClient.cpp ProcessMultiPacketAnswer for deprecated 0x93
// ===========================================================================

void UpDownClient::processMultiPacketAnswerLegacy(const uint8* data, uint32 size)
{
    (void)checkHandshakeFinished();

    if (size < 16)
        return;

    SafeMemFile dataIn(data, size);

    // Legacy header: hash16 only (no FileIdentifier)
    uint8 fileHash[16];
    dataIn.readHash16(fileHash);

    // Find the file we requested
    if (!m_reqFile || !md4equ(fileHash, m_reqFile->fileHash()))
        return;

    // Process sub-responses (same as EXT2 answer)
    while ((dataIn.length() - dataIn.position()) > 0) {
        const uint8 subOpcode = dataIn.readUInt8();

        switch (subOpcode) {
        case OP_REQFILENAMEANSWER:
            processFileInfo(dataIn, m_reqFile);
            break;

        case OP_FILESTATUS:
            processFileStatus(false, dataIn, m_reqFile);
            break;

        default:
            // Unknown sub-response with unknown length — can't continue
            return;
        }
    }

    // Initiate download if processFileStatus didn't already handle it
    if (m_reqFile && (m_downloadState == DownloadState::Connected
                      || m_downloadState == DownloadState::Connecting)) {
        sendStartupLoadReq();
    }
}

// ===========================================================================
// processMultiPacketAnswer — handle OP_MULTIPACKETANSWER_EXT2 (download side)
// ===========================================================================

void UpDownClient::processMultiPacketAnswer(const uint8* data, uint32 size)
{
    (void)checkHandshakeFinished();

    if (size < 1)
        return;

    SafeMemFile dataIn(data, size);

    // Read file identifier
    FileIdentifierSA fileIdent;
    if (!fileIdent.readIdentifier(dataIn))
        return;

    // Find the file we requested
    if (!m_reqFile || !md4equ(fileIdent.getMD4Hash(), m_reqFile->fileHash())) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("processMultiPacketAnswer: hash mismatch or no reqFile, reqFile=%1")
                         .arg(m_reqFile ? m_reqFile->fileName() : QStringLiteral("null")));
        return;
    }

    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("processMultiPacketAnswer: file=%1 dlState=%2 from %3")
                     .arg(m_reqFile->fileName())
                     .arg(static_cast<int>(m_downloadState))
                     .arg(userName()));

    // Process sub-responses
    while ((dataIn.length() - dataIn.position()) > 0) {
        const uint8 subOpcode = dataIn.readUInt8();

        switch (subOpcode) {
        case OP_REQFILENAMEANSWER:
            processFileInfo(dataIn, m_reqFile);
            break;

        case OP_FILESTATUS:
            processFileStatus(false, dataIn, m_reqFile);
            break;

        default:
            // Unknown sub-response, can't continue (unknown length)
            return;
        }
    }

    // Initiate download if processFileStatus didn't already handle it.
    // For single-part files, OP_SETREQFILEID is not sent so the response
    // contains no OP_FILESTATUS — sendStartupLoadReq() is never reached.
    // Matches the separate-packet OP_REQFILENAMEANSWER handler in onFileRequestReceived.
    if (m_reqFile && (m_downloadState == DownloadState::Connected
                      || m_downloadState == DownloadState::Connecting)) {
        sendStartupLoadReq();
    }
}

} // namespace eMule
