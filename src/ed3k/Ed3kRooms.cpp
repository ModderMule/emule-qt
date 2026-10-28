/// @file Ed3kRooms.cpp
/// @brief ed3k rooms in the daemon — implementation.

#include "Ed3kRooms.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"

#include "utils/Log.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kRoomsDir = QStringLiteral("rooms");
const QString kJoinedFile = QStringLiteral("joined.cbor");   // links with s: owner-only
const QString kAdmitPrefix = QStringLiteral("ed3k-admit:");
constexpr std::uint64_t kBanMax = 30ull * 24 * 3600;   // §2.3 (initial)

QByteArray bytesOf(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

std::string_view viewOf(const QByteArray& b)
{
    return {b.constData(), std::size_t(b.size())};
}

QString fromStd(const std::string& s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> arrayFrom(const QByteArray& b)
{
    std::array<std::uint8_t, N> out{};
    if (b.size() != qsizetype(N))
        return std::nullopt;
    std::memcpy(out.data(), b.constData(), N);
    return out;
}

} // namespace

bool Ed3kRooms::Room::isModerator(const ffi::Key32& member) const
{
    return std::ranges::find(mods, member) != mods.end();
}

Ed3kRooms::Ed3kRooms(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kRooms::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kRooms::onSessionStatus);
    onSessionStatus();
}

Ed3kRooms::~Ed3kRooms() = default;

bool Ed3kRooms::isRunning() const
{
    return m_session.isRunning();
}

const Ed3kRooms::Room* Ed3kRooms::find(const ffi::Room& room) const
{
    const auto it = m_rooms.find(room);
    return it == m_rooms.end() ? nullptr : &it->second;
}

ffi::Result<ffi::Room> Ed3kRooms::create(const QString& title, const QString& about, std::uint32_t flags, const QString& nick)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    // closed needs a secret (§2.1): the link gets s
    if (flags & ED3K_ROOM_CLOSED)
        flags |= ED3K_ROOM_PRIVATE;
    auto roomSeed = newSecret(QStringLiteral("new.key"));
    auto memberSeed = newSecret(QStringLiteral("new.member"));
    QFile::remove(QDir(roomsDir()).filePath(QStringLiteral("new.key")));
    QFile::remove(QDir(roomsDir()).filePath(QStringLiteral("new.member")));
    if (!roomSeed || !memberSeed)
        return std::unexpected(ffi::Error::Io);
    const Secret roomKey(*roomSeed);
    const Secret member(*memberSeed);
    wipe(std::span<std::uint8_t>{*roomSeed});
    wipe(std::span<std::uint8_t>{*memberSeed});

    const auto memberPub = ffi::roomMemberKey(member.expose());
    const auto roomPub = ffi::roomMemberKey(roomKey.expose());   // the same Ed25519 derivation
    if (!memberPub || !roomPub)
        return std::unexpected(ffi::Error::Config);
    // id: 8 random bytes, so one key could hold more rooms (§1.2)
    std::array<quint32, 2> rnd{};
    QRandomGenerator::system()->generate(rnd.begin(), rnd.end());
    std::array<std::byte, 8> id{};
    std::memcpy(id.data(), rnd.data(), id.size());
    const QByteArray n = nick.toUtf8();
    const QByteArray t = title.toUtf8();
    const QByteArray a = about.toUtf8();
    const std::array<ffi::Key32, 1> mods{*memberPub};
    auto made = e->roomCreate(m_session.now(), roomKey.expose(), member.expose(), viewOf(n), id, flags, viewOf(t), viewOf(a), mods);
    if (!made) {
        m_session.pump();
        return std::unexpected(made.error());
    }
    // tracked before the pump: the room's first events come with it
    const auto linkText = made->link.text();
    Room& r = track(made->room, QString::fromUtf8(linkText.data(), qsizetype(linkText.size())), nick);
    r.owner = true;
    r.memberKey = *memberPub;
    r.title = title;
    r.about = about;
    r.closed = (flags & ED3K_ROOM_CLOSED) != 0;
    r.moderated = (flags & ED3K_ROOM_MODERATED) != 0;
    r.mods.assign(mods.begin(), mods.end());
    QString err;
    if (!replaceSecret(roomsDir(), ownerFile(*roomPub), roomKey.expose(), &err)
        || !replaceSecret(roomsDir(), memberFile(made->room), member.expose(), &err)) {
        // a room whose keys are lost cannot be joined again: leave it
        logEd3kWarning(QStringLiteral("ed3k: room keys not stored (%1)").arg(err));
        (void)e->roomLeave(m_session.now(), made->room);
        m_rooms.erase(made->room);
        m_saved.erase(made->room);
        m_session.pump();
        return std::unexpected(ffi::Error::Io);
    }
    m_session.pump();
    save();
    logEd3k(QStringLiteral("ed3k: room %1 made").arg(roomHex(made->room)));
    emit roomsChanged();
    return made->room;
}

ffi::Result<ffi::Room> Ed3kRooms::join(const QString& link, const QString& nick)
{
    if (!m_session.engine())
        return std::unexpected(ffi::Error::State);
    const QString l = link.trimmed();
    for (const auto& [room, r] : m_rooms)
        if (r.link == l)
            return room;
    auto room = joinWith(l, nick);
    if (!room)
        return room;
    save();
    logEd3k(QStringLiteral("ed3k: room %1 joined").arg(roomHex(*room)));
    emit roomsChanged();
    return room;
}

ffi::Result<> Ed3kRooms::leave(const ffi::Room& room)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->roomLeave(m_session.now(), room);
    m_session.pump();
    if (!ok && ok.error() != ffi::Error::NotFound)
        return ok;
    // the member key goes with the room (a new one for a new join, §1.1); the room key stays
    QFile::remove(QDir(roomsDir()).filePath(memberFile(room)));
    m_rooms.erase(room);
    m_saved.erase(room);
    save();
    logEd3k(QStringLiteral("ed3k: room %1 left").arg(roomHex(room)));
    emit roomsChanged();
    return {};
}

ffi::Result<> Ed3kRooms::connectPeer(const ffi::Room& room, const ffi::Source& peer)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->roomConnect(m_session.now(), room, peer);
    m_session.pump();
    return ok;
}

ffi::Result<std::vector<ffi::PostId>> Ed3kRooms::post(const ffi::Room& room, const QString& text, std::optional<ffi::PostId> replyTo)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = text.toUtf8();
    auto ids = e->roomPost(m_session.now(), room, viewOf(t), replyTo);
    m_session.pump();
    return ids;
}

ffi::Result<> Ed3kRooms::setNick(const ffi::Room& room, const QString& nick)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray n = nick.toUtf8();
    auto ok = e->roomSetNick(m_session.now(), room, viewOf(n));
    m_session.pump();
    if (!ok)
        return ok;
    if (auto it = m_rooms.find(room); it != m_rooms.end())
        it->second.nick = nick;
    if (auto it = m_saved.find(room); it != m_saved.end())
        it->second.nick = nick;
    save();
    emit roomsChanged();
    return {};
}

ffi::Result<> Ed3kRooms::setInfo(const ffi::Room& room, const QString& title, const QString& about, std::uint32_t flags,
                                 const std::vector<ffi::Key32>& mods)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = title.toUtf8();
    const QByteArray a = about.toUtf8();
    auto ok = e->roomSetInfo(m_session.now(), room, flags & (ED3K_ROOM_CLOSED | ED3K_ROOM_MODERATED), viewOf(t), viewOf(a), mods);
    m_session.pump();
    return ok;
}

ffi::Result<QString> Ed3kRooms::admit(const ffi::Room& room, const ffi::Key32& member, std::uint64_t until)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto item = e->roomAdmit(m_session.now(), room, member, until);
    m_session.pump();
    if (!item)
        return std::unexpected(item.error());
    logEd3k(QStringLiteral("ed3k: room %1: member %2 admitted").arg(roomHex(room), keyText(member).left(8)));
    return admitText(room, item->bytes());
}

ffi::Result<ffi::Room> Ed3kRooms::importAdmit(const QString& text)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto parsed = parseAdmitText(text);
    if (!parsed)
        return std::unexpected(ffi::Error::Malformed);
    const auto& [room, item] = *parsed;
    if (!m_rooms.contains(room))
        return std::unexpected(ffi::Error::NotFound);
    auto ok = e->roomImportItem(m_session.now(), room, std::as_bytes(std::span{item.constData(), std::size_t(item.size())}));
    m_session.pump();
    if (!ok)
        return std::unexpected(ok.error());
    logEd3k(QStringLiteral("ed3k: room %1: admit taken in").arg(roomHex(room)));
    return room;
}

ffi::Result<> Ed3kRooms::ban(const ffi::Room& room, const ffi::Key32& member, std::uint64_t until)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const ffi::Now t = m_session.now();
    // at most 30 days on (§2.3): the core refuses more
    until = std::min(until, t.unixS + kBanMax);
    auto ok = e->roomBan(t, room, member, until);
    m_session.pump();
    return ok;
}

ffi::Result<> Ed3kRooms::lift(const ffi::Room& room, const ffi::Key32& member)
{
    // until at or below now lifts (§2.3)
    return ban(room, member, m_session.now().unixS);
}

ffi::Result<std::uint64_t> Ed3kRooms::startGeneration(const ffi::Room& room)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto g = e->roomStartGeneration(m_session.now(), room);
    m_session.pump();
    return g;
}

QString Ed3kRooms::memberKeyText(const ffi::Room& room) const
{
    const Room* r = find(room);
    return r ? keyText(r->memberKey) : QString();
}

QString Ed3kRooms::roomHex(const ffi::Room& room)
{
    return QString::fromLatin1(bytesOf(room.bytes).toHex());
}

std::optional<ffi::Room> Ed3kRooms::roomFrom(const QString& hex)
{
    const auto a = arrayFrom<16>(QByteArray::fromHex(hex.toLatin1()));
    if (!a)
        return std::nullopt;
    return ffi::Room{*a};
}

std::optional<ffi::Key32> Ed3kRooms::keyFromText(const QString& text)
{
    const QString t = text.trimmed();
    if (t.size() == 64)
        return arrayFrom<32>(QByteArray::fromHex(t.toLatin1()));
    const auto raw = fromBase32(t);
    return raw ? arrayFrom<32>(*raw) : std::nullopt;
}

QString Ed3kRooms::keyText(const ffi::Key32& key)
{
    return base32(key);
}

QString Ed3kRooms::admitText(const ffi::Room& room, std::span<const std::byte> item)
{
    return kAdmitPrefix + base32(room.bytes) + QLatin1Char(':')
         + base32({reinterpret_cast<const std::uint8_t*>(item.data()), item.size()});
}

std::optional<std::pair<ffi::Room, QByteArray>> Ed3kRooms::parseAdmitText(const QString& text)
{
    // blanks and line breaks of a mail or chat are dropped
    QString t = text;
    t.remove(QRegularExpression(QStringLiteral("\\s")));
    if (!t.startsWith(kAdmitPrefix, Qt::CaseInsensitive))
        return std::nullopt;
    const QStringList parts = t.mid(kAdmitPrefix.size()).split(QLatin1Char(':'));
    if (parts.size() != 2)
        return std::nullopt;
    const auto room = fromBase32(parts.at(0));
    const auto item = fromBase32(parts.at(1));
    const auto r = room ? arrayFrom<16>(*room) : std::nullopt;
    if (!r || !item || item->isEmpty())
        return std::nullopt;
    return std::pair{ffi::Room{*r}, *item};
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRooms::onEventPolled(const ffi::Event& ev, bool* handled)
{
    namespace e = ffi::event;
    const auto roomOf = [this](const ffi::Room& r) -> Room* {
        const auto it = m_rooms.find(r);
        return it == m_rooms.end() ? nullptr : &it->second;
    };
    const auto noticeOf = [this](const ffi::Room& r, Notice kind, quint64 value, bool flag, const QByteArray& who) {
        emit notice(bytesOf(r.bytes), int(kind), value, flag, who);
    };
    if (const auto* p = std::get_if<e::RoomPost>(&ev)) {
        *handled = true;
        onPost(*p);
    } else if (const auto* x = std::get_if<e::RoomPostsRemoved>(&ev)) {
        *handled = true;
        QList<QByteArray> ids;
        if (Room* r = roomOf(x->room)) {
            for (const auto& id : x->ids) {
                std::erase_if(r->posts, [&](const Post& q) { return q.id == id; });
                ids.append(bytesOf(id));
            }
            emit postsRemoved(bytesOf(x->room.bytes), ids);
        }
    } else if (const auto* pr = std::get_if<e::RoomPresence>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(pr->room)) {
            const QString nick = fromStd(pr->nick);
            if (pr->left)
                r->present.erase(pr->member);
            else
                r->present[pr->member] = nick;
            emit presenceChanged(bytesOf(pr->room.bytes), bytesOf(pr->member), nick, pr->left);
        }
    } else if (const auto* in = std::get_if<e::RoomInfo>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(in->room)) {
            r->title = fromStd(in->title);
            r->about = fromStd(in->about);
            r->closed = in->closed;
            r->moderated = in->moderated;
            r->mods = in->mods;
            save();
            emit roomsChanged();
        }
    } else if (const auto* b = std::get_if<e::RoomBanned>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(b->room)) {
            r->present.erase(b->member);
            // a ban drops every post of the key (§2.3). The core now reports RoomPostsRemoved
            // for an own ban too (batch 12); this stays as a cheap guard, a no-op then
            // was: the core's own ban gives no RoomPostsRemoved (take_own drops its notes),
            // so the history is cleared here too
            QList<QByteArray> ids;
            std::erase_if(r->posts, [&](const Post& q) {
                if (q.author != b->member)
                    return false;
                ids.append(bytesOf(q.id));
                return true;
            });
            if (!ids.isEmpty())
                emit postsRemoved(bytesOf(b->room.bytes), ids);
            noticeOf(b->room, Notice::Banned, 0, false, bytesOf(b->member));
        }
    } else if (const auto* g = std::get_if<e::RoomGeneration>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(g->room)) {
            r->generation = g->generation;
            r->keyHeld = g->held;
            if (g->held)
                r->keyMissing = false;
            noticeOf(g->room, Notice::Generation, g->generation, g->held, {});
            emit roomsChanged();
        }
    } else if (const auto* k = std::get_if<e::RoomKeyMissing>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(k->room)) {
            r->keyMissing = true;
            noticeOf(k->room, Notice::KeyMissing, k->generation, false, {});
            emit roomsChanged();
        }
    } else if (const auto* w = std::get_if<e::RoomWrapBad>(&ev)) {
        *handled = true;
        noticeOf(w->room, Notice::WrapBad, w->generation, false, bytesOf(w->moderator));
    } else if (const auto* j = std::get_if<e::RoomJoined>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(j->room)) {
            r->joined = true;
            noticeOf(j->room, Notice::Joined, 0, false, {});
            emit roomsChanged();
        }
    } else if (const auto* l = std::get_if<e::RoomLeft>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(l->room)) {
            r->joined = false;
            r->neighbours = 0;
            r->present.clear();
            noticeOf(l->room, Notice::Left, 0, false, {});
            emit roomsChanged();
        }
    } else if (const auto* n = std::get_if<e::RoomNeighbour>(&ev)) {
        *handled = true;
        if (Room* r = roomOf(n->room)) {
            r->neighbours = std::max(0, r->neighbours + (n->up ? 1 : -1));
            noticeOf(n->room, Notice::Neighbour, n->up ? 1 : 0, n->up, bytesOf(n->node.bytes));
            emit roomsChanged();
        }
    } else if (const auto* rf = std::get_if<e::RoomRefused>(&ev)) {
        *handled = true;
        logEd3kDebug(QStringLiteral("ed3k: room %1 refused by %2 (%3)")
                         .arg(roomHex(rf->room), QString::fromLatin1(bytesOf(rf->node.bytes).toHex()))
                         .arg(rf->refusal));
        noticeOf(rf->room, Notice::Refused, rf->refusal, false, bytesOf(rf->node.bytes));
    } else if (const auto* f = std::get_if<e::RoomFault>(&ev)) {
        *handled = true;
        logEd3kDebug(QStringLiteral("ed3k: room %1: %2 sent a bad item").arg(roomHex(f->room), QString::fromLatin1(bytesOf(f->node.bytes).toHex())));
        noticeOf(f->room, Notice::Fault, f->until, false, bytesOf(f->node.bytes));
    }
}

void Ed3kRooms::onSessionStatus()
{
    if (m_session.isRunning() && !m_attached)
        attach();
    else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        m_rooms.clear();
        emit roomsChanged();
    }
}

void Ed3kRooms::attach()
{
    m_attached = true;
    loadSaved();
    // the rooms of the last run: their State::Rooms blob is in the engine already
    const auto saved = m_saved;
    for (const auto& [room, s] : saved) {
        auto joined = joinWith(s.link, s.nick);
        if (!joined) {
            logEd3kWarning(QStringLiteral("ed3k: room %1 not joined again (error %2)").arg(roomHex(room)).arg(int(joined.error())));
            continue;
        }
        if (*joined != room) {
            // another room from the same link: kept under the room the core names
            m_saved.erase(room);
            m_saved[*joined] = s;
        }
        // the info of the last run until a neighbour sends a newer one
        if (auto it = m_rooms.find(*joined); it != m_rooms.end() && it->second.title.isEmpty() && !s.info.isEmpty()) {
            Room& r = it->second;
            r.title = s.info.value(QStringLiteral("title")).toString();
            r.about = s.info.value(QStringLiteral("about")).toString();
            r.closed = s.info.value(QStringLiteral("closed")).toBool();
            r.moderated = s.info.value(QStringLiteral("moderated")).toBool();
            r.mods.clear();
            for (const QCborValue& v : s.info.value(QStringLiteral("mods")).toArray())
                if (const auto k = keyFromText(v.toString()))
                    r.mods.push_back(*k);
        }
    }
    save();
    emit roomsChanged();
}

ffi::Result<ffi::Room> Ed3kRooms::joinWith(const QString& link, const QString& nick)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    // the member key of this room when it was joined before, else a new one (§1.1)
    std::optional<ffi::Room> known;
    for (const auto& [room, s] : m_saved)
        if (s.link == link)
            known = room;
    std::optional<Seed> seed = known ? loadSecret(roomsDir(), memberFile(*known)) : std::nullopt;
    const bool fresh = !seed;
    if (fresh) {
        seed = newSecret(QStringLiteral("new.member"));
        QFile::remove(QDir(roomsDir()).filePath(QStringLiteral("new.member")));
    }
    if (!seed)
        return std::unexpected(ffi::Error::Io);
    const Secret member(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    // owner: the room key of the link's key is held
    std::optional<Seed> ownerSeed;
    if (const auto key = linkKey(link))
        ownerSeed = loadSecret(roomsDir(), ownerFile(*key));
    std::optional<Secret> owner;
    if (ownerSeed) {
        owner.emplace(*ownerSeed);
        wipe(std::span<std::uint8_t>{*ownerSeed});
    }

    const QByteArray l = link.toUtf8();
    const QByteArray n = nick.toUtf8();
    auto room = e->roomJoin(m_session.now(), viewOf(l), member.expose(), viewOf(n), owner ? &owner->expose() : nullptr);
    if (!room) {
        m_session.pump();
        return room;
    }
    if (fresh || *room != known) {
        QString err;
        if (!replaceSecret(roomsDir(), memberFile(*room), member.expose(), &err)) {
            logEd3kWarning(QStringLiteral("ed3k: room member key not stored (%1)").arg(err));
            (void)e->roomLeave(m_session.now(), *room);
            m_session.pump();
            return std::unexpected(ffi::Error::Io);
        }
    }
    // tracked before the pump: the room's info and history come with it
    Room& r = track(*room, link, nick);
    r.owner = owner.has_value();
    if (const auto pub = ffi::roomMemberKey(member.expose()))
        r.memberKey = *pub;
    m_session.pump();
    return room;
}

Ed3kRooms::Room& Ed3kRooms::track(const ffi::Room& room, const QString& link, const QString& nick)
{
    Room& r = m_rooms[room];
    r.room = room;
    r.link = link;
    r.nick = nick;
    r.isPrivate = QUrlQuery(link.mid(link.indexOf(QLatin1Char('?')) + 1)).hasQueryItem(QStringLiteral("s"));
    Saved& s = m_saved[room];
    s.link = link;
    s.nick = nick;
    return r;
}

void Ed3kRooms::onPost(const ffi::event::RoomPost& p)
{
    const auto it = m_rooms.find(p.room);
    if (it == m_rooms.end())
        return;
    Room& r = it->second;
    Post post;
    post.id = p.id;
    post.author = p.author;
    post.authorId = p.authorId;
    post.nick = fromStd(p.nick);
    post.moderator = p.moderator;
    post.sent = p.sent;
    post.text = fromStd(p.text);
    post.replyTo = p.replyTo;
    r.posts.push_back(std::move(post));
    while (r.posts.size() > kHistory)
        r.posts.pop_front();
    emit postShown(bytesOf(p.room.bytes), bytesOf(p.id));
}

void Ed3kRooms::loadSaved()
{
    m_saved.clear();
    QByteArray blob = readBlob(QDir(roomsDir()).filePath(kJoinedFile));
    const QCborArray list = QCborValue::fromCbor(blob).toArray();
    wipe(blob);
    for (const QCborValue& v : list) {
        const QCborMap m = v.toMap();
        const auto room = roomFrom(m.value(QStringLiteral("room")).toString());
        const QString link = m.value(QStringLiteral("link")).toString();
        if (room && link.startsWith(QLatin1String("ed3k:room?"), Qt::CaseInsensitive))
            m_saved[*room] = Saved{link, m.value(QStringLiteral("nick")).toString(), m.value(QStringLiteral("info")).toMap()};
    }
}

void Ed3kRooms::save() const
{
    if (m_session.stateDir().isEmpty() || !QDir().mkpath(roomsDir()))
        return;
    QCborArray list;
    for (const auto& [room, s] : m_saved) {
        QCborMap info = s.info;
        if (const Room* r = find(room); r && !r->title.isEmpty()) {
            QCborArray mods;
            for (const auto& k : r->mods)
                mods.append(keyText(k));
            info = QCborMap{{QStringLiteral("title"), r->title}, {QStringLiteral("about"), r->about},
                            {QStringLiteral("closed"), r->closed}, {QStringLiteral("moderated"), r->moderated},
                            {QStringLiteral("mods"), mods}};
        }
        list.append(QCborMap{
            {QStringLiteral("room"), roomHex(room)},
            {QStringLiteral("link"), s.link},
            {QStringLiteral("nick"), s.nick},
            {QStringLiteral("info"), info},
        });
    }
    QByteArray blob = list.toCborValue().toCbor();
    if (!writeBlob(QDir(roomsDir()).filePath(kJoinedFile), std::as_bytes(std::span{blob.constData(), std::size_t(blob.size())})))
        logEd3kWarning(QStringLiteral("ed3k: cannot write the joined rooms"));
    wipe(blob);
}

QString Ed3kRooms::roomsDir() const
{
    return QDir(m_session.stateDir()).filePath(kRoomsDir);
}

QString Ed3kRooms::memberFile(const ffi::Room& room)
{
    return roomHex(room) + QStringLiteral(".member");
}

QString Ed3kRooms::ownerFile(const ffi::Key32& roomKey)
{
    return QStringLiteral("owner-%1.key").arg(QString::fromLatin1(bytesOf(roomKey).toHex()));
}

std::optional<Seed> Ed3kRooms::newSecret(const QString& name) const
{
    // loadOrCreateSecret makes it from the CSPRNG; the caller stores it under its own name
    QFile::remove(QDir(roomsDir()).filePath(name));
    QString err;
    auto seed = loadOrCreateSecret(roomsDir(), name, {}, &err);
    if (!seed)
        logEd3kWarning(QStringLiteral("ed3k: no room secret (%1)").arg(err));
    return seed;
}

std::optional<ffi::Key32> Ed3kRooms::linkKey(const QString& link)
{
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    if (q < 0)
        return std::nullopt;
    return keyFromText(QUrlQuery(link.mid(q + 1)).queryItemValue(QStringLiteral("key")));
}

} // namespace eMule::ed3k
