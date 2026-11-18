#pragma once

/// @file Ed3kRoomMedia.h
/// @brief Room media (room.md §2.6, §4.6) in the daemon: the disk cache, what is fetched by
/// itself, the sealing of a file in a room with a secret.
///
/// - Cache: Config/ed3k/rooms/media/<root hex>, the bytes as transferred (sealed in a room
///   with a secret, opened only to show). Oldest used dropped beyond ed3k.roomMediaCacheMiB.
///   Never in Transfers, never shared: the core serves a file only to that room's neighbours.
/// - By itself (chat.roomMedia): images, videos only with chat.roomVideos; at most
///   ed3k.roomMediaPerHour per room and hour, posts of the last day only. The rest waits for
///   a click (State::Click).
/// - The file key of a sealed room is a secret: kept in memory and in the sealed posts file,
///   never in the cache index, never logged.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>

class QTimer;

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kRoomMedia : public QObject {
    Q_OBJECT

public:
    /// What the GUI shows for a post's media (IPC: `media.state`).
    enum class State : int {
        None = 0,       ///< not asked for yet
        Fetching = 1,
        Ready = 2,      ///< in the cache
        Failed = 3,     ///< no neighbour had it; a click tries again
        Click = 4,      ///< showing off, video, over the hourly limit or old: a click fetches
        Ignored = 5,    ///< invalid by §2.6 or over the room's limit: text only
    };
    using Root = std::array<std::uint8_t, 32>;
    /// RoomMedia of a post (§2.6).
    struct Media {
        Root root{};
        std::uint64_t size = 0;
        QString type;
        QString name;
        std::optional<std::array<std::uint8_t, 32>> key;   ///< sealed room: the file key (secret)
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    /// Formats the GUI shows; anything else stays a file line.
    [[nodiscard]] static bool isImageType(const QString& type);
    [[nodiscard]] static bool isVideoType(const QString& type);
    /// The media type of @p bytes by their first bytes; empty: not one we show.
    [[nodiscard]] static QString sniffType(const QByteArray& bytes);
    /// Spec cap of media_max (§2.1).
    static constexpr std::uint64_t kMaxSize = 16ull << 20;

    explicit Ed3kRoomMedia(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kRoomMedia() override;

    /// The node runs (again): cached files held for their rooms, the cache trimmed.
    void attach(const QString& roomsDir);
    void detach();

    /// A post with @p media arrived (or came back from the store, @p sent its time): fetched
    /// now, or waits for a click.
    void onPost(const ::ed3k::Room& room, ::ed3k::KeyKind key, const Media& media, std::uint64_t sent);
    /// The user asked for it (a click): fetched whatever the prefs and the hourly count say.
    ::ed3k::Result<> fetch(const ::ed3k::Room& room, const Root& root);
    [[nodiscard]] State state(const Root& root) const;
    /// The file to show, opened; nullopt: not cached. Read only.
    [[nodiscard]] std::optional<QByteArray> read(const Root& root) const;
    /// The core finished a fetch (ED3K_EV_ROOM_MEDIA).
    void onFetched(const ::ed3k::Room& room, const Root& root, bool ok);

    /// An own post's file: sealed for a room with a secret, cached, held. The Media to post.
    ::ed3k::Result<Media> add(const ::ed3k::Room& room, ::ed3k::KeyKind key, const QByteArray& bytes, const QString& name,
                              bool sealed, std::uint32_t width, std::uint32_t height);
    /// @p room joined: its cached files held (a hold before the join may have failed).
    void roomJoined(const ::ed3k::Room& room);
    /// @p room was left: its files are no longer served (kept in the cache until trimmed).
    void roomLeft(const ::ed3k::Room& room);
    /// Prefs changed (cache size): trims.
    void applyPrefs();

    [[nodiscard]] static QString rootHex(const Root& root);
    [[nodiscard]] static std::optional<Root> rootFrom(const QString& hex);

signals:
    /// A media's state changed; 16-byte room, 32-byte root.
    void stateChanged(const QByteArray& room, const QByteArray& root, int state);

private:
    struct Entry {
        ::ed3k::Room room{};
        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node;
        Media media;
        State state = State::None;
        bool held = false;   ///< the core serves it
    };

    void setState(Entry& e, State s);
    [[nodiscard]] bool autoFetch(const ::ed3k::Room& room, const Media& media, std::uint64_t sent);
    ::ed3k::Result<> startFetch(Entry& e);
    void hold(Entry& e);
    [[nodiscard]] QString path(const Root& root) const;
    [[nodiscard]] QString partPath(const Root& root) const;
    /// Oldest used files dropped until the cache fits ed3k.roomMediaCacheMiB.
    void trim();
    /// ChaCha20, zero nonce (§2.6): sealing and opening are the same.
    [[nodiscard]] static std::optional<QByteArray> chacha(const std::array<std::uint8_t, 32>& key, const QByteArray& in);

    Ed3kSession& m_session;
    QString m_dir;   ///< rooms/media; empty while detached
    std::map<Root, Entry> m_entries;
    /// fetches by itself per room in the last hour
    std::map<::ed3k::Room, std::deque<qint64>> m_autoTimes;
    QTimer* m_trimTimer = nullptr;
};

} // namespace eMule::ed3k
