#pragma once

/// @file UploadDiskIOThread.h
/// @brief Async disk reads for upload — replaces MFC CUploadDiskIOThread.
///
/// Replaces Windows IOCP with a queue-based worker thread using QFile.
/// Emits signals when block packets are ready for sending.

#include "utils/Opcodes.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QThread>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

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

    /// A reader unused this long is closed. Settable for tests.
    void setIdleCloseMs(uint64 ms) { m_idleCloseMs.store(ms); }
    [[nodiscard]] uint64 openCount() const { return m_openCount.load(); }
    [[nodiscard]] uint64 diskReadCount() const { return m_diskReadCount.load(); }
    [[nodiscard]] std::size_t cachedReaderCount();

    /// Determine if a file should be compressed based on extension.
    [[nodiscard]] static bool shouldCompressFile(const QString& fileName);

signals:
    /// Emitted when block packets are ready to be sent. fileId/start/end name the requested
    /// block, so the receiver can match it against the client's still-pending requests.
    void blockPacketsReady(eMule::UpDownClient* client, QByteArray fileId,
                           quint64 startOffset, quint64 endOffset,
                           QList<std::shared_ptr<eMule::Packet>> packets);
    /// Emitted on read error for a client.
    void readError(eMule::UpDownClient* client);

protected:
    void run() override;

private:
    /// One slot's open file and what was read past its last request.
    struct Reader {
        std::unique_ptr<QFile> file;
        QString path;
        QByteArray ahead;
        uint64 aheadStart = 0;
        uint64 lastUsed = 0;
    };

    void readBlock(const BlockReadRequest& req);
    /// The bytes of @p req. Caller holds m_readerMutex.
    [[nodiscard]] bool readRange(const BlockReadRequest& req, QByteArray& data);
    [[nodiscard]] bool readUncached(const BlockReadRequest& req, QByteArray& data);
    void closeIdleReaders();

    static QList<std::shared_ptr<Packet>> createStandardPackets(
        const uint8* fileHash, bool isPartFile,
        uint64 startOffset, uint64 endOffset, const QByteArray& data);
    static QList<std::shared_ptr<Packet>> createPackedPackets(
        const uint8* fileHash, bool isPartFile,
        uint64 startOffset, uint64 endOffset, const QByteArray& data);

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<BlockReadRequest> m_requestQueue;
    std::atomic<bool> m_run{true};

    std::mutex m_readerMutex;   // held across a read, so a release waits for it
    std::unordered_map<const UpDownClient*, Reader> m_readers;
    std::unordered_map<QString, uint64> m_releasedPaths;   // path → uncached until
    std::atomic<bool> m_haveReaders{false};
    std::atomic<uint64> m_idleCloseMs{10'000};
    std::atomic<uint64> m_openCount{0};
    std::atomic<uint64> m_diskReadCount{0};
};

} // namespace eMule
