#pragma once

/// @file UploadDiskIOThread.h
/// @brief Async disk reads for upload — replaces MFC CUploadDiskIOThread.
///
/// Replaces Windows IOCP with one worker per drive using QFile. MFC's overlapped
/// reads did not wait for each other; one serial queue did: a USB drive stalling
/// for seconds starved every slot, on every drive. A drive is told from the mount
/// table, never by touching the file, so routing cannot hang on a stalled disk.
/// Emits signals when block packets are ready for sending.

#include "utils/Opcodes.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QThread>

#include <thread>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class QFile;

namespace eMule {

class KnownFile;
class Packet;
class UpDownClient;

/// Block read request posted to the IO thread. Self-contained: the worker must not
/// touch the KnownFile, which lives (and dies) on the main thread.
struct BlockReadRequest {
    QString filePath;                   // where the bytes are (KnownFile::dataFilePath)
    std::array<uint8, 16> fileHash{};
    UpDownClient* client = nullptr;     // opaque token, only compared on return
    uint64 startOffset = 0;
    uint64 endOffset = 0;
    bool isPartFile = false;
    bool compress = false;

    /// Fill the file-derived fields. Main thread only.
    void setFile(const KnownFile& file, bool peerTakesCompression);
};

class UploadDiskIOThread : public QThread {
    Q_OBJECT
public:
    explicit UploadDiskIOThread(QObject* parent = nullptr);
    ~UploadDiskIOThread() override;

    UploadDiskIOThread(const UploadDiskIOThread&) = delete;
    UploadDiskIOThread& operator=(const UploadDiskIOThread&) = delete;

    void endThread();
    void wakeUp();

    /// Queue a block read for the given client/file.
    void queueBlockRead(BlockReadRequest request);

    /// Close the reader kept for @p client — it left the upload queue.
    void releaseClient(const UpDownClient* client);
    /// Close every reader on @p path and keep it uncached for kReleaseHoldMs: the caller
    /// is about to rename or delete the file. Waits for at most the read in progress.
    void releaseFile(const QString& path);

    // Bytes read beyond a full-block request of a complete file, kept for the next ones.
    static constexpr uint64 kReadAheadBytes = 3 * EMBLOCKSIZE;
    static constexpr uint64 kReleaseHoldMs = 3000;
    /// A read slower than this is logged (at most once a minute per drive).
    static constexpr uint64 kSlowReadMs = 1000;
    /// A read still running this long is logged while it runs.
    static constexpr uint64 kStuckReadMs = 5000;
    static constexpr uint64 kSlowReadLogGapMs = 60'000;

    /// A reader unused this long is closed. Settable for tests.
    void setIdleCloseMs(uint64 ms) { m_idleCloseMs.store(ms); }
    [[nodiscard]] uint64 openCount() const { return m_openCount.load(); }
    [[nodiscard]] uint64 diskReadCount() const { return m_diskReadCount.load(); }
    [[nodiscard]] std::size_t cachedReaderCount();
    /// Slow and stuck reads logged so far.
    [[nodiscard]] uint64 slowReadWarnings() const { return m_slowReadWarnings.load(); }
    /// Workers started, one per drive read from.
    [[nodiscard]] std::size_t driveWorkerCount();

    /// The drive @p path is on, from the mount table (no I/O on the drive itself).
    /// Empty when unknown: those reads share one worker.
    [[nodiscard]] static QString driveOf(const QString& path);

    /// Tests: lower the slow/stuck read thresholds.
    void setReadWarnMs(uint64 slowMs, uint64 stuckMs) { m_slowReadMs.store(slowMs); m_stuckReadMs.store(stuckMs); }
    /// Tests: pick the drive per path, and run @p hook on the worker before each read.
    void setDriveResolver(std::function<QString(const QString&)> resolver);
    void setBeforeReadHook(std::function<void(const QString& path)> hook);

    /// Determine if a file should be compressed based on extension.
    [[nodiscard]] static bool shouldCompressFile(const QString& fileName);
    /// zlib-compress one block into @p out. False when it does not pay: the result
    /// must be at least 5 % smaller, and a block whose head does not shrink is not
    /// compressed at all.
    [[nodiscard]] static bool compressBlock(const QByteArray& data, std::vector<uint8>& out);

signals:
    /// Emitted when block packets are ready to be sent. fileId/start/end name the requested
    /// block, so the receiver can match it against the client's still-pending requests.
    void blockPacketsReady(eMule::UpDownClient* client, QByteArray fileId,
                           quint64 startOffset, quint64 endOffset,
                           QList<std::shared_ptr<eMule::Packet>> packets);
    /// Emitted on read error for a client. Names the block and the path it was read
    /// from, so the receiver can tell a file that moved since from a broken one.
    void readError(eMule::UpDownClient* client, QString filePath, QByteArray fileId,
                   quint64 startOffset, quint64 endOffset);

protected:
    /// Housekeeping: closes idle readers, logs reads stuck on a drive.
    void run() override;

private:
    /// One slot's open file and what was read past its last request. Shared, so a
    /// release never waits for a read in progress (except releaseFile, which must).
    struct Reader {
        std::mutex readMutex;   // held across the disk access
        std::unique_ptr<QFile> file;
        QString path;
        QByteArray ahead;
        uint64 aheadStart = 0;
        bool retired = false;   // released: the next read opens its own handle
        std::atomic<uint64> lastUsed{0};
    };

    /// One drive: its own queue and worker.
    struct Lane {
        QString drive;
        std::deque<BlockReadRequest> queue;   // m_mutex
        std::condition_variable wake;
        std::thread worker;
        // The read in progress, m_mutex
        QString readPath;
        uint64 readStartTick = 0;   // 0: none
        bool stuckLogged = false;
        // Slow-read log throttle, worker only
        uint64 lastSlowLogTick = 0;
        uint32 slowNotLogged = 0;
    };

    void laneLoop(Lane& lane);
    void readBlock(Lane& lane, const BlockReadRequest& req);
    void emitReadError(const BlockReadRequest& req);
    /// The bytes of @p req, through the slot's kept reader.
    [[nodiscard]] bool readRange(const BlockReadRequest& req, QByteArray& data);
    [[nodiscard]] bool readUncached(const BlockReadRequest& req, QByteArray& data);
    /// Open read-only without locking the file against deletion or renaming.
    [[nodiscard]] static bool openForRead(QFile& file);
    void closeIdleReaders();
    void logStuckReads();
    void noteReadTime(Lane& lane, const BlockReadRequest& req, uint64 ms);
    [[nodiscard]] Lane& laneFor(const QString& drive);   // caller holds m_mutex
    void forgetReader(const UpDownClient* client, const std::shared_ptr<Reader>& reader);

    static QList<std::shared_ptr<Packet>> createStandardPackets(
        const uint8* fileHash, bool isPartFile,
        uint64 startOffset, uint64 endOffset, const QByteArray& data);
    static QList<std::shared_ptr<Packet>> createPackedPackets(
        const uint8* fileHash, bool isPartFile,
        uint64 startOffset, uint64 endOffset, const QByteArray& data);

    std::mutex m_mutex;                        // lanes and their queues
    std::condition_variable m_condition;       // housekeeping
    std::vector<std::unique_ptr<Lane>> m_lanes;
    std::atomic<bool> m_run{true};
    std::atomic<int> m_busyLanes{0};

    std::mutex m_readerMutex;   // the maps only, never held across a disk access
    std::unordered_map<const UpDownClient*, std::shared_ptr<Reader>> m_readers;
    std::vector<std::weak_ptr<Reader>> m_allReaders;       // incl. ones only a worker holds
    std::unordered_map<QString, uint64> m_releasedPaths;   // path → uncached until
    std::atomic<bool> m_haveReaders{false};
    std::atomic<uint64> m_idleCloseMs{10'000};
    std::atomic<uint64> m_openCount{0};
    std::atomic<uint64> m_diskReadCount{0};
    std::atomic<uint64> m_slowReadWarnings{0};
    std::atomic<uint64> m_slowReadMs{kSlowReadMs};
    std::atomic<uint64> m_stuckReadMs{kStuckReadMs};

    std::mutex m_hookMutex;
    std::function<QString(const QString&)> m_driveResolver;
    std::function<void(const QString&)> m_beforeRead;
};

} // namespace eMule
