/// @file Ed3kRoomsBridge.cpp
/// @brief ed3k rooms over IPC — implementation.

#include "Ed3kRoomsBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kRooms.h"

#include <QObject>
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

QCborMap postToCbor(const Rooms::Post& p)
{
    return QCborMap{
        {QStringLiteral("id"), hexOf(p.id)},
        {QStringLiteral("author"), Rooms::keyText(p.author)},
        {QStringLiteral("authorId"), hexOf(p.authorId)},
        {QStringLiteral("nick"), p.nick},
        {QStringLiteral("moderator"), p.moderator},
        {QStringLiteral("sent"), qint64(p.sent)},
        {QStringLiteral("text"), p.text},
        {QStringLiteral("replyTo"), p.replyTo ? hexOf(*p.replyTo) : QString()},
    };
}

QCborMap roomToCbor(const Rooms::Room& r)
{
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
        });
    return QCborMap{
        {QStringLiteral("room"), Rooms::roomHex(r.room)},
        {QStringLiteral("link"), r.link},
        {QStringLiteral("title"), r.title},
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
    };
}

QCborMap roomsToCbor(const Rooms& rooms)
{
    QCborArray list;
    for (const auto& [room, r] : rooms.rooms())
        list.append(roomToCbor(r));
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
        ok(QCborMap{{QStringLiteral("room"), Rooms::roomHex(*room)}, {QStringLiteral("link"), r ? r->link : QString()}});
        return;
    }
    if (msg.type() == IpcMsgType::JoinEd3kRoom) {
        if (const auto room = rooms.join(msg.fieldString(0), msg.fieldString(1)))
            ok(QCborMap{{QStringLiteral("room"), Rooms::roomHex(*room)}});
        else
            fail(room.error());
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
    case IpcMsgType::GetEd3kRoomPosts: {
        QCborArray posts;
        for (const auto& p : rooms.find(*room)->posts)
            posts.append(postToCbor(p));
        ok(posts);
        break;
    }
    case IpcMsgType::LeaveEd3kRoom:
        done(rooms.leave(*room));
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
    return t >= 1120 && t <= 1129;
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
        m.append(postToCbor(*it));
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
