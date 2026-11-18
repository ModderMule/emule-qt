#include "pch.h"
/// @file Ed3kRoomMedia.cpp
/// @brief Room media in the daemon — implementation.

#include "Ed3kRoomMedia.h"

#include "Ed3kSession.h"
#include "prefs/Preferences.h"
#include "utils/MediaSniff.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>

#include <openssl/rand.h>

#include <algorithm>
#include <span>
#include <vector>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kMediaDir = QStringLiteral("media");
const QString kPartSuffix = QStringLiteral(".part");
constexpr qint64 kHourSecs = 3600;
constexpr std::uint64_t kAutoMaxAge = 24 * 3600;   // older posts wait for a click
constexpr int kTrimDelayMs = 10'000;

QByteArray bytesOf(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

std::string_view viewOf(const QByteArray& b)
{
    return {b.constData(), std::size_t(b.size())};
}

} // namespace

bool Ed3kRoomMedia::isImageType(const QString& type)
{
    return MediaSniff::isImage(type);
}

bool Ed3kRoomMedia::isVideoType(const QString& type)
{
    return MediaSniff::isVideo(type);
}

QString Ed3kRoomMedia::sniffType(const QByteArray& b)
{
    return MediaSniff::type(b);
}

Ed3kRoomMedia::Ed3kRoomMedia(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_trimTimer(new QTimer(this))
{
    m_trimTimer->setSingleShot(true);
    m_trimTimer->setInterval(kTrimDelayMs);
    connect(m_trimTimer, &QTimer::timeout, this, &Ed3kRoomMedia::trim);
}

Ed3kRoomMedia::~Ed3kRoomMedia()
{
    for (auto& [root, e] : m_entries)
        if (e.media.key)
            std::ranges::fill(*e.media.key, 0);
}

void Ed3kRoomMedia::attach(const QString& roomsDir)
{
    m_dir = QDir(roomsDir).filePath(kMediaDir);
    QDir().mkpath(m_dir);
    QFile::setPermissions(m_dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // a fetch cut by a stop: started again when its post comes
    for (const QString& f : QDir(m_dir).entryList({QStringLiteral("*") + kPartSuffix}, QDir::Files))
        QFile::remove(QDir(m_dir).filePath(f));
    m_trimTimer->start();
}

void Ed3kRoomMedia::detach()
{
    m_dir.clear();
    for (auto& [root, e] : m_entries) {
        e.held = false;
        if (e.state == State::Fetching)
            e.state = State::None;
    }
}

void Ed3kRoomMedia::onPost(const ffi::Room& room, ffi::KeyKind key, const Media& media, std::uint64_t sent)
{
    auto [it, fresh] = m_entries.try_emplace(media.root);
    Entry& e = it->second;
    if (fresh || e.room != room) {
        e.room = room;
        e.key = key;
        e.media = media;
    }
    if (media.size == 0 || media.size > kMaxSize) {
        setState(e, State::Ignored);
        return;
    }
    if (QFileInfo::exists(path(media.root))) {
        hold(e);
        setState(e, State::Ready);
        return;
    }
    if (e.state == State::Fetching || e.state == State::Ready)
        return;
    if (autoFetch(room, media, sent)) {
        if (!startFetch(e))
            setState(e, State::Failed);
    } else if (e.state != State::Failed) {
        setState(e, State::Click);
    }
}

ffi::Result<> Ed3kRoomMedia::fetch(const ffi::Room& room, const Root& root)
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end() || it->second.room != room)
        return std::unexpected(ffi::Error::NotFound);
    Entry& e = it->second;
    if (e.state == State::Ignored)
        return std::unexpected(ffi::Error::NotFound);
    if (e.state == State::Ready || e.state == State::Fetching)
        return {};
    return startFetch(e);
}

Ed3kRoomMedia::State Ed3kRoomMedia::state(const Root& root) const
{
    const auto it = m_entries.find(root);
    return it == m_entries.end() ? State::None : it->second.state;
}

std::optional<QByteArray> Ed3kRoomMedia::read(const Root& root) const
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end() || m_dir.isEmpty())
        return std::nullopt;
    QFile f(path(root));
    if (!f.open(QIODevice::ReadOnly) || f.size() > qint64(kMaxSize))
        return std::nullopt;
    QByteArray bytes = f.readAll();
    // used: last to go when trimmed
    f.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    f.close();
    if (!it->second.media.key)
        return bytes;
    return chacha(*it->second.media.key, bytes);
}

void Ed3kRoomMedia::onFetched(const ffi::Room& room, const Root& root, bool ok)
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end() || it->second.room != room)
        return;
    Entry& e = it->second;
    if (ok && QFileInfo::exists(path(root))) {
        e.held = true;   // the core serves a fetched file by itself (§4.6 holder)
        setState(e, State::Ready);
        m_trimTimer->start();
    } else {
        setState(e, State::Failed);
    }
}

ffi::Result<Ed3kRoomMedia::Media> Ed3kRoomMedia::add(const ffi::Room& room, ffi::KeyKind key, const QByteArray& bytes,
                                                     const QString& name, bool sealed, std::uint32_t width, std::uint32_t height)
{
    if (m_dir.isEmpty() || !m_session.engine())
        return std::unexpected(ffi::Error::State);
    const QString type = sniffType(bytes);
    if (type.isEmpty() || bytes.isEmpty() || std::uint64_t(bytes.size()) > kMaxSize)
        return std::unexpected(ffi::Error::Argument);
    Media m;
    m.size = std::uint64_t(bytes.size());
    m.type = type;
    // §2.6: a name of at most 255 bytes, no path
    QString n = QFileInfo(name).fileName();
    while (n.toUtf8().size() > 255)
        n.chop(1);
    m.name = n;
    m.width = width;
    m.height = height;
    QByteArray wire = bytes;
    if (sealed) {
        std::array<std::uint8_t, 32> k{};
        if (RAND_bytes(k.data(), int(k.size())) != 1)
            return std::unexpected(ffi::Error::State);
        auto out = chacha(k, bytes);
        if (!out)
            return std::unexpected(ffi::Error::State);
        wire = std::move(*out);
        m.key = k;
        std::ranges::fill(k, 0);
    }
    ffi::Hasher h;
    if (auto ok = h.update(std::as_bytes(std::span(wire.constData(), std::size_t(wire.size())))); !ok)
        return std::unexpected(ok.error());
    auto hashed = h.finish();
    if (!hashed)
        return std::unexpected(hashed.error());
    m.root = hashed->root.bytes;
    QSaveFile f(path(m.root));
    if (!f.open(QIODevice::WriteOnly) || f.write(wire) != wire.size() || !f.commit())
        return std::unexpected(ffi::Error::State);
    Entry& e = m_entries[m.root];
    e.room = room;
    e.key = key;
    e.media = m;
    hold(e);
    setState(e, State::Ready);
    m_trimTimer->start();
    return m;
}

void Ed3kRoomMedia::roomJoined(const ffi::Room& room)
{
    for (auto& [root, e] : m_entries)
        if (e.room == room && e.state == State::Ready)
            hold(e);
}

void Ed3kRoomMedia::roomLeft(const ffi::Room& room)
{
    auto* engine = m_session.engine();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.room != room) {
            ++it;
            continue;
        }
        if (engine) {
            if (it->second.state == State::Fetching)
                (void)engine->roomMediaCancel(it->second.key, m_session.now(), it->first);
            if (it->second.held)
                (void)engine->roomMediaDrop(it->second.key, m_session.now(), it->first);
        }
        if (it->second.media.key)
            std::ranges::fill(*it->second.media.key, 0);
        it = m_entries.erase(it);
    }
    m_autoTimes.erase(room);
}

void Ed3kRoomMedia::applyPrefs()
{
    m_trimTimer->start();
}

QString Ed3kRoomMedia::rootHex(const Root& root)
{
    return QString::fromLatin1(bytesOf(root).toHex());
}

std::optional<Ed3kRoomMedia::Root> Ed3kRoomMedia::rootFrom(const QString& hex)
{
    const QByteArray b = QByteArray::fromHex(hex.toLatin1());
    if (b.size() != 32)
        return std::nullopt;
    Root r{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(b.constData()), 32, r.begin());
    return r;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRoomMedia::setState(Entry& e, State s)
{
    if (e.state == s)
        return;
    e.state = s;
    emit stateChanged(bytesOf(e.room.bytes), bytesOf(e.media.root), int(s));
}

bool Ed3kRoomMedia::autoFetch(const ffi::Room& room, const Media& media, std::uint64_t sent)
{
    if (!thePrefs.chatRoomMedia())
        return false;
    if (!isImageType(media.type) && !(isVideoType(media.type) && thePrefs.chatRoomVideos()))
        return false;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (sent + kAutoMaxAge < std::uint64_t(now))
        return false;
    // per room and hour; beyond: a click (user's rule)
    auto& times = m_autoTimes[room];
    while (!times.empty() && times.front() + kHourSecs <= now)
        times.pop_front();
    if (qsizetype(times.size()) >= thePrefs.ed3kRoomMediaPerHour())
        return false;
    times.push_back(now);
    return true;
}

ffi::Result<> Ed3kRoomMedia::startFetch(Entry& e)
{
    auto* engine = m_session.engine();
    if (!engine || m_dir.isEmpty())
        return std::unexpected(ffi::Error::State);
    // the core writes <path>.part, renames it once verified (a file at path = complete)
    const QByteArray p = path(e.media.root).toUtf8();
    auto ok = engine->roomMediaFetch(e.key, m_session.now(), e.room, e.media.root, e.media.size, viewOf(p));
    m_session.pump();
    if (!ok) {
        logEd3kDebug([&] { return QStringLiteral("ed3k: room media %1 not fetched: %2").arg(rootHex(e.media.root).left(16)).arg(int(ok.error())); });
        return ok;
    }
    setState(e, State::Fetching);
    return {};
}

void Ed3kRoomMedia::hold(Entry& e)
{
    if (e.held)
        return;
    auto* engine = m_session.engine();
    if (!engine || m_dir.isEmpty())
        return;
    const QByteArray p = path(e.media.root).toUtf8();
    if (engine->roomMediaHold(e.key, m_session.now(), e.media.root, e.media.size, viewOf(p)))
        e.held = true;
}

QString Ed3kRoomMedia::path(const Root& root) const
{
    return QDir(m_dir).filePath(rootHex(root));
}

QString Ed3kRoomMedia::partPath(const Root& root) const
{
    return path(root) + kPartSuffix;
}

void Ed3kRoomMedia::trim()
{
    if (m_dir.isEmpty())
        return;
    const qint64 limit = qint64(thePrefs.ed3kRoomMediaCacheMiB()) << 20;
    QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);   // oldest first
    qint64 total = 0;
    for (const QFileInfo& f : files)
        total += f.size();
    auto* engine = m_session.engine();
    for (const QFileInfo& f : files) {
        if (total <= limit)
            break;
        if (f.fileName().endsWith(kPartSuffix))
            continue;
        const auto root = rootFrom(f.fileName());
        if (root) {
            if (const auto it = m_entries.find(*root); it != m_entries.end()) {
                if (engine && it->second.held)
                    (void)engine->roomMediaDrop(it->second.key, m_session.now(), *root);
                it->second.held = false;
                setState(it->second, State::Click);
            }
        }
        total -= f.size();
        QFile::remove(f.filePath());
    }
}

std::optional<QByteArray> Ed3kRoomMedia::chacha(const std::array<std::uint8_t, 32>& key, const QByteArray& in)
{
    // §2.6: ChaCha20, zero nonce; sealing and opening are one call (packmule's)
    QByteArray out = in;
    if (!ffi::roomMediaCrypt(key, 0, std::span(reinterpret_cast<std::uint8_t*>(out.data()), std::size_t(out.size()))))
        return std::nullopt;
    return out;
}

} // namespace eMule::ed3k
