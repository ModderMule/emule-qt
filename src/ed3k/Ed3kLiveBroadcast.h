#pragma once

/// @file Ed3kLiveBroadcast.h
/// @brief Broadcasting live channels (live.md §1, §2, §4) from an HLS fMP4 output folder, as
/// OBS or `ffmpeg -f hls -hls_segment_type fmp4` write it: the init data (`EXT-X-MAP`, e.g.
/// init.mp4), the `.m4s` segments and the `.m3u8` media playlist.
///
/// - start(): the channel key of the title is made once and kept (Config/ed3k/live/<id>.key,
///   owner-only, never logged; channels.json names them), liveBroadcast, the `ed3k:live`
///   link given back (with `s` for a private channel: then a credential, never logged).
/// - The playlist is read every 500 ms: each new segment (by `EXT-X-MEDIA-SEQUENCE` + index)
///   goes to livePushSegment with its EXTINF in ms, `gap` after `EXT-X-DISCONTINUITY`, a
///   changed `EXT-X-MAP` or a playlist that started over; its init data with it. At the start
///   only the newest segment goes out (a live stream, not a recording).
/// - stop(): liveEnd (the `end` record, §2.2).
/// - Not resumed after a restart of the daemon: the user starts again (the same title keeps
///   its channel, so the link stays).
///
/// ToDo: a paid channel (subscription.md §7: offer, passes for its own nodes) is not offered.
/// ToDo: the channel file (§1.2, keywords) is not published; the link is how viewers come.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

/// An HLS media playlist as far as a broadcast needs it (RFC 8216 §4.3).
struct Ed3kHlsPlaylist {
    struct Entry {
        QString uri;              ///< relative to the playlist
        std::uint32_t ms = 0;     ///< EXTINF
        bool discontinuity = false;
        QString map;              ///< EXT-X-MAP URI in force; empty = none
    };
    std::uint64_t mediaSequence = 0;
    std::vector<Entry> entries;
    bool ended = false;           ///< EXT-X-ENDLIST

    /// nullopt: no `#EXTM3U` first line.
    [[nodiscard]] static std::optional<Ed3kHlsPlaylist> parse(const QByteArray& text);
};

class Ed3kLiveBroadcast : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString folder;           ///< HLS output folder
        QString playlist;         ///< a .m3u8 in it; empty = the newest there
        QString title;            ///< dn; also names the channel key kept
        QString media = QStringLiteral("video/mp4");
        QString codecs;           ///< RFC 6381; empty = unknown
        bool isPrivate = false;   ///< live.md §10: a secret `s` in the link
    };
    struct Info {
        QString id;               ///< the stream, 32 hex digits
        QString title;
        QString folder;
        QString link;             ///< `ed3k:live?…`; with `s` for a private channel (a credential)
        bool isPrivate = false;
        int neighbours = 0;
        std::uint64_t segments = 0;
        std::uint64_t bytes = 0;
        QString error;            ///< the last problem, localized; empty = none
    };

    /// Segments larger than this are skipped (live.md §2.1), init data larger too.
    static constexpr qint64 kSegmentMax = 4 << 20;
    static constexpr qint64 kInitMax = 256 << 10;

    /// @p configDir: Config/ (ed3k/live/ below it).
    Ed3kLiveBroadcast(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kLiveBroadcast() override;

    /// Starts broadcasting @p o; the error is user-visible (localized).
    std::expected<Info, QString> start(const Options& o);
    /// Ends the broadcast @p id (liveEnd). False: not one of ours.
    bool stop(const QString& id);
    [[nodiscard]] bool isBroadcast(const QString& id) const;
    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& id) const;

    /// Reads the playlists now (the timer does every 500 ms).
    void scan();

signals:
    void changed();

private:
    struct Running {
        ::ed3k::Stream stream;
        Options options;
        QString link;
        std::optional<std::uint64_t> last;   ///< the newest segment number pushed
        QString mapPath;                     ///< the init data of the last segment
        QByteArray init;
        bool gapNext = false;                ///< a segment was skipped: the next is not continuous
        std::set<::ed3k::NodeId> neighbours;
        std::uint64_t segments = 0;
        std::uint64_t bytes = 0;
        QString error;
    };
    struct Channel {
        QByteArray id;            ///< 16 B, the channel `id`
        bool isPrivate = false;
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void scanOne(Running& r);
    bool push(Running& r, const Ed3kHlsPlaylist::Entry& e, const QString& dir, bool gap);
    [[nodiscard]] QString playlistOf(const Running& r) const;
    Running* find(const ::ed3k::Stream& stream);
    [[nodiscard]] Info infoOf(const Running& r) const;
    void markChanged();
    /// The channel of @p title (made and saved when new).
    std::optional<Channel> channelOf(const QString& title, bool isPrivate);
    [[nodiscard]] QString liveDir() const;

    Ed3kSession& m_session;
    QString m_configDir;
    std::map<::ed3k::Stream, Running> m_running;
    QTimer m_timer;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
