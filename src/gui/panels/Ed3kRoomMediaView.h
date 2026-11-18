#pragma once

/// @file Ed3kRoomMediaView.h
/// @brief The media of an ed3k room's posts in its log (room.md §2.6, §6): an inline image
/// (animated GIF/WebP play), a video playing muted and looped (QtMultimedia), or a box saying
/// what a click does.
///
/// - The log draws `<img src="emuleqt-media:<root>">`; this keeps that document resource: a
///   placeholder until the bytes come, then the frame. Frames replace the resource; the log
///   only repaints, never lays out again.
/// - Decoded by the bytes (utils/MediaSniff.h), never by the claimed type; at most
///   kMaxPixels a side before decoding (room.md §6).
/// - Videos and animations run while the tab shows (setActive).
/// - A click: a ready one opens big (a video with sound then), else asks for the fetch.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

class QBuffer;
class QMediaPlayer;
class QMovie;
class QVideoFrame;
class QVideoSink;

namespace eMule {

class LogTextView;

class Ed3kRoomMediaView : public QObject {
    Q_OBJECT

public:
    /// Ed3kRoomMedia::State of the daemon.
    enum State : int { None = 0, Fetching = 1, Ready = 2, Failed = 3, Click = 4, Ignored = 5 };
    /// A post's media as the daemon lists it.
    struct Ref {
        QString root;
        qint64 size = 0;
        QString type;
        QString name;
        int width = 0;
        int height = 0;
        int state = None;
    };
    /// Image side above which nothing is decoded.
    static constexpr int kMaxPixels = 8192;

    explicit Ed3kRoomMediaView(LogTextView* log, QObject* parent = nullptr);
    ~Ed3kRoomMediaView() override;

    /// How the GUI gets a ready file's bytes (IPC GetEd3kRoomMedia); it calls setBytes.
    void setLoader(std::function<void(const QString& root)> load) { m_load = std::move(load); }
    /// HTML for @p m under its post; its resource is in the log's document after this.
    [[nodiscard]] QString html(const Ref& m);
    /// The daemon's state of @p root; true: the post must be drawn again (file line <-> image).
    bool setState(const QString& root, int state);
    [[nodiscard]] int state(const QString& root) const;
    /// The opened file of @p root (GetEd3kRoomMedia).
    void setBytes(const QString& root, const QByteArray& bytes);
    /// Tab on screen: videos and animations run; else paused.
    void setActive(bool on);
    /// A click on @p root's link or image.
    void clicked(const QString& root, QWidget* parent);
    /// emuleqt-media:<root> -> root; empty: not ours.
    [[nodiscard]] static QString rootOfLink(const QString& link);

signals:
    /// Fetch @p root now (FetchEd3kRoomMedia).
    void fetchRequested(const QString& root);

private:
    struct Item {
        Ref ref;
        QSize box;                       ///< shown size, CSS pixels
        QByteArray bytes;                ///< the opened file, once loaded
        QImage frame;                    ///< the last frame put, put again after a clear
        bool loading = false;
        bool decoded = false;
        bool broken = false;             ///< bytes we cannot show
        qint64 lastFrameMs = 0;
        std::unique_ptr<QBuffer> buffer;
        std::unique_ptr<QMovie> movie;
        std::unique_ptr<QMediaPlayer> player;
        std::unique_ptr<QVideoSink> sink;
    };

    [[nodiscard]] static QSize boxOf(const Ref& m);
    [[nodiscard]] bool showsInline(const Item& it) const;
    void putPlaceholder(Item& it);
    void putFrame(Item& it, const QImage& frame);
    void decode(Item& it);
    void startVideo(Item& it);
    void loadIfReady(Item& it);
    [[nodiscard]] QUrl urlOf(const QString& root) const;
    void showBig(Item& it, QWidget* parent);

    QPointer<LogTextView> m_log;
    std::function<void(const QString&)> m_load;
    QHash<QString, std::shared_ptr<Item>> m_items;
    bool m_active = true;
};

} // namespace eMule
