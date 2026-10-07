#pragma once

/// @file PartFileWriteThread.h
/// @brief Worker that writes download buffers to the .part file and hashes the
///        parts they complete, so neither blocks the network loop.
///
/// A job is self-contained and the worker never touches a PartFile: it gets a
/// path, the bytes and what to hash, and hands back what it read. The verdict
/// on a part is the main thread's, against the hash sets it holds then.

#include "crypto/AICHData.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QFile;

namespace eMule {

class PartFile;

/// One part to read back and hash. With wantAICH the tree is built to the given
/// geometry, so its root compares with the stored part hash.
struct PartDigestRequest {
    uint32 part = 0;
    uint64 start = 0;
    uint64 length = 0;
    bool wantAICH = false;
    uint64 aichDataSize = 0;
    bool aichLeftBranch = false;
    uint64 aichBaseSize = 0;
};

/// What a part's bytes hash to. `read` false: the part could not be read.
struct PartDigest {
    bool read = false;
    std::array<uint8, 16> md4{};
    bool aichValid = false;
    AICHHash aich;
};

/// Reads the part through `file` (open, readable) and hashes it. Thread-safe as
/// long as the QFile is the caller's own.
[[nodiscard]] PartDigest digestPart(QFile& file, const PartDigestRequest& request);

struct PartFileWriteJob {
    struct Chunk {
        uint64 start = 0;
        std::vector<uint8> data;
    };

    QByteArray fileHash;                    ///< for the log only
    quint64 token = 0;                      ///< PartFileWriteThread::nextToken()
    QString partPath;
    std::vector<Chunk> chunks;              ///< written in order
    std::vector<PartDigestRequest> digests; ///< hashed after the write
};

struct PartFileWriteResult {
    QByteArray fileHash;
    quint64 token = 0;
    bool written = false;
    QString error;
    /// The job's chunks again when the write failed: nothing was lost.
    std::vector<PartFileWriteJob::Chunk> chunks;
    std::map<uint32, PartDigest> digests;
};

class PartFileWriteThread : public QThread {
    Q_OBJECT
public:
    /// Starts the thread.
    explicit PartFileWriteThread(QObject* parent = nullptr);
    /// Finishes every queued job, then stops.
    ~PartFileWriteThread() override;

    [[nodiscard]] static quint64 nextToken();

    /// `owner` gets the result through applyFlushResult() on the main thread. It
    /// must collect the job (waitFor) before it dies; PartFile does, in flushBuffer().
    void enqueue(PartFileWriteJob job, PartFile* owner = nullptr);

    /// Blocks until the job with this token is done and returns its result; null
    /// if there is no such job or its result was already taken.
    [[nodiscard]] std::optional<PartFileWriteResult> waitFor(quint64 token);
    [[nodiscard]] std::optional<PartFileWriteResult> takeResult(quint64 token);

    /// Stop after the queue is empty. Queued writes are data: none is dropped.
    void requestStop();

signals:
    void jobDone(const QByteArray& fileHash, quint64 token);

protected:
    void run() override;

private slots:
    void onJobDone(const QByteArray& fileHash, quint64 token);

private:
    [[nodiscard]] static PartFileWriteResult execute(PartFileWriteJob& job);

    QMutex m_mutex;
    QWaitCondition m_wake;                  ///< work arrived / stop
    QWaitCondition m_done;                  ///< a result landed
    std::deque<PartFileWriteJob> m_queue;
    std::map<quint64, PartFileWriteResult> m_results;
    std::map<quint64, PartFile*> m_owners;  ///< by token; gone once the result is taken
    quint64 m_runningToken = 0;
    bool m_stopRequested = false;
};

} // namespace eMule
