#include "pch.h"
// This file is part of eMule
// Copyright (C) 2002-2024 Merkur
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from MFC CFrameGrabThread — video frame extraction.
// Uses QMediaPlayer + QVideoSink (Qt Multimedia) instead of DirectShow COM.

#include "media/FrameGrabThread.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace eMule {

// ===================================================================
// FrameGrabWorker
// ===================================================================

void FrameGrabWorker::grabFrames(eMule::FrameGrabRequest request)
{
    // Validate file exists
    if (!QFileInfo::exists(request.filePath)) {
        emit error(QStringLiteral("File does not exist: %1").arg(request.filePath));
        return;
    }

    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(request.filePath));

    // Wait for the media to load (or error out)
    {
        QEventLoop loop;
        bool loaded = false;
        bool loadError = false;

        auto statusConn = QObject::connect(&player, &QMediaPlayer::mediaStatusChanged,
            [&](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia ||
                    status == QMediaPlayer::BufferedMedia) {
                    loaded = true;
                    loop.quit();
                } else if (status == QMediaPlayer::InvalidMedia ||
                           status == QMediaPlayer::NoMedia) {
                    loadError = true;
                    loop.quit();
                }
            });

        auto errorConn = QObject::connect(&player, &QMediaPlayer::errorOccurred,
            [&](QMediaPlayer::Error, const QString&) {
                loadError = true;
                loop.quit();
            });

        // Check if already in final state
        auto currentStatus = player.mediaStatus();
        if (currentStatus == QMediaPlayer::LoadedMedia ||
            currentStatus == QMediaPlayer::BufferedMedia) {
            loaded = true;
        } else if (currentStatus == QMediaPlayer::InvalidMedia ||
                   currentStatus == QMediaPlayer::NoMedia) {
            loadError = true;
        }

        if (!loaded && !loadError) {
            QTimer::singleShot(10'000, &loop, &QEventLoop::quit); // 10s timeout
            loop.exec();
        }

        QObject::disconnect(statusConn);
        QObject::disconnect(errorConn);

        if (!loaded || loadError) {
            emit error(QStringLiteral("Failed to load media: %1").arg(request.filePath));
            return;
        }
    }

    const qint64 durationMs = player.duration();
    if (durationMs <= 0) {
        emit error(QStringLiteral("Cannot determine media duration: %1").arg(request.filePath));
        return;
    }

    // Calculate seek positions
    const qint64 startMs = static_cast<qint64>(request.startTimeSec * 1000.0);
    const qint64 availableMs = durationMs - startMs;
    if (availableMs <= 0) {
        emit error(QStringLiteral("Start time beyond media duration"));
        return;
    }

    const int frameCount = std::max(1, static_cast<int>(request.frameCount));
    std::vector<qint64> seekPositions;
    seekPositions.reserve(static_cast<size_t>(frameCount));

    if (frameCount == 1) {
        seekPositions.push_back(startMs);
    } else {
        const qint64 interval = availableMs / frameCount;
        for (int i = 0; i < frameCount; ++i)
            seekPositions.push_back(startMs + i * interval);
    }

    FrameGrabResult result;
    result.filePath = request.filePath;
    result.frames.reserve(static_cast<size_t>(frameCount));

    // Grab frames at each position
    for (qint64 posMs : seekPositions) {
        QEventLoop frameLoop;
        QImage capturedImage;
        bool frameCaptured = false;

        auto frameConn = QObject::connect(&sink, &QVideoSink::videoFrameChanged,
            [&](const QVideoFrame& frame) {
                if (frameCaptured)
                    return;
                QVideoFrame copy = frame;
                capturedImage = copy.toImage();
                if (!capturedImage.isNull()) {
                    frameCaptured = true;
                    frameLoop.quit();
                }
            });

        player.setPosition(posMs);
        player.play();

        if (!frameCaptured) {
            QTimer::singleShot(5'000, &frameLoop, &QEventLoop::quit); // 5s per frame
            frameLoop.exec();
        }

        player.pause();
        QObject::disconnect(frameConn);

        if (frameCaptured && !capturedImage.isNull()) {
            result.frames.push_back(
                scaleAndReduce(capturedImage, request.maxWidth, request.reduceColor));
        }
    }

    if (result.frames.empty()) {
        emit error(QStringLiteral("No frames could be captured from: %1").arg(request.filePath));
        return;
    }

    emit finished(std::move(result));
}

QImage FrameGrabWorker::scaleAndReduce(const QImage& frame, uint16 maxWidth, bool reduceColor) const
{
    QImage result = frame;

    // Scale down if wider than maxWidth
    if (maxWidth > 0 && result.width() > maxWidth) {
        result = result.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    }

    // Reduce color depth (convert to 8-bit indexed)
    if (reduceColor) {
        result = result.convertToFormat(QImage::Format_Indexed8);
    }

    return result;
}

// ===================================================================
// FrameGrabThread
// ===================================================================

FrameGrabThread::FrameGrabThread(QObject* parent)
    : QThread(parent)
{
    m_worker = new FrameGrabWorker;
    m_worker->moveToThread(this);

    // Forward worker signals to thread signals
    connect(m_worker, &FrameGrabWorker::finished,
            this, &FrameGrabThread::finished);
    connect(m_worker, &FrameGrabWorker::error,
            this, &FrameGrabThread::error);

    // Queued connection: requestGrab → worker::grabFrames
    connect(this, &FrameGrabThread::startGrab,
            m_worker, &FrameGrabWorker::grabFrames);

    start();
}

FrameGrabThread::~FrameGrabThread()
{
    quit();
    wait();
    delete m_worker;
}

void FrameGrabThread::requestGrab(FrameGrabRequest request)
{
    emit startGrab(std::move(request));
}

} // namespace eMule
