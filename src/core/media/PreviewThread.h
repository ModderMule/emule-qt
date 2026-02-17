#pragma once

/// @file PreviewThread.h
/// @brief Background preview thread — port of MFC CPreviewThread.
///
/// Copies partial file data to a temp file, launches an external preview
/// application, waits for it to finish, then cleans up the temp file.

#include "media/PreviewApps.h"
#include "utils/Types.h"

#include <QThread>

namespace eMule {

// ---------------------------------------------------------------------------
// PreviewRequest — what to preview and how
// ---------------------------------------------------------------------------

struct PreviewRequest {
    QString sourceFilePath;   // the .part file path
    QString destFilePath;     // temp file to create
    PreviewApp app;           // which app to run
    uint64 bytesToCopy = 0;   // bytes from start to copy
};

// ---------------------------------------------------------------------------
// PreviewThread — sequential: copy → launch → wait → cleanup
// ---------------------------------------------------------------------------

class PreviewThread : public QThread {
    Q_OBJECT

public:
    explicit PreviewThread(PreviewRequest request, QObject* parent = nullptr);

signals:
    void previewStarted(const QString& appTitle);
    void previewFinished(const QString& appTitle, int exitCode);
    void previewError(const QString& errorMessage);

protected:
    void run() override;

private:
    bool copyPartialFile();
    bool launchApp();
    void cleanupTempFile();

    PreviewRequest m_request;
};

} // namespace eMule
