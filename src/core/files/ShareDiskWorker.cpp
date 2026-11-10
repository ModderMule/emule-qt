#include "pch.h"
/// @file ShareDiskWorker.cpp
/// @brief Runs the shared file list's slow disk reads on a thread of its own.

#include "files/ShareDiskWorker.h"
#include "utils/Log.h"

#include <QThread>

namespace eMule {

namespace {
// How long the destructor waits for a job stuck on a share before leaving it.
constexpr unsigned long kStopWaitMs = 2000;
} // namespace

void ShareDiskJobRunner::run(const ShareDiskJob& job)
{
    emit done(job());
}

ShareDiskWorker::ShareDiskWorker(QObject* parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_runner(new ShareDiskJobRunner)
{
    qRegisterMetaType<ShareDiskApply>("eMule::ShareDiskApply");
    m_thread->setObjectName(QStringLiteral("ShareDiskWorker"));
    m_runner->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &ShareDiskJobRunner::done, this, &ShareDiskWorker::onDone);
    m_thread->start(QThread::LowPriority);
}

ShareDiskWorker::~ShareDiskWorker()
{
    disconnect(m_runner, nullptr, this, nullptr);
    // the thread deletes itself once done, so a share that hangs does not hold up the owner
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->quit();
    if (!m_thread->wait(kStopWaitMs))
        logWarning(QStringLiteral("Shared files disk job still busy, left to finish"));
}

void ShareDiskWorker::run(const QString& lane, ShareDiskJob job)
{
    if (!lane.isEmpty()) {
        const auto it = std::ranges::find(m_queue, lane, &Queued::lane);
        if (it != m_queue.end()) {
            it->job = std::move(job);
            return;
        }
    }
    m_queue.push_back({lane, std::move(job)});
    if (!m_running)
        startNext();
}

bool ShareDiskWorker::busy(const QString& lane) const
{
    return (m_running && m_runningLane == lane)
        || std::ranges::find(m_queue, lane, &Queued::lane) != m_queue.end();
}

bool ShareDiskWorker::waiting(const QString& lane) const
{
    return std::ranges::find(m_queue, lane, &Queued::lane) != m_queue.end();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void ShareDiskWorker::startNext()
{
    if (m_queue.empty())
        return;
    Queued next = std::move(m_queue.front());
    m_queue.pop_front();
    m_running = true;
    m_runningLane = next.lane;
    QMetaObject::invokeMethod(m_runner, [r = m_runner, job = std::move(next.job)] { r->run(job); });
}

void ShareDiskWorker::onDone(const ShareDiskApply& apply)
{
    m_running = false;
    m_runningLane.clear();
    // the next one first: busy() stays true while the owner handles this result
    startNext();
    if (apply)
        apply();
}

} // namespace eMule
