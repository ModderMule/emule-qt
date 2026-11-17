/// @file Ed3kRooms.cpp
/// @brief ed3k rooms in the daemon — implementation.

#include "Ed3kRooms.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kRoomsDir = QStringLiteral("rooms");
const QString kJoinedFile = QStringLiteral("joined.cbor");   // links with s: owner-only
const QString kAdmitPrefix = QStringLiteral("ed3k-admit:");
const QString kPostsSuffix = QStringLiteral(".posts");   // stored posts, owner-only; private rooms sealed
const QString kPostsKeyFile = QStringLiteral("posts.key");   // master key of sealed posts files
// sealed posts file: nonce(12) ‖ ChaCha20-Poly1305(CBOR) ‖ tag(16), AAD = room id
constexpr int kNonceLen = 12;
constexpr int kTagLen = 16;
constexpr int kPostsWriteDelayMs = 5000;
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

/// Per-room subkey: HKDF-SHA256(master, info = "ed3k room posts v1" ‖ room). The master is
/// uniformly random, so Extract adds nothing but costs nothing either.
std::optional<Seed> postsSubkey(const Seed& master, const ffi::Room& room)
{
    static constexpr char kInfo[] = "ed3k room posts v1";
    std::array<std::uint8_t, sizeof kInfo - 1 + 16> info{};
    std::memcpy(info.data(), kInfo, sizeof kInfo - 1);
    std::memcpy(info.data() + sizeof kInfo - 1, room.bytes.data(), room.bytes.size());
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    Seed out{};
    std::size_t len = out.size();
    const bool ok = ctx && EVP_PKEY_derive_init(ctx) > 0 && EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0
        && EVP_PKEY_CTX_set1_hkdf_key(ctx, master.data(), int(master.size())) > 0
        && EVP_PKEY_CTX_add1_hkdf_info(ctx, info.data(), int(info.size())) > 0
        && EVP_PKEY_derive(ctx, out.data(), &len) > 0 && len == out.size();
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        wipe(std::span<std::uint8_t>{out});
        return std::nullopt;
    }
    return out;
}

/// ChaCha20-Poly1305 of @p plain under @p key; empty: cipher failed.
QByteArray sealPosts(const Seed& key, const ffi::Room& room, const QByteArray& plain)
{
    QByteArray out(kNonceLen + plain.size() + kTagLen, Qt::Uninitialized);
    auto* nonce = reinterpret_cast<unsigned char*>(out.data());
    auto* ct = nonce + kNonceLen;
    if (RAND_bytes(nonce, kNonceLen) != 1)
        return {};
    const QByteArray aad = bytesOf(room.bytes);   // another room's file fails
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int fin = 0;
    const bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key.data(), nonce) == 1
        && EVP_EncryptUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(aad.constData()), int(aad.size())) == 1
        && EVP_EncryptUpdate(ctx, ct, &len, reinterpret_cast<const unsigned char*>(plain.constData()), int(plain.size())) == 1
        && EVP_EncryptFinal_ex(ctx, ct + len, &fin) == 1 && len + fin == plain.size()
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, kTagLen, ct + plain.size()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok ? out : QByteArray{};
}

/// The CBOR of a sealed posts file; nullopt: wrong key, another room's file or damaged.
std::optional<QByteArray> openPosts(const Seed& key, const ffi::Room& room, const QByteArray& sealed)
{
    const qsizetype size = sealed.size() - kNonceLen - kTagLen;
    if (size < 0)
        return std::nullopt;
    const auto* nonce = reinterpret_cast<const unsigned char*>(sealed.constData());
    const auto* ct = nonce + kNonceLen;
    QByteArray tag(reinterpret_cast<const char*>(ct + size), kTagLen);
    QByteArray plain(size, Qt::Uninitialized);
    const QByteArray aad = bytesOf(room.bytes);   // another room's file fails
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int fin = 0;
    const bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key.data(), nonce) == 1
        && EVP_DecryptUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(aad.constData()), int(aad.size())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &len, ct, int(size)) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, kTagLen, tag.data()) == 1
        && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + len, &fin) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        wipe(plain);
        return std::nullopt;
    }
    return plain;
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
    // was: connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kRooms::onEventPolled, Qt::DirectConnection);
    // node key from eventPolled, the anonymous node's from keyedEventPolled: each event once
    connect(&m_session, &Ed3kSession::eventPolled, this,
            [this](const ffi::Event& ev, bool* handled) { onEventPolled(ev, ffi::KeyKind::Node, handled); }, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::keyedEventPolled, this,
            [this](const ffi::Event& ev, ffi::KeyKind key, bool* handled) {
                if (key == ffi::KeyKind::Anon)
                    onEventPolled(ev, key, handled);
            },
            Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kRooms::onSessionStatus);
    connect(&m_session.hide(), &Ed3kHide::started, this, [this] {
        if (m_attached)
            rejoinSaved(ffi::KeyKind::Anon);
    });
    connect(&m_session.hide(), &Ed3kHide::stopped, this, &Ed3kRooms::hideStopped);
    m_postsTimer = new QTimer(this);
    m_postsTimer->setSingleShot(true);
    m_postsTimer->setInterval(kPostsWriteDelayMs);
    m_historyStored = historyStored();
    m_historyMax = historyMax();
    connect(m_postsTimer, &QTimer::timeout, this, &Ed3kRooms::writeDirtyPosts);
    onSessionStatus();
}

Ed3kRooms::~Ed3kRooms()
{
    writeDirtyPosts();
}

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
    // hops.md §1: under the anonymous key while hiding, and it stays there
    const ffi::KeyKind key = m_session.hide().keyForUse();
    // was: auto made = e->roomCreate(m_session.now(), roomKey.expose(), ...);
    auto made = e->roomCreate(key, m_session.now(), roomKey.expose(), member.expose(), viewOf(n), id, flags, viewOf(t), viewOf(a), mods);
    if (!made) {
        m_session.pump();
        return std::unexpected(made.error());
    }
    // tracked before the pump: the room's first events come with it
    const auto linkText = made->link.text();
    Room& r = track(made->room, QString::fromUtf8(linkText.data(), qsizetype(linkText.size())), nick, key);
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
        (void)e->roomLeave(key, m_session.now(), made->room);
        m_rooms.erase(made->room);
        m_saved.erase(made->room);
        m_session.pump();
        return std::unexpected(ffi::Error::Io);
    }
    m_session.pump();
    save();
    logEd3k(QStringLiteral("ed3k: room %1 made (%2 key)").arg(roomHex(made->room), Ed3kHide::keyName(key)));
    emit roomsChanged();
    return made->room;
}

ffi::Result<ffi::Room> Ed3kRooms::join(const QString& link, const QString& nick)
{
    if (!m_session.engine())
        return std::unexpected(ffi::Error::State);
    const QString l = link.trimmed();
    // the same room by key + id: a link with another dn or case is no new join
    for (const auto& [room, r] : m_rooms)
        if (sameRoomLink(r.link, l)) {
            // hops.md §1: a room of the anonymous key is not joined under the node key
            if (r.waitingHide)
                return std::unexpected(ffi::Error::State);
            return room;
        }
    // was: auto room = joinWith(l, nick);
    const ffi::KeyKind key = m_session.hide().keyForUse();
    auto room = joinWith(l, nick, key);
    if (!room)
        return room;
    save();
    logEd3k(QStringLiteral("ed3k: room %1 joined (%2 key)").arg(roomHex(*room), Ed3kHide::keyName(key)));
    emit roomsChanged();
    return room;
}

ffi::Result<> Ed3kRooms::leave(const ffi::Room& room, bool dropRoomKey)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    // a waiting anonymous room is not on any node: only forgotten here
    const auto key = keyOf(room);
    if (key) {
        // was: auto ok = e->roomLeave(m_session.now(), room);
        auto ok = e->roomLeave(*key, m_session.now(), room);
        m_session.pump();
        if (!ok && ok.error() != ffi::Error::NotFound)
            return ok;
    }
    // the member key goes with the room (a new one for a new join, §1.1); the room key stays
    // unless asked
    QFile::remove(QDir(roomsDir()).filePath(memberFile(room)));
    bool keyDropped = false;
    if (dropRoomKey) {
        const auto it = m_rooms.find(room);
        if (it != m_rooms.end())
            if (const auto roomKey = linkKey(it->second.link))
                keyDropped = QFile::remove(QDir(roomsDir()).filePath(ownerFile(*roomKey)));
    }
    m_rooms.erase(room);
    m_saved.erase(room);
    m_dirtyPosts.erase(room);
    removePostsFile(room);
    save();
    logEd3k(keyDropped ? QStringLiteral("ed3k: room %1 left, room key deleted").arg(roomHex(room))
                       : QStringLiteral("ed3k: room %1 left").arg(roomHex(room)));
    emit roomsChanged();
    return {};
}

ffi::Result<> Ed3kRooms::connectPeer(const ffi::Room& room, const ffi::Source& peer)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    auto ok = e->roomConnect(*key, m_session.now(), room, peer);
    m_session.pump();
    return ok;
}

ffi::Result<std::vector<ffi::PostId>> Ed3kRooms::post(const ffi::Room& room, const QString& text, std::optional<ffi::PostId> replyTo)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    const QByteArray t = text.toUtf8();
    auto ids = e->roomPost(*key, m_session.now(), room, viewOf(t), replyTo);
    m_session.pump();
    return ids;
}

ffi::Result<> Ed3kRooms::setNick(const ffi::Room& room, const QString& nick)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    const QByteArray n = nick.toUtf8();
    auto ok = e->roomSetNick(*key, m_session.now(), room, viewOf(n));
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
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    const QByteArray t = title.toUtf8();
    const QByteArray a = about.toUtf8();
    auto ok = e->roomSetInfo(*key, m_session.now(), room, flags & (ED3K_ROOM_CLOSED | ED3K_ROOM_MODERATED), viewOf(t), viewOf(a), mods);
    m_session.pump();
    return ok;
}

ffi::Result<QString> Ed3kRooms::admit(const ffi::Room& room, const ffi::Key32& member, std::uint64_t until)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    auto item = e->roomAdmit(*key, m_session.now(), room, member, until);
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
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    auto ok = e->roomImportItem(*key, m_session.now(), room, std::as_bytes(std::span{item.constData(), std::size_t(item.size())}));
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
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    const ffi::Now t = m_session.now();
    // at most 30 days on (§2.3): the core refuses more
    until = std::min(until, t.unixS + kBanMax);
    auto ok = e->roomBan(*key, t, room, member, until);
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
    const auto key = keyOf(room);
    if (!key)
        return std::unexpected(key.error());
    auto g = e->roomStartGeneration(*key, m_session.now(), room);
    m_session.pump();
    return g;
}

ffi::Result<> Ed3kRooms::setMuted(const ffi::Room& room, const std::array<std::uint8_t, 16>& memberId, bool on)
{
    const auto it = m_rooms.find(room);
    if (it == m_rooms.end())
        return std::unexpected(ffi::Error::NotFound);
    // local only (§6): nothing goes to the core; posts are kept, the GUI hides them
    Saved& s = m_saved[room];
    if (on) {
        it->second.muted.insert(memberId);
        s.muted.insert(memberId);
    } else {
        it->second.muted.erase(memberId);
        s.muted.erase(memberId);
    }
    save();
    emit roomsChanged();
    return {};
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

void Ed3kRooms::applyHistoryPrefs()
{
    const std::size_t max = historyMax();
    const bool stored = historyStored();
    if (stored == m_historyStored && max == m_historyMax)
        return;
    m_historyStored = stored;
    m_historyMax = max;
    for (auto& [room, r] : m_rooms) {
        bool trimmed = false;
        while (r.posts.size() > max) {
            r.postIds.erase(r.posts.front().id);
            r.posts.pop_front();
            trimmed = true;
        }
        if (trimmed)
            m_dirtyPosts.insert(room);
    }
    if (!historyStored()) {
        // off: nothing stays on disk
        m_dirtyPosts.clear();
        m_postsTimer->stop();
        for (const auto& [room, s] : m_saved)
            removePostsFile(room);
        if (!m_session.stateDir().isEmpty())
            QFile::remove(QDir(roomsDir()).filePath(kPostsKeyFile));
        return;
    }
    // on (again): every room written, the trimmed ones too
    for (const auto& [room, r] : m_rooms)
        m_dirtyPosts.insert(room);
    writeDirtyPosts();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRooms::onEventPolled(const ffi::Event& ev, ffi::KeyKind key, bool* handled)
{
    namespace e = ffi::event;
    // a room's events come from the node it is joined under (hops.md §1)
    const auto roomOf = [this, key](const ffi::Room& r) -> Room* {
        const auto it = m_rooms.find(r);
        return it == m_rooms.end() || it->second.key != key ? nullptr : &it->second;
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
                r->postIds.erase(id);
                ids.append(bytesOf(id));
            }
            postsChanged(x->room);
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
                r->postIds.erase(q.id);
                ids.append(bytesOf(q.id));
                return true;
            });
            if (!ids.isEmpty()) {
                postsChanged(b->room);
                emit postsRemoved(bytesOf(b->room.bytes), ids);
            }
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
            r->notAdmitted = false;   // a member took us (admitted, #17)
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
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: room %1 refused by %2 (%3)")
                         .arg(roomHex(rf->room), QString::fromLatin1(bytesOf(rf->node.bytes).toHex()))
                         .arg(rf->refusal); });
        // #17: ROOM_MEMBER: the room is closed and we are not admitted; the GUI shows the key
        if (Room* r = roomOf(rf->room); r && rf->refusal == ED3K_ROOM_MEMBER && !r->notAdmitted) {
            r->notAdmitted = true;
            emit roomsChanged();
        }
        noticeOf(rf->room, Notice::Refused, rf->refusal, false, bytesOf(rf->node.bytes));
    } else if (const auto* f = std::get_if<e::RoomFault>(&ev)) {
        *handled = true;
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: room %1: %2 sent a bad item").arg(roomHex(f->room), QString::fromLatin1(bytesOf(f->node.bytes).toHex())); });
        noticeOf(f->room, Notice::Fault, f->until, false, bytesOf(f->node.bytes));
    }
}

void Ed3kRooms::onSessionStatus()
{
    if (m_session.isRunning() && !m_attached)
        attach();
    else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        writeDirtyPosts();
        m_rooms.clear();
        emit roomsChanged();
    }
}

void Ed3kRooms::attach()
{
    m_attached = true;
    loadSaved();
    rejoinSaved(std::nullopt);
}

void Ed3kRooms::rejoinSaved(std::optional<ffi::KeyKind> only)
{
    const bool hiding = m_session.hide().hiding();
    // the rooms of the last run: their State::Rooms blob is in the engine already
    const auto saved = m_saved;
    for (const auto& [room, s] : saved) {
        if (only && s.key != *only)
            continue;
        if (const auto it = m_rooms.find(room); it != m_rooms.end() && !it->second.waitingHide)
            continue;
        // hops.md §1: hiding off, nothing falls back to the node key: listed, waits
        if (s.key == ffi::KeyKind::Anon && !hiding) {
            trackWaiting(room, s);
            continue;
        }
        // was: auto joined = joinWith(s.link, s.nick);
        auto joined = joinWith(s.link, s.nick, s.key);
        if (!joined) {
            logEd3kWarning(QStringLiteral("ed3k: room %1 not joined again (error %2)").arg(roomHex(room)).arg(int(joined.error())));
            continue;
        }
        if (*joined != room) {
            // another room from the same link: kept under the room the core names
            m_saved.erase(room);
            m_saved[*joined] = s;
        }
        if (auto it = m_rooms.find(*joined); it != m_rooms.end()) {
            it->second.muted = s.muted;
            // the info of the last run until a neighbour sends a newer one
            applySavedInfo(it->second, s);
        }
    }
    save();
    emit roomsChanged();
}

void Ed3kRooms::hideStopped()
{
    // the anonymous node took its rooms along: they wait, listed (rejoinSaved at the next start)
    bool any = false;
    for (auto& [room, r] : m_rooms) {
        if (r.key != ffi::KeyKind::Anon || r.waitingHide)
            continue;
        r.waitingHide = true;
        r.joined = false;
        r.neighbours = 0;
        r.present.clear();
        r.keyHeld = false;
        any = true;
    }
    if (any) {
        logEd3k(QStringLiteral("ed3k: hiding stopped; rooms of the anonymous key wait until it is on again"));
        emit roomsChanged();
    }
}

ffi::Result<ffi::KeyKind> Ed3kRooms::keyOf(const ffi::Room& room) const
{
    const Room* r = find(room);
    if (!r)
        return ffi::KeyKind::Node;   // not tracked: the core answers (NotFound)
    if (r->waitingHide || (r->key == ffi::KeyKind::Anon && !m_session.hide().hiding()))
        return std::unexpected(ffi::Error::State);
    return r->key;
}

void Ed3kRooms::trackWaiting(const ffi::Room& room, const Saved& s)
{
    Room& r = track(room, s.link, s.nick, s.key);
    r.waitingHide = true;
    r.muted = s.muted;
    if (const auto roomKey = linkKey(s.link))
        r.owner = QFile::exists(QDir(roomsDir()).filePath(ownerFile(*roomKey)));
    if (auto seed = loadSecret(roomsDir(), memberFile(room))) {
        const Secret member(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        if (const auto pub = ffi::roomMemberKey(member.expose()))
            r.memberKey = *pub;
    }
    applySavedInfo(r, s);
}

void Ed3kRooms::applySavedInfo(Room& r, const Saved& s)
{
    if (!r.title.isEmpty() || s.info.isEmpty())
        return;
    r.title = s.info.value(QStringLiteral("title")).toString();
    r.about = s.info.value(QStringLiteral("about")).toString();
    r.closed = s.info.value(QStringLiteral("closed")).toBool();
    r.moderated = s.info.value(QStringLiteral("moderated")).toBool();
    r.mods.clear();
    for (const QCborValue& v : s.info.value(QStringLiteral("mods")).toArray())
        if (const auto k = keyFromText(v.toString()))
            r.mods.push_back(*k);
}

ffi::Result<ffi::Room> Ed3kRooms::joinWith(const QString& link, const QString& nick, ffi::KeyKind key)
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
    if (const auto roomKey = linkKey(link))
        ownerSeed = loadSecret(roomsDir(), ownerFile(*roomKey));
    std::optional<Secret> owner;
    if (ownerSeed) {
        owner.emplace(*ownerSeed);
        wipe(std::span<std::uint8_t>{*ownerSeed});
    }

    const QByteArray l = link.toUtf8();
    const QByteArray n = nick.toUtf8();
    // was: auto room = e->roomJoin(m_session.now(), viewOf(l), member.expose(), viewOf(n), owner ? &owner->expose() : nullptr);
    auto room = e->roomJoin(key, m_session.now(), viewOf(l), member.expose(), viewOf(n), owner ? &owner->expose() : nullptr);
    if (!room) {
        m_session.pump();
        return room;
    }
    if (fresh || *room != known) {
        QString err;
        if (!replaceSecret(roomsDir(), memberFile(*room), member.expose(), &err)) {
            logEd3kWarning(QStringLiteral("ed3k: room member key not stored (%1)").arg(err));
            (void)e->roomLeave(key, m_session.now(), *room);
            m_session.pump();
            return std::unexpected(ffi::Error::Io);
        }
    }
    // tracked before the pump: the room's info and history come with it
    Room& r = track(*room, link, nick, key);
    r.waitingHide = false;
    r.owner = owner.has_value();
    if (const auto pub = ffi::roomMemberKey(member.expose()))
        r.memberKey = *pub;
    m_session.pump();
    return room;
}

Ed3kRooms::Room& Ed3kRooms::track(const ffi::Room& room, const QString& link, const QString& nick, ffi::KeyKind key)
{
    const auto [it, added] = m_rooms.try_emplace(room);
    Room& r = it->second;
    r.room = room;
    r.link = link;
    r.nick = nick;
    r.key = key;
    r.isPrivate = QUrlQuery(link.mid(link.indexOf(QLatin1Char('?')) + 1)).hasQueryItem(QStringLiteral("s"));
    // a restart: the posts of the last run before the network's history comes
    if (added)
        loadPosts(r);
    Saved& s = m_saved[room];
    s.link = link;
    s.nick = nick;
    s.key = key;
    return r;
}

void Ed3kRooms::onPost(const ffi::event::RoomPost& p)
{
    const auto it = m_rooms.find(p.room);
    if (it == m_rooms.end())
        return;
    Room& r = it->second;
    // the network's history repeats stored and shown posts (room.md §4.3: since 0 at start)
    if (!r.postIds.insert(p.id).second)
        return;
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
    const std::size_t max = historyMax();
    while (r.posts.size() > max) {
        r.postIds.erase(r.posts.front().id);
        r.posts.pop_front();
    }
    postsChanged(p.room);
    emit postShown(bytesOf(p.room.bytes), bytesOf(p.id));
}

std::size_t Ed3kRooms::historyMax()
{
    return historyStored() ? std::size_t(std::max(thePrefs.ed3kRoomHistoryMax(), 1)) : kHistory;
}

bool Ed3kRooms::historyStored()
{
    return thePrefs.ed3kRoomHistoryStore();
}

void Ed3kRooms::loadPosts(Room& r) const
{
    if (!historyStored() || m_session.stateDir().isEmpty())
        return;
    QByteArray blob = readBlob(QDir(roomsDir()).filePath(postsFile(r.room)));
    if (blob.isEmpty())
        return;
    if (sealedRoom(r)) {
        auto key = postsKey(r.room, false);
        auto plain = key ? openPosts(*key, r.room, blob) : std::nullopt;
        if (key)
            wipe(std::span<std::uint8_t>{*key});
        wipe(blob);
        if (!plain) {
            // key lost or file damaged: history forgotten, next write overwrites
            logEd3kWarning(QStringLiteral("ed3k: room %1: stored posts not readable").arg(roomHex(r.room)));
            return;
        }
        blob = std::move(*plain);
    }
    const QCborArray list = QCborValue::fromCbor(blob).toArray();
    wipe(blob);
    const std::size_t max = historyMax();
    const qsizetype from = std::max<qsizetype>(0, list.size() - qsizetype(max));
    for (qsizetype i = from; i < list.size(); ++i) {
        const QCborMap m = list.at(i).toMap();
        const auto id = arrayFrom<16>(m.value(QStringLiteral("id")).toByteArray());
        const auto author = arrayFrom<32>(m.value(QStringLiteral("author")).toByteArray());
        const auto authorId = arrayFrom<16>(m.value(QStringLiteral("authorId")).toByteArray());
        if (!id || !author || !authorId || !r.postIds.insert(*id).second)
            continue;
        Post post;
        post.id = *id;
        post.author = *author;
        post.authorId = *authorId;
        post.nick = m.value(QStringLiteral("nick")).toString();
        post.moderator = m.value(QStringLiteral("moderator")).toBool();
        post.sent = std::uint64_t(m.value(QStringLiteral("sent")).toInteger());
        post.text = m.value(QStringLiteral("text")).toString();
        post.replyTo = arrayFrom<16>(m.value(QStringLiteral("replyTo")).toByteArray());
        r.posts.push_back(std::move(post));
    }
    logEd3kDebug([&] { return QStringLiteral("ed3k: room %1: %2 stored posts").arg(roomHex(r.room)).arg(r.posts.size()); });
}

void Ed3kRooms::postsChanged(const ffi::Room& room)
{
    if (!historyStored())
        return;
    m_dirtyPosts.insert(room);
    if (!m_postsTimer->isActive())
        m_postsTimer->start();
}

void Ed3kRooms::writeDirtyPosts()
{
    m_postsTimer->stop();
    const auto dirty = std::exchange(m_dirtyPosts, {});
    if (!historyStored())
        return;
    for (const auto& room : dirty)
        if (const Room* r = find(room))
            writePosts(*r);
}

void Ed3kRooms::writePosts(const Room& r) const
{
    if (m_session.stateDir().isEmpty() || !QDir().mkpath(roomsDir()))
        return;
    const QString path = QDir(roomsDir()).filePath(postsFile(r.room));
    if (r.posts.empty()) {
        removePostsFile(r.room);
        return;
    }
    QCborArray list;
    for (const Post& p : r.posts) {
        QCborMap m{
            {QStringLiteral("id"), bytesOf(p.id)},
            {QStringLiteral("author"), bytesOf(p.author)},
            {QStringLiteral("authorId"), bytesOf(p.authorId)},
            {QStringLiteral("nick"), p.nick},
            {QStringLiteral("sent"), qint64(p.sent)},
            {QStringLiteral("text"), p.text},
        };
        if (p.moderator)
            m.insert(QStringLiteral("moderator"), true);
        if (p.replyTo)
            m.insert(QStringLiteral("replyTo"), bytesOf(*p.replyTo));
        list.append(m);
    }
    QByteArray blob = list.toCborValue().toCbor();
    if (sealedRoom(r)) {
        auto key = postsKey(r.room, true);
        QByteArray sealed = key ? sealPosts(*key, r.room, blob) : QByteArray{};
        if (key)
            wipe(std::span<std::uint8_t>{*key});
        wipe(blob);
        if (sealed.isEmpty()) {
            // never plain for a private room
            logEd3kWarning(QStringLiteral("ed3k: room %1: posts not sealed, not stored").arg(roomHex(r.room)));
            removePostsFile(r.room);
            return;
        }
        blob = std::move(sealed);
    }
    writeBlobLater(path, std::as_bytes(std::span{blob.constData(), std::size_t(blob.size())}));
    wipe(blob);
}

bool Ed3kRooms::sealedRoom(const Room& r)
{
    return r.isPrivate || r.closed;
}

std::optional<Seed> Ed3kRooms::postsKey(const ffi::Room& room, bool create) const
{
    auto master = create ? loadOrCreateSecret(roomsDir(), kPostsKeyFile, nullptr) : loadSecret(roomsDir(), kPostsKeyFile);
    if (!master)
        return std::nullopt;
    auto key = postsSubkey(*master, room);
    wipe(std::span<std::uint8_t>{*master});
    return key;
}

void Ed3kRooms::removePostsFile(const ffi::Room& room) const
{
    if (m_session.stateDir().isEmpty())
        return;
    const QString path = QDir(roomsDir()).filePath(postsFile(room));
    forgetBlobWrites(path);
    QFile::remove(path);
}

QString Ed3kRooms::postsFile(const ffi::Room& room)
{
    return roomHex(room) + kPostsSuffix;
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
        if (!room || !link.startsWith(QLatin1String("ed3k:room?"), Qt::CaseInsensitive))
            continue;
        Saved saved{link, m.value(QStringLiteral("nick")).toString(), m.value(QStringLiteral("info")).toMap(), {},
                    m.value(QStringLiteral("key")).toString() == QLatin1String("anon") ? ffi::KeyKind::Anon : ffi::KeyKind::Node};
        for (const QCborValue& id : m.value(QStringLiteral("muted")).toArray()) {
            const QByteArray raw = QByteArray::fromHex(id.toString().toLatin1());
            std::array<std::uint8_t, 16> mid{};
            if (raw.size() != qsizetype(mid.size()))
                continue;
            std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(mid.data()));
            saved.muted.insert(mid);
        }
        m_saved[*room] = std::move(saved);
        // was: m_saved[*room] = Saved{link, nick, info};
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
        QCborArray muted;
        for (const auto& id : s.muted)
            muted.append(QString::fromLatin1(bytesOf(id).toHex()));
        QCborMap m{
            {QStringLiteral("room"), roomHex(room)},
            {QStringLiteral("link"), s.link},
            {QStringLiteral("nick"), s.nick},
            {QStringLiteral("info"), info},
            {QStringLiteral("muted"), muted},
        };
        if (s.key == ffi::KeyKind::Anon)
            m.insert(QStringLiteral("key"), QStringLiteral("anon"));
        list.append(m);
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

QString Ed3kRooms::linkWithName(const QString& link, const QString& title)
{
    const QString name = title.trimmed();
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    if (name.isEmpty() || q < 0)
        return link;
    // parameters kept as written (s among them); only dn replaced
    QStringList items;
    for (const QString& item : link.mid(q + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts))
        if (!item.startsWith(QLatin1String("dn="), Qt::CaseInsensitive))
            items << item;
    items << QStringLiteral("dn=") + QString::fromLatin1(QUrl::toPercentEncoding(name));
    return link.left(q + 1) + items.join(QLatin1Char('&'));
}

bool Ed3kRooms::sameRoomLink(const QString& a, const QString& b)
{
    const auto ident = [](const QString& link) {
        const qsizetype q = link.indexOf(QLatin1Char('?'));
        const QUrlQuery query(q < 0 ? QString() : link.mid(q + 1));
        return std::pair{query.queryItemValue(QStringLiteral("key")).toLower(), query.queryItemValue(QStringLiteral("id")).toLower()};
    };
    const auto x = ident(a);
    return !x.first.isEmpty() && !x.second.isEmpty() && x == ident(b);
}

std::optional<ffi::Key32> Ed3kRooms::linkKey(const QString& link)
{
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    if (q < 0)
        return std::nullopt;
    return keyFromText(QUrlQuery(link.mid(q + 1)).queryItemValue(QStringLiteral("key")));
}

} // namespace eMule::ed3k
