#pragma once

/// @file PartFileConvert.h
/// @brief Legacy format converter — port of MFC CPartFileConvert.
///
/// Imports downloads of other clients into the temp directory and the queue:
/// eMule / eDonkey ".part.met" (also the old eDonkey layouts and split chunk
/// files "NNN.<n>.part") and Shareaza ".sd".

#include "utils/Types.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <list>

namespace eMule {

// ---------------------------------------------------------------------------
// ConvertStatus
// ---------------------------------------------------------------------------

enum class ConvertStatus : uint8 {
    OK             = 0,
    Queued         = 1,
    InProgress     = 2,
    OutOfDiskSpace = 3,
    PartMetNotFound = 4,
    IOError        = 5,
    Failed         = 6,
    BadFormat      = 7,
    AlreadyExists  = 8
};

// ---------------------------------------------------------------------------
// ConvertJob — represents one file to convert
// ---------------------------------------------------------------------------

struct ConvertJob {
    QString folder;
    QString filename;
    QString fileHash;
    uint64 size = 0;
    uint64 spaceNeeded = 0;
    int format = 0;
    ConvertStatus state = ConvertStatus::Queued;
    bool removeSource = false;      ///< delete the source after a successful import
};

// ---------------------------------------------------------------------------
// ConvertThread — background worker for file conversions
// ---------------------------------------------------------------------------

class ConvertThread : public QThread {
    Q_OBJECT
public:
    explicit ConvertThread(QObject* parent = nullptr);
    void requestStop();

protected:
    void run() override;

private:
    QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_stopRequested = false;

    friend class PartFileConvert;
};

// ---------------------------------------------------------------------------
// PartFileConvert — all static methods, manages conversion queue
// ---------------------------------------------------------------------------

class PartFileConvert : public QObject {
    Q_OBJECT
public:
    explicit PartFileConvert(QObject* parent = nullptr);
    ~PartFileConvert() override;

    static void scanFolderToAdd(const QString& folder, bool recursive = false, bool removeSource = false);
    static void addJob(ConvertJob job);
    static void removeJob(int index);
    static void removeAllJobs();
    static void retryJob(int index);

    static int jobCount();
    static ConvertJob jobAt(int index);
    static const std::list<ConvertJob>& jobs();

    static void startThread();
    static void stopThread();
    static void processQueue();

    /// PartFileFormat of a met / .sd file as int, 0 when it is neither.
    static int detectFormat(const QString& filePath);

    /// Perform the actual conversion of a single job.
    /// @return status after conversion attempt.
    static ConvertStatus performConvertToeMule(ConvertJob& job);

signals:
    void jobAdded(int index);
    void jobRemoved(int index);
    void jobUpdated(int index);
    void conversionFinished();

private:
    friend class ConvertThread;

    static std::list<ConvertJob> s_jobs;
    static QMutex s_mutex;
    static ConvertThread* s_thread;
    static QWaitCondition s_condition;
    static bool s_running;
};

} // namespace eMule
