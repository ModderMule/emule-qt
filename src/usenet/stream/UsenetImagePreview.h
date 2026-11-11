#pragma once

/// @file UsenetImagePreview.h
/// @brief Pictures of a release before it is downloaded: images posted with it
///        (preview.jpg, covers) and frames from the head of its main video.
///
/// Search Details › Images runs one of these when the tab is opened, never
/// before. Only the articles needed are fetched: the posted images in full, the
/// video's first kHeadBytes (and its last articles when an mp4 keeps its index
/// at the end). A RAR'd video is decompressed from the head of its first volume;
/// stored or compressed both work, since only the start of the stream is read.
///
/// Runs on the daemon thread with its own small pool (kLanes per server), so
/// a busy queue's connection budget is not touched beyond that.

#include "nzb/NzbInfo.h"
#include "nntp/NntpError.h"
#include "prefs/NewsServer.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <vector>

namespace eMule::usenet {

class ArticleFetcher;
class ArticleWriter;
class NntpServerPool;
class NntpSocket;

struct UsenetPreviewImage {
    enum class Kind { Posted, Frame, Cover };
    QString name;
    Kind kind = Kind::Posted;
    QByteArray data;   ///< encoded image (jpg/png/...), as posted or JPEG for frames
};

/// Which NZB files a preview reads. Pure, for tests.
struct UsenetPreviewPlan {
    QList<int> images;   ///< posted image files, NZB order
    int video = -1;      ///< video file, or the opening volume of the largest RAR set
    bool videoInArchive = false;
};

class UsenetImagePreview : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxImages = 12;
    static constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;   ///< encoded, per image
    static constexpr qint64 kHeadBytes = 24 * 1024 * 1024;      ///< encoded, of the video
    static constexpr int kTailSegments = 4;                     ///< mp4 with moov at the end
    static constexpr int kFrames = 6;
    static constexpr int kFrameWidth = 1280;
    static constexpr int kLanes = 3;   ///< connections per server, articles in flight

    UsenetImagePreview(QList<NewsServer> servers, const QNetworkProxy& proxy,
                       QObject* parent = nullptr);
    ~UsenetImagePreview() override;

    [[nodiscard]] static UsenetPreviewPlan plan(const NzbInfo& nzb);

    /// Fetch and grab. finished() fires once, also when nothing was found.
    void start(const NzbInfo& nzb);

    /// Where the fetched video head was put (tests).
    [[nodiscard]] QString videoPath() const { return m_videoPath; }
    /// The video's own name, inside its archive when packed; empty before the head.
    [[nodiscard]] QString videoName() const { return m_videoName; }

signals:
    void finished(const QList<eMule::usenet::UsenetPreviewImage>& images, const QString& note);

private:
    /// One article.
    struct Job {
        int file = -1;
        int segment = -1;
    };
    /// One connection working through the jobs.
    struct Lane {
        std::unique_ptr<ArticleFetcher> fetcher;
        std::unique_ptr<ArticleWriter> writer;
        NntpSocket* socket = nullptr;
        Job job;
        QString piecePath;
        int level = 0;
        int retries = 0;
        bool busy = false;   ///< connecting or fetching
    };

    void fetchJobs(QList<Job> jobs, std::function<void()> done);
    void runLane(Lane& lane);
    void onFetchFinished(Lane& lane, NntpError error, const QString& text);
    void kickIdleLanes();
    void dropSocket(Lane& lane, bool reusable);
    [[nodiscard]] bool laneHasServer(int level) const;
    /// Fetched pieces of @p file into @p path at their offsets; bytes written, -1 on error.
    qint64 assemble(int file, const QString& path, qint64* contiguous = nullptr);
    void collectImages();
    void startVideo();
    void onVideoHead();
    void grabFrames(const QString& path, double span);
    void finish(const QString& note = {});

    std::unique_ptr<NntpServerPool> m_pool;
    QTemporaryDir m_dir;
    NzbInfo m_nzb;
    UsenetPreviewPlan m_plan;
    QList<UsenetPreviewImage> m_images;
    std::vector<std::unique_ptr<Lane>> m_lanes;
    QList<Job> m_jobs;
    std::function<void()> m_jobsDone;
    QHash<int, QMap<qint64, QString>> m_pieces;   ///< file -> offset -> piece path
    int m_pieceCounter = 0;
    int m_missing = 0;
    qint64 m_videoSize = 0;     ///< declared size of the video file (=ybegin size)
    QString m_videoPath;
    QString m_videoName;
    qint64 m_videoContiguous = 0;   ///< head bytes readable from 0
    bool m_tailFetched = false;
    bool m_done = false;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetPreviewImage)
