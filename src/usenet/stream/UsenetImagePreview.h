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
/// Runs on the daemon thread; the fetching is UsenetArticleGrab's.

#include "stream/UsenetArticleGrab.h"

#include <QList>
#include <QString>

namespace eMule::usenet {

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

class UsenetImagePreview : public UsenetArticleGrab {
    Q_OBJECT

public:
    static constexpr int kMaxImages = 12;
    static constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;   ///< encoded, per image
    static constexpr qint64 kHeadBytes = 24 * 1024 * 1024;      ///< encoded, of the video
    static constexpr int kTailSegments = 4;                     ///< mp4 with moov at the end
    static constexpr int kFrames = 6;
    static constexpr int kFrameWidth = 1280;

    UsenetImagePreview(QList<NewsServer> servers, const QNetworkProxy& proxy,
                       QObject* parent = nullptr);

    [[nodiscard]] static UsenetPreviewPlan plan(const NzbInfo& nzb);

    /// Fetch and grab. finished() fires once, also when nothing was found.
    void start(const NzbInfo& nzb);

    /// Where the fetched video head was put (tests).
    [[nodiscard]] QString videoPath() const { return m_videoPath; }
    /// The video's own name, inside its archive when packed; empty before the head.
    [[nodiscard]] QString videoName() const { return m_videoName; }

signals:
    void finished(const QList<eMule::usenet::UsenetPreviewImage>& images, const QString& note);

protected:
    [[nodiscard]] QString targetPath(int file) const override;
    void articleDone(const Job& job, const ArticleFetcher& fetcher) override;

private:
    void collectImages();
    void startVideo();
    void onArchivePeek();
    void onVideoHead();
    void grabFrames(const QString& path, double span);
    void finish(const QString& note = {});

    UsenetPreviewPlan m_plan;
    QList<UsenetPreviewImage> m_images;
    qint64 m_videoSize = 0;     ///< declared size of the video file (=ybegin size)
    QString m_videoPath;
    QString m_videoName;
    qint64 m_videoContiguous = 0;   ///< head bytes readable from 0
    bool m_tailFetched = false;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetPreviewImage)
