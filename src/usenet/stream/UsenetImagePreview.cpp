#include "pch.h"
/// @file UsenetImagePreview.cpp
/// @brief Posted images and video head frames for search Details › Images.

#include "stream/UsenetImagePreview.h"

#include "archive/ArchiveUnpack.h"
#include "media/FrameGrabThread.h"
#include "nntp/ArticleFetcher.h"
#include "post/UsenetUnpacker.h"
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
    : UsenetArticleGrab(std::move(servers), proxy, parent)
{
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
    if (m_plan.images.isEmpty() && m_plan.video < 0) {
        finish();
        return;
    }
    if (QString note; !canStart(note)) {
        finish(note);
        return;
    }

    QList<Job> jobs;
    for (int fi : std::as_const(m_plan.images))
        jobs.append(allSegments(fi));
    QPointer<UsenetImagePreview> self(this);
    fetchJobs(std::move(jobs), [self] {
        if (!self)
            return;
        self->collectImages();
        self->startVideo();
    });
}

QString UsenetImagePreview::targetPath(int file) const
{
    return file == m_plan.video ? m_videoPath : UsenetArticleGrab::targetPath(file);
}

void UsenetImagePreview::articleDone(const Job& job, const ArticleFetcher& fetcher)
{
    // the video's real size, for the head fraction
    if (job.file == m_plan.video && m_videoSize <= 0)
        m_videoSize = fetcher.declaredFileSize();
}

void UsenetImagePreview::collectImages()
{
    for (int fi : std::as_const(m_plan.images)) {
        const QByteArray data = readReceived(fi);
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
    QPointer<UsenetImagePreview> self(this);
    if (m_plan.videoInArchive) {
        // an archive may hold anything: its first article names the entry, the rest only for a video
        fetchJobs({{m_plan.video, 0}}, [self] {
            if (self)
                self->onArchivePeek();
        });
        return;
    }
    QList<Job> jobs;
    const int n = segmentsFor(file, kHeadBytes);
    for (int s = 0; s < n; ++s)
        jobs.append({m_plan.video, s});
    fetchJobs(std::move(jobs), [self] {
        if (self)
            self->onVideoHead();
    });
}

void UsenetImagePreview::onArchivePeek()
{
    const QByteArray head = readReceived(m_plan.video);
    if (head.isEmpty()) {
        finish(tr("The video is not on the server."));
        return;
    }
    const ArchiveHead peek = readArchiveHead(head, 64 * 1024);
    if (peek.entryName.isEmpty() && peek.data.isEmpty()) {
        // encrypted, 7z, or not an archive we read
        finish();
        return;
    }
    if (!peek.entryName.isEmpty() && !kVideoExts.contains(extOf(peek.entryName))) {
        m_videoName = peek.entryName;
        finish(tr("The archive holds %1, not a video.").arg(QFileInfo(peek.entryName).fileName()));
        return;
    }
    QList<Job> jobs;
    const int n = segmentsFor(m_nzb.files.at(m_plan.video), kHeadBytes);
    for (int s = 1; s < n; ++s)
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
    if (stop())
        emit finished(m_images, note);
}

} // namespace eMule::usenet
