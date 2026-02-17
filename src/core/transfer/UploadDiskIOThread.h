#pragma once

/// @file UploadDiskIOThread.h
/// @brief Async disk reads for upload — replaces MFC CUploadDiskIOThread.
///
/// Replaces Windows IOCP with a queue-based worker thread using QFile.
/// Emits signals when block packets are ready for sending.

#include "utils/Types.h"

#include <QList>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace eMule {

class KnownFile;
class Packet;
class UpDownClient;

/// Block read request posted to the IO thread.
struct BlockReadRequest {
    KnownFile* file = nullptr;
    UpDownClient* client = nullptr;
    uint64 startOffset = 0;
    uint64 endOffset = 0;
    bool disableCompression = false;
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

    /// Determine if a file should be compressed based on extension.
    [[nodiscard]] static bool shouldCompressFile(const QString& fileName);

signals:
    /// Emitted when block packets are ready to be sent.
    void blockPacketsReady(eMule::UpDownClient* client,
                           QList<std::shared_ptr<eMule::Packet>> packets);
    /// Emitted on read error for a client.
    void readError(eMule::UpDownClient* client);

protected:
    void run() override;

private:
    void processRequests();
    void readBlock(const BlockReadRequest& req);

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
};

} // namespace eMule
