/// @file Ed3kLiveBroadcast.cpp
/// @brief Broadcasting live channels from an HLS folder — implementation.

#include "Ed3kLiveBroadcast.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrl>

#include <cmath>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kChannelsFile = QStringLiteral("channels.json");

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

std::span<const std::uint8_t> u8Of(const QByteArray& b)
{
    return {reinterpret_cast<const std::uint8_t*>(b.constData()), std::size_t(b.size())};
}

QString idOf(const ffi::Stream& s)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(s.bytes.data()), qsizetype(s.bytes.size())).toHex());
}

std::optional<ffi::Stream> streamOf(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    ffi::Stream s;
    if (raw.size() != qsizetype(s.bytes.size()) || hex.size() != qsizetype(2 * s.bytes.size()))
        return std::nullopt;
    std::memcpy(s.bytes.data(), raw.constData(), s.bytes.size());
    return s;
}

/// the value of URI="…" in an attribute list (RFC 8216 §4.2)
QString uriAttr(const QByteArray& attrs)
{
    const qsizetype at = attrs.indexOf("URI=\"");
    if (at < 0)
        return {};
    const qsizetype from = at + 5;
    const qsizetype to = attrs.indexOf('"', from);
    return to < 0 ? QString() : QString::fromUtf8(attrs.mid(from, to - from));
}

QByteArray readSmall(const QString& path, qint64 max)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > max)
        return {};
    return f.readAll();
}

} // namespace

std::optional<Ed3kHlsPlaylist> Ed3kHlsPlaylist::parse(const QByteArray& text)
{
    QList<QByteArray> lines = text.split('\n');
    if (lines.isEmpty() || !lines.first().trimmed().startsWith("#EXTM3U"))
        return std::nullopt;
    Ed3kHlsPlaylist out;
    QString map;
    std::optional<std::uint32_t> ms;
    bool disc = false;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray l = lines.at(i).trimmed();
        if (l.isEmpty())
            continue;
        if (l.startsWith("#EXT-X-MEDIA-SEQUENCE:")) {
            out.mediaSequence = l.mid(22).toULongLong();
        } else if (l.startsWith("#EXTINF:")) {
            const QByteArray v = l.mid(8);
            const qsizetype comma = v.indexOf(',');
            const double s = (comma < 0 ? v : v.left(comma)).toDouble();
            ms = s > 0 && s < 4.0e6 ? std::uint32_t(std::lround(s * 1000.0)) : 0u;
        } else if (l.startsWith("#EXT-X-DISCONTINUITY") && !l.startsWith("#EXT-X-DISCONTINUITY-SEQUENCE")) {
            disc = true;
        } else if (l.startsWith("#EXT-X-MAP:")) {
            map = uriAttr(l.mid(11));
        } else if (l.startsWith("#EXT-X-ENDLIST")) {
            out.ended = true;
        } else if (!l.startsWith('#') && ms) {
            out.entries.push_back(Entry{QString::fromUtf8(l), *ms, disc, map});
            ms.reset();
            disc = false;
        }
    }
    return out;
}

Ed3kLiveBroadcast::Ed3kLiveBroadcast(Ed3kSession& session, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_configDir(configDir)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kLiveBroadcast::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kLiveBroadcast::onSessionStatus);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kLiveBroadcast::scan);
}

Ed3kLiveBroadcast::~Ed3kLiveBroadcast()
{
    if (auto* e = m_session.engine())
        for (const auto& [stream, r] : m_running)
            (void)e->liveEnd(m_session.now(), stream);
}

std::expected<Ed3kLiveBroadcast::Info, QString> Ed3kLiveBroadcast::start(const Options& o)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(tr("ed3k is not running."));
    if (!QFileInfo(o.folder).isDir())
        return std::unexpected(tr("The folder %1 does not exist.").arg(o.folder));
    const QString title = o.title.trimmed();
    if (title.isEmpty())
        return std::unexpected(tr("The channel needs a title."));
    for (const auto& [s, r] : m_running)
        if (r.options.title == title)
            return std::unexpected(tr("The channel \"%1\" is on air already.").arg(title));
    const auto ch = channelOf(title, o.isPrivate);
    const QString keyName = ch ? QString::fromLatin1(ch->id.toHex()) + QStringLiteral(".key") : QString();
    auto seed = ch ? loadOrCreateSecret(liveDir(), keyName, nullptr) : std::nullopt;
    if (!seed)
        return std::unexpected(tr("The channel key could not be made."));
    const Secret seedS(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    std::optional<Secret> secret;
    if (ch->isPrivate) {
        auto s = loadOrCreateSecret(liveDir(), QString::fromLatin1(ch->id.toHex()) + QStringLiteral(".secret"), nullptr);
        if (!s)
            return std::unexpected(tr("The channel key could not be made."));
        secret.emplace(*s);
        wipe(std::span<std::uint8_t>{*s});
    }
    ffi::LiveBroadcast b;
    b.seed = seedS.expose();
    for (char c : ch->id)
        b.id.push_back(std::byte(c));
    b.media = o.media.isEmpty() ? std::string("video/mp4") : o.media.toStdString();
    b.codecs = o.codecs.toStdString();
    if (secret)
        b.secret = secret->expose();
    auto stream = e->liveBroadcast(m_session.now(), b);
    wipe(std::span<std::uint8_t>{b.seed});
    if (b.secret)
        wipe(std::span<std::uint8_t>{*b.secret});
    m_session.pump();
    if (!stream)
        return std::unexpected(stream.error() == ffi::Error::Unsupported ? tr("Live streams are turned off for ed3k.")
                                                                         : tr("The broadcast could not start."));
    const auto key = ffi::roomMemberKey(seedS.expose());   // the Ed25519 key of the seed
    if (!key) {
        (void)e->liveEnd(m_session.now(), *stream);
        return std::unexpected(tr("The broadcast could not start."));
    }
    // live.md §1.3, §10: key, id, s base32 lower case
    QString link = QStringLiteral("ed3k:live?key=%1&id=%2").arg(base32(*key), base32(u8Of(ch->id)));
    if (secret)
        link += QStringLiteral("&s=") + base32(secret->expose());
    link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(title));
    Running r;
    r.stream = *stream;
    r.options = o;
    r.options.title = title;
    r.link = link;
    auto [it, inserted] = m_running.insert_or_assign(*stream, std::move(r));
    // the link of a private channel is a credential: never in a log line
    logEd3k(QStringLiteral("ed3k: broadcasting live channel %1 (%2) from %3").arg(idOf(*stream), title, o.folder));
    scanOne(it->second);
    if (!m_timer.isActive())
        m_timer.start();
    markChanged();
    return infoOf(it->second);
}

bool Ed3kLiveBroadcast::stop(const QString& id)
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    if (it == m_running.end())
        return false;
    if (auto* e = m_session.engine())
        (void)e->liveEnd(m_session.now(), it->first);
    m_running.erase(it);
    if (m_running.empty())
        m_timer.stop();
    m_session.pump();
    logEd3k(QStringLiteral("ed3k: live broadcast %1 ended").arg(id));
    markChanged();
    return true;
}

bool Ed3kLiveBroadcast::isBroadcast(const QString& id) const
{
    const auto s = streamOf(id);
    return s && m_running.contains(*s);
}

std::vector<Ed3kLiveBroadcast::Info> Ed3kLiveBroadcast::list() const
{
    std::vector<Info> out;
    for (const auto& [s, r] : m_running)
        out.push_back(infoOf(r));
    return out;
}

std::optional<Ed3kLiveBroadcast::Info> Ed3kLiveBroadcast::info(const QString& id) const
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    return it == m_running.end() ? std::nullopt : std::optional(infoOf(it->second));
}

void Ed3kLiveBroadcast::scan()
{
    for (auto& [s, r] : m_running)
        scanOne(r);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kLiveBroadcast::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* n = std::get_if<e::LiveNeighbour>(&event)) {
        if (Running* r = find(n->stream)) {
            *handled = true;
            r->neighbours.insert(n->node);
            markChanged();
        }
    } else if (const auto* g = std::get_if<e::LiveGone>(&event)) {
        if (Running* r = find(g->stream)) {
            *handled = true;
            r->neighbours.erase(g->node);
            markChanged();
        }
    } else if (const auto* rf = std::get_if<e::LiveRefused>(&event)) {
        if (find(rf->stream))
            *handled = true;
    } else if (const auto* b = std::get_if<e::LiveBadBlock>(&event)) {
        if (find(b->stream))
            *handled = true;
    }
}

void Ed3kLiveBroadcast::onSessionStatus()
{
    if (m_session.isRunning() || m_running.empty())
        return;
    // the engine and its broadcasts are gone
    m_running.clear();
    m_timer.stop();
    markChanged();
}

void Ed3kLiveBroadcast::scanOne(Running& r)
{
    const QString path = playlistOf(r);
    if (path.isEmpty())
        return;   // the encoder has not begun yet
    const auto pl = Ed3kHlsPlaylist::parse(readSmall(path, 4 << 20));
    if (!pl || pl->entries.empty())
        return;
    const QString dir = QFileInfo(path).absolutePath();
    const std::uint64_t first = pl->mediaSequence;
    const std::uint64_t newest = first + (pl->entries.size() - 1);
    bool gap = false;
    std::size_t from = 0;
    if (!r.last) {
        // a live stream begins at the newest segment
        from = pl->entries.size() - 1;
    } else if (newest < *r.last) {
        // the encoder started over (numbers went back): a gap, from its newest
        from = pl->entries.size() - 1;
        gap = true;
    } else if (newest == *r.last) {
        return;
    } else {
        from = *r.last >= first ? std::size_t(*r.last - first + 1) : 0;
    }
    for (std::size_t i = from; i < pl->entries.size(); ++i) {
        const auto& e = pl->entries.at(i);
        if (!push(r, e, dir, gap || e.discontinuity))
            break;   // tried again at the next scan
        r.last = first + i;
        gap = false;
    }
}

bool Ed3kLiveBroadcast::push(Running& r, const Ed3kHlsPlaylist::Entry& e, const QString& dir, bool gap)
{
    auto* eng = m_session.engine();
    if (!eng)
        return false;
    // the init data of the map (live.md §2.1: ≤ 256 KiB), a changed one starts the player anew
    const QString mapPath = e.map.isEmpty() ? QString() : QDir(dir).filePath(e.map);
    if (mapPath != r.mapPath || (r.init.isEmpty() && !mapPath.isEmpty())) {
        const QByteArray init = mapPath.isEmpty() ? QByteArray() : readSmall(mapPath, kInitMax);
        if (!mapPath.isEmpty() && init.isEmpty()) {
            r.error = tr("The init data %1 is missing or larger than 256 KiB.").arg(e.map);
            markChanged();
            return false;
        }
        if (!r.mapPath.isEmpty() && r.init != init)
            gap = true;
        r.mapPath = mapPath;
        r.init = init;
    }
    const QString segPath = QDir(dir).filePath(e.uri);
    const QFileInfo fi(segPath);
    if (!fi.exists())
        return false;
    if (fi.size() > kSegmentMax || fi.size() == 0) {
        // §2.1: at most 4 MiB; skipped, the next one is not continuous
        r.error = tr("The segment %1 is larger than 4 MiB and was skipped.").arg(e.uri);
        r.gapNext = true;
        logEd3kWarning(QStringLiteral("ed3k: live broadcast %1: segment %2 skipped (%3 bytes)").arg(idOf(r.stream), e.uri).arg(fi.size()));
        markChanged();
        return true;
    }
    const QByteArray data = readSmall(segPath, kSegmentMax);
    if (data.isEmpty())
        return false;
    gap = gap || r.gapNext;
    const auto seq = eng->livePushSegment(m_session.now(), r.stream, spanOf(data), e.ms, gap, spanOf(r.init));
    m_session.pump();
    if (!seq) {
        r.error = tr("A segment was not sent (error %1).").arg(int(seq.error()));
        logEd3kWarning(QStringLiteral("ed3k: live broadcast %1: segment not pushed (%2)").arg(idOf(r.stream)).arg(int(seq.error())));
        markChanged();
        return true;
    }
    r.error.clear();
    r.gapNext = false;
    ++r.segments;
    r.bytes += std::uint64_t(data.size());
    markChanged();
    return true;
}

QString Ed3kLiveBroadcast::playlistOf(const Running& r) const
{
    const QDir d(r.options.folder);
    if (!r.options.playlist.isEmpty())
        return QFileInfo(r.options.playlist).isAbsolute() ? r.options.playlist : d.filePath(r.options.playlist);
    const auto files = d.entryInfoList({QStringLiteral("*.m3u8")}, QDir::Files, QDir::Time);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

Ed3kLiveBroadcast::Running* Ed3kLiveBroadcast::find(const ffi::Stream& stream)
{
    const auto it = m_running.find(stream);
    return it == m_running.end() ? nullptr : &it->second;
}

Ed3kLiveBroadcast::Info Ed3kLiveBroadcast::infoOf(const Running& r) const
{
    Info i;
    i.id = idOf(r.stream);
    i.title = r.options.title;
    i.folder = r.options.folder;
    i.link = r.link;
    i.isPrivate = r.options.isPrivate;
    i.neighbours = int(r.neighbours.size());
    i.segments = r.segments;
    i.bytes = r.bytes;
    i.error = r.error;
    return i;
}

void Ed3kLiveBroadcast::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(300, this, [this] {
        m_changePending = false;
        emit changed();
    });
}

std::optional<Ed3kLiveBroadcast::Channel> Ed3kLiveBroadcast::channelOf(const QString& title, bool isPrivate)
{
    // channels.json: [{title, id (hex), private}]; the keys are files of their own
    const QString path = QDir(liveDir()).filePath(kChannelsFile);
    QJsonArray all = QJsonDocument::fromJson(readBlob(path)).array();
    for (const QJsonValue& v : all) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("title")).toString() == title && o.value(QStringLiteral("private")).toBool() == isPrivate) {
            const QByteArray id = QByteArray::fromHex(o.value(QStringLiteral("id")).toString().toLatin1());
            if (id.size() == 16)
                return Channel{id, isPrivate};
        }
    }
    QByteArray id(16, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(id.data()), 4);
    all.append(QJsonObject{{QStringLiteral("title"), title}, {QStringLiteral("id"), QString::fromLatin1(id.toHex())},
                           {QStringLiteral("private"), isPrivate}});
    QDir().mkpath(liveDir());
    const QByteArray out = QJsonDocument(all).toJson(QJsonDocument::Compact);
    if (!writeBlob(path, spanOf(out)))
        return std::nullopt;
    return Channel{id, isPrivate};
}

QString Ed3kLiveBroadcast::liveDir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("live"));
}

} // namespace eMule::ed3k
