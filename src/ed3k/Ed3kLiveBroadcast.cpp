/// @file Ed3kLiveBroadcast.cpp
/// @brief Broadcasting live channels from an HLS folder — implementation.

#include "Ed3kLiveBroadcast.h"

#include "Ed3kHide.h"
#include "Ed3kLiveFiles.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
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

std::uint64_t u64Of(const QJsonValue& v)
{
    const qint64 n = v.toInteger();
    return n > 0 ? std::uint64_t(n) : 0;
}

/// the seeds of a channel wiped however the call ends
struct WipeChannel {
    ffi::LiveBroadcast& b;
    ~WipeChannel()
    {
        wipe(std::span<std::uint8_t>{b.seed});
        if (b.secret)
            wipe(std::span<std::uint8_t>{*b.secret});
    }
};

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
    // first: its start (ed3k up) comes before resumeSaved() publishes through it
    m_files = std::make_unique<Ed3kLiveFiles>(session, configDir, QStringLiteral("live"));
    // was: connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kLiveBroadcast::onEventPolled, Qt::DirectConnection);
    // node key's events as before; the anonymous node's from keyedEventPolled (each once)
    connect(&m_session, &Ed3kSession::eventPolled, this,
            [this](const ffi::Event& ev, bool* handled) { onEventPolled(ev, ffi::KeyKind::Node, handled); }, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::keyedEventPolled, this,
            [this](const ffi::Event& ev, ffi::KeyKind key, bool* handled) {
                if (key == ffi::KeyKind::Anon)
                    onEventPolled(ev, key, handled);
            },
            Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kLiveBroadcast::onSessionStatus);
    connect(&m_session.hide(), &Ed3kHide::stopped, this, &Ed3kLiveBroadcast::onHideStopped);
    // the anonymous key's channels go on (§4 resume) and their files are shared again
    connect(&m_session.hide(), &Ed3kHide::started, this, [this] {
        resumeSaved();
        publishSaved();
    });
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kLiveBroadcast::scan);
    if (m_session.isRunning()) {
        resumeSaved();
        publishSaved();   // §1.2: the channel files kept, on air or not
    }
}

Ed3kLiveBroadcast::~Ed3kLiveBroadcast()
{
    // was: liveEnd of every broadcast; now a cut (§4): kept on air, resumed at the next start,
    // only stop() ends one
    // if (auto* e = m_session.engine())
    //     for (const auto& [stream, r] : m_running)
    //         (void)e->liveEnd(m_session.now(), stream);
    for (const auto& [stream, r] : m_running)
        saveOnAir(r);
}

std::optional<ffi::LiveBroadcast::Resume> Ed3kLiveBroadcast::resumeOf(const OnAir& a, std::uint64_t now)
{
    // nothing sent yet: a new broadcast is the same; a start ahead of now: the core refuses it
    if (a.nextSeq == 0 || a.start == 0 || a.start > now)
        return std::nullopt;
    // the clock went back: the cut is "now"
    const std::uint64_t since = now > a.cut ? now - a.cut : 0;
    if (since > kResumeS)
        return std::nullopt;
    return ffi::LiveBroadcast::Resume{a.start, a.nextSeq};
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
    if (!ch)
        return std::unexpected(tr("The channel key could not be made."));
    // hops.md §1: the key of its first broadcast, else the one in use now
    const ffi::KeyKind key = channelKeyOf(ch->id).value_or(m_session.hide().keyForUse());
    if (key == ffi::KeyKind::Anon && !m_session.hide().hiding())
        return std::unexpected(tr("The channel \"%1\" is broadcast under the anonymous key: turn hiding on to broadcast it.").arg(title));
    ffi::LiveBroadcast b;
    const WipeChannel wiper{b};
    if (!loadChannel(*ch, o, b))
        return std::unexpected(tr("The channel key could not be made."));
    // §4: cut off and back within kResumeS: the same broadcast goes on at its next seq
    const auto now = m_session.now();
    std::optional<OnAir> saved;
    for (const QJsonValue& v : readChannels()) {
        const QJsonObject c = v.toObject();
        if (QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1()) != ch->id)
            continue;
        if (const QJsonObject a = c.value(QStringLiteral("onAir")).toObject(); !a.isEmpty())
            saved = OnAir{u64Of(a.value(QStringLiteral("start"))), u64Of(a.value(QStringLiteral("seq"))), u64Of(a.value(QStringLiteral("cut"))),
                          a.contains(QStringLiteral("last")) ? std::optional(u64Of(a.value(QStringLiteral("last")))) : std::nullopt};
    }
    if (saved)
        b.resume = resumeOf(*saved, now.unixS);
    auto stream = e->liveBroadcast(now, b, key);
    if (!stream && b.resume) {
        logEd3kWarning(QStringLiteral("ed3k: live channel %1 not resumed (error %2): a new broadcast").arg(title).arg(int(stream.error())));
        b.resume.reset();
        stream = e->liveBroadcast(now, b, key);
    }
    // wipe(std::span<std::uint8_t>{b.seed});   (was: here; now WipeChannel, the channel file needs it)
    m_session.pump();
    if (!stream)
        return std::unexpected(stream.error() == ffi::Error::Unsupported ? tr("Live streams are turned off for ed3k.")
                                                                         : tr("The broadcast could not start."));
    const auto channelKey = ffi::roomMemberKey(b.seed);   // the Ed25519 key of the seed
    if (!channelKey) {
        (void)e->liveEnd(m_session.now(), *stream, key);
        return std::unexpected(tr("The broadcast could not start."));
    }
    // live.md §1.3, §10: key, id, s base32 lower case
    QString link = QStringLiteral("ed3k:live?key=%1&id=%2").arg(base32(*channelKey), base32(u8Of(ch->id)));
    if (b.paid)
        link += QStringLiteral("&paid=1");   // subscription.md §7
    if (b.secret)
        link += QStringLiteral("&s=") + base32(*b.secret);
    link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(title));
    Running r;
    r.stream = *stream;
    r.options = o;
    r.options.title = title;
    r.link = link;
    r.channelId = ch->id;
    r.paid = b.paid;
    r.key = key;
    // the channel keeps its key (and its channel file with it)
    (void)editChannel(ch->id, [key](QJsonObject& c) {
        c.insert(QStringLiteral("key"), key == ffi::KeyKind::Anon ? QStringLiteral("anon") : QStringLiteral("node"));
    });
    if (b.resume) {
        r.start = b.resume->start;
        r.nextSeq = b.resume->seq;
        r.resumed = true;
        r.last = saved ? saved->last : std::nullopt;   // what went out before the cut is not sent again
    } else {
        // as the core: a later broadcast of a stream has a later start (§1.1)
        const auto last = m_lastStart.find(*stream);
        r.start = last == m_lastStart.end() ? now.unixS : std::max(now.unixS, last->second + 1);
    }
    m_lastStart[*stream] = r.start;
    auto [it, inserted] = m_running.insert_or_assign(*stream, std::move(r));
    // the link of a private channel is a credential: never in a log line
    logEd3k(QStringLiteral("ed3k: broadcasting live channel %1 (%2) from %3%4")
                .arg(idOf(*stream), title, o.folder, it->second.resumed ? QStringLiteral(", resumed at %1").arg(it->second.nextSeq) : QString()));
    // publishChannelFile(it->second, b);
    if (!it->second.options.isPrivate)
        publishChannelFile(it->second.channelId, it->second.options, b);
    saveOnAir(it->second);
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
        (void)e->liveEnd(m_session.now(), it->first, it->second.key);
    // ended: not resumed; its channel file stays shared while the channel is kept (§1.2,
    // batch 15 #5; deleteChannel() withdraws it)
    // if (it->second.channelFile)
    //     m_files->withdraw(*it->second.channelFile);
    (void)editChannel(it->second.channelId, [](QJsonObject& c) { c.remove(QStringLiteral("onAir")); });
    m_running.erase(it);
    if (m_running.empty())
        m_timer.stop();
    m_session.pump();
    logEd3k(QStringLiteral("ed3k: live broadcast %1 ended").arg(id));
    markChanged();
    return true;
}

std::expected<void, QString> Ed3kLiveBroadcast::describe(const QString& id, const QString& comment, const QString& lang,
                                                         std::uint32_t kbps)
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    if (it == m_running.end())
        return std::unexpected(tr("This channel is not on air."));
    Running& r = it->second;
    r.options.comment = comment;
    r.options.lang = lang;
    r.options.kbps = kbps;
    saveOnAir(r);   // a resume keeps it
    if (r.options.isPrivate)
        return {};   // §10: a private channel has no channel file
    ffi::LiveBroadcast b;
    const WipeChannel wiper{b};
    if (!loadChannel(Channel{r.channelId, r.options.isPrivate}, r.options, b))
        return std::unexpected(tr("The channel key could not be made."));
    // publishChannelFile(r, b);
    publishChannelFile(r.channelId, r.options, b);
    markChanged();
    if (!channelFileOf(r.channelId))
        return std::unexpected(tr("The channel file could not be shared."));
    return {};
}

void Ed3kLiveBroadcast::resumeSaved()
{
    if (!m_session.engine())
        return;
    for (const QJsonValue& v : readChannels()) {
        const QJsonObject c = v.toObject();
        const QJsonObject a = c.value(QStringLiteral("onAir")).toObject();
        if (a.isEmpty())
            continue;
        Options o;
        o.title = c.value(QStringLiteral("title")).toString();
        o.isPrivate = c.value(QStringLiteral("private")).toBool();
        if (std::ranges::any_of(m_running, [&](const auto& kv) { return kv.second.options.title == o.title; }))
            continue;
        // hops.md §1: the anonymous key's waits for hiding (Ed3kHide::started), never node key
        if (c.value(QStringLiteral("key")).toString() == QLatin1String("anon") && !m_session.hide().hiding())
            continue;
        o.folder = a.value(QStringLiteral("folder")).toString();
        o.playlist = a.value(QStringLiteral("playlist")).toString();
        o.media = a.value(QStringLiteral("media")).toString();
        o.codecs = a.value(QStringLiteral("codecs")).toString();
        o.comment = a.value(QStringLiteral("comment")).toString();
        o.lang = a.value(QStringLiteral("lang")).toString();
        o.kbps = std::uint32_t(std::min<std::uint64_t>(u64Of(a.value(QStringLiteral("kbps"))), UINT32_MAX));
        o.segmentMs = std::uint32_t(std::min<std::uint64_t>(u64Of(a.value(QStringLiteral("segmentMs"))), UINT32_MAX));
        // start() goes on with the broadcast within kResumeS of the cut, else a new one
        if (auto r = start(o); !r) {
            logEd3kWarning(QStringLiteral("ed3k: live channel %1 not started again: %2").arg(o.title, r.error()));
            const QByteArray id = QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1());
            (void)editChannel(id, [](QJsonObject& e) { e.remove(QStringLiteral("onAir")); });
        }
    }
}

void Ed3kLiveBroadcast::publishSaved()
{
    if (!m_session.engine())
        return;
    for (const QJsonValue& v : readChannels()) {
        const QJsonObject c = v.toObject();
        const QJsonObject d = c.value(QStringLiteral("desc")).toObject();
        const QByteArray id = QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1());
        if (d.isEmpty() || id.size() != 16 || c.value(QStringLiteral("private")).toBool())
            continue;   // §10: a private channel has none; never on air: no description yet
        if (std::ranges::any_of(m_running, [&](const auto& kv) { return kv.second.channelId == id; }))
            continue;   // on air: start() published it
        if (!m_offerSource && !d.value(QStringLiteral("offer")).toString().isEmpty())
            continue;   // sold: waits for setOfferSource (else a file without the offer, a new created)
        Options o;
        o.title = d.value(QStringLiteral("title")).toString();
        if (o.title.isEmpty())
            o.title = c.value(QStringLiteral("title")).toString();
        o.comment = d.value(QStringLiteral("comment")).toString();
        o.lang = d.value(QStringLiteral("lang")).toString();
        o.media = d.value(QStringLiteral("media")).toString();
        o.codecs = d.value(QStringLiteral("codecs")).toString();
        o.kbps = std::uint32_t(std::min<std::uint64_t>(u64Of(d.value(QStringLiteral("kbps"))), UINT32_MAX));
        o.segmentMs = std::uint32_t(std::min<std::uint64_t>(u64Of(d.value(QStringLiteral("segmentMs"))), UINT32_MAX));
        publishChannelFile(id, o);
    }
    markChanged();
}

std::expected<void, QString> Ed3kLiveBroadcast::deleteChannel(const QString& channel)
{
    const QByteArray id = QByteArray::fromHex(channel.toLatin1());
    QJsonArray all = readChannels();
    qsizetype at = -1;
    for (qsizetype i = 0; i < all.size() && id.size() == 16; ++i)
        if (QByteArray::fromHex(all.at(i).toObject().value(QStringLiteral("id")).toString().toLatin1()) == id)
            at = i;
    if (at < 0)
        return std::unexpected(tr("Unknown ed3k live channel."));
    // on air: the user's stop, the end record (§2.2)
    for (const auto& [stream, r] : m_running)
        if (r.channelId == id) {
            (void)stop(idOf(stream));
            break;
        }
    // its channel file no longer shared nor published (§1.2)
    if (const auto f = m_channelFiles.find(id); f != m_channelFiles.end()) {
        m_files->withdraw(f->second.root);
        m_channelFiles.erase(f);
    }
    all = readChannels();   // stop() edited it
    for (qsizetype i = all.size() - 1; i >= 0; --i)
        if (QByteArray::fromHex(all.at(i).toObject().value(QStringLiteral("id")).toString().toLatin1()) == id)
            all.removeAt(i);
    const QByteArray out = QJsonDocument(all).toJson(QJsonDocument::Compact);
    if (!writeBlob(QDir(liveDir()).filePath(kChannelsFile), spanOf(out)))
        return std::unexpected(tr("The list of live channels could not be saved."));
    // the channel key and secret (credentials) go with it
    const QString base = QString::fromLatin1(id.toHex());
    QFile::remove(QDir(liveDir()).filePath(base + QStringLiteral(".key")));
    QFile::remove(QDir(liveDir()).filePath(base + QStringLiteral(".secret")));
    logEd3k(QStringLiteral("ed3k: live channel %1 deleted").arg(base));
    markChanged();
    return {};
}

std::vector<Ed3kLiveBroadcast::ChannelInfo> Ed3kLiveBroadcast::channels() const
{
    std::vector<ChannelInfo> out;
    for (const QJsonValue& v : readChannels()) {
        const QJsonObject c = v.toObject();
        const QByteArray id = QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1());
        if (id.size() != 16)
            continue;
        ChannelInfo i;
        i.channel = QString::fromLatin1(id.toHex());
        i.title = c.value(QStringLiteral("title")).toString();
        i.isPrivate = c.value(QStringLiteral("private")).toBool();
        for (const auto& [stream, r] : m_running)
            if (r.channelId == id)
                i.stream = idOf(stream);
        if (const auto f = channelFileOf(id))
            i.channelFile = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(f->bytes.data()), 32).toHex());
        out.push_back(std::move(i));
    }
    return out;
}

void Ed3kLiveBroadcast::setOfferSource(OfferSource source)
{
    m_offerSource = std::move(source);
    // a channel sold: its file off air carries the offer too (subscription.md §7); unchanged
    // ones are not shared again (publishChannelFile)
    publishSaved();
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

std::optional<ffi::Key32> Ed3kLiveBroadcast::channelSeed(const QString& id, QByteArray* channelId) const
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    if (it == m_running.end())
        return std::nullopt;
    if (channelId)
        *channelId = it->second.channelId;
    return loadSecret(liveDir(), QString::fromLatin1(it->second.channelId.toHex()) + QStringLiteral(".key"));
}

std::optional<ffi::Key32> Ed3kLiveBroadcast::channelSecret(const QString& id) const
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    if (it == m_running.end() || !it->second.options.isPrivate)
        return std::nullopt;
    return channelSecretOf(m_configDir, it->second.channelId);
}

std::optional<ffi::Key32> Ed3kLiveBroadcast::channelSecretOf(const QString& configDir, const QByteArray& channelId)
{
    if (channelId.size() != 16)
        return std::nullopt;
    const QString dir = QDir(stateDir(configDir)).filePath(QStringLiteral("live"));
    return loadSecret(dir, QString::fromLatin1(channelId.toHex()) + QStringLiteral(".secret"));
}

std::optional<Ed3kLiveBroadcast::Options> Ed3kLiveBroadcast::optionsOf(const QString& id) const
{
    const auto s = streamOf(id);
    const auto it = s ? m_running.find(*s) : m_running.end();
    return it == m_running.end() ? std::nullopt : std::optional(it->second.options);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kLiveBroadcast::onEventPolled(const ffi::Event& event, ffi::KeyKind key, bool* handled)
{
    namespace e = ffi::event;
    // a broadcast of the node the event is of (hops.md §1)
    const auto find = [this, key](const ffi::Stream& stream) { return this->find(stream, key); };
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
    if (m_session.isRunning()) {
        // ed3k (again): what was on air goes on (§4)
        if (m_running.empty())
            resumeSaved();
        // §1.2: the channel files kept (Ed3kLiveFiles shares again what it had; new ones here)
        publishSaved();
        return;
    }
    if (m_running.empty())
        return;
    // the engine and its broadcasts are gone: a cut, kept for a resume
    for (const auto& [stream, r] : m_running)
        saveOnAir(r);
    m_running.clear();
    m_timer.stop();
    markChanged();
}

void Ed3kLiveBroadcast::onHideStopped()
{
    // the anonymous node is gone with its broadcasts: a cut, resumed when hiding runs (§4)
    bool any = false;
    for (auto it = m_running.begin(); it != m_running.end();) {
        if (it->second.key != ffi::KeyKind::Anon) {
            ++it;
            continue;
        }
        saveOnAir(it->second);
        logEd3k(QStringLiteral("ed3k: live broadcast %1 cut: hiding stopped").arg(idOf(it->first)));
        it = m_running.erase(it);
        any = true;
    }
    if (m_running.empty())
        m_timer.stop();
    if (any)
        markChanged();
}

std::optional<ffi::KeyKind> Ed3kLiveBroadcast::channelKeyOf(const QByteArray& channelId) const
{
    for (const QJsonValue& v : readChannels()) {
        const QJsonObject c = v.toObject();
        if (QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1()) != channelId)
            continue;
        const QString k = c.value(QStringLiteral("key")).toString();
        if (k == QLatin1String("anon"))
            return ffi::KeyKind::Anon;
        if (k == QLatin1String("node"))
            return ffi::KeyKind::Node;
        return std::nullopt;
    }
    return std::nullopt;
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
        saveOnAir(r);   // §4: the next seq and this playlist number kept for a resume
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
            // §4, #11: the segment is not sent, the next one carries gap; the init is read
            // again for it (r.mapPath unchanged)
            // was: return false (the same segment tried at every scan: forever on a bad init)
            r.error = tr("The init data %1 is missing or larger than 256 KiB.").arg(e.map);
            r.gapNext = true;
            ++r.skipped;
            logEd3kWarning(QStringLiteral("ed3k: live broadcast %1: segment %2 skipped, init data %3 missing or too large")
                               .arg(idOf(r.stream), e.uri, e.map));
            markChanged();
            return true;
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
        ++r.skipped;
        logEd3kWarning(QStringLiteral("ed3k: live broadcast %1: segment %2 skipped (%3 bytes)").arg(idOf(r.stream), e.uri).arg(fi.size()));
        markChanged();
        return true;
    }
    const QByteArray data = readSmall(segPath, kSegmentMax);
    if (data.isEmpty())
        return false;
    gap = gap || r.gapNext;
    const auto seq = eng->livePushSegment(m_session.now(), r.stream, spanOf(data), e.ms, gap, spanOf(r.init), r.key);
    m_session.pump();
    if (!seq) {
        // TOO_LARGE: the core sets gap on the next one itself (#11)
        ++r.skipped;
        r.error = tr("A segment was not sent (error %1).").arg(int(seq.error()));
        logEd3kWarning(QStringLiteral("ed3k: live broadcast %1: segment not pushed (%2)").arg(idOf(r.stream)).arg(int(seq.error())));
        markChanged();
        return true;
    }
    r.error.clear();
    r.gapNext = false;
    ++r.segments;
    r.bytes += std::uint64_t(data.size());
    r.nextSeq = *seq + 1;
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

Ed3kLiveBroadcast::Running* Ed3kLiveBroadcast::find(const ffi::Stream& stream, std::optional<ffi::KeyKind> key)
{
    const auto it = m_running.find(stream);
    if (it == m_running.end() || (key && it->second.key != *key))
        return nullptr;
    return &it->second;
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
    i.skipped = r.skipped;
    i.resumed = r.resumed;
    // if (r.channelFile)
    if (const auto f = channelFileOf(r.channelId))
        i.channelFile = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(f->bytes.data()), 32).toHex());
    i.error = r.error;
    i.paid = r.paid;
    i.channel = QString::fromLatin1(r.channelId.toHex());
    i.anon = r.key == ffi::KeyKind::Anon;
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

bool Ed3kLiveBroadcast::loadChannel(const Channel& ch, const Options& o, ffi::LiveBroadcast& b)
{
    const QString base = QString::fromLatin1(ch.id.toHex());
    auto seed = loadOrCreateSecret(liveDir(), base + QStringLiteral(".key"), nullptr);
    if (!seed)
        return false;
    b.seed = *seed;
    wipe(std::span<std::uint8_t>{*seed});
    if (ch.isPrivate) {
        auto sec = loadOrCreateSecret(liveDir(), base + QStringLiteral(".secret"), nullptr);
        if (!sec)
            return false;
        b.secret = *sec;
        wipe(std::span<std::uint8_t>{*sec});
    }
    b.id.clear();
    for (char c : ch.id)
        b.id.push_back(std::byte(c));
    b.media = o.media.isEmpty() ? std::string("video/mp4") : o.media.toStdString();
    b.codecs = o.codecs.toStdString();
    // subscription.md §7: a channel sold (Ed3kPaidSeller) is paid, its offer in the channel file
    // (a private one has none: the offer goes with the core's key answers, §9.1 Ed3k-Offer)
    b.paid = false;
    b.offer.clear();
    // if (m_offerSource && !ch.isPrivate) {   (was: a private channel was not sold)
    if (m_offerSource) {
        if (const auto key = ffi::keyPublic(b.seed)) {
            const QByteArray offer = m_offerSource(*key, ch.id);
            b.paid = !offer.isEmpty();
            for (char c : offer)
                b.offer.push_back(std::byte(c));
        }
    }
    return true;
}

void Ed3kLiveBroadcast::publishChannelFile(const QByteArray& channelId, const Options& o)
{
    if (o.isPrivate || !m_files)
        return;   // §10: a private channel has none
    ffi::LiveBroadcast b;
    const WipeChannel wiper{b};
    if (!loadChannel(Channel{channelId, false}, o, b)) {
        logEd3kWarning(QStringLiteral("ed3k: no channel key for live channel %1").arg(QString::fromLatin1(channelId.toHex())));
        return;
    }
    publishChannelFile(channelId, o, b);
}

void Ed3kLiveBroadcast::publishChannelFile(const QByteArray& channelId, const Options& o, const ffi::LiveBroadcast& b)
{
    if (o.isPrivate || !m_files)
        return;   // §10: a private channel has none
    const QString idHex = QString::fromLatin1(channelId.toHex());
    Desc d = descOf(o);
    // a changed offer (subscription.md §7, field 11) is a changed file too
    if (!b.offer.empty())
        d.offer = QString::fromLatin1(
            QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(b.offer.data()), qsizetype(b.offer.size())),
                                     QCryptographicHash::Sha256)
                .toHex());
    // the description as before: the same file (the same created); else a later created
    QJsonObject saved;
    for (const QJsonValue& v : readChannels())
        if (QByteArray::fromHex(v.toObject().value(QStringLiteral("id")).toString().toLatin1()) == channelId)
            saved = v.toObject().value(QStringLiteral("desc")).toObject();
    const Desc old{saved.value(QStringLiteral("title")).toString(),
                   saved.value(QStringLiteral("comment")).toString(),
                   saved.value(QStringLiteral("lang")).toString(),
                   saved.value(QStringLiteral("media")).toString(),
                   saved.value(QStringLiteral("codecs")).toString(),
                   std::uint32_t(std::min<std::uint64_t>(u64Of(saved.value(QStringLiteral("kbps"))), UINT32_MAX)),
                   std::uint32_t(std::min<std::uint64_t>(u64Of(saved.value(QStringLiteral("segmentMs"))), UINT32_MAX)),
                   saved.value(QStringLiteral("offer")).toString()};
    const std::uint64_t oldCreated = u64Of(saved.value(QStringLiteral("created")));
    const std::uint64_t created = !saved.isEmpty() && old == d && oldCreated != 0
                                      ? oldCreated
                                      : std::max(m_session.now().unixS, oldCreated + 1);
    const ffi::LiveDesc desc{d.title.toStdString(), d.comment.toStdString(), d.lang.toStdString(), d.kbps, d.segmentMs, created};
    auto file = ffi::liveChannelFile(b, desc);
    if (!file) {
        logEd3kWarning(QStringLiteral("ed3k: no channel file for live channel %1 (error %2)").arg(idHex).arg(int(file.error())));
        return;
    }
    const QByteArray bytes(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->size()));
    // the same file shared already (an ed3k start again: Ed3kLiveFiles shares it again itself)
    const auto had = m_channelFiles.find(channelId);
    if (had != m_channelFiles.end() && had->second.bytes == bytes && m_files->isPublished(had->second.root))
        return;
    // hops.md §1: under the channel's key (a record naming it under the other would tie them)
    // was: const auto root = m_files->publish(bytes, Ed3kLiveFiles::fileNameOf(d.title));
    const auto root = m_files->publish(bytes, Ed3kLiveFiles::fileNameOf(d.title), channelKeyOf(channelId).value_or(ffi::KeyKind::Node));
    if (!root) {
        logEd3kWarning(QStringLiteral("ed3k: the channel file of live channel %1 is not shared").arg(idHex));
        return;
    }
    // a changed description: the file before is withdrawn (§1.2, readers take the later created)
    // if (r.channelFile && *r.channelFile != *root)
    //     m_files->withdraw(*r.channelFile);
    // r.channelFile = root;
    if (had != m_channelFiles.end() && had->second.root != *root)
        m_files->withdraw(had->second.root);
    m_channelFiles[channelId] = ChannelFile{*root, bytes};
    (void)editChannel(channelId, [&](QJsonObject& c) {
        c.insert(QStringLiteral("desc"), QJsonObject{{QStringLiteral("title"), d.title},
                                                     {QStringLiteral("comment"), d.comment},
                                                     {QStringLiteral("lang"), d.lang},
                                                     {QStringLiteral("media"), d.media},
                                                     {QStringLiteral("codecs"), d.codecs},
                                                     {QStringLiteral("kbps"), qint64(d.kbps)},
                                                     {QStringLiteral("segmentMs"), qint64(d.segmentMs)},
                                                     {QStringLiteral("offer"), d.offer},
                                                     {QStringLiteral("created"), qint64(created)}});
    });
}

std::optional<ffi::Root> Ed3kLiveBroadcast::channelFileOf(const QByteArray& channelId) const
{
    const auto it = m_channelFiles.find(channelId);
    return it == m_channelFiles.end() ? std::nullopt : std::optional(it->second.root);
}

QJsonArray Ed3kLiveBroadcast::readChannels() const
{
    return QJsonDocument::fromJson(readBlob(QDir(liveDir()).filePath(kChannelsFile))).array();
}

bool Ed3kLiveBroadcast::editChannel(const QByteArray& channelId, const std::function<void(QJsonObject&)>& edit)
{
    QJsonArray all = readChannels();
    for (qsizetype i = 0; i < all.size(); ++i) {
        QJsonObject c = all.at(i).toObject();
        if (QByteArray::fromHex(c.value(QStringLiteral("id")).toString().toLatin1()) != channelId)
            continue;
        edit(c);
        all.replace(i, c);
        const QByteArray out = QJsonDocument(all).toJson(QJsonDocument::Compact);
        return writeBlob(QDir(liveDir()).filePath(kChannelsFile), spanOf(out));
    }
    return false;
}

void Ed3kLiveBroadcast::saveOnAir(const Running& r)
{
    const auto& o = r.options;
    (void)editChannel(r.channelId, [&](QJsonObject& c) {
        c.insert(QStringLiteral("onAir"), QJsonObject{{QStringLiteral("folder"), o.folder},
                                                      {QStringLiteral("playlist"), o.playlist},
                                                      {QStringLiteral("media"), o.media},
                                                      {QStringLiteral("codecs"), o.codecs},
                                                      {QStringLiteral("comment"), o.comment},
                                                      {QStringLiteral("lang"), o.lang},
                                                      {QStringLiteral("kbps"), qint64(o.kbps)},
                                                      {QStringLiteral("segmentMs"), qint64(o.segmentMs)},
                                                      {QStringLiteral("start"), qint64(r.start)},
                                                      {QStringLiteral("seq"), qint64(r.nextSeq)},
                                                      {QStringLiteral("cut"), qint64(m_session.now().unixS)}});
        if (r.last) {
            QJsonObject a = c.value(QStringLiteral("onAir")).toObject();
            a.insert(QStringLiteral("last"), qint64(*r.last));
            c.insert(QStringLiteral("onAir"), a);
        }
    });
}

Ed3kLiveBroadcast::Desc Ed3kLiveBroadcast::descOf(const Options& o)
{
    return Desc{o.title, o.comment, o.lang, o.media.isEmpty() ? QStringLiteral("video/mp4") : o.media, o.codecs, o.kbps, o.segmentMs, {}};
}

} // namespace eMule::ed3k
