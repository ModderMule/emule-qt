#include "pch.h"
/// @file UsenetImagePreview.cpp
/// @brief Posted images and video head frames for search Details › Images.

#include "stream/UsenetImagePreview.h"

#include "archive/ArchiveUnpack.h"
#include "media/FrameGrabThread.h"
#include "nntp/ArticleFetcher.h"
#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"
#include "post/UsenetUnpacker.h"
#include "queue/ArticleWriter.h"
#include "utils/Log.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace eMule::usenet {

namespace {

const QStringList kImageExts{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                             QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp")};
const QStringList kVideoExts{QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"),
                             QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("wmv"),
                             QStringLiteral("webm"), QStringLiteral("ts"),  QStringLiteral("m2ts"),
                             QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("flv")};

QString fileNameOf(const NzbFileInfo& f)
{
    return f.fileName.isEmpty() ? f.subject : f.fileName;
}

QString extOf(const QString& name)
{
    return QFileInfo(name).suffix().toLower();
}

/// Top-level mp4 boxes in @p head: whether moov came before mdat. nullopt = not mp4.
std::optional<bool> mp4MoovFirst(const QByteArray& head)
{
    if (head.size() < 12 || head.mid(4, 4) != "ftyp")
        return std::nullopt;
    qint64 pos = 0;
    while (pos + 8 <= head.size()) {
        quint64 size = qFromBigEndian<quint32>(head.constData() + pos);
        const QByteArray type = head.mid(pos + 4, 4);
        if (type == "moov")
            return true;
        if (type == "mdat")
            return false;
        if (size == 1 && pos + 16 <= head.size())
            size = qFromBigEndian<quint64>(head.constData() + pos + 8);
        if (size < 8)
            return false;
        pos += qint64(size);
    }
    return false;
}

/// Image by its magic, not by decoding: the daemon may ship no image plugins.
bool looksLikeImage(const QByteArray& d)
{
    return d.startsWith("\xFF\xD8\xFF") || d.startsWith("\x89PNG") || d.startsWith("GIF8")
        || d.startsWith("BM") || (d.startsWith("RIFF") && d.mid(8, 4) == "WEBP");
}

/// A fade or title card: near one colour. Sampled on a 32x18 grid.
bool isBlankFrame(const QImage& img)
{
    if (img.isNull())
        return true;
    double sum = 0.0;
    double sq = 0.0;
    int n = 0;
    for (int y = 0; y < 18; ++y) {
        for (int x = 0; x < 32; ++x) {
            const QRgb p = img.pixel(img.width() * (2 * x + 1) / 64, img.height() * (2 * y + 1) / 36);
            const double l = qGray(p);
            sum += l;
            sq += l * l;
            ++n;
        }
    }
    const double mean = sum / n;
    const double sd = std::sqrt(std::max(0.0, sq / n - mean * mean));
    return sd < 24.0 || mean < 20.0 || mean > 240.0;
}

/// Encoded bytes of @p f's leading segments, until @p budget.
int segmentsFor(const NzbFileInfo& f, qint64 budget)
{
    qint64 sum = 0;
    int n = 0;
    for (const NzbSegment& s : f.segments) {
        if (sum >= budget)
            break;
        sum += s.bytes;
        ++n;
    }
    return n;
}

} // namespace

UsenetImagePreview::UsenetImagePreview(QList<NewsServer> servers, const QNetworkProxy& proxy,
                                       QObject* parent)
    : QObject(parent)
    , m_pool(std::make_unique<NntpServerPool>())
{
    for (NewsServer& s : servers)
        s.maxConnections = std::clamp(s.maxConnections, 1, kLanes);
    m_pool->setProxy(proxy);
    m_pool->setIdleTimeout(15'000);
    m_pool->setServers(std::move(servers));
}

UsenetImagePreview::~UsenetImagePreview()
{
    for (auto& lane : m_lanes) {
        lane->fetcher.reset();   // no finished() into a dying object
        if (lane->socket)
            m_pool->release(std::exchange(lane->socket, nullptr), false);
    }
}

UsenetPreviewPlan UsenetImagePreview::plan(const NzbInfo& nzb)
{
    UsenetPreviewPlan plan;
    qint64 bestVideo = 0;
    // RAR sets by base name: total bytes, opening volume
    struct Set {
        qint64 bytes = 0;
        int first = -1;
        int firstIndex = 0;
    };
    QHash<QString, Set> sets;

    for (int i = 0; i < nzb.files.size(); ++i) {
        const NzbFileInfo& f = nzb.files.at(i);
        if (f.isPar2() || f.segments.isEmpty())
            continue;
        const QString name = fileNameOf(f);
        const QString ext = extOf(name);
        const qint64 bytes = f.encodedBytes();
        if (kImageExts.contains(ext)) {
            if (bytes <= kMaxImageBytes && plan.images.size() < kMaxImages)
                plan.images.append(i);
            continue;
        }
        if (kVideoExts.contains(ext)) {
            if (bytes > bestVideo) {
                bestVideo = bytes;
                plan.video = i;
            }
            continue;
        }
        const auto pos = UsenetUnpacker::volumePositionOf(name);
        if (pos.index < 0 || !(ext == QLatin1String("rar") || ext.startsWith(u'r')))
            continue;
        Set& s = sets[pos.baseName];
        s.bytes += bytes;
        if (s.first < 0 || pos.index < s.firstIndex) {
            s.first = i;
            s.firstIndex = pos.index;
        }
    }
    // a plain video wins; else the biggest archive set, if it is bigger than any video
    for (auto it = sets.cbegin(); it != sets.cend(); ++it) {
        if (it->bytes > bestVideo && it->first >= 0) {
            bestVideo = it->bytes;
            plan.video = it->first;
            plan.videoInArchive = true;
        }
    }
    return plan;
}

void UsenetImagePreview::start(const NzbInfo& nzb)
{
    m_nzb = nzb;
    m_plan = plan(nzb);
    if (!m_dir.isValid()) {
        finish(tr("No temporary folder."));
        return;
    }
    if (m_pool->servers().isEmpty()) {
        finish(tr("No news server is configured."));
        return;
    }
    if (m_plan.images.isEmpty() && m_plan.video < 0) {
        finish();
        return;
    }

    QList<Job> jobs;
    for (int fi : std::as_const(m_plan.images))
        for (int s = 0; s < m_nzb.files.at(fi).segments.size(); ++s)
            jobs.append({fi, s});
    QPointer<UsenetImagePreview> self(this);
    fetchJobs(std::move(jobs), [self] {
        if (!self)
            return;
        self->collectImages();
        self->startVideo();
    });
}

void UsenetImagePreview::fetchJobs(QList<Job> jobs, std::function<void()> done)
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

void UsenetImagePreview::runLane(Lane& lane)
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
            QPointer<UsenetImagePreview> self(this);
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

bool UsenetImagePreview::laneHasServer(int level) const
{
    for (const NewsServer& s : m_pool->servers())
        if (s.enabled && s.isValid() && m_pool->levelOf(s.key()) == level)
            return true;
    return false;
}

void UsenetImagePreview::onFetchFinished(Lane& lane, NntpError error, const QString& text)
{
    lane.busy = false;
    if (m_done || lane.job.file < 0)
        return;
    QString flushError;
    if (lane.writer) {
        (void) lane.writer->flush(flushError);
        lane.writer->close();
        lane.writer.reset();
    }
    if (error == NntpError::None) {
        // ToDo: count these bytes in UsenetUsage and honour account quotas
        // the video's real size, for the head fraction
        if (lane.job.file == m_plan.video && m_videoSize <= 0)
            m_videoSize = lane.fetcher->declaredFileSize();
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

void UsenetImagePreview::kickIdleLanes()
{
    for (auto& l : m_lanes)
        if (!l->busy && l->job.file < 0)
            runLane(*l);
}

void UsenetImagePreview::dropSocket(Lane& lane, bool reusable)
{
    if (lane.socket)
        m_pool->release(std::exchange(lane.socket, nullptr), reusable);
}

QString UsenetImagePreview::targetPath(int file) const
{
    return file == m_plan.video ? m_videoPath : m_dir.filePath(QStringLiteral("img%1").arg(file));
}

qint64 UsenetImagePreview::received(int file, qint64* contiguous) const
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

void UsenetImagePreview::collectImages()
{
    for (int fi : std::as_const(m_plan.images)) {
        const QString path = targetPath(fi);
        if (received(fi) <= 0)
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = f.readAll();
        // a missing article leaves a hole: only what decodes is shown
        if (!looksLikeImage(data))
            continue;
        m_images.append({QFileInfo(fileNameOf(m_nzb.files.at(fi))).fileName(),
                         UsenetPreviewImage::Kind::Posted, data});
    }
}

void UsenetImagePreview::startVideo()
{
    if (m_plan.video < 0) {
        finish();
        return;
    }
    const NzbFileInfo& file = m_nzb.files.at(m_plan.video);
    const QString ext = m_plan.videoInArchive ? QStringLiteral("rar") : extOf(fileNameOf(file));
    m_videoPath = m_dir.filePath(QStringLiteral("video.") + ext);
    QList<Job> jobs;
    const int n = segmentsFor(file, kHeadBytes);
    for (int s = 0; s < n; ++s)
        jobs.append({m_plan.video, s});
    QPointer<UsenetImagePreview> self(this);
    fetchJobs(std::move(jobs), [self] {
        if (self)
            self->onVideoHead();
    });
}

void UsenetImagePreview::onVideoHead()
{
    const NzbFileInfo& file = m_nzb.files.at(m_plan.video);
    qint64 contiguous = 0;
    const qint64 written = received(m_plan.video, &contiguous);
    if (!m_tailFetched) {
        if (written <= 0 || contiguous <= 0) {
            finish(tr("The video is not on the server."));
            return;
        }
        m_videoContiguous = contiguous;
    }
    contiguous = m_videoContiguous;

    if (m_plan.videoInArchive) {
        QFile f(m_videoPath);
        QByteArray head;
        if (f.open(QIODevice::ReadOnly))
            head = f.read(contiguous);
        f.close();
        const ArchiveHead unpacked = readArchiveHead(head, kHeadBytes * 4);
        if (unpacked.data.isEmpty()) {
            // encrypted, 7z, or no video in it
            finish();
            return;
        }
        m_videoName = unpacked.entryName;
        const QString ext = extOf(unpacked.entryName);
        if (!kVideoExts.contains(ext)) {
            finish();
            return;
        }
        const QString path = m_dir.filePath(QStringLiteral("unpacked.") + ext);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(unpacked.data) != unpacked.data.size()) {
            finish(out.errorString());
            return;
        }
        out.close();
        const double span = unpacked.entrySize > 0 ? double(unpacked.data.size()) / double(unpacked.entrySize) : 0.0;
        grabFrames(path, span);
        return;
    }

#ifndef Q_OS_WIN
    // ToDo: Windows files are not sparse, the gap would be written out; fetch the tail there too
    // moov at the end: the last articles too, ffmpeg reads them at their offsets
    if (!m_tailFetched) {
        QFile f(m_videoPath);
        const QByteArray head = f.open(QIODevice::ReadOnly) ? f.read(std::min<qint64>(contiguous, 4 << 20))
                                                            : QByteArray();
        if (mp4MoovFirst(head) == false) {
            m_tailFetched = true;
            QList<Job> jobs;
            const int count = int(file.segments.size());
            for (int s = std::max(segmentsFor(file, kHeadBytes), count - kTailSegments); s < count; ++s)
                jobs.append({m_plan.video, s});
            QPointer<UsenetImagePreview> self(this);
            fetchJobs(std::move(jobs), [self] {
                if (self)
                    self->onVideoHead();
            });
            return;
        }
    }
#endif
    m_videoName = QFileInfo(fileNameOf(file)).fileName();
    const double span = m_videoSize > 0 ? double(contiguous) / double(m_videoSize) : 0.0;
    grabFrames(m_videoPath, span);
}

void UsenetImagePreview::grabFrames(const QString& path, double span)
{
    if (span <= 0.0) {
        finish();
        return;
    }
    // stay clear of the cut: the last GOP may be incomplete
    span = std::min(1.0, span * 0.9);

    auto* grabber = new FrameGrabThread(this);
    QPointer<UsenetImagePreview> self(this);
    auto done = [self, grabber](const std::vector<QImage>& frames) {
        if (!self)
            return;
        int n = 0;
        // the first is at 0 s: mostly black or a logo; fades too, while others are left
        std::vector<size_t> keep;
        for (size_t i = frames.size() > 1 ? 1 : 0; i < frames.size(); ++i)
            if (!isBlankFrame(frames[i]) && keep.size() < size_t(kFrames))
                keep.push_back(i);
        if (keep.empty() && !frames.empty())
            keep.push_back(frames.size() - 1);
        for (size_t i : keep) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            // PNG when the jpeg plugin is not deployed
            if (!frames[i].save(&buf, "JPG", 85)) {
                buf.seek(0);
                bytes.clear();
                (void) frames[i].save(&buf, "PNG");
            }
            if (!bytes.isEmpty())
                self->m_images.append({tr("Frame %1").arg(++n), UsenetPreviewImage::Kind::Frame, bytes});
        }
        grabber->deleteLater();
        self->finish();
    };
    connect(grabber, &FrameGrabThread::finished, this,
            [done](const FrameGrabResult& result) { done(result.frames); });
    connect(grabber, &FrameGrabThread::error, this, [done](const QString& error) {
        logUsenetWarning(QStringLiteral("Usenet: preview frames: %1").arg(error));
        done({});
    });
    FrameGrabRequest request;
    request.filePath = path;
    request.frameCount = kFrames + 3;   // spares for the blank ones
    request.maxWidth = kFrameWidth;
    request.spanFraction = span;
    grabber->requestGrab(request);
}

void UsenetImagePreview::finish(const QString& note)
{
    if (m_done)
        return;
    m_done = true;
    for (auto& lane : m_lanes)
        dropSocket(*lane, true);
    if (m_missing > 0)
        logUsenet(QStringLiteral("Usenet: preview skipped %1 missing article(s)").arg(m_missing));
    emit finished(m_images, note);
}

} // namespace eMule::usenet
