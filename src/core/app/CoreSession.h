#pragma once

/// @file CoreSession.h
/// @brief Lightweight timer driver that calls process() on core managers.
///
/// Drives DownloadQueue, UploadQueue, ListenSocket, KnownFileList,
/// SharedFileList, and Statistics at the correct intervals.
/// This is a minimal timer driver, not the full Module 31 CoreSession.

#include "utils/Types.h"

#include <QObject>
#include <QTimer>

namespace eMule {

class CoreSession : public QObject {
    Q_OBJECT

public:
    explicit CoreSession(QObject* parent = nullptr);

    void start();
    void stop();

private slots:
    void onTimer();

private:
    QTimer m_timer;
    uint32 m_tickCounter = 0;
};

} // namespace eMule
