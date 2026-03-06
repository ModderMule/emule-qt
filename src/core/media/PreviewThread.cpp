#include "pch.h"
// This file is part of eMule
// Copyright (C) 2002-2024 Merkur
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from MFC CPreviewThread — background file copy + app launch.

#include "media/PreviewThread.h"

#include <QFile>
#include <QProcess>

namespace eMule {

// ===================================================================
// Public API
// ===================================================================

PreviewThread::PreviewThread(PreviewRequest request, QObject* parent)
    : QThread(parent)
    , m_request(std::move(request))
{
}

// ===================================================================
// Thread entry point
// ===================================================================

void PreviewThread::run()
{
    if (!copyPartialFile())
        return;

    if (!launchApp()) {
        cleanupTempFile();
        return;
    }

    cleanupTempFile();
}

// ===================================================================
// Private helpers
// ===================================================================

bool PreviewThread::copyPartialFile()
{
    QFile src(m_request.sourceFilePath);
    if (!src.open(QIODevice::ReadOnly)) {
        emit previewError(
            QStringLiteral("Cannot open source file: %1").arg(m_request.sourceFilePath));
        return false;
    }

    QFile dst(m_request.destFilePath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit previewError(
            QStringLiteral("Cannot create temp file: %1").arg(m_request.destFilePath));
        return false;
    }

    constexpr qint64 kChunkSize = 64 * 1024; // 64 KB chunks
    uint64 remaining = m_request.bytesToCopy;

    while (remaining > 0) {
        const qint64 toRead = static_cast<qint64>(std::min(remaining, static_cast<uint64>(kChunkSize)));
        const QByteArray chunk = src.read(toRead);

        if (chunk.isEmpty()) {
            emit previewError(QStringLiteral("Read error during file copy"));
            return false;
        }

        if (dst.write(chunk) != chunk.size()) {
            emit previewError(QStringLiteral("Write error during file copy"));
            return false;
        }

        remaining -= static_cast<uint64>(chunk.size());
    }

    dst.flush();
    return true;
}

bool PreviewThread::launchApp()
{
    emit previewStarted(m_request.app.title);

    QProcess process;

    // Build arguments: replace %1 with the temp file path
    QString args = m_request.app.commandArgs;
    if (args.contains(QStringLiteral("%1"))) {
        args.replace(QStringLiteral("%1"), m_request.destFilePath);
    } else {
        // If no %1 placeholder, append the file path
        if (!args.isEmpty())
            args += u' ';
        args += u'"' + m_request.destFilePath + u'"';
    }

    process.setProgram(m_request.app.command);
    process.setArguments(QProcess::splitCommand(args));
    process.start();

    if (!process.waitForStarted(10'000)) {
        emit previewError(
            QStringLiteral("Failed to start preview app: %1").arg(m_request.app.command));
        return false;
    }

    // Wait indefinitely for the preview app to finish
    process.waitForFinished(-1);

    const int exitCode = process.exitCode();
    emit previewFinished(m_request.app.title, exitCode);
    return true;
}

void PreviewThread::cleanupTempFile()
{
    if (!m_request.destFilePath.isEmpty())
        QFile::remove(m_request.destFilePath);
}

} // namespace eMule
