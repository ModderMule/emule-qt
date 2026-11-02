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
/// - stop(): liveEnd (the `end` record, §2.2), the user's stop: the only end. A quit of the
///   daemon or a stopped session is a cut, no `end` (§4, ruling #4): resumed below.
/// - A segment over 4 MiB or init data over 256 KiB (or missing) is skipped, the next one
///   carries `gap` (§4, #11); a changed init goes with `gap` too.
/// - The channel file (§1.2) of a public channel describes the channel, not a broadcast: shared
///   as "<title>.ed3klive" and published under its keywords (Ed3kLiveFiles) while the channel is
///   kept, on air or not (batch 15 #5): at each start of ed3k every channel of channels.json with
///   a description, with its `created` kept (the same bytes, the same root); a paid one with the
///   offer of the offer source. A changed description (start with other media/codecs/comment/
///   lang/kbps, describe(), another offer) is a new file with a later `created`, the old one is
///   withdrawn. deleteChannel() withdraws it and drops the channel.
///   (was: shared while on air only, withdrawn by stop())
/// - Resumed after a restart (§4): channels.json keeps what is on air (folder, start, next
///   seq, the time of the cut); back within kResumeS of the cut it goes on at the next seq of
///   the same start (the core sets `gap`), later as a new broadcast. Only stop() ends it.
///
/// was: (Not resumed after a restart of the daemon: the user starts again.)
/// was: ToDo: a paid channel (subscription.md §7: offer, passes for its own nodes) is not offered.
/// (done: an open channel sold in Ed3kPaidSeller: setOfferSource, `paid` + offer, `paid=1` in
/// the link, the offer in the channel file; a private paid channel too (batch 15): `paid` +
/// offer + secret, no channel file, channelSecret() for the seller's link and paidFollowPrivate)
/// was: ToDo: the channel file (§1.2, keywords) is not published; the link is how viewers come.
/// (done: Ed3kLiveFiles, shared while the channel is kept, batch 15)

#include "Ed3kFfi.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;
class Ed3kLiveFiles;

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
        // the channel file's description (§1.2)
        QString comment;
        QString lang;             ///< BCP 47, e.g. "en"
        std::uint32_t kbps = 0;   ///< typical bit rate
        std::uint32_t segmentMs = 0;
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
        std::uint64_t skipped = 0;   ///< segments not sent (too large, init missing/too large)
        bool resumed = false;     ///< went on with the broadcast cut off (§4)
        QString channelFile;      ///< root (hex) of the channel file shared; empty: none
        QString error;            ///< the last problem, localized; empty = none
        bool paid = false;        ///< subscription.md §7: sealed, an offer (Ed3kPaidSeller)
        QString channel;          ///< the channel `id` (hex, 32), deleteChannel()
    };
    /// A channel kept here (channels.json), on air or not.
    struct ChannelInfo {
        QString channel;          ///< the channel `id` (hex, 32)
        QString title;
        bool isPrivate = false;
        QString stream;           ///< the broadcast on air (Info::id); empty: off air
        QString channelFile;      ///< root (hex) of the channel file shared; empty: none
    };
    /// What channels.json keeps of a channel on air (resumed after a restart).
    struct OnAir {
        std::uint64_t start = 0;
        std::uint64_t nextSeq = 0;
        std::uint64_t cut = 0;    ///< unix s of the last segment sent
        std::optional<std::uint64_t> last;   ///< its number in the playlist: not sent again
    };

    /// Segments larger than this are skipped (live.md §2.1), init data larger too.
    static constexpr qint64 kSegmentMax = 4 << 20;
    static constexpr qint64 kInitMax = 256 << 10;
    /// §4: back within 10 min of the cut (initial) goes on with the broadcast
    static constexpr std::uint64_t kResumeS = 10 * 60;

    /// §4: the broadcast to go on with at @p now, nullopt: a new one (too late, nothing sent).
    [[nodiscard]] static std::optional<::ed3k::LiveBroadcast::Resume> resumeOf(const OnAir& a, std::uint64_t now);

    /// @p configDir: Config/ (ed3k/live/ below it).
    Ed3kLiveBroadcast(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kLiveBroadcast() override;

    /// Starts broadcasting @p o; the error is user-visible (localized).
    std::expected<Info, QString> start(const Options& o);
    /// Ends the broadcast @p id (liveEnd). False: not one of ours.
    bool stop(const QString& id);
    /// A new description of the channel on air @p id: a new channel file (later `created`),
    /// the old one withdrawn. The error is user-visible.
    std::expected<void, QString> describe(const QString& id, const QString& comment, const QString& lang, std::uint32_t kbps);
    /// Starts again what was on air before (channels.json); at start and when ed3k starts.
    void resumeSaved();
    /// §1.2: shares and publishes the channel file of every public channel kept (channels.json
    /// with a description), on air or not, the same `created`; at start and when ed3k starts.
    void publishSaved();
    /// Ends the channel @p channel (hex id) if on air, withdraws its channel file and drops it
    /// (its keys too: the link no longer works). The error is user-visible.
    std::expected<void, QString> deleteChannel(const QString& channel);
    /// Every channel kept here, on air or not.
    [[nodiscard]] std::vector<ChannelInfo> channels() const;
    [[nodiscard]] bool isBroadcast(const QString& id) const;
    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& id) const;

    /// Reads the playlists now (the timer does every 500 ms).
    void scan();

    /// subscription.md §7: the SignedOffer of a channel sold (channel key, id); empty = open.
    using OfferSource = std::function<QByteArray(const ::ed3k::Key32& channelKey, const QByteArray& id)>;
    /// Asked at each start(): a channel with an offer broadcasts paid (Ed3kPaidSeller); the
    /// channel files kept are published again with their offers.
    // void setOfferSource(OfferSource source) { m_offerSource = std::move(source); }
    void setOfferSource(OfferSource source);
    /// The seed of the channel key of the broadcast @p id (a secret: the caller wipes it) and
    /// its channel `id` in @p channelId; nullopt: not on air here.
    [[nodiscard]] std::optional<::ed3k::Key32> channelSeed(const QString& id, QByteArray* channelId) const;
    /// The secret `s` of the private channel on air @p id (live.md §10: a credential, the
    /// caller wipes it, never logged); nullopt: open or not on air here.
    [[nodiscard]] std::optional<::ed3k::Key32> channelSecret(const QString& id) const;
    /// The secret of private channel @p channelId kept under @p configDir (Config/ed3k/live/
    /// <id hex>.secret, owner-only); nullopt: none. For a sale while off air (Ed3kPaidSeller).
    [[nodiscard]] static std::optional<::ed3k::Key32> channelSecretOf(const QString& configDir, const QByteArray& channelId);
    /// What the broadcast @p id was started with (to start it again).
    [[nodiscard]] std::optional<Options> optionsOf(const QString& id) const;

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
        std::uint64_t skipped = 0;
        QString error;
        // §4: the broadcast and the seq of the next segment (kept for a resume)
        std::uint64_t start = 0;
        std::uint64_t nextSeq = 0;
        bool resumed = false;
        QByteArray channelId;                ///< 16 B
        // std::optional<::ed3k::Root> channelFile;   (was; now m_channelFiles, kept off air)
        bool paid = false;                   ///< broadcast with an offer (subscription.md §7)
    };
    struct Channel {
        QByteArray id;            ///< 16 B, the channel `id`
        bool isPrivate = false;
    };
    /// The description a channel file carries (§1.2): a change is a new file.
    struct Desc {
        QString title, comment, lang, media, codecs;
        std::uint32_t kbps = 0, segmentMs = 0;
        QString offer;            ///< SHA-256 (hex) of the offer of a paid channel; empty: none
        bool operator==(const Desc&) const = default;
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
    /// The seed (and secret) of channel @p ch and the options' media into @p b; false: no key.
    bool loadChannel(const Channel& ch, const Options& o, ::ed3k::LiveBroadcast& b);
    /// Shares the channel file of channel @p channelId (the channel @p b) for the description
    /// of @p o; a changed one gets a later `created`, the one before is withdrawn.
    // void publishChannelFile(Running& r, const ::ed3k::LiveBroadcast& b);   (was: on air only)
    void publishChannelFile(const QByteArray& channelId, const Options& o, const ::ed3k::LiveBroadcast& b);
    /// The same, the channel loaded (loadChannel) from @p channelId and @p o.
    void publishChannelFile(const QByteArray& channelId, const Options& o);
    /// The root of the channel file shared of @p channelId; nullopt: none.
    [[nodiscard]] std::optional<::ed3k::Root> channelFileOf(const QByteArray& channelId) const;
    /// channels.json: every entry; @p edit changes the one of @p channelId and it is saved.
    [[nodiscard]] QJsonArray readChannels() const;
    bool editChannel(const QByteArray& channelId, const std::function<void(QJsonObject&)>& edit);
    /// The on-air state of @p r saved (cut = now).
    void saveOnAir(const Running& r);
    [[nodiscard]] static Desc descOf(const Options& o);

    Ed3kSession& m_session;
    QString m_configDir;
    std::map<::ed3k::Stream, Running> m_running;
    /// the start of the last broadcast of a stream here (a later one starts later, §1.1)
    std::map<::ed3k::Stream, std::uint64_t> m_lastStart;
    std::unique_ptr<Ed3kLiveFiles> m_files;
    /// the channel file shared of each channel (by its `id`), on air or not (§1.2)
    struct ChannelFile {
        ::ed3k::Root root;
        QByteArray bytes;
    };
    std::map<QByteArray, ChannelFile> m_channelFiles;
    OfferSource m_offerSource;
    QTimer m_timer;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
