/// @file UploadClient.cpp
/// @brief UpDownClient upload methods — scoring, block management, upload statistics.
///
/// Ported from MFC srchybrid/UploadClient.cpp.
/// Methods of UpDownClient related to upload functionality.

#include "client/UpDownClient.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QDebug>

#include <algorithm>
#include <cstring>

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
    const uint32 waitTime = m_credits ? m_credits->secureWaitStartTime(m_connectIP) : 0;
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

    return prioNum * m_credits->scoreRatio(m_connectIP);
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

    if (partCount == 0)
        return true;

    // Read part availability bitmap
    const uint16 byteCount = (partCount + 7) / 8;
    std::vector<uint8> bitmap(byteCount);
    data.read(bitmap.data(), byteCount);

    for (uint16 i = 0; i < partCount; ++i) {
        m_upPartStatus[i] = (bitmap[i / 8] & (1 << (i % 8))) ? 1 : 0;
    }

    return true;
}

// ===========================================================================
// setUploadFileID — MFC UploadClient.cpp:281-319
// ===========================================================================

void UpDownClient::setUploadFileID(KnownFile* newReqFile)
{
    if (m_uploadFile == newReqFile)
        return;

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

    if (m_socket) {
        const auto sentBytesCompleteFile = m_socket->getSentBytesCompleteFileSinceLastCallAndReset();
        const auto sentBytesPartFile = m_socket->getSentBytesPartFileSinceLastCallAndReset();
        const auto sentPayload = sentBytesCompleteFile + sentBytesPartFile;

        TransferredData newData;
        newData.dataLen = sentPayload;
        newData.timestamp = curTick;
        m_averageUDR.push_back(newData);

        m_sumForAvgUpDataRate += sentPayload;
    }

    // Remove entries older than 10 seconds
    while (!m_averageUDR.empty() && (curTick - m_averageUDR.front().timestamp) > 10000) {
        m_sumForAvgUpDataRate -= m_averageUDR.front().dataLen;
        m_averageUDR.pop_front();
    }

    // Calculate rate
    if (!m_averageUDR.empty()) {
        const uint32 elapsed = curTick - m_averageUDR.front().timestamp;
        if (elapsed > 0)
            m_upDatarate = static_cast<uint32>((m_sumForAvgUpDataRate * 1000) / elapsed);
        else
            m_upDatarate = 0;
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

    m_sentOutOfPartReqs = true;

    if (m_socket) {
        auto packet = std::make_unique<Packet>(OP_OUTOFPARTREQS, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
    }

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
    Q_UNUSED(fileIdentifiers);

    if (!m_socket || !data || size < 16)
        return;

    const uint8* fileHash = data; // First 16 bytes = file hash

    // Look up file in shared files by hash
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(fileHash);

    SafeMemFile response;
    response.writeHash16(fileHash);

    if (file) {
        // Send actual hashset from the file
        const uint16 hashCount = file->fileIdentifier().getAvailableMD4PartHashCount();
        response.writeUInt16(hashCount);
        for (uint16 i = 0; i < hashCount; ++i) {
            const uint8* partHash = file->fileIdentifier().getMD4PartHash(i);
            if (partHash)
                response.writeHash16(partHash);
        }
    } else {
        response.writeUInt16(0); // part count = 0 (file not found)
    }

    const uint8 opcode = fileIdentifiers ? OP_HASHSETANSWER2 : OP_HASHSETANSWER;
    const uint8 proto = fileIdentifiers ? OP_EMULEPROT : OP_EDONKEYPROT;
    auto packet = std::make_unique<Packet>(response, proto, opcode);
    sendPacket(std::move(packet));
}

// ===========================================================================
// sendRankingInfo
// ===========================================================================

void UpDownClient::sendRankingInfo()
{
    if (!m_socket || !extProtocolAvailable())
        return;

    SafeMemFile data;
    uint16 rank = 0;
    if (theApp.uploadQueue)
        rank = static_cast<uint16>(theApp.uploadQueue->waitingPosition(this));
    data.writeUInt16(rank);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_QUEUERANKING);
    sendPacket(std::move(packet));
}

// ===========================================================================
// sendCommentInfo
// ===========================================================================

void UpDownClient::sendCommentInfo(const KnownFile* file)
{
    if (!m_socket || !file || m_acceptCommentVer == 0)
        return;

    SafeMemFile data;
    data.writeUInt8(static_cast<uint8>(const_cast<KnownFile*>(file)->getFileRating()));

    const QString& comment = const_cast<KnownFile*>(file)->getFileComment();
    if (!comment.isEmpty()) {
        const QByteArray utf8 = comment.toUtf8();
        data.writeUInt32(static_cast<uint32>(utf8.size()));
        data.write(utf8.constData(), utf8.size());
    } else {
        data.writeUInt32(0);
    }

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
        qDebug() << "Banning client:" << userName() << "reason:" << reason;
        setUploadState(UploadState::Banned);
        if (theApp.clientList)
            theApp.clientList->addBannedClient(m_connectIP);
    }
}

void UpDownClient::unBan()
{
    if (m_uploadState == UploadState::Banned) {
        setUploadState(UploadState::None);
        if (theApp.clientList)
            theApp.clientList->removeBannedClient(m_connectIP);
    }
}

// ===========================================================================
// Wait time management — delegates to ClientCredits
// ===========================================================================

uint32 UpDownClient::waitStartTime() const
{
    if (!m_credits)
        return 0;
    return m_credits->secureWaitStartTime(m_connectIP);
}

void UpDownClient::setWaitStartTime()
{
    if (m_credits)
        m_credits->setSecWaitStartTime(m_connectIP);
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

} // namespace eMule
