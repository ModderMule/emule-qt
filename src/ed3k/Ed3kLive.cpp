/// @file Ed3kLive.cpp
/// @brief Watching live streams — implementation.

#include "Ed3kLive.h"

#include "Ed3kSession.h"
#include "Ed3kSiteServer.h"

#include "utils/Log.h"

#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

// One HTTP response of a stream.
struct Ed3kLive::Viewer {
    explicit Viewer(QHttpServerResponder&& r) : responder(std::move(r)) {}

    QHttpServerResponder responder;
    bool begun = false;
    bool head = false;
};

/// init data roots kept for later viewers
constexpr std::size_t kInitsKept = 4;

namespace {

QByteArray bytesOf(const ffi::Bytes& b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
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

void answerPlain(QHttpServerResponder& r, QHttpServerResponder::StatusCode code, const QByteArray& body = {})
{
    QHttpHeaders h;
    h.append(QHttpHeaders::WellKnownHeader::ContentType, "text/plain; charset=utf-8");
    h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    if (body.isEmpty())
        r.write(h, code);
    else
        r.write(body, h, code);
}

/// A last chunk the container skips (Qt warns on an empty one): an ISO BMFF `free` box, a
/// null TS packet, an EBML Void element.
QByteArray fillerOf(const QString& media)
{
    if (media.contains(QLatin1String("mp2t"), Qt::CaseInsensitive)) {
        QByteArray ts(188, char(0xFF));
        ts[0] = char(0x47);
        ts[1] = char(0x1F);
        ts[2] = char(0xFF);
        ts[3] = char(0x10);
        return ts;
    }
    if (media.contains(QLatin1String("webm"), Qt::CaseInsensitive))
        return QByteArray("\xEC\x81\x00", 3);
    return QByteArray("\x00\x00\x00\x08" "free", 8);
}

} // namespace

Ed3kLive::Ed3kLive(Ed3kSession& session, Ed3kSiteServer& sites, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_sites(sites)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kLive::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kLive::onSessionStatus);
    m_sites.setHostHandler([this](const QHttpServerRequest& req, QHttpServerResponder& resp) { return onRequest(req, resp); });
}

Ed3kLive::~Ed3kLive()
{
    m_sites.setHostHandler({});
    for (auto& [stream, w] : m_streams)
        endViewers(w);
}

std::expected<QString, QString> Ed3kLive::watch(const QString& link)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(tr("ed3k is not running."));
    const QByteArray utf8 = link.trimmed().toUtf8();
    auto stream = e->liveWatchLink(m_session.now(), std::string_view(utf8.constData(), std::size_t(utf8.size())));
    m_session.pump();
    if (!stream) {
        if (stream.error() == ffi::Error::Unsupported)
            return std::unexpected(tr("Live streams are turned off for ed3k."));
        return std::unexpected(tr("This is not a valid ed3k live link."));
    }
    Watched& w = m_streams[*stream];
    w.stream = *stream;
    if (w.title.isEmpty()) {
        const qsizetype q = link.indexOf(QLatin1Char('?'));
        w.title = q < 0 ? QString() : QUrlQuery(link.mid(q + 1)).queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded);
    }
    logEd3k(QStringLiteral("ed3k: watching live stream %1").arg(idOf(*stream)));
    markChanged();
    return idOf(*stream);
}

bool Ed3kLive::leave(const QString& id)
{
    const auto s = streamOf(id);
    if (!s)
        return false;
    const auto it = m_streams.find(*s);
    if (it == m_streams.end())
        return false;
    if (auto* e = m_session.engine(); e && !it->second.left)
        (void)e->liveLeave(m_session.now(), *s);
    endViewers(it->second);
    m_streams.erase(it);
    m_session.pump();
    logEd3k(QStringLiteral("ed3k: left live stream %1").arg(id));
    markChanged();
    return true;
}

std::vector<Ed3kLive::Info> Ed3kLive::list() const
{
    std::vector<Info> out;
    for (const auto& [stream, w] : m_streams)
        out.push_back(infoOf(w));
    return out;
}

std::optional<Ed3kLive::Info> Ed3kLive::info(const QString& id) const
{
    const auto s = streamOf(id);
    const auto it = s ? m_streams.find(*s) : m_streams.end();
    return it == m_streams.end() ? std::nullopt : std::optional(infoOf(it->second));
}

QString Ed3kLive::urlOf(const QString& id) const
{
    if (!m_sites.isListening())
        return {};
    return QStringLiteral("http://live-%1.localhost:%2/").arg(id).arg(m_sites.port());
}

bool Ed3kLive::addPeer(const QString& id, const ffi::Source& peer)
{
    const auto s = streamOf(id);
    auto* e = m_session.engine();
    if (!s || !e || !m_streams.contains(*s))
        return false;
    const auto ok = e->liveAddPeer(m_session.now(), *s, peer);
    m_session.pump();
    return ok.has_value();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kLive::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* n = std::get_if<e::LiveNeighbour>(&event)) {
        if (Watched* w = find(n->stream)) {
            *handled = true;
            w->neighbours.insert(n->node);
            if (w->state == State::NoPeers)
                w->state = w->segments > 0 ? State::Playing : State::Joining;
            markChanged();
        }
    } else if (const auto* g = std::get_if<e::LiveGone>(&event)) {
        if (Watched* w = find(g->stream)) {
            *handled = true;
            w->neighbours.erase(g->node);
            if (w->neighbours.empty() && w->state != State::Ended)
                w->state = State::NoPeers;
            markChanged();
        }
    } else if (const auto* r = std::get_if<e::LiveRefused>(&event)) {
        if (find(r->stream)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: a peer refused live stream %1 (%2)").arg(idOf(r->stream)).arg(r->refusal));
        }
    } else if (const auto* c = std::get_if<e::LiveChannel>(&event)) {
        if (Watched* w = find(c->stream)) {
            *handled = true;
            if (!c->media.empty())
                w->media = QString::fromStdString(c->media);
            w->codecs = QString::fromStdString(c->codecs);
            markChanged();
        }
    } else if (const auto* i = std::get_if<e::LiveInit>(&event)) {
        if (Watched* w = find(i->stream)) {
            *handled = true;
            w->initRoot = i->root;
            w->init = bytesOf(i->data);
            // kept by root: the window may still need the one before (#15)
            std::erase_if(w->inits, [&](const auto& kv) { return kv.first == i->root; });
            w->inits.emplace_back(i->root, w->init);
            while (w->inits.size() > kInitsKept)
                w->inits.pop_front();
        }
    } else if (const auto* s = std::get_if<e::LiveSegment>(&event)) {
        if (Watched* w = find(s->stream)) {
            *handled = true;
            onSegment(*w, *s);
        }
    } else if (const auto* en = std::get_if<e::LiveEnded>(&event)) {
        if (Watched* w = find(en->stream)) {
            *handled = true;
            w->state = State::Ended;
            endViewers(*w);
            logEd3k(QStringLiteral("ed3k: live stream %1 ended").arg(idOf(en->stream)));
            markChanged();
        }
    } else if (const auto* in = std::get_if<e::LiveInterrupted>(&event)) {
        if (Watched* w = find(in->stream)) {
            *handled = true;
            if (w->state != State::Ended)
                w->state = State::Interrupted;
            markChanged();
        }
    } else if (const auto* l = std::get_if<e::LiveLeft>(&event)) {
        if (Watched* w = find(l->stream)) {
            *handled = true;
            // the engine left it (after its end): shown as ended until the user leaves it
            endViewers(*w);
            w->state = State::Ended;
            w->left = true;
            markChanged();
        }
    } else if (const auto* b = std::get_if<e::LiveBadBlock>(&event)) {
        if (find(b->stream)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: a bad block of live stream %1 at %2").arg(idOf(b->stream)).arg(b->offset));
        }
    }
}

void Ed3kLive::onSessionStatus()
{
    if (m_session.isRunning() || m_streams.empty())
        return;
    // the engine is gone and its streams with it
    for (auto& [stream, w] : m_streams)
        endViewers(w);
    m_streams.clear();
    markChanged();
}

bool Ed3kLive::onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp)
{
    // only live-<32 hex>.localhost on this port (DNS rebinding reaches nothing else)
    const QString host = QString::fromLatin1(req.value("host")).toLower();
    static const QString kPrefix = QStringLiteral("live-");
    const QString expect = QStringLiteral(".localhost:%1").arg(m_sites.port());
    if (!host.startsWith(kPrefix) || !host.endsWith(expect))
        return false;
    const QString id = host.mid(kPrefix.size(), host.size() - kPrefix.size() - expect.size());
    const auto s = streamOf(id);
    if (!s)
        return false;
    const auto method = req.method();
    if (method != QHttpServerRequest::Method::Get && method != QHttpServerRequest::Method::Head) {
        answerPlain(resp, QHttpServerResponder::StatusCode::MethodNotAllowed);
        return true;
    }
    const auto it = m_streams.find(*s);
    if (it == m_streams.end()) {
        answerPlain(resp, QHttpServerResponder::StatusCode::NotFound, "not watched\n");
        return true;
    }
    Watched& w = it->second;
    if (w.state == State::Ended) {
        answerPlain(resp, QHttpServerResponder::StatusCode::Gone, "the broadcast ended\n");
        return true;
    }
    // a browser at / gets the player page; the stream is at /stream (and / for players)
    const QString path = req.url().path();
    if (method == QHttpServerRequest::Method::Get && (path.isEmpty() || path == QLatin1String("/"))
        && req.value("accept").contains("text/html")) {
        answerPage(resp, id);
        return true;
    }
    auto v = std::make_unique<Viewer>(std::move(resp));
    v->head = method == QHttpServerRequest::Method::Head;
    if (w.viewers.size() >= kViewersMax) {
        auto& old = *w.viewers.front();
        if (old.begun)
            endResponse(w, old);   // was: writeEndChunked({}) (Qt: "Chunk must have length > 0")
        else
            answerPlain(old.responder, QHttpServerResponder::StatusCode::ServiceUnavailable);
        w.viewers.erase(w.viewers.begin());
    }
    // nothing to play yet: it waits for the first segment
    if (w.segments > 0)
        begin(w, *v);
    if (v->head) {
        if (!v->begun)
            answerPlain(v->responder, QHttpServerResponder::StatusCode::Ok);
    } else {
        w.viewers.push_back(std::move(v));
    }
    markChanged();
    return true;
}

void Ed3kLive::onSegment(Watched& w, const ffi::event::LiveSegment& s)
{
    const QByteArray data = bytesOf(s.data);
    // #15: one fMP4 response can't take a second moov: a gap, another init or a new
    // broadcast end the begun responses; the player asks again and gets the new init
    const bool restart = (w.start || w.windowInit || !w.window.empty())
                         && (s.gap || s.init != w.windowInit || (w.start && *w.start != s.start));
    if (restart)
        restartViewers(w);
    w.start = s.start;
    w.windowInit = s.init;
    // w.window.push_back(data);
    w.window.push_back(Held{data, s.init});
    while (w.window.size() > kWindow)
        w.window.pop_front();
    ++w.segments;
    w.bytes += std::uint64_t(data.size());
    if (w.state != State::Ended)
        w.state = State::Playing;
    for (auto& v : w.viewers) {
        if (!v->begun)
            begin(w, *v);   // init and the window, this segment included
        else
            v->responder.writeChunk(data);
    }
    markChanged();
}

void Ed3kLive::begin(Watched& w, Viewer& v)
{
    QHttpHeaders h;
    QString type = w.media;
    if (!w.codecs.isEmpty())
        type += QStringLiteral("; codecs=\"%1\"").arg(w.codecs);
    h.append(QHttpHeaders::WellKnownHeader::ContentType, type.toUtf8());
    h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    h.append("X-Content-Type-Options", "nosniff");
    v.begun = true;
    if (v.head) {
        v.responder.write(h, QHttpServerResponder::StatusCode::Ok);
        return;
    }
    v.responder.writeBeginChunked(h);
    // if (!w.init.isEmpty())
    //     v.responder.writeChunk(w.init);   (was: the newest init, maybe not the window's)
    const QByteArray init = w.window.empty() ? w.init : initOf(w, w.window.back().init);
    if (!init.isEmpty())
        v.responder.writeChunk(init);
    for (const Held& seg : w.window)
        v.responder.writeChunk(seg.data);
}

void Ed3kLive::endViewers(Watched& w)
{
    for (auto& v : w.viewers) {
        if (v->begun)
            endResponse(w, *v);   // was: writeEndChunked({})
        else
            answerPlain(v->responder, QHttpServerResponder::StatusCode::Gone, "the broadcast ended\n");
    }
    w.viewers.clear();
}

void Ed3kLive::restartViewers(Watched& w)
{
    const auto before = w.viewers.size();
    std::erase_if(w.viewers, [&](const std::unique_ptr<Viewer>& v) {
        if (!v->begun)
            return false;   // waits on, begins with the new init
        endResponse(w, *v);
        return true;
    });
    w.window.clear();
    ++w.restarts;
    if (before != w.viewers.size())
        logEd3kDebug(QStringLiteral("ed3k: live stream %1 restarted (gap, init or broadcast): %2 response(s) ended")
                         .arg(idOf(w.stream))
                         .arg(before - w.viewers.size()));
    markChanged();
}

void Ed3kLive::endResponse(const Watched& w, Viewer& v)
{
    v.responder.writeEndChunked(fillerOf(w.media));
}

QByteArray Ed3kLive::initOf(const Watched& w, const std::optional<ffi::Root>& r)
{
    if (!r)
        return {};
    for (const auto& [root, data] : w.inits)
        if (root == *r)
            return data;
    return w.initRoot == r ? w.init : QByteArray();
}

void Ed3kLive::answerPage(QHttpServerResponder& r, const QString& id) const
{
    // the player reconnects when a response ends (a restart, #15) until the stream is gone
    const QString title = [&] {
        const auto i = info(id);
        return (i && !i->title.isEmpty() ? i->title : tr("ed3k live stream")).toHtmlEscaped();
    }();
    const QByteArray page = QStringLiteral(
                                "<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title>"
                                "<style>html,body{margin:0;height:100%;background:#000;color:#ccc;font-family:sans-serif}"
                                "video{width:100%;height:100%}#m{position:fixed;top:8px;left:8px}</style></head><body>"
                                "<video id=\"v\" controls autoplay muted playsinline></video><div id=\"m\"></div><script>"
                                "const v=document.getElementById('v'),m=document.getElementById('m');let n=0;"
                                "function go(){v.src='/stream?r='+(n++);v.play().catch(()=>{});}"
                                "async function again(){const r=await fetch('/stream',{method:'HEAD'}).catch(()=>null);"
                                "if(r&&r.status===410){m.textContent='%2';return;}setTimeout(go,500);}"
                                "v.addEventListener('ended',again);v.addEventListener('error',again);go();"
                                "</script></body></html>")
                                .arg(title, tr("The broadcast ended."))
                                .toUtf8();
    QHttpHeaders h;
    h.append(QHttpHeaders::WellKnownHeader::ContentType, "text/html; charset=utf-8");
    h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    h.append("X-Content-Type-Options", "nosniff");
    r.write(page, h, QHttpServerResponder::StatusCode::Ok);
}

Ed3kLive::Watched* Ed3kLive::find(const ffi::Stream& stream)
{
    const auto it = m_streams.find(stream);
    return it == m_streams.end() ? nullptr : &it->second;
}

Ed3kLive::Info Ed3kLive::infoOf(const Watched& w) const
{
    Info i;
    i.id = idOf(w.stream);
    i.title = w.title;
    i.media = w.media;
    i.codecs = w.codecs;
    i.state = w.state;
    i.neighbours = int(w.neighbours.size());
    i.segments = w.segments;
    i.bytes = w.bytes;
    i.viewers = int(w.viewers.size());
    i.restarts = w.restarts;
    i.url = urlOf(i.id);
    return i;
}

void Ed3kLive::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(300, this, [this] {
        m_changePending = false;
        emit changed();
    });
}

QString Ed3kLive::idOf(const ffi::Stream& stream)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(stream.bytes.data()), qsizetype(stream.bytes.size())).toHex());
}

} // namespace eMule::ed3k
