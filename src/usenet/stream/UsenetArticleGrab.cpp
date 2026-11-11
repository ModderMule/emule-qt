#include "pch.h"
/// @file UsenetArticleGrab.cpp
/// @brief Articles fetched outside the queue — see UsenetArticleGrab.h.

#include "stream/UsenetArticleGrab.h"

#include "nntp/ArticleFetcher.h"
#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"
#include "queue/ArticleWriter.h"
#include "utils/Log.h"

#include <QFile>
#include <QPointer>

#include <algorithm>

namespace eMule::usenet {

UsenetArticleGrab::UsenetArticleGrab(QList<NewsServer> servers, const QNetworkProxy& proxy, QObject* parent)
    : QObject(parent)
    , m_pool(std::make_unique<NntpServerPool>())
{
    for (NewsServer& s : servers)
        s.maxConnections = std::clamp(s.maxConnections, 1, kLanes);
    m_pool->setProxy(proxy);
    m_pool->setIdleTimeout(15'000);
    m_pool->setServers(std::move(servers));
}

UsenetArticleGrab::~UsenetArticleGrab()
{
    for (auto& lane : m_lanes) {
        lane->fetcher.reset();   // no finished() into a dying object
        if (lane->socket) {
            chargeBytes(lane->socket);
            m_pool->release(std::exchange(lane->socket, nullptr), false);
        }
    }
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

bool UsenetArticleGrab::canStart(QString& note) const
{
    if (!m_dir.isValid()) {
        note = tr("No temporary folder.");
        return false;
    }
    const auto& servers = m_pool->servers();
    if (servers.isEmpty()) {
        note = tr("No news server is configured.");
        return false;
    }
    if (std::ranges::none_of(servers, [](const NewsServer& s) { return s.enabled && s.isValid(); })) {
        note = tr("No news server can be used: they are off or have spent their allowance.");
        return false;
    }
    return true;
}

QList<UsenetArticleGrab::Job> UsenetArticleGrab::allSegments(int file) const
{
    QList<Job> jobs;
    for (int s = 0; s < m_nzb.files.at(file).segments.size(); ++s)
        jobs.append({file, s});
    return jobs;
}

void UsenetArticleGrab::fetchJobs(QList<Job> jobs, std::function<void()> done)
{
    m_jobs = std::move(jobs);
    m_jobsDone = std::move(done);
    if (m_lanes.empty()) {
        for (int i = 0; i < kLanes; ++i) {
            auto lane = std::make_unique<Lane>();
            lane->fetcher = std::make_unique<ArticleFetcher>();
            Lane* raw = lane.get();
            connect(lane->fetcher.get(), &ArticleFetcher::finished, this,
                    [this, raw](NntpError error, const QString& text) { onFetchFinished(*raw, error, text); });
            m_lanes.push_back(std::move(lane));
        }
    }
    for (auto& lane : m_lanes)
        runLane(*lane);
}

QString UsenetArticleGrab::targetPath(int file) const
{
    return m_dir.filePath(QStringLiteral("file%1").arg(file));
}

void UsenetArticleGrab::articleDone(const Job&, const ArticleFetcher&)
{
}

qint64 UsenetArticleGrab::received(int file, qint64* contiguous) const
{
    qint64 total = 0;
    qint64 end = 0;   // bytes readable from 0 without a hole
    const auto ranges = m_ranges.value(file);
    for (auto it = ranges.cbegin(); it != ranges.cend(); ++it) {
        total += it.value();
        if (it.key() <= end)
            end = std::max(end, it.key() + it.value());
    }
    if (contiguous)
        *contiguous = end;
    return total;
}

QByteArray UsenetArticleGrab::readReceived(int file) const
{
    if (received(file) <= 0)
        return {};
    QFile f(targetPath(file));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool UsenetArticleGrab::stop()
{
    if (m_done)
        return false;
    m_done = true;
    for (auto& lane : m_lanes)
        dropSocket(*lane, true);
    if (m_missing > 0)
        logUsenet(QStringLiteral("Usenet: preview skipped %1 missing article(s)").arg(m_missing));
    return true;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void UsenetArticleGrab::runLane(Lane& lane)
{
    if (m_done || lane.busy)
        return;
    if (lane.job.file < 0) {
        if (m_jobs.isEmpty()) {
            // the batch is over when no lane holds a job any more
            const bool idle = std::ranges::none_of(m_lanes, [](const auto& l) { return l->job.file >= 0; });
            if (idle) {
                if (auto done = std::exchange(m_jobsDone, {}))
                    done();
            }
            return;
        }
        lane.job = m_jobs.takeFirst();
        lane.level = 0;
        lane.retries = 0;
    }
    if (lane.level > m_pool->maxLevel()) {
        // no server has it
        ++m_missing;
        lane.job = {};
        runLane(lane);
        return;
    }
    if (!lane.socket) {
        lane.socket = m_pool->acquire(lane.level);
        if (!lane.socket) {
            // this rung is full or absent: a lane past the first waits for its turn
            if (&lane != m_lanes.front().get() && lane.level == 0 && m_pool->maxLevel() >= 0
                && !m_pool->servers().isEmpty() && laneHasServer(lane.level)) {
                m_jobs.prepend(std::exchange(lane.job, {}));
                return;
            }
            ++lane.level;
            runLane(lane);
            return;
        }
        if (!lane.socket->isReady()) {
            lane.busy = true;
            QPointer<UsenetArticleGrab> self(this);
            Lane* raw = &lane;
            NntpSocket* socket = lane.socket;
            auto readyConn = std::make_shared<QMetaObject::Connection>();
            auto failConn = std::make_shared<QMetaObject::Connection>();
            *readyConn = connect(socket, &NntpSocket::ready, this, [self, raw, readyConn, failConn] {
                disconnect(*readyConn);
                disconnect(*failConn);
                if (!self)
                    return;
                raw->busy = false;
                self->runLane(*raw);
            });
            *failConn = connect(socket, &NntpSocket::failed, this,
                                [self, raw, readyConn, failConn](NntpError, const QString& text) {
                disconnect(*readyConn);
                disconnect(*failConn);
                if (!self)
                    return;
                logUsenetWarning(QStringLiteral("Usenet: preview connection failed: %1").arg(text));
                raw->busy = false;
                self->dropSocket(*raw, false);
                ++raw->level;   // try the next rung's server
                self->runLane(*raw);
            });
            return;
        }
    }
    // a writer per lane on the file itself: each keeps its own position
    lane.writer = std::make_unique<ArticleWriter>();
    QString error;
    if (!lane.writer->open(targetPath(lane.job.file), error)) {
        logUsenetWarning(QStringLiteral("Usenet: preview: %1").arg(error));
        ++m_missing;
        lane.job = {};
        lane.writer.reset();
        runLane(lane);
        return;
    }
    lane.busy = true;
    const NzbFileInfo& file = m_nzb.files.at(lane.job.file);
    lane.fetcher->fetch(lane.socket, file.segments.at(lane.job.segment), lane.writer.get(), file.groups);
}

bool UsenetArticleGrab::laneHasServer(int level) const
{
    for (const NewsServer& s : m_pool->servers())
        if (s.enabled && s.isValid() && m_pool->levelOf(s.key()) == level)
            return true;
    return false;
}

void UsenetArticleGrab::onFetchFinished(Lane& lane, NntpError error, const QString& text)
{
    lane.busy = false;
    chargeBytes(lane.socket);
    if (m_done || lane.job.file < 0)
        return;
    QString flushError;
    if (lane.writer) {
        (void) lane.writer->flush(flushError);
        lane.writer->close();
        lane.writer.reset();
    }
    if (error == NntpError::None) {
        articleDone(lane.job, *lane.fetcher);
        m_ranges[lane.job.file].insert(lane.fetcher->decodedOffset(), lane.fetcher->decodedBytes());
        lane.job = {};
        runLane(lane);
        kickIdleLanes();
        return;
    }
    if (escalatesToNextLevel(error)) {
        dropSocket(lane, true);
        ++lane.level;
        runLane(lane);
        return;
    }
    // the connection went wrong: a fresh one, twice, then skip the article
    dropSocket(lane, false);
    if (++lane.retries > 2) {
        logUsenetWarning(QStringLiteral("Usenet: preview article skipped: %1").arg(text));
        ++m_missing;
        lane.job = {};
    }
    runLane(lane);
}

void UsenetArticleGrab::kickIdleLanes()
{
    for (auto& l : m_lanes)
        if (!l->busy && l->job.file < 0)
            runLane(*l);
}

void UsenetArticleGrab::dropSocket(Lane& lane, bool reusable)
{
    if (!lane.socket)
        return;
    chargeBytes(lane.socket);   // a failed handshake was read too
    m_pool->release(std::exchange(lane.socket, nullptr), reusable);
}

void UsenetArticleGrab::chargeBytes(NntpSocket* socket)
{
    if (!socket)
        return;
    if (const qint64 n = socket->takeBytesRead(); n > 0)
        emit bytesUsed(socket->server().accountId, n);
}

} // namespace eMule::usenet
