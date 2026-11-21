#pragma once

/// @file Ed3kRooms.h
/// @brief ed3k rooms (room.md) in the daemon: the Room* events of the session, the rooms
/// the user is in, their secrets, a short post history for the GUI.
///
/// - Rooms are keyed by the 16-byte room (§1.2).
/// - Secrets (room.md §1.1, ffi.md "Rooms"): the member key of each room in
///   Config/ed3k/rooms/<room hex>.member, the room key of an owned room in
///   rooms/owner-<room key hex>.key (kept on leave unless asked: a join by its link makes
///   this node the owner again), both 32-byte seeds, owner-only, never logged. The link with `s` is a
///   credential too: kept in rooms/joined.cbor (owner-only), never logged.
/// - Joined rooms (link, nick) are persisted and joined again when the node starts; the
///   core's own room state (State::Rooms: generation keys, info) is saved by Ed3kSession.
/// - A closed room's admit goes out of band (batch 10 reading 1): the newcomer copies its
///   member key (memberKeyText), the moderator's core signs an Admit (admit: the admit
///   text), the newcomer imports it (importAdmit).
/// - Key (hops.md §1): a room is made or joined under Ed3kHide::keyForUse() and stays under
///   it (joined.cbor "key"); every call and event goes to that key's node. An anonymous room
///   waits while hiding is off (listed, not joined; nothing falls back to the node key) and
///   is joined again when hiding starts. The anonymous node's room state is not in
///   State::Rooms: generation keys come again from members.
/// - Post store (ed3k.roomHistoryStore, default on): the last ed3k.roomHistoryMax posts of a
///   room in rooms/<room hex>.posts (CBOR, owner-only), shown again at start. Private and
///   closed rooms: sealed with ChaCha20-Poly1305 under HKDF(rooms/posts.key, room id).
///   GUI-only, longer than the network's: the core keeps its own 24 h / 1,000 posts in
///   State::Rooms (rooms.dat) and reports them again at the join; it serves them to joiners.

#include "Ed3kFfi.h"
#include "Ed3kRoomMedia.h"
#include "Ed3kStore.h"

#include <QByteArray>
#include <QCborMap>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

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
        Joined = 0,       ///< the first neighbour, the first answer taken; flag: a re-join
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
        std::optional<Ed3kRoomMedia::Media> media;   ///< §2.6, valid by the core
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
        std::deque<Post> posts;    ///< the last historyMax(), in arrival order
        std::set<::ed3k::PostId> postIds;   ///< ids of posts: network history repeats stored ones
        bool joined = false;       ///< RoomJoined seen
        bool everJoined = false;   ///< RoomJoined seen once: the next is a re-join
        int neighbours = 0;
        std::uint64_t generation = 0;
        bool keyHeld = false;
        bool keyMissing = false;
        /// member IDs muted by the user (§6: local, for themselves alone); kept in joined.cbor
        std::set<std::array<std::uint8_t, 16>> muted;
        /// a member answered ROOM_MEMBER (§4.2 step 4): a closed room not admitted yet (#17)
        bool notAdmitted = false;
        /// the node it is joined under (hops.md §1), kept
        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node;
        /// anonymous room while hiding is off: listed, not joined
        bool waitingHide = false;
        /// room info's media and history limits (§2.1, as read: clamped)
        std::uint64_t mediaMax = 0;
        std::uint32_t keepHours = 24;
        std::uint32_t keepPosts = 1000;
        /// other small files besides media (§2.6 `files`)
        bool files = false;
        /// owner joined with a new member key (old one lost): put it back into the mods
        bool reModSelf = false;

        [[nodiscard]] bool isModerator(const ::ed3k::Key32& member) const;
        /// This member may admit, ban and start generations.
        [[nodiscard]] bool canModerate() const { return owner || isModerator(memberKey); }
    };
    /// Posts held per room while the store is off.
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
    /// Leaves @p room; @p dropRoomKey also deletes an owned room's room key (no info or
    /// moderator change ever again, §11).
    ::ed3k::Result<> leave(const ::ed3k::Room& room, bool dropRoomKey = false);
    /// A node of the room as a neighbour (others are found through Kad).
    ::ed3k::Result<> connectPeer(const ::ed3k::Room& room, const ::ed3k::Source& peer);
    ::ed3k::Result<std::vector<::ed3k::PostId>> post(const ::ed3k::Room& room, const QString& text,
                                                      std::optional<::ed3k::PostId> replyTo = {});
    ::ed3k::Result<> setNick(const ::ed3k::Room& room, const QString& nick);
    /// A post with a file (§2.6): sealed in a room with a secret, cached, held; its id.
    ::ed3k::Result<::ed3k::PostId> postMedia(const ::ed3k::Room& room, const QString& text, std::optional<::ed3k::PostId> replyTo,
                                             const QByteArray& bytes, const QString& name, std::uint32_t width, std::uint32_t height);
    /// Owner: media and history limits (§2.1); 0 media = none; @p files: other small files too.
    ::ed3k::Result<> setLimits(const ::ed3k::Room& room, std::uint64_t mediaMax, std::uint32_t keepHours, std::uint32_t keepPosts,
                               bool files = false);
    [[nodiscard]] Ed3kRoomMedia& media() { return *m_media; }
    [[nodiscard]] const Ed3kRoomMedia& media() const { return *m_media; }
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
    /// ED3K_ROOM_MEMBER_* bits of @p member now (§2.2, §2.3); 0 when unknown. Read only.
    [[nodiscard]] std::uint32_t memberStatus(const ::ed3k::Room& room, const ::ed3k::Key32& member) const;
    /// The keys banned now by a held ban and its end (§2.3). Read only.
    [[nodiscard]] std::vector<::ed3k::Engine::RoomBan> bans(const ::ed3k::Room& room) const;

    /// Mutes (or unmutes) a member ID here alone (§6); the GUI hides its posts. Kept.
    ::ed3k::Result<> setMuted(const ::ed3k::Room& room, const std::array<std::uint8_t, 16>& memberId, bool on);

    /// The post store prefs (may have) changed: trims, writes or deletes the stored posts.
    void applyHistoryPrefs();

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
    /// The link to hand out: @p link with `dn` = @p title (room.md §1.3); no title: as it is.
    [[nodiscard]] static QString linkWithName(const QString& link, const QString& title);
    /// Whether two ed3k:room links name the same room: same `key` and `id`, any case, any `dn`.
    [[nodiscard]] static bool sameRoomLink(const QString& a, const QString& b);

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
        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node;   ///< hops.md §1
    };

    // was: void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled);
    void onSessionStatus();
    void attach();
    /// Joins the saved rooms of @p key again (attach, hiding started); anonymous ones wait
    /// while hiding is off.
    void rejoinSaved(std::optional<::ed3k::KeyKind> only);
    /// Hiding stopped: the anonymous node's rooms are gone, they wait (listed).
    void hideStopped();
    /// The key of a tracked room that can act now; State: an anonymous room while hiding is off.
    [[nodiscard]] ::ed3k::Result<::ed3k::KeyKind> keyOf(const ::ed3k::Room& room) const;
    /// Joins with the stored member key (made when missing); owner when its room key is held.
    // was: ::ed3k::Result<::ed3k::Room> joinWith(const QString& link, const QString& nick);
    ::ed3k::Result<::ed3k::Room> joinWith(const QString& link, const QString& nick, ::ed3k::KeyKind key);
    /// A saved room of @p key listed while it waits for hiding.
    void trackWaiting(const ::ed3k::Room& room, const Saved& s);
    // was: Room& track(const ::ed3k::Room& room, const QString& link, const QString& nick);
    Room& track(const ::ed3k::Room& room, const QString& link, const QString& nick, ::ed3k::KeyKind key);
    /// The saved info (title, about, closed, moderated, mods) into @p r while it has none.
    static void applySavedInfo(Room& r, const Saved& s);
    void onPost(const ::ed3k::event::RoomPost& p);
    /// Owner with a new member key not in the mods yet: adds it (a plain room info update).
    void reModSelf(const ::ed3k::Room& room);
    /// The limits of @p r's held room info (§2.1) into it.
    void readLimits(Room& r);
    /// Posts kept per room: ed3k.roomHistoryMax while stored, else kHistory.
    [[nodiscard]] static std::size_t historyMax();
    [[nodiscard]] static bool historyStored();
    /// The stored posts of @p r (rooms/<hex>.posts) into it; unreadable: forgotten.
    void loadPosts(Room& r) const;
    /// @p room's posts changed: written a few seconds later (one write per burst).
    void postsChanged(const ::ed3k::Room& room);
    void writeDirtyPosts();
    /// dev aid EMULE_ED3K_ROOM_PEER=<node id hex>@<udp port>: rooms without a neighbour dial it on
    /// 127.0.0.1 (two nodes on one host behind a VPN never meet by Kad)
    void dialDevPeer();
    void writePosts(const Room& r) const;
    void removePostsFile(const ::ed3k::Room& room) const;
    [[nodiscard]] static QString postsFile(const ::ed3k::Room& room);
    /// Private (`s`) or closed: its posts file is sealed (ChaCha20-Poly1305).
    [[nodiscard]] static bool sealedRoom(const Room& r);
    /// @p room's posts key, from rooms/posts.key (made when @p create); the caller wipes it.
    [[nodiscard]] std::optional<Seed> postsKey(const ::ed3k::Room& room, bool create) const;
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
    std::set<::ed3k::Room> m_dirtyPosts;      ///< rooms whose posts wait for writeDirtyPosts
    QTimer* m_postsTimer = nullptr;
    Ed3kRoomMedia* m_media = nullptr;
    bool m_historyStored = true;      ///< what applyHistoryPrefs saw last
    std::size_t m_historyMax = 0;
};

} // namespace eMule::ed3k
