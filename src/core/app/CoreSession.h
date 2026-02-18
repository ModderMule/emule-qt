#pragma once

/// @file CoreSession.h
/// @brief Lightweight timer driver that calls process() on core managers.
///
/// Drives DownloadQueue, UploadQueue, ListenSocket, KnownFileList,
/// SharedFileList, and Statistics at the correct intervals.
/// Creates and owns core upload pipeline components.

#include "utils/Types.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace eMule {

class KnownFileList;
class SharedFileList;
class UploadBandwidthThrottler;
class UploadDiskIOThread;
class UploadQueue;

class CoreSession : public QObject {
    Q_OBJECT

public:
    explicit CoreSession(QObject* parent = nullptr);
    ~CoreSession() override;

    void start();
    void stop();

private slots:
    void onTimer();

private:
    void initUploadPipeline();
    void shutdownUploadPipeline();

    QTimer m_timer;
    uint32 m_tickCounter = 0;

    // Owned components
    std::unique_ptr<KnownFileList> m_knownFileList;
    std::unique_ptr<SharedFileList> m_sharedFileList;
    std::unique_ptr<UploadQueue> m_uploadQueue;
    std::unique_ptr<UploadBandwidthThrottler> m_uploadThrottler;
    std::unique_ptr<UploadDiskIOThread> m_uploadDiskIO;
};

} // namespace eMule
