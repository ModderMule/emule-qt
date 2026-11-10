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

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>


#include <zlib.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#if defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
#include <sys/mount.h>
#include <sys/param.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace eMule {

namespace {

/// Mount point → drive, longest mount point first. Read from the kernel's table,
/// which never waits on a drive.
struct MountTable {
    std::vector<std::pair<QString, QString>> mounts;
    uint64 loadedTick = 0;
};

constexpr uint64 kMountTableRefreshMs = 30'000;

#if defined(Q_OS_LINUX)
/// mountinfo escapes space, tab, newline and backslash as octal.
QString unescapeMountPath(const QByteArray& raw)
{
    QByteArray out;
    out.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 3 < raw.size()) {
            bool ok = false;
            const int c = raw.mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(static_cast<char>(c));
                i += 3;
                continue;
            }
        }
        out.append(raw[i]);
    }
    return QFile::decodeName(out);
}

/// "8:1" → "sda": partitions of one disk share its worker.
QString wholeDiskOf(const QByteArray& majorMinor)
{
    const QString dev = QFileInfo(QStringLiteral("/sys/dev/block/") + QString::fromLatin1(majorMinor))
                            .canonicalFilePath();
    if (dev.isEmpty())
        return QStringLiteral("dev:") + QString::fromLatin1(majorMinor);
    const QFileInfo info(dev);
    if (QFileInfo::exists(dev + QStringLiteral("/partition")))
        return info.dir().dirName();
    return info.fileName();
}
#endif

std::vector<std::pair<QString, QString>> readMounts()
{
    std::vector<std::pair<QString, QString>> mounts;
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    if (!file.open(QIODevice::ReadOnly))
        return mounts;
    QHash<QByteArray, QString> disks;
    // readAll: a /proc file has size 0, so atEnd() is true before the first read
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray& line : lines) {
        // id parent major:minor root mountpoint ...
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 5)
            continue;
        auto disk = disks.constFind(fields[2]);
        if (disk == disks.constEnd())
            disk = disks.insert(fields[2], wholeDiskOf(fields[2]));
        mounts.emplace_back(unescapeMountPath(fields[4]), *disk);
    }
#elif defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
    struct statfs* table = nullptr;
    const int count = ::getmntinfo(&table, MNT_NOWAIT);
    static const QRegularExpression wholeDisk(QStringLiteral("^/dev/(disk\\d+)"));
    for (int i = 0; i < count; ++i) {
        QString from = QFile::decodeName(table[i].f_mntfromname);
        // APFS: /dev/disk3s1 and /dev/disk3s5 are one physical disk
        if (const auto m = wholeDisk.match(from); m.hasMatch())
            from = m.captured(1);
        mounts.emplace_back(QFile::decodeName(table[i].f_mntonname), from);
    }
#endif
    std::ranges::sort(mounts, [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    return mounts;
}

} // namespace

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
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(m_mutex);
        for (auto& lane : m_lanes) {
            lane->wake.notify_all();
            if (lane->worker.joinable())
                workers.push_back(std::move(lane->worker));
        }
        m_condition.notify_all();
    }
    // A read hung on a dead drive holds this, as the single worker did before
    for (std::thread& worker : workers)
        worker.join();
    if (isRunning())
        wait();
}

void UploadDiskIOThread::wakeUp()
{
    std::lock_guard lock(m_mutex);
    for (auto& lane : m_lanes)
        lane->wake.notify_one();
}

void UploadDiskIOThread::queueBlockRead(BlockReadRequest request)
{
    std::function<QString(const QString&)> resolver;
    {
        std::lock_guard lock(m_hookMutex);
        resolver = m_driveResolver;
    }
    const QString drive = resolver ? resolver(request.filePath) : driveOf(request.filePath);

    std::lock_guard lock(m_mutex);
    if (!m_run.load())
        return;
    Lane& lane = laneFor(drive);
    lane.queue.push_back(std::move(request));
    lane.wake.notify_one();
}

QString UploadDiskIOThread::driveOf(const QString& path)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QStringLiteral("\\\\"))) {
        // \\server\share: one network drive
        const QStringList parts = native.mid(2).split(QLatin1Char('\\'));
        return parts.size() >= 2 ? (QStringLiteral("\\\\") + parts[0] + QLatin1Char('\\') + parts[1]).toLower()
                                 : QString();
    }
    if (native.size() >= 2 && native[1] == QLatin1Char(':'))
        return native.left(2).toUpper();
    return {};
#else
    if (!path.startsWith(QLatin1Char('/')))
        return {};
    static std::mutex mutex;
    static MountTable table;
    std::lock_guard lock(mutex);
    const uint64 now = getTickCount();
    if (table.loadedTick == 0 || now - table.loadedTick >= kMountTableRefreshMs) {
        table.mounts = readMounts();
        table.loadedTick = now;
    }
    for (const auto& [mountPoint, drive] : table.mounts) {
        if (mountPoint == QStringLiteral("/") || path == mountPoint
            || (path.startsWith(mountPoint) && path.size() > mountPoint.size()
                && path[mountPoint.size()] == QLatin1Char('/')))
            return drive;
    }
    return {};
#endif
}

void UploadDiskIOThread::setDriveResolver(std::function<QString(const QString&)> resolver)
{
    std::lock_guard lock(m_hookMutex);
    m_driveResolver = std::move(resolver);
}

void UploadDiskIOThread::setBeforeReadHook(std::function<void(const QString&)> hook)
{
    std::lock_guard lock(m_hookMutex);
    m_beforeRead = std::move(hook);
}

std::size_t UploadDiskIOThread::driveWorkerCount()
{
    std::lock_guard lock(m_mutex);
    return m_lanes.size();
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
        {
            std::unique_lock lock(m_mutex);
            const auto stop = [this] { return !m_run.load(); };
            if (m_haveReaders.load() || m_busyLanes.load() > 0) {
                // Ticks only while there is something to close or watch
                const uint64 tick = std::clamp<uint64>(std::min(m_idleCloseMs.load() / 2, m_stuckReadMs.load() / 4),
                                                       20, 1000);
                m_condition.wait_for(lock, std::chrono::milliseconds(tick), stop);
            } else {
                m_condition.wait(lock, [this] {
                    return !m_run.load() || m_busyLanes.load() > 0 || m_haveReaders.load();
                });
            }
        }
        if (!m_run.load())
            break;
        closeIdleReaders();
        logStuckReads();
    }
}

void UploadDiskIOThread::releaseClient(const UpDownClient* client)
{
    // No wait: a read in progress keeps its reader, which closes when it is done
    std::lock_guard lock(m_readerMutex);
    m_readers.erase(client);
    m_haveReaders.store(!m_readers.empty());
}

void UploadDiskIOThread::releaseFile(const QString& path)
{
    std::vector<std::shared_ptr<Reader>> open;
    {
        std::lock_guard lock(m_readerMutex);
        std::erase_if(m_readers, [&](const auto& kv) { return kv.second->path == path; });
        m_haveReaders.store(!m_readers.empty());
        const uint64 now = getTickCount();
        std::erase_if(m_releasedPaths, [now](const auto& kv) { return kv.second <= now; });
        m_releasedPaths[path] = now + kReleaseHoldMs;
        // Also the ones a worker still holds after their slot let them go
        std::erase_if(m_allReaders, [&](const std::weak_ptr<Reader>& weak) {
            const auto reader = weak.lock();
            if (!reader)
                return true;
            if (reader->path == path)
                open.push_back(reader);
            return false;
        });
    }
    // The caller renames or deletes next: wait for a read in progress, then close
    for (const auto& reader : open) {
        std::lock_guard readLock(reader->readMutex);
        reader->retired = true;
        reader->file.reset();
        reader->ahead.clear();
    }
}

std::size_t UploadDiskIOThread::cachedReaderCount()
{
    std::lock_guard lock(m_readerMutex);
    return m_readers.size();
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

UploadDiskIOThread::Lane& UploadDiskIOThread::laneFor(const QString& drive)
{
    for (auto& lane : m_lanes) {
        if (lane->drive == drive)
            return *lane;
    }
    auto lane = std::make_unique<Lane>();
    lane->drive = drive;
    Lane& ref = *lane;
    m_lanes.push_back(std::move(lane));
    ref.worker = std::thread([this, &ref] { laneLoop(ref); });
    logDebug([&]() -> QString {
        return QStringLiteral("Upload reads: worker %1 for drive %2")
            .arg(m_lanes.size()).arg(drive.isEmpty() ? QStringLiteral("(unknown)") : drive);
    });
    return ref;
}

void UploadDiskIOThread::laneLoop(Lane& lane)
{
    for (;;) {
        BlockReadRequest req;
        {
            std::unique_lock lock(m_mutex);
            lane.wake.wait(lock, [&] { return !lane.queue.empty() || !m_run.load(); });
            if (!m_run.load())
                return;
            req = std::move(lane.queue.front());
            lane.queue.pop_front();
            lane.readPath = req.filePath;
            lane.readStartTick = getTickCount();
            lane.stuckLogged = false;
            if (m_busyLanes.fetch_add(1) == 0)
                m_condition.notify_one();   // the housekeeper watches for stuck reads
        }
        // Disk I/O + signal emission WITHOUT holding the mutex
        readBlock(lane, req);
        {
            std::lock_guard lock(m_mutex);
            lane.readStartTick = 0;
            lane.readPath.clear();
            m_busyLanes.fetch_sub(1);
        }
    }
}

void UploadDiskIOThread::readBlock(Lane& lane, const BlockReadRequest& req)
{
    if (req.filePath.isEmpty() || !req.client)
        return;

    if (req.startOffset >= req.endOffset) {
        emitReadError(req);
        return;
    }

    const uint64 dataLen = req.endOffset - req.startOffset;
    if (dataLen > EMBLOCKSIZE * 3) {
        logWarning(QStringLiteral("UploadDiskIOThread: Block too large: %1").arg(dataLen));
        emitReadError(req);
        return;
    }

    std::function<void(const QString&)> beforeRead;
    {
        std::lock_guard lock(m_hookMutex);
        beforeRead = m_beforeRead;
    }
    QElapsedTimer timer;
    timer.start();
    if (beforeRead)
        beforeRead(req.filePath);
    QByteArray data;
    const bool ok = readRange(req, data);
    noteReadTime(lane, req, static_cast<uint64>(timer.elapsed()));
    if (!ok || static_cast<uint64>(data.size()) != dataLen) {
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

bool UploadDiskIOThread::readRange(const BlockReadRequest& req, QByteArray& data)
{
    const uint64 now = getTickCount();
    const uint64 dataLen = req.endOffset - req.startOffset;

    // A part file is renamed on completion and its bytes past the request may still
    // change, so it gets neither a kept handle nor read-ahead. Same for a path that
    // is about to be renamed or deleted.
    std::shared_ptr<Reader> reader;
    {
        std::lock_guard lock(m_readerMutex);
        const auto released = m_releasedPaths.find(req.filePath);
        const bool held = released != m_releasedPaths.end() && released->second > now;
        if (!req.isPartFile && !held) {
            std::shared_ptr<Reader>& slot = m_readers[req.client];
            if (!slot || slot->path != req.filePath) {
                slot = std::make_shared<Reader>();
                slot->path = req.filePath;
                m_allReaders.push_back(slot);
            }
            reader = slot;
            reader->lastUsed.store(now);
            m_haveReaders.store(true);
        }
    }
    if (!reader)
        return readUncached(req, data);

    std::unique_lock readLock(reader->readMutex);
    if (reader->retired) {
        readLock.unlock();
        return readUncached(req, data);
    }
    const auto readKept = [&]() -> bool {
        if (!reader->file) {
            reader->file = std::make_unique<QFile>(req.filePath);
            if (!openForRead(*reader->file)) {
                logWarning(QStringLiteral("UploadDiskIOThread: Cannot open file: %1").arg(req.filePath));
                return false;
            }
            ++m_openCount;
        }

        if (req.startOffset >= reader->aheadStart
            && req.endOffset <= reader->aheadStart + static_cast<uint64>(reader->ahead.size()))
        {
            data = reader->ahead.mid(static_cast<qsizetype>(req.startOffset - reader->aheadStart),
                                     static_cast<qsizetype>(dataLen));
            return true;
        }

        const uint64 fileSize = static_cast<uint64>(reader->file->size());
        if (req.endOffset > fileSize) {
            logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 file ends at: %2")
                           .arg(req.endOffset).arg(fileSize));
            return false;
        }
        // Requests come a few blocks at a time and in order; fetch the next ones with this one.
        const uint64 want = dataLen >= EMBLOCKSIZE
            ? std::min(std::max(dataLen, kReadAheadBytes), fileSize - req.startOffset) : dataLen;

        if (!reader->file->seek(static_cast<qint64>(req.startOffset))) {
            logWarning(QStringLiteral("UploadDiskIOThread: Seek failed at offset %1").arg(req.startOffset));
            return false;
        }
        QByteArray buf = reader->file->read(static_cast<qint64>(want));
        ++m_diskReadCount;
        if (static_cast<uint64>(buf.size()) < dataLen) {
            logWarning(QStringLiteral("UploadDiskIOThread: Read mismatch — wanted: %1 got: %2")
                           .arg(dataLen).arg(buf.size()));
            return false;
        }
        if (static_cast<uint64>(buf.size()) == dataLen) {
            reader->ahead.clear();
            reader->aheadStart = 0;
            data = std::move(buf);
        } else {
            data = buf.left(static_cast<qsizetype>(dataLen));
            reader->aheadStart = req.startOffset;
            reader->ahead = std::move(buf);
        }
        return true;
    };
    if (readKept())
        return true;
    // Drop the reader so a retry starts from a fresh handle
    reader->file.reset();
    reader->ahead.clear();
    readLock.unlock();
    forgetReader(req.client, reader);
    data.clear();
    return false;
}

void UploadDiskIOThread::forgetReader(const UpDownClient* client, const std::shared_ptr<Reader>& reader)
{
    std::lock_guard lock(m_readerMutex);
    if (const auto it = m_readers.find(client); it != m_readers.end() && it->second == reader)
        m_readers.erase(it);
    m_haveReaders.store(!m_readers.empty());
}

void UploadDiskIOThread::closeIdleReaders()
{
    std::lock_guard lock(m_readerMutex);
    const uint64 now = getTickCount();
    const uint64 idle = m_idleCloseMs.load();
    std::erase_if(m_readers, [&](const auto& kv) { return now - kv.second->lastUsed.load() >= idle; });
    std::erase_if(m_allReaders, [](const std::weak_ptr<Reader>& weak) { return weak.expired(); });
    m_haveReaders.store(!m_readers.empty());
}

void UploadDiskIOThread::logStuckReads()
{
    struct Stuck {
        QString drive;
        QString path;
        uint64 ms;
        std::size_t waiting;
    };
    std::vector<Stuck> stuck;
    {
        std::lock_guard lock(m_mutex);
        const uint64 now = getTickCount();
        for (auto& lane : m_lanes) {
            if (lane->readStartTick == 0 || lane->stuckLogged || now - lane->readStartTick < m_stuckReadMs.load())
                continue;
            lane->stuckLogged = true;
            stuck.push_back({lane->drive, lane->readPath, now - lane->readStartTick, lane->queue.size()});
        }
    }
    for (const Stuck& s : stuck) {
        ++m_slowReadWarnings;
        logWarning(QStringLiteral("Upload: disk read waiting for %1 s on drive %2: %3. %4 more reads wait "
                                  "behind it; other drives go on")
                       .arg(s.ms / 1000)
                       .arg(s.drive.isEmpty() ? QStringLiteral("(unknown)") : s.drive,
                            QDir::toNativeSeparators(s.path))
                       .arg(s.waiting));
    }
}

void UploadDiskIOThread::noteReadTime(Lane& lane, const BlockReadRequest& req, uint64 ms)
{
    if (ms < m_slowReadMs.load())
        return;
    bool stuckLogged = false;
    std::size_t waiting = 0;
    {
        std::lock_guard lock(m_mutex);
        stuckLogged = lane.stuckLogged;
        waiting = lane.queue.size();
    }
    // Once a minute per drive, but always the end of a read already logged as stuck
    const uint64 now = getTickCount();
    if (!stuckLogged && lane.lastSlowLogTick != 0 && now - lane.lastSlowLogTick < kSlowReadLogGapMs) {
        ++lane.slowNotLogged;
        return;
    }
    const QString more = lane.slowNotLogged == 0
        ? QString()
        : QStringLiteral(" (%1 more slow reads on it since the last note)").arg(lane.slowNotLogged);
    lane.lastSlowLogTick = now;
    lane.slowNotLogged = 0;
    ++m_slowReadWarnings;
    logWarning(QStringLiteral("Upload: slow disk read, %1 ms for %2 KB of %3 on drive %4, %5 reads waiting%6")
                   .arg(ms)
                   .arg((req.endOffset - req.startOffset) / 1024)
                   .arg(QDir::toNativeSeparators(req.filePath),
                        lane.drive.isEmpty() ? QStringLiteral("(unknown)") : lane.drive)
                   .arg(waiting)
                   .arg(more));
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

} // namespace eMule
