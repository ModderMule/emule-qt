#include "pch.h"
/// @file PartFileWriteThread.cpp
/// @brief Worker for PartFile disk writes and part hashing.

#include "files/PartFileWriteThread.h"

#include "crypto/AICHHashTree.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "utils/DiskLoadLimiter.h"

#include <QFile>

#include <atomic>

namespace eMule {

PartDigest digestPart(QFile& file, const PartDigestRequest& request)
{
    PartDigest digest;
    if (!file.seek(static_cast<qint64>(request.start)))
        return digest;
    QByteArray data;
    {
        // One limiter per part: what it has not paused for yet is dropped with it.
        DiskLoadLimiter diskLoad;
        const DiskLoadLimiter::Read timed(diskLoad);
        data = file.read(static_cast<qint64>(request.length));
    }
    if (static_cast<uint64>(data.size()) != request.length)
        return digest;

    // A fresh tree with the geometry of the recovery set's node for this part.
    std::unique_ptr<AICHHashTree> tree;
    if (request.wantAICH)
        tree = std::make_unique<AICHHashTree>(request.aichDataSize, request.aichLeftBranch,
                                              request.aichBaseSize);

    // One pass fills both hashes, as MFC's single CreateHash call does.
    KnownFile::createHashFromMemory(reinterpret_cast<const uint8*>(data.constData()),
                                    static_cast<uint32>(request.length), digest.md4.data(),
                                    tree.get());
    digest.read = true;
    if (tree && tree->m_hashValid) {
        digest.aichValid = true;
        digest.aich = tree->m_hash;
    }
    return digest;
}

PartFileWriteThread::PartFileWriteThread(QObject* parent)
    : QThread(parent)
{
    connect(this, &PartFileWriteThread::jobDone, this, &PartFileWriteThread::onJobDone,
            Qt::QueuedConnection);
    start();
}

PartFileWriteThread::~PartFileWriteThread()
{
    requestStop();
    wait();
}

quint64 PartFileWriteThread::nextToken()
{
    static std::atomic<quint64> serial{0};
    return ++serial;
}

void PartFileWriteThread::enqueue(PartFileWriteJob job, PartFile* owner)
{
    QMutexLocker locker(&m_mutex);
    if (owner)
        m_owners[job.token] = owner;
    m_queue.push_back(std::move(job));
    m_wake.wakeOne();
}

std::optional<PartFileWriteResult> PartFileWriteThread::waitFor(quint64 token)
{
    QMutexLocker locker(&m_mutex);
    const auto pending = [&] {
        if (m_runningToken == token)
            return true;
        return std::ranges::any_of(m_queue, [token](const PartFileWriteJob& j) { return j.token == token; });
    };
    while (!m_results.contains(token) && pending())
        m_done.wait(&m_mutex);

    m_owners.erase(token);
    const auto it = m_results.find(token);
    if (it == m_results.end())
        return std::nullopt;
    PartFileWriteResult result = std::move(it->second);
    m_results.erase(it);
    return result;
}

std::optional<PartFileWriteResult> PartFileWriteThread::takeResult(quint64 token)
{
    QMutexLocker locker(&m_mutex);
    m_owners.erase(token);
    const auto it = m_results.find(token);
    if (it == m_results.end())
        return std::nullopt;
    PartFileWriteResult result = std::move(it->second);
    m_results.erase(it);
    return result;
}

void PartFileWriteThread::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_wake.wakeAll();
}

void PartFileWriteThread::run()
{
    for (;;) {
        PartFileWriteJob job;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.empty() && !m_stopRequested)
                m_wake.wait(&m_mutex);
            if (m_queue.empty())
                return;                  // stopping, and nothing left to write
            job = std::move(m_queue.front());
            m_queue.pop_front();
            m_runningToken = job.token;
        }

        PartFileWriteResult result = execute(job);
        const QByteArray fileHash = result.fileHash;
        const quint64 token = result.token;
        {
            QMutexLocker locker(&m_mutex);
            m_results.emplace(token, std::move(result));
            m_runningToken = 0;
            m_done.wakeAll();
        }
        emit jobDone(fileHash, token);
    }
}

void PartFileWriteThread::onJobDone(const QByteArray& /*fileHash*/, quint64 token)
{
    // By owner, not by hash: two files may carry the same hash for a moment (a
    // finished download and its re-download), and only one of them asked.
    PartFile* owner = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_owners.find(token);
        if (it != m_owners.end())
            owner = it->second;
    }
    // Taken by a synchronous flush in the meantime: already dealt with.
    auto result = takeResult(token);
    if (result && owner)
        owner->applyFlushResult(*result);
}

PartFileWriteResult PartFileWriteThread::execute(PartFileWriteJob& job)
{
    PartFileWriteResult result;
    result.fileHash = job.fileHash;
    result.token = job.token;

    QFile file(job.partPath);
    if (!file.open(QIODevice::ReadWrite)) {
        result.error = file.errorString();
        result.chunks = std::move(job.chunks);
        return result;
    }

    bool wrote = true;
    for (const auto& chunk : job.chunks) {
        if (!file.seek(static_cast<qint64>(chunk.start))
            || file.write(reinterpret_cast<const char*>(chunk.data.data()),
                          static_cast<qint64>(chunk.data.size()))
                   != static_cast<qint64>(chunk.data.size())) {
            wrote = false;
            break;
        }
    }
    if (wrote && !file.flush())
        wrote = false;
    if (!wrote) {
        result.error = file.errorString();
        result.chunks = std::move(job.chunks);
        return result;
    }
    result.written = true;

    for (const PartDigestRequest& request : job.digests)
        result.digests.emplace(request.part, digestPart(file, request));
    return result;
}

} // namespace eMule
