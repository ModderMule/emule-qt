/// @file Ed3kRoomsBridge.cpp
/// @brief ed3k rooms over IPC — implementation.

#include "Ed3kRoomsBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kHide.h"
#include "Ed3kRooms.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include <map>
#endif

namespace eMule::Ed3kRoomsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kRooms* s_rooms = nullptr;

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

QCborMap emptyList()
{
    return QCborMap{{QStringLiteral("running"), false}, {QStringLiteral("rooms"), QCborArray{}}};
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;
using Rooms = ed3k::Ed3kRooms;

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

/// The member ID of a member key (room §1.1) as hex, shown in part (§6); empty on error.
QString memberIdOf(const ffi::Key32& key)
{
    const auto id = ffi::roomMemberId(key);
    return id ? hexOf(*id) : QString();
}

constexpr qsizetype kMediaChunk = 4 << 20;   // under MaxPayloadSize
constexpr qint64 kUploadTtlSecs = 300;

/// A post's file coming from the GUI in parts (PostEd3kRoomMedia), by token.
struct Upload {
    QByteArray data;
    qint64 total = 0;
    qint64 touched = 0;
};
std::map<QString, Upload> s_uploads;

QCborMap postToCbor(const Rooms& rooms, const Rooms::Post& p)
{
    QCborMap m{
        {QStringLiteral("id"), hexOf(p.id)},
        {QStringLiteral("author"), Rooms::keyText(p.author)},
        {QStringLiteral("authorId"), hexOf(p.authorId)},
        {QStringLiteral("nick"), p.nick},
        {QStringLiteral("moderator"), p.moderator},
        {QStringLiteral("sent"), qint64(p.sent)},
        {QStringLiteral("text"), p.text},
        {QStringLiteral("replyTo"), p.replyTo ? hexOf(*p.replyTo) : QString()},
    };
    // §2.6: never the file key
    if (p.media)
        m.insert(QStringLiteral("media"), QCborMap{
            {QStringLiteral("root"), hexOf(p.media->root)},
            {QStringLiteral("size"), qint64(p.media->size)},
            {QStringLiteral("type"), p.media->type},
            {QStringLiteral("name"), p.media->name},
            {QStringLiteral("width"), qint64(p.media->width)},
            {QStringLiteral("height"), qint64(p.media->height)},
            {QStringLiteral("state"), int(rooms.media().state(p.media->root))},
        });
    return m;
}

std::optional<ffi::PostId> replyOf(const QString& hex, bool* bad)
{
    *bad = false;
    if (hex.isEmpty())
        return std::nullopt;
    const QByteArray b = QByteArray::fromHex(hex.toLatin1());
    if (b.size() != 16) {
        *bad = true;
        return std::nullopt;
    }
    ffi::PostId id{};
    std::copy(b.cbegin(), b.cend(), reinterpret_cast<char*>(id.data()));
    return id;
}

/// `dn` of a room link (room.md §1.3), empty without.
QString linkName(const QString& link)
{
    return QUrlQuery(link.mid(link.indexOf(QLatin1Char('?')) + 1)).queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded).trimmed();
}

QCborMap roomToCbor(const Rooms& rooms, const Rooms::Room& r)
{
    // admit/ban state for the moderator's menus (room.md §2.2, §2.3)
    const bool askStatus = r.canModerate() && (r.closed || r.moderated);
    QCborArray mods;
    for (const auto& k : r.mods)
        mods.append(Rooms::keyText(k));
    QCborArray members;
    for (const auto& [key, nick] : r.present)
        members.append(QCborMap{
            {QStringLiteral("key"), Rooms::keyText(key)},
            {QStringLiteral("id"), memberIdOf(key)},
            {QStringLiteral("nick"), nick},
            {QStringLiteral("moderator"), r.isModerator(key)},
            {QStringLiteral("admitted"), askStatus && (rooms.memberStatus(r.room, key) & ED3K_ROOM_MEMBER_ADMITTED) != 0},
        });
    QCborArray bans;   // keys banned now by a held ban, for the ban list
    if (r.canModerate())
        for (const auto& b : rooms.bans(r.room))
            bans.append(QCborMap{{QStringLiteral("key"), Rooms::keyText(b.member)},
                                 {QStringLiteral("id"), memberIdOf(b.member)},
                                 {QStringLiteral("until"), qint64(b.until)}});
    QCborArray muted;   // batch 13: member IDs muted here (room.md §6)
    for (const auto& id : r.muted)
        muted.append(hexOf(id));
    return QCborMap{
        {QStringLiteral("room"), Rooms::roomHex(r.room)},
        {QStringLiteral("link"), Rooms::linkWithName(r.link, r.title)},   // room.md §1.3: dn carried
        {QStringLiteral("title"), r.title.isEmpty() ? linkName(r.link) : r.title},   // no room info yet: the link's dn
        {QStringLiteral("about"), r.about},
        {QStringLiteral("nick"), r.nick},
        {QStringLiteral("owner"), r.owner},
        {QStringLiteral("moderator"), r.canModerate()},
        {QStringLiteral("private"), r.isPrivate},
        {QStringLiteral("closed"), r.closed},
        {QStringLiteral("moderated"), r.moderated},
        {QStringLiteral("joined"), r.joined},
        {QStringLiteral("neighbours"), r.neighbours},
        {QStringLiteral("generation"), qint64(r.generation)},
        {QStringLiteral("keyHeld"), r.keyHeld},
        {QStringLiteral("keyMissing"), r.keyMissing},
        {QStringLiteral("memberKey"), Rooms::keyText(r.memberKey)},
        {QStringLiteral("memberId"), memberIdOf(r.memberKey)},
        {QStringLiteral("mods"), mods},
        {QStringLiteral("members"), members},
        {QStringLiteral("muted"), muted},
        {QStringLiteral("bans"), bans},
        {QStringLiteral("notAdmitted"), r.notAdmitted},
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(r.key)},   // hops.md §1: shown
        {QStringLiteral("waitingHide"), r.waitingHide},
        {QStringLiteral("mediaMax"), qint64(r.mediaMax)},
        {QStringLiteral("keepHours"), qint64(r.keepHours)},
        {QStringLiteral("keepPosts"), qint64(r.keepPosts)},
    };
}

QCborMap roomsToCbor(const Rooms& rooms)
{
    QCborArray list;
    for (const auto& [room, r] : rooms.rooms())
        list.append(roomToCbor(rooms, r));
    return QCborMap{{QStringLiteral("running"), rooms.isRunning()}, {QStringLiteral("rooms"), list}};
}

void answer(const IpcMessage& msg, Rooms& rooms, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    const auto fail = [&](ffi::Error e) { reply(failure(seq, int(e))); };
    const auto done = [&](const ffi::Result<>& r) { r ? ok() : fail(r.error()); };

    if (msg.type() == IpcMsgType::GetEd3kRooms) {
        ok(roomsToCbor(rooms));
        return;
    }
    if (!rooms.isRunning()) {
        reply(failure(seq, 0));
        return;
    }
    if (msg.type() == IpcMsgType::CreateEd3kRoom) {
        const auto room = rooms.create(msg.fieldString(0), msg.fieldString(1), std::uint32_t(msg.fieldInt(2)), msg.fieldString(3));
        if (!room) {
            fail(room.error());
            return;
        }
        const Rooms::Room* r = rooms.find(*room);
        ok(QCborMap{{QStringLiteral("room"), Rooms::roomHex(*room)}, {QStringLiteral("link"), r ? Rooms::linkWithName(r->link, r->title) : QString()}});
        return;
    }
    if (msg.type() == IpcMsgType::JoinEd3kRoom) {
        if (const auto room = rooms.join(msg.fieldString(0), msg.fieldString(1)))
            ok(QCborMap{{QStringLiteral("room"), Rooms::roomHex(*room)}});
        else
            fail(room.error());
        return;
    }
    if (msg.type() == IpcMsgType::GetEd3kRoomMedia) {
        const auto root = ed3k::Ed3kRoomMedia::rootFrom(msg.fieldString(0));
        const auto bytes = root ? rooms.media().read(*root) : std::nullopt;
        if (!bytes) {
            fail(ffi::Error::NotFound);
            return;
        }
        const qint64 offset = std::clamp<qint64>(msg.fieldInt(1), 0, bytes->size());
        ok(QCborMap{{QStringLiteral("type"), ed3k::Ed3kRoomMedia::sniffType(*bytes)},
                    {QStringLiteral("size"), qint64(bytes->size())},
                    {QStringLiteral("offset"), offset},
                    {QStringLiteral("data"), bytes->mid(offset, kMediaChunk)}});
        return;
    }
    if (msg.type() == IpcMsgType::ImportEd3kRoomAdmit) {
        if (const auto room = rooms.importAdmit(msg.fieldString(0)))
            ok(QCborMap{{QStringLiteral("room"), Rooms::roomHex(*room)}});
        else
            fail(room.error());
        return;
    }
    // the rest names a room first
    const auto room = Rooms::roomFrom(msg.fieldString(0));
    if (!room || !rooms.find(*room)) {
        fail(room ? ffi::Error::NotFound : ffi::Error::Argument);
        return;
    }
    switch (msg.type()) {
    case IpcMsgType::MuteEd3kRoomMember: {
        const QByteArray raw = QByteArray::fromHex(msg.fieldString(1).trimmed().toLatin1());
        std::array<std::uint8_t, 16> id{};
        if (raw.size() != qsizetype(id.size())) {
            fail(ffi::Error::Argument);
            break;
        }
        std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(id.data()));
        done(rooms.setMuted(*room, id, msg.fieldBool(2)));
        break;
    }
    case IpcMsgType::GetEd3kRoomPosts: {
        QCborArray posts;
        for (const auto& p : rooms.find(*room)->posts)
            posts.append(postToCbor(rooms, p));
        ok(posts);
        break;
    }
    case IpcMsgType::LeaveEd3kRoom:
        done(rooms.leave(*room, msg.fieldBool(1)));
        break;
    case IpcMsgType::PostEd3kRoom: {
        std::optional<ffi::PostId> replyTo;
        if (const QString r = msg.fieldString(2); !r.isEmpty()) {
            const QByteArray b = QByteArray::fromHex(r.toLatin1());
            if (b.size() != 16) {
                fail(ffi::Error::Argument);
                break;
            }
            replyTo.emplace();
            std::copy(b.cbegin(), b.cend(), reinterpret_cast<char*>(replyTo->data()));
        }
        if (auto ids = rooms.post(*room, msg.fieldString(1), replyTo)) {
            QCborArray out;
            for (const auto& id : *ids)
                out.append(hexOf(id));
            ok(out);
        } else {
            fail(ids.error());
        }
        break;
    }
    case IpcMsgType::SetEd3kRoomNick:
        done(rooms.setNick(*room, msg.fieldString(1)));
        break;
    case IpcMsgType::SetEd3kRoomLimits:
        done(rooms.setLimits(*room, std::uint64_t(std::max<qint64>(0, msg.fieldInt(1))), std::uint32_t(std::max<qint64>(0, msg.fieldInt(2))),
                             std::uint32_t(std::max<qint64>(0, msg.fieldInt(3)))));
        break;
    case IpcMsgType::FetchEd3kRoomMedia: {
        const auto root = ed3k::Ed3kRoomMedia::rootFrom(msg.fieldString(1));
        if (!root) {
            fail(ffi::Error::Argument);
            break;
        }
        done(rooms.media().fetch(*room, *root));
        break;
    }
    case IpcMsgType::PostEd3kRoomMedia: {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        std::erase_if(s_uploads, [now](const auto& u) { return u.second.touched + kUploadTtlSecs < now; });
        const QString token = msg.fieldString(1);
        const qint64 offset = msg.fieldInt(2);
        const qint64 total = msg.fieldInt(3);
        const QByteArray data = msg.field(4).toByteArray();
        if (token.isEmpty() || total <= 0 || total > qint64(ed3k::Ed3kRoomMedia::kMaxSize) || data.size() > kMediaChunk) {
            fail(ffi::Error::Argument);
            break;
        }
        Upload& u = s_uploads[token];
        if (u.data.size() != offset || (u.total && u.total != total) || offset + data.size() > total) {
            s_uploads.erase(token);
            fail(ffi::Error::Argument);
            break;
        }
        u.total = total;
        u.touched = now;
        u.data += data;
        if (u.data.size() < total) {
            ok(QCborMap{{QStringLiteral("more"), true}});
            break;
        }
        const QByteArray file = std::move(u.data);
        s_uploads.erase(token);
        bool bad = false;
        const auto replyTo = replyOf(msg.fieldString(6), &bad);
        if (bad) {
            fail(ffi::Error::Argument);
            break;
        }
        const auto id = rooms.postMedia(*room, msg.fieldString(5), replyTo, file, msg.fieldString(7),
                                        std::uint32_t(std::max<qint64>(0, msg.fieldInt(8))), std::uint32_t(std::max<qint64>(0, msg.fieldInt(9))));
        if (id)
            ok(QCborArray{hexOf(*id)});
        else
            fail(id.error());
        break;
    }
    case IpcMsgType::SetEd3kRoomInfo: {
        std::vector<ffi::Key32> mods;
        for (const QCborValue& v : msg.fieldArray(4)) {
            const auto key = Rooms::keyFromText(v.toString());
            if (!key) {
                fail(ffi::Error::Argument);
                return;
            }
            mods.push_back(*key);
        }
        done(rooms.setInfo(*room, msg.fieldString(1), msg.fieldString(2), std::uint32_t(msg.fieldInt(3)), mods));
        break;
    }
    case IpcMsgType::ModerateEd3kRoom: {
        const auto action = Rooms::Moderation(msg.fieldInt(1));
        if (action == Rooms::Moderation::NewGeneration) {
            if (const auto g = rooms.startGeneration(*room))
                ok(QCborMap{{QStringLiteral("generation"), qint64(*g)}});
            else
                fail(g.error());
            break;
        }
        const auto member = Rooms::keyFromText(msg.fieldString(2));
        if (!member) {
            fail(ffi::Error::Argument);
            break;
        }
        const auto until = std::uint64_t(std::max<qint64>(0, msg.fieldInt(3)));
        switch (action) {
        case Rooms::Moderation::Admit:
            if (const auto text = rooms.admit(*room, *member, until))
                ok(QCborMap{{QStringLiteral("admit"), *text}});
            else
                fail(text.error());
            break;
        case Rooms::Moderation::Ban:
            done(rooms.ban(*room, *member, until));
            break;
        case Rooms::Moderation::Lift:
            done(rooms.lift(*room, *member));
            break;
        default:
            fail(ffi::Error::Argument);
            break;
        }
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setRooms(ed3k::Ed3kRooms* rooms)
{
    s_rooms = rooms;
}

bool isRoomsRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return (t >= 1120 && t <= 1129) || type == IpcMsgType::MuteEd3kRoomMember   // batch 13: mute
        || type == IpcMsgType::SetEd3kRoomLimits || type == IpcMsgType::PostEd3kRoomMedia
        || type == IpcMsgType::GetEd3kRoomMedia || type == IpcMsgType::FetchEd3kRoomMedia;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isRoomsRequest(msg.type()))
        return false;
#ifdef EMULE_HAVE_ED3K
    if (s_rooms) {
        answer(msg, *s_rooms, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kRooms)
        reply(IpcMessage::makeResult(msg.seqId(), true, emptyList()));
    else
        reply(failure(msg.seqId(), 0));
    return true;
}

void connectPushes(ed3k::Ed3kRooms& rooms, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    const auto hex = [](const QByteArray& b) { return QString::fromLatin1(b.toHex()); };
    QObject::connect(&rooms, &Rooms::roomsChanged, &rooms, [broadcast] {
        broadcast(IpcMessage(IpcMsgType::PushEd3kRoomsChanged, 0));
    });
    QObject::connect(&rooms, &Rooms::postShown, &rooms, [broadcast, hex, r = &rooms](const QByteArray& room, const QByteArray& id) {
        const auto key = Rooms::roomFrom(hex(room));
        const Rooms::Room* view = key ? r->find(*key) : nullptr;
        if (!view)
            return;
        // the newest is at the end
        const auto it = std::ranges::find_if(view->posts.rbegin(), view->posts.rend(), [&](const Rooms::Post& p) {
            return QByteArray(reinterpret_cast<const char*>(p.id.data()), 16) == id;
        });
        if (it == view->posts.rend())
            return;
        IpcMessage m(IpcMsgType::PushEd3kRoomPost, 0);
        m.append(hex(room));
        m.append(postToCbor(*r, *it));
        broadcast(m);
    });
    QObject::connect(&rooms, &Rooms::postsRemoved, &rooms, [broadcast, hex](const QByteArray& room, const QList<QByteArray>& ids) {
        IpcMessage m(IpcMsgType::PushEd3kRoomPostsRemoved, 0);
        m.append(hex(room));
        QCborArray list;
        for (const QByteArray& id : ids)
            list.append(hex(id));
        m.append(list);
        broadcast(m);
    });
    // a member key as base32 (§1.1), a node as hex
    const auto who = [hex](const QByteArray& b) {
        if (b.size() != 32)
            return hex(b);
        ffi::Key32 k{};
        std::copy(b.cbegin(), b.cend(), reinterpret_cast<char*>(k.data()));
        return Rooms::keyText(k);
    };
    QObject::connect(&rooms, &Rooms::presenceChanged, &rooms,
                     [broadcast, hex, who](const QByteArray& room, const QByteArray& member, const QString& nick, bool left) {
                         IpcMessage m(IpcMsgType::PushEd3kRoomPresence, 0);
                         m.append(hex(room));
                         m.append(who(member));
                         m.append(nick);
                         m.append(left);
                         // field 4: the member ID (§6), for the shown name
                         ffi::Key32 k{};
                         if (member.size() == qsizetype(k.size()))
                             std::copy(member.cbegin(), member.cend(), reinterpret_cast<char*>(k.data()));
                         m.append(member.size() == qsizetype(k.size()) ? memberIdOf(k) : QString());
                         broadcast(m);
                     });
    QObject::connect(&rooms.media(), &ed3k::Ed3kRoomMedia::stateChanged, &rooms,
                     [broadcast, hex](const QByteArray& room, const QByteArray& root, int state) {
                         IpcMessage m(IpcMsgType::PushEd3kRoomMedia, 0);
                         m.append(hex(room));
                         m.append(hex(root));
                         m.append(qint64(state));
                         broadcast(m);
                     });
    QObject::connect(&rooms, &Rooms::notice, &rooms,
                     [broadcast, hex, who](const QByteArray& room, int kind, quint64 value, bool flag, const QByteArray& by) {
                         IpcMessage m(IpcMsgType::PushEd3kRoomNotice, 0);
                         m.append(hex(room));
                         m.append(qint64(kind));
                         m.append(qint64(value));
                         m.append(flag);
                         m.append(by.isEmpty() ? QString() : who(by));
                         broadcast(m);
                     });
#else
    (void)rooms;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kRoomsBridge
