#pragma once

/// @file Ed3kRooms.h
/// @brief ed3k rooms (room.md) in the daemon: the Room* events of the session, the rooms
/// the user is in, their secrets, a short post history for the GUI.
///
/// - Rooms are keyed by the 16-byte room (§1.2).
/// - Secrets (room.md §1.1, ffi.md "Rooms"): the member key of each room in
///   Config/ed3k/rooms/<room hex>.member, the room key of an owned room in
///   rooms/owner-<room key hex>.key (kept on leave: a join by its link makes this node the
///   owner again), both 32-byte seeds, owner-only, never logged. The link with `s` is a
///   credential too: kept in rooms/joined.cbor (owner-only), never logged.
/// - Joined rooms (link, nick) are persisted and joined again when the node starts; the
///   core's own room state (State::Rooms: generation keys, info) is saved by Ed3kSession.
/// - A closed room's admit goes out of band (batch 10 reading 1): the newcomer copies its
///   member key (memberKeyText), the moderator's core signs an Admit (admit: the admit
///   text), the newcomer imports it (importAdmit).

#include "Ed3kFfi.h"
#include "Ed3kStore.h"

#include <QByteArray>
#include <QCborMap>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kRooms : public QObject {
    Q_OBJECT

public:
    /// What notice() says (the Room* events without a list of their own).
    enum class Notice : int {
        Joined = 0,       ///< the first neighbour, the first answer taken
        Left = 1,
        Neighbour = 2,    ///< value 1 up, 0 gone; who: the node
        Banned = 3,       ///< who: the member key
        Generation = 4,   ///< value: the generation; flag held
        KeyMissing = 5,   ///< value: the generation
        WrapBad = 6,      ///< value: the generation; who: the moderator
        Refused = 7,      ///< value: ED3K_ROOM_FULL ...; who: the node
        Fault = 8,        ///< value: until (Unix); who: the node
    };
    /// What a moderator does (IPC Ed3kRoomModerate).
    enum class Moderation : int { Admit = 0, Ban = 1, Lift = 2, NewGeneration = 3 };

    struct Post {
        ::ed3k::PostId id{};
        ::ed3k::Key32 author{};
        std::array<std::uint8_t, 16> authorId{};
        QString nick;
        bool moderator = false;
        std::uint64_t sent = 0;
        QString text;
        std::optional<::ed3k::PostId> replyTo;
    };
    /// One room as the GUI shows it.
    struct Room {
        ::ed3k::Room room;
        QString link;              ///< a credential when it has `s`: never logged
        QString nick;
        bool owner = false;        ///< the room key is held
        ::ed3k::Key32 memberKey{}; ///< this member's public key
        bool isPrivate = false;    ///< the link has `s`
        QString title;
        QString about;
        bool closed = false;
        bool moderated = false;
        std::vector<::ed3k::Key32> mods;
        std::map<::ed3k::Key32, QString> present;   ///< member key -> nick
        std::deque<Post> posts;    ///< the last kHistory, in arrival order
        bool joined = false;       ///< RoomJoined seen
        int neighbours = 0;
        std::uint64_t generation = 0;
        bool keyHeld = false;
        bool keyMissing = false;
        /// member IDs muted by the user (§6: local, for themselves alone); kept in joined.cbor
        std::set<std::array<std::uint8_t, 16>> muted;
        /// a member answered ROOM_MEMBER (§4.2 step 4): a closed room not admitted yet (#17)
        bool notAdmitted = false;

        [[nodiscard]] bool isModerator(const ::ed3k::Key32& member) const;
        /// This member may admit, ban and start generations.
        [[nodiscard]] bool canModerate() const { return owner || isModerator(memberKey); }
    };
    static constexpr std::size_t kHistory = 200;

    explicit Ed3kRooms(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kRooms() override;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] const std::map<::ed3k::Room, Room>& rooms() const { return m_rooms; }
    [[nodiscard]] const Room* find(const ::ed3k::Room& room) const;

    /// Makes a room (ED3K_ROOM_PRIVATE, _CLOSED, _MODERATED) with a new room key and member
    /// key, and joins it; the owner's member key is a moderator.
    ::ed3k::Result<::ed3k::Room> create(const QString& title, const QString& about, std::uint32_t flags, const QString& nick);
    /// Joins the room of an `ed3k:room` link (a room joined already: its room).
    ::ed3k::Result<::ed3k::Room> join(const QString& link, const QString& nick);
    ::ed3k::Result<> leave(const ::ed3k::Room& room);
    /// A node of the room as a neighbour (others are found through Kad).
    ::ed3k::Result<> connectPeer(const ::ed3k::Room& room, const ::ed3k::Source& peer);
    ::ed3k::Result<std::vector<::ed3k::PostId>> post(const ::ed3k::Room& room, const QString& text,
                                                      std::optional<::ed3k::PostId> replyTo = {});
    ::ed3k::Result<> setNick(const ::ed3k::Room& room, const QString& nick);
    /// Owner: a new room info (ED3K_ROOM_CLOSED, _MODERATED) and the moderators.
    ::ed3k::Result<> setInfo(const ::ed3k::Room& room, const QString& title, const QString& about, std::uint32_t flags,
                             const std::vector<::ed3k::Key32>& mods);
    /// Moderator: admits @p member until @p until (0: no end); the admit text to hand over.
    ::ed3k::Result<QString> admit(const ::ed3k::Room& room, const ::ed3k::Key32& member, std::uint64_t until = 0);
    /// Newcomer: takes in an admit text; the room it is for.
    ::ed3k::Result<::ed3k::Room> importAdmit(const QString& text);
    /// Moderator: bans until (Unix, at most 30 days on); lift() ends a ban.
    ::ed3k::Result<> ban(const ::ed3k::Room& room, const ::ed3k::Key32& member, std::uint64_t until);
    ::ed3k::Result<> lift(const ::ed3k::Room& room, const ::ed3k::Key32& member);
    ::ed3k::Result<std::uint64_t> startGeneration(const ::ed3k::Room& room);

    /// Mutes (or unmutes) a member ID here alone (§6); the GUI hides its posts. Kept.
    ::ed3k::Result<> setMuted(const ::ed3k::Room& room, const std::array<std::uint8_t, 16>& memberId, bool on);

    /// This member's key as text (base32, 52 characters, §1.1): what a newcomer sends.
    [[nodiscard]] QString memberKeyText(const ::ed3k::Room& room) const;

    [[nodiscard]] static QString roomHex(const ::ed3k::Room& room);
    [[nodiscard]] static std::optional<::ed3k::Room> roomFrom(const QString& hex);
    /// A member key from base32 (52) or hex (64).
    [[nodiscard]] static std::optional<::ed3k::Key32> keyFromText(const QString& text);
    [[nodiscard]] static QString keyText(const ::ed3k::Key32& key);
    /// The admit text: `ed3k-admit:<room base32>:<RoomItem base32>` (an app format, not the spec's).
    [[nodiscard]] static QString admitText(const ::ed3k::Room& room, std::span<const std::byte> item);
    [[nodiscard]] static std::optional<std::pair<::ed3k::Room, QByteArray>> parseAdmitText(const QString& text);

signals:
    /// The list, a room's info, nick or state changed.
    void roomsChanged();
    /// A post to show once (find(room)->posts holds it); 16-byte room and id.
    void postShown(const QByteArray& room, const QByteArray& id);
    /// Posts dropped by a ban.
    void postsRemoved(const QByteArray& room, const QList<QByteArray>& ids);
    /// A member came, changed its nick or left; @p member 32 bytes.
    void presenceChanged(const QByteArray& room, const QByteArray& member, const QString& nick, bool left);
    /// A Notice; @p who a node (16 B) or a member key (32 B), may be empty.
    void notice(const QByteArray& room, int kind, quint64 value, bool flag, const QByteArray& who);

private:
    /// What rooms/joined.cbor keeps of a room.
    struct Saved {
        QString link;
        QString nick;
        QCborMap info;   ///< the last room info (title, about, closed, moderated, mods): shown before a neighbour sends one
        std::set<std::array<std::uint8_t, 16>> muted;   ///< member IDs muted (§6)
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void attach();
    /// Joins with the stored member key (made when missing); owner when its room key is held.
    ::ed3k::Result<::ed3k::Room> joinWith(const QString& link, const QString& nick);
    Room& track(const ::ed3k::Room& room, const QString& link, const QString& nick);
    void onPost(const ::ed3k::event::RoomPost& p);
    void loadSaved();
    void save() const;
    [[nodiscard]] QString roomsDir() const;
    [[nodiscard]] static QString memberFile(const ::ed3k::Room& room);
    [[nodiscard]] static QString ownerFile(const ::ed3k::Key32& roomKey);
    /// A new 32-byte seed in @p name of roomsDir (owner-only); nullopt: no CSPRNG or disk.
    [[nodiscard]] std::optional<Seed> newSecret(const QString& name) const;
    [[nodiscard]] static std::optional<::ed3k::Key32> linkKey(const QString& link);

    Ed3kSession& m_session;
    bool m_attached = false;
    std::map<::ed3k::Room, Room> m_rooms;
    std::map<::ed3k::Room, Saved> m_saved;   ///< what is joined again at start
};

} // namespace eMule::ed3k
