#pragma once

/// @file FrameGrabThread.h
/// @brief Video frame thumbnail extraction — port of MFC CFrameGrabThread.
///
/// Uses QMediaPlayer + QVideoSink (Qt Multimedia) to grab frames at
/// specified positions.  Worker-object pattern: the worker lives on
/// a QThread with its own event loop so that QMediaPlayer signals work.

#include "utils/Types.h"

#include <QImage>
#include <QObject>
#include <QThread>

#include <vector>

namespace eMule {

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

struct FrameGrabRequest {
    QString filePath;
    uint8 frameCount = 1;
    double startTimeSec = 0.0;
    bool reduceColor = false;
    uint16 maxWidth = 0;       // 0 = no limit
};

struct FrameGrabResult {
    QString filePath;
    std::vector<QImage> frames;
};

// ---------------------------------------------------------------------------
// FrameGrabWorker — lives on the thread, does actual grabbing
// ---------------------------------------------------------------------------

class FrameGrabWorker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void grabFrames(eMule::FrameGrabRequest request);

signals:
    void finished(eMule::FrameGrabResult result);
    void error(const QString& errorMessage);

private:
    QImage scaleAndReduce(const QImage& frame, uint16 maxWidth, bool reduceColor) const;
};

// ---------------------------------------------------------------------------
// FrameGrabThread — host thread with convenience API
// ---------------------------------------------------------------------------

class FrameGrabThread : public QThread {
    Q_OBJECT

public:
    explicit FrameGrabThread(QObject* parent = nullptr);
    ~FrameGrabThread() override;

    /// Submit a grab request (thread-safe, queued connection).
    void requestGrab(FrameGrabRequest request);

signals:
    void finished(eMule::FrameGrabResult result);
    void error(const QString& errorMessage);
    void startGrab(eMule::FrameGrabRequest request);

private:
    FrameGrabWorker* m_worker = nullptr;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::FrameGrabRequest)
Q_DECLARE_METATYPE(eMule::FrameGrabResult)
