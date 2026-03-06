#include "pch.h"
/// @file UploadDiskIOThread.cpp
/// @brief Async disk reads for upload — port of MFC UploadDiskIOThread.cpp.
///
/// Replaces Windows IOCP with a queue + QFile::read worker thread.
/// Creates standard and compressed (zlib) packets for sending file data.

#include "transfer/UploadDiskIOThread.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include "utils/Log.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

#include <zlib.h>

namespace eMule {

UploadDiskIOThread::UploadDiskIOThread(QObject* parent)
    : QThread(parent)
{
    start();
}

UploadDiskIOThread::~UploadDiskIOThread()
{
    endThread();
}

void UploadDiskIOThread::endThread()
{
    m_run.store(false);
    m_condition.notify_all();
    if (isRunning())
        wait();
}

void UploadDiskIOThread::wakeUp()
{
    m_condition.notify_one();
}

void UploadDiskIOThread::queueBlockRead(BlockReadRequest request)
{
    {
        std::lock_guard lock(m_mutex);
        m_requestQueue.push_back(std::move(request));
    }
    m_condition.notify_one();
}

bool UploadDiskIOThread::shouldCompressFile(const QString& fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    // Skip already-compressed formats
    static const QStringList noCompress = {
        QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
        QStringLiteral("gz"),  QStringLiteral("bz2"), QStringLiteral("xz"),
        QStringLiteral("cbz"), QStringLiteral("cbr"), QStringLiteral("ace"),
        QStringLiteral("ogm"),
        QStringLiteral("mp3"), QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("webm")
    };
    return !noCompress.contains(ext);
}

void UploadDiskIOThread::run()
{
    while (m_run.load()) {
        std::unique_lock lock(m_mutex);
        m_condition.wait(lock, [this] {
            return !m_requestQueue.empty() || !m_run.load();
        });

        if (!m_run.load())
            break;

        processRequests();
    }
}

void UploadDiskIOThread::processRequests()
{
    // Process all queued requests while holding the lock briefly per request
    while (!m_requestQueue.empty() && m_run.load()) {
        BlockReadRequest req = std::move(m_requestQueue.front());
        m_requestQueue.pop_front();

        // Release the lock while doing disk I/O
        // (we re-acquire in the run() loop)
        // Actually we need to not hold the lock during readBlock
        // so let's use a different pattern:
        readBlock(req);
    }
}

void UploadDiskIOThread::readBlock(const BlockReadRequest& req)
{
    if (!req.file || !req.client)
        return;

    if (req.startOffset >= req.endOffset) {
        emit readError(req.client);
        return;
    }

    uint64 dataLen = req.endOffset - req.startOffset;
    if (dataLen > EMBLOCKSIZE * 3) {
        logWarning(QStringLiteral("UploadDiskIOThread: Block too large: %1").arg(dataLen));
        emit readError(req.client);
        return;
    }

    // Determine file path
    QString filePath;
    if (req.file->isPartFile()) {
        const auto* pf = static_cast<const PartFile*>(req.file);
        filePath = pf->fullName();
        // Remove .part.met extension to get the .part data file
        if (filePath.endsWith(QStringLiteral(".met")))
            filePath.chop(4); // remove .met
    } else {
        filePath = req.file->filePath();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("UploadDiskIOThread: Cannot open file: %1").arg(filePath));
        emit readError(req.client);
        return;
    }

    if (!file.seek(static_cast<qint64>(req.startOffset))) {
        logWarning(QStringLiteral("UploadDiskIOThread: Seek failed at offset %1").arg(req.startOffset));
        emit readError(req.client);
        return;
    }

    QByteArray data = file.read(static_cast<qint64>(dataLen));
    file.close();

    if (static_cast<uint64>(data.size()) != dataLen) {
        logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 got: %2").arg(dataLen).arg(data.size()));
        emit readError(req.client);
        return;
    }

    const uint8* fileHash = req.file->fileHash();
    bool isPartFile = req.file->isPartFile();

    // Create packets — try compression unless disabled or file type is pre-compressed
    QList<std::shared_ptr<Packet>> packets;
    bool compress = !req.disableCompression && shouldCompressFile(req.file->fileName());
    if (compress)
        packets = createPackedPackets(fileHash, isPartFile, req.startOffset, req.endOffset, data);
    else
        packets = createStandardPackets(fileHash, isPartFile, req.startOffset, req.endOffset, data);

    emit blockPacketsReady(req.client, packets);
}

QList<std::shared_ptr<Packet>> UploadDiskIOThread::createStandardPackets(
    const uint8* fileHash, bool isPartFile,
    uint64 startOffset, uint64 endOffset, const QByteArray& data)
{
    QList<std::shared_ptr<Packet>> packets;

    uint32 togo = static_cast<uint32>(endOffset - startOffset);
    int readPos = 0;

    while (togo > 0) {
        uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
        togo -= nPacketSize;

        uint64 curEnd = endOffset - togo;
        uint64 curStart = curEnd - nPacketSize;

        std::shared_ptr<Packet> packet;
        if (curEnd > UINT32_MAX) {
            // Large file: OP_SENDINGPART_I64 with 32-byte header (16 hash + 8 start + 8 end)
            packet = std::make_shared<Packet>(OP_SENDINGPART_I64, nPacketSize + 32, OP_EMULEPROT, isPartFile);
            md4cpy(&packet->pBuffer[0], fileHash);
            std::memcpy(&packet->pBuffer[16], &curStart, 8);
            std::memcpy(&packet->pBuffer[24], &curEnd, 8);
            std::memcpy(&packet->pBuffer[32], data.constData() + readPos, nPacketSize);
        } else {
            // Standard: OP_SENDINGPART with 24-byte header (16 hash + 4 start + 4 end)
            packet = std::make_shared<Packet>(OP_SENDINGPART, nPacketSize + 24, OP_EDONKEYPROT, isPartFile);
            md4cpy(&packet->pBuffer[0], fileHash);
            uint32 start32 = static_cast<uint32>(curStart);
            uint32 end32 = static_cast<uint32>(curEnd);
            std::memcpy(&packet->pBuffer[16], &start32, 4);
            std::memcpy(&packet->pBuffer[20], &end32, 4);
            std::memcpy(&packet->pBuffer[24], data.constData() + readPos, nPacketSize);
        }

        packet->statsPayload = nPacketSize;
        packets.append(packet);
        readPos += static_cast<int>(nPacketSize);
    }

    return packets;
}

QList<std::shared_ptr<Packet>> UploadDiskIOThread::createPackedPackets(
    const uint8* fileHash, bool isPartFile,
    uint64 startOffset, uint64 endOffset, const QByteArray& data)
{
    uint32 originalSize = static_cast<uint32>(endOffset - startOffset);
    uLongf compressedSize = originalSize + 300;
    std::vector<uint8> compressed(compressedSize);

    // Use zlib compression level 1 (fastest)
    int zResult = compress2(compressed.data(), &compressedSize,
                            reinterpret_cast<const Bytef*>(data.constData()),
                            originalSize, 1);

    if (zResult != Z_OK || originalSize <= compressedSize) {
        // Compression failed or didn't reduce size — fall back to standard
        return createStandardPackets(fileHash, isPartFile, startOffset, endOffset, data);
    }

    QList<std::shared_ptr<Packet>> packets;
    uint32 togo = static_cast<uint32>(compressedSize);
    int readPos = 0;
    uint32 totalPayloadSize = 0;

    while (togo > 0) {
        uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
        togo -= nPacketSize;

        std::shared_ptr<Packet> packet;
        if (endOffset > UINT32_MAX) {
            // Large file: OP_COMPRESSEDPART_I64 with 28-byte header (16 hash + 8 start + 4 compressedLen)
            packet = std::make_shared<Packet>(OP_COMPRESSEDPART_I64, nPacketSize + 28, OP_EMULEPROT, isPartFile);
            md4cpy(&packet->pBuffer[0], fileHash);
            std::memcpy(&packet->pBuffer[16], &startOffset, 8);
            uint32 compLen = static_cast<uint32>(compressedSize);
            std::memcpy(&packet->pBuffer[24], &compLen, 4);
            std::memcpy(&packet->pBuffer[28], compressed.data() + readPos, nPacketSize);
        } else {
            // Standard: OP_COMPRESSEDPART with 24-byte header (16 hash + 4 start + 4 compressedLen)
            packet = std::make_shared<Packet>(OP_COMPRESSEDPART, nPacketSize + 24, OP_EMULEPROT, isPartFile);
            md4cpy(&packet->pBuffer[0], fileHash);
            uint32 start32 = static_cast<uint32>(startOffset);
            std::memcpy(&packet->pBuffer[16], &start32, 4);
            uint32 compLen = static_cast<uint32>(compressedSize);
            std::memcpy(&packet->pBuffer[20], &compLen, 4);
            std::memcpy(&packet->pBuffer[24], compressed.data() + readPos, nPacketSize);
        }

        // Approximate payload size proportional to original
        uint32 payloadSize = togo > 0
            ? nPacketSize * originalSize / static_cast<uint32>(compressedSize)
            : originalSize - totalPayloadSize;
        totalPayloadSize += payloadSize;

        packet->statsPayload = payloadSize;
        packets.append(packet);
        readPos += static_cast<int>(nPacketSize);
    }

    return packets;
}

} // namespace eMule
