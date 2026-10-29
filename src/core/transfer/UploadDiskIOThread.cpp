#include "pch.h"
/// @file UploadDiskIOThread.cpp
/// @brief Async disk reads for upload — port of MFC UploadDiskIOThread.cpp.
///
/// Replaces Windows IOCP with a queue + QFile::read worker thread.
/// Creates standard and compressed (zlib) packets for sending file data.

#include "transfer/UploadDiskIOThread.h"
#include "app/AppContext.h"
#include "files/PartFile.h"
#include "net/Packet.h"
#include "stats/Statistics.h"

#include "utils/Log.h"
#include "utils/TimeUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>


#include <zlib.h>

#ifdef Q_OS_WIN
#include <QDir>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>

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
    // Skip already-compressed formats (MFC knows seven; the rest is not MFC)
    static const QSet<QString> noCompress = {
        // archives
        QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
        QStringLiteral("gz"), QStringLiteral("bz2"), QStringLiteral("xz"),
        QStringLiteral("cbz"), QStringLiteral("cbr"), QStringLiteral("ace"),
        QStringLiteral("zst"), QStringLiteral("lz4"), QStringLiteral("lzma"),
        QStringLiteral("tgz"), QStringLiteral("tbz2"), QStringLiteral("txz"),
        QStringLiteral("cab"),
        // zip containers
        QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
        QStringLiteral("odt"), QStringLiteral("epub"), QStringLiteral("jar"),
        QStringLiteral("apk"),
        // video
        QStringLiteral("ogm"), QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("wmv"),
        QStringLiteral("mov"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("m4v"), QStringLiteral("flv"),
        QStringLiteral("m2ts"), QStringLiteral("vob"), QStringLiteral("divx"),
        // audio
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"),
        QStringLiteral("opus"),
        // images
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("heic"),
        QStringLiteral("avif")
    };
    return !noCompress.contains(ext);
}

void BlockReadRequest::setFile(const KnownFile& file, bool peerTakesCompression)
{
    // A completed share reads from itself, a partfile from its .part; the rule
    // lives on KnownFile so every reader agrees on it.
    filePath = file.dataFilePath();
    std::memcpy(fileHash.data(), file.fileHash(), 16);
    isPartFile = file.isPartFile();
    compress = peerTakesCompression && UploadDiskIOThread::shouldCompressFile(file.fileName());
}

void UploadDiskIOThread::run()
{
    while (m_run.load()) {
        BlockReadRequest req;
        {
            std::unique_lock lock(m_mutex);
            const auto ready = [this] { return !m_requestQueue.empty() || !m_run.load(); };
            if (m_haveReaders.load()) {
                // Only tick while there is something to close.
                if (!m_condition.wait_for(lock, std::chrono::milliseconds(
                                                    std::max<uint64>(m_idleCloseMs.load() / 2, 50)), ready)) {
                    lock.unlock();
                    closeIdleReaders();
                    continue;
                }
            } else {
                m_condition.wait(lock, ready);
            }
            if (!m_run.load())
                break;
            req = std::move(m_requestQueue.front());
            m_requestQueue.pop_front();
        }
        // Disk I/O + signal emission WITHOUT holding the mutex —
        // allows queueBlockRead() to proceed concurrently.
        readBlock(req);
    }
}

void UploadDiskIOThread::readBlock(const BlockReadRequest& req)
{
    if (req.filePath.isEmpty() || !req.client)
        return;

    if (req.startOffset >= req.endOffset) {
        emitReadError(req);
        return;
    }

    uint64 dataLen = req.endOffset - req.startOffset;
    if (dataLen > EMBLOCKSIZE * 3) {
        logWarning(QStringLiteral("UploadDiskIOThread: Block too large: %1").arg(dataLen));
        emitReadError(req);
        return;
    }

    QByteArray data;
    {
        std::lock_guard lock(m_readerMutex);
        if (!readRange(req, data)) {
            // Drop the reader so a retry starts from a fresh handle.
            m_readers.erase(req.client);
            m_haveReaders.store(!m_readers.empty());
            data.clear();
        }
    }
    if (static_cast<uint64>(data.size()) != dataLen) {
        emitReadError(req);
        return;
    }

    const uint8* fileHash = req.fileHash.data();
    const bool isPartFile = req.isPartFile;

    // Create packets — compressed unless the peer or the file type rules it out
    QList<std::shared_ptr<Packet>> packets;
    if (req.compress)
        packets = createPackedPackets(fileHash, isPartFile, req.startOffset, req.endOffset, data);
    else
        packets = createStandardPackets(fileHash, isPartFile, req.startOffset, req.endOffset, data);

    emit blockPacketsReady(req.client,
                           QByteArray(reinterpret_cast<const char*>(fileHash), 16),
                           req.startOffset, req.endOffset, packets);
}

void UploadDiskIOThread::releaseClient(const UpDownClient* client)
{
    std::lock_guard lock(m_readerMutex);
    m_readers.erase(client);
    m_haveReaders.store(!m_readers.empty());
}

void UploadDiskIOThread::releaseFile(const QString& path)
{
    std::lock_guard lock(m_readerMutex);
    std::erase_if(m_readers, [&](const auto& kv) { return kv.second.path == path; });
    m_haveReaders.store(!m_readers.empty());
    const uint64 now = getTickCount();
    std::erase_if(m_releasedPaths, [now](const auto& kv) { return kv.second <= now; });
    m_releasedPaths[path] = now + kReleaseHoldMs;
}

std::size_t UploadDiskIOThread::cachedReaderCount()
{
    std::lock_guard lock(m_readerMutex);
    return m_readers.size();
}

bool UploadDiskIOThread::readRange(const BlockReadRequest& req, QByteArray& data)
{
    const uint64 now = getTickCount();
    const uint64 dataLen = req.endOffset - req.startOffset;

    // A part file is renamed on completion and its bytes past the request may still
    // change, so it gets neither a kept handle nor read-ahead. Same for a path that
    // is about to be renamed or deleted.
    const auto released = m_releasedPaths.find(req.filePath);
    if (req.isPartFile || (released != m_releasedPaths.end() && released->second > now))
        return readUncached(req, data);

    Reader& reader = m_readers[req.client];
    m_haveReaders.store(true);
    if (!reader.file || reader.path != req.filePath) {
        reader = Reader{};
        reader.path = req.filePath;
        reader.file = std::make_unique<QFile>(req.filePath);
        if (!openForRead(*reader.file)) {
            logWarning(QStringLiteral("UploadDiskIOThread: Cannot open file: %1").arg(req.filePath));
            return false;
        }
        ++m_openCount;
    }
    reader.lastUsed = now;

    if (req.startOffset >= reader.aheadStart
        && req.endOffset <= reader.aheadStart + static_cast<uint64>(reader.ahead.size()))
    {
        data = reader.ahead.mid(static_cast<qsizetype>(req.startOffset - reader.aheadStart),
                                static_cast<qsizetype>(dataLen));
        return true;
    }

    const uint64 fileSize = static_cast<uint64>(reader.file->size());
    if (req.endOffset > fileSize) {
        logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 file ends at: %2")
                       .arg(req.endOffset).arg(fileSize));
        return false;
    }
    // Requests come a few blocks at a time and in order; fetch the next ones with this one.
    const uint64 want = dataLen >= EMBLOCKSIZE
        ? std::min(std::max(dataLen, kReadAheadBytes), fileSize - req.startOffset) : dataLen;

    if (!reader.file->seek(static_cast<qint64>(req.startOffset))) {
        logWarning(QStringLiteral("UploadDiskIOThread: Seek failed at offset %1").arg(req.startOffset));
        return false;
    }
    QByteArray buf = reader.file->read(static_cast<qint64>(want));
    ++m_diskReadCount;
    if (static_cast<uint64>(buf.size()) < dataLen) {
        logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 got: %2")
                       .arg(dataLen).arg(buf.size()));
        return false;
    }
    if (static_cast<uint64>(buf.size()) == dataLen) {
        reader.ahead.clear();
        reader.aheadStart = 0;
        data = std::move(buf);
    } else {
        data = buf.left(static_cast<qsizetype>(dataLen));
        reader.aheadStart = req.startOffset;
        reader.ahead = std::move(buf);
    }
    return true;
}

bool UploadDiskIOThread::readUncached(const BlockReadRequest& req, QByteArray& data)
{
    const uint64 dataLen = req.endOffset - req.startOffset;
    QFile file(req.filePath);
    if (!openForRead(file)) {
        logWarning(QStringLiteral("UploadDiskIOThread: Cannot open file: %1").arg(req.filePath));
        return false;
    }
    ++m_openCount;
    if (!file.seek(static_cast<qint64>(req.startOffset))) {
        logWarning(QStringLiteral("UploadDiskIOThread: Seek failed at offset %1").arg(req.startOffset));
        return false;
    }
    data = file.read(static_cast<qint64>(dataLen));
    ++m_diskReadCount;
    if (static_cast<uint64>(data.size()) != dataLen) {
        logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 got: %2")
                       .arg(dataLen).arg(data.size()));
        return false;
    }
    return true;
}

bool UploadDiskIOThread::openForRead(QFile& file)
{
#ifdef Q_OS_WIN
    // QFile leaves FILE_SHARE_DELETE out, so a file being uploaded could be neither
    // deleted nor renamed. MFC shares all three (srchybrid/UploadDiskIOThread.cpp:147).
    QString native = QDir::toNativeSeparators(QFileInfo(file.fileName()).absoluteFilePath());
    if (native.size() >= MAX_PATH && !native.startsWith(QStringLiteral("\\\\?\\"))) {
        if (native.startsWith(QStringLiteral("\\\\")))
            native = QStringLiteral("\\\\?\\UNC") + native.mid(1);
        else
            native.prepend(QStringLiteral("\\\\?\\"));
    }
    const HANDLE handle = ::CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (fd == -1) {
        ::CloseHandle(handle);
        return false;
    }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::_close(fd);   // closes the handle too
        return false;
    }
    return true;
#else
    return file.open(QIODevice::ReadOnly);
#endif
}

void UploadDiskIOThread::closeIdleReaders()
{
    std::lock_guard lock(m_readerMutex);
    const uint64 now = getTickCount();
    const uint64 idle = m_idleCloseMs.load();
    std::erase_if(m_readers, [&](const auto& kv) { return now - kv.second.lastUsed >= idle; });
    m_haveReaders.store(!m_readers.empty());
}

QList<std::shared_ptr<Packet>> UploadDiskIOThread::createStandardPackets(
    const uint8* fileHash, bool isPartFile,
    uint64 startOffset, uint64 endOffset, const QByteArray& data)
{
    QList<std::shared_ptr<Packet>> packets;

    uint32 togo = static_cast<uint32>(endOffset - startOffset);
    int readPos = 0;

    while (togo > 0) {
        // eMule 2026 bandwidth: larger packets reduce framing overhead. MFC default: 10240, threshold 13000.
        // Trade-off (emule-qt#58): an MFC peer at its download limit reads a new source at
        // 200-600 B/s until one whole packet is in, and cancels after 100 s without one
        // (DOWNLOADTIMEOUT). 32 KiB can miss that, MFC's 10 KiB mostly not.
        uint32 nPacketSize = (togo < 40000) ? togo : 32768u;
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
            if (auto* stats = theApp.statistics)
                stats->addUpDataOverheadFileRequest(32);
        } else {
            // Standard: OP_SENDINGPART with 24-byte header (16 hash + 4 start + 4 end)
            packet = std::make_shared<Packet>(OP_SENDINGPART, nPacketSize + 24, OP_EDONKEYPROT, isPartFile);
            md4cpy(&packet->pBuffer[0], fileHash);
            uint32 start32 = static_cast<uint32>(curStart);
            uint32 end32 = static_cast<uint32>(curEnd);
            std::memcpy(&packet->pBuffer[16], &start32, 4);
            std::memcpy(&packet->pBuffer[20], &end32, 4);
            std::memcpy(&packet->pBuffer[24], data.constData() + readPos, nPacketSize);
            if (auto* stats = theApp.statistics)
                stats->addUpDataOverheadFileRequest(24);
        }

        packet->statsPayload = nPacketSize;
        packets.append(packet);
        readPos += static_cast<int>(nPacketSize);
    }

    return packets;
}

// Not MFC, which sends a compressed block for any gain at all (UploadDiskIOThread.cpp:422)
// and so deflates whole blocks of media to save a fraction of a percent.
bool UploadDiskIOThread::compressBlock(const QByteArray& data, std::vector<uint8>& out)
{
    constexpr uLong kProbeBytes = 16 * 1024;
    // out * 20 <= in * 19: at least 5 % smaller
    const auto pays = [](uLong in, uLong packed) { return packed * 20 <= in * 19; };

    const auto* src = reinterpret_cast<const Bytef*>(data.constData());
    const uLong size = static_cast<uLong>(data.size());

    // Level 1 (fastest), as MFC. The head of the block tells whether the rest is
    // worth the time: deflating incompressible data costs several times a plain send.
    if (size > 2 * kProbeBytes) {
        uLongf probeSize = compressBound(kProbeBytes);
        std::vector<uint8> probe(probeSize);
        if (compress2(probe.data(), &probeSize, src, kProbeBytes, 1) != Z_OK
            || !pays(kProbeBytes, probeSize))
            return false;
    }

    uLongf packedSize = size + 300;
    out.resize(packedSize);
    if (compress2(out.data(), &packedSize, src, size, 1) != Z_OK || !pays(size, packedSize))
        return false;
    out.resize(packedSize);
    return true;
}

QList<std::shared_ptr<Packet>> UploadDiskIOThread::createPackedPackets(
    const uint8* fileHash, bool isPartFile,
    uint64 startOffset, uint64 endOffset, const QByteArray& data)
{
    std::vector<uint8> compressed;
    if (!compressBlock(data, compressed))
        return createStandardPackets(fileHash, isPartFile, startOffset, endOffset, data);
    const uLongf compressedSize = static_cast<uLongf>(compressed.size());
    const uint32 originalSize = static_cast<uint32>(endOffset - startOffset);

    QList<std::shared_ptr<Packet>> packets;
    uint32 togo = static_cast<uint32>(compressedSize);
    int readPos = 0;
    uint32 totalPayloadSize = 0;

    while (togo > 0) {
        // eMule 2026 bandwidth: larger packets reduce framing overhead. MFC default: 10240, threshold 13000.
        // Trade-off (emule-qt#58): an MFC peer at its download limit reads a new source at
        // 200-600 B/s until one whole packet is in, and cancels after 100 s without one
        // (DOWNLOADTIMEOUT). 32 KiB can miss that, MFC's 10 KiB mostly not.
        uint32 nPacketSize = (togo < 40000) ? togo : 32768u;
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
        if (auto* stats = theApp.statistics)
            stats->addUpDataOverheadFileRequest(24);

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

void UploadDiskIOThread::emitReadError(const BlockReadRequest& req)
{
    emit readError(req.client, req.filePath,
                   QByteArray(reinterpret_cast<const char*>(req.fileHash.data()), 16),
                   req.startOffset, req.endOffset);
}

} // namespace eMule
