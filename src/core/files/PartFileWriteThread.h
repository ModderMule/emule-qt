#pragma once

/// @file PartFileWriteThread.h
/// @brief Async buffered I/O thread for PartFile disk writes.
///
/// Port of MFC PartFileWriteThread. Replaces Windows IO Completion Ports
/// with QThread + QWaitCondition.

#include "utils/Types.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <list>
#include <vector>

namespace eMule {

class PartFile;

// ---------------------------------------------------------------------------
// WriteJob — single write operation
// ---------------------------------------------------------------------------

struct WriteJob {
    PartFile* partFile = nullptr;
    QString filePath;
    uint64 offset = 0;
    std::vector<uint8> data;
};

// ---------------------------------------------------------------------------
// PartFileWriteThread
// ---------------------------------------------------------------------------

class PartFileWriteThread : public QThread {
    Q_OBJECT
public:
    explicit PartFileWriteThread(QObject* parent = nullptr);

    void enqueueWrite(WriteJob job);
    void requestStop();

signals:
    void writeCompleted(eMule::PartFile* file);
    void writeError(eMule::PartFile* file, const QString& error);

protected:
    void run() override;

private:
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::list<WriteJob> m_queue;
    bool m_stopRequested = false;
};

} // namespace eMule
