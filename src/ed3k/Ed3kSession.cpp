/// @file Ed3kSession.cpp
/// @brief The ed3k node (packmule) in the daemon — implementation.

#include "Ed3kSession.h"

#include "Ed3kHttp.h"
#include "Ed3kPlumbing.h"
#include "Ed3kStore.h"

#include "app/AppConfig.h"
#include "net/Address.h"
#include "net/InterfacePin.h"
#include "net/IPv6SourcePin.h"
#include "net/ProxySettings.h"
#include "prefs/Preferences.h"
#include "utils/DebugUtils.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

// BAD blob at most once a minute (ffi.md "State slots", guide §5.4)
constexpr std::chrono::seconds kBadSaveEvery{60};

const QString kBadFile = QStringLiteral("bad.dat");
const QString kKadFile = QStringLiteral("kad.dat");
const QString kSetsFile = QStringLiteral("sets.dat");
const QString kFriendFile = QStringLiteral("friend.dat");
const QString kFilterFile = QStringLiteral("filter.dat");
const QString kHopsFile = QStringLiteral("hops.dat");
const QString kRoomsFile = QStringLiteral("rooms.dat");   // secret (generation keys): owner-only
const QString kNamesFile = QStringLiteral("names.dat");
const QString kPaidFile = QStringLiteral("paid.dat");

// toAddress / toQHostAddress: moved to Ed3kPlumbing (shared with Ed3kLan)

QString hex(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

QString errorName(ffi::Error e)
{
    return QString::number(static_cast<int>(e));
}

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

} // namespace

Ed3kSession::Ed3kSession(QObject* parent)
    : QObject(parent)
{
    m_readPool.setMaxThreadCount(2);
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kSession::onTimer);
    // the engine's timer may sleep a minute when idle; the pin is looked at more often
    m_pinTimer.setInterval(5000);
    connect(&m_pinTimer, &QTimer::timeout, this, [this] {
        followIPv6Pin();
        pump();
    });
    m_badSaveTimer.setSingleShot(true);
    connect(&m_badSaveTimer, &QTimer::timeout, this, [this] {
        if (m_engine && m_badDirty) {
            m_badDirty = false;
            saveSlot(ffi::State::BadBlocks, kBadFile);
        }
    });
}

Ed3kSession::~Ed3kSession()
{
    stop();
}

bool Ed3kSession::start(const Ed3kSettings& settings)
{
    if (m_engine)
        return true;
    m_clock.start();
    m_dir = eMule::ed3k::stateDir(settings.configDir);

    QString error;
    std::optional<Seed> seed = loadOrCreateKey(m_dir, &error);
    if (!seed) {
        logEd3kError(QStringLiteral("ed3k: key file: %1").arg(error));
        return false;
    }

    // the socket first: the engine is told its port
    auto socket = std::make_unique<QUdpSocket>();
    if (!socket->bind(settings.bindAddress, settings.udpPort)) {
        logEd3kError(QStringLiteral("ed3k: cannot bind UDP port %1: %2").arg(settings.udpPort).arg(socket->errorString()));
        wipe(*seed);
        return false;
    }
    // the bindAddress pref: a selected interface pins the socket (no default route around it)
    if (!InterfacePin::pin(socket->socketDescriptor())) {
        logEd3kError(QStringLiteral("ed3k: the bound interface cannot be pinned; not started"));
        wipe(*seed);
        return false;
    }
    socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 2 * 1024 * 1024);
    const std::uint16_t port = socket->localPort();

    ffi::Config cfg;
    cfg.seed = *seed;
    wipe(*seed);
    cfg.network = settings.network.toStdString();
    cfg.agent = QStringLiteral("eMuleQt %1").arg(kAppVersion).toStdString();
    // was: ToDo: nick from thePrefs (Hello.nick), once the GUI shows ed3k peers (done:
    // settings.nick, the daemon's thePrefs.nick())
    cfg.nick = settings.nick.toStdString();
    // the socket of each family; [::] / 0.0.0.0 for a wildcard bind
    const QHostAddress& bind = settings.bindAddress;
    if (bind == QHostAddress::Any) {
        cfg.local4 = ffi::Address::v4(0, 0, 0, 0, port);
        // was: ToDo: the stable IPv6 source address (IPv6SourcePin) instead of [::], socket-modes.md
        // §8.3 (done: advertised and sent from, followIPv6Pin; [::] while none applies)
        // cfg.local6 = ffi::Address{{}, port};
        m_ipv6Source = advertisedIPv6(bind, ipv6Pin());
        cfg.local6 = m_ipv6Source.isNull() ? ffi::Address{{}, port} : toAddress(m_ipv6Source, port);
    } else if (bind == QHostAddress::AnyIPv4) {
        cfg.local4 = ffi::Address::v4(0, 0, 0, 0, port);
    } else if (bind == QHostAddress::AnyIPv6) {
        m_ipv6Source = advertisedIPv6(bind, ipv6Pin());
        cfg.local6 = m_ipv6Source.isNull() ? ffi::Address{{}, port} : toAddress(m_ipv6Source, port);
    } else if (bind.protocol() == QAbstractSocket::IPv4Protocol) {
        cfg.local4 = toAddress(bind, port);
    } else {
        cfg.local6 = toAddress(bind, port);
    }
    cfg.forceOpen4 = settings.forceOpen;
    cfg.forceOpen6 = settings.forceOpen;
    // was: ToDo: one pref per optional extension (cfg.extOff), socket-modes.md "extensions"
    // (done: pref ed3k.extOff → settings.extOff)
    cfg.extOff = settings.extOff;

    auto engine = ffi::Engine::create(cfg, now());
    cfg.seed.fill(0);
    wipe(std::span<std::uint8_t>{cfg.seed});
    if (!engine) {
        logEd3kError(QStringLiteral("ed3k: engine not created (error %1)").arg(errorName(engine.error())));
        return false;
    }
    m_engine = std::make_unique<ffi::Engine>(std::move(*engine));
    m_socket = std::move(socket);
    m_port = port;
    m_settings = settings;

    m_http = std::make_unique<Ed3kHttp>(
        Ed3kHttp::Answers{
            [this](std::uint64_t id, const ffi::Address& addr) {
                if (!m_engine)
                    return false;
                const auto ok = m_engine->httpResolved(now(), id, addr);
                pump();
                // Denied (or Stale): the request is over, no other address
                return ok.has_value();
            },
            [this](std::uint64_t id) {
                if (m_engine)
                    (void)m_engine->httpUnreachable(now(), id);
                pump();
            },
            [this](std::uint64_t id, std::uint16_t status, const QByteArray& headers, const QByteArray& body) {
                if (m_engine)
                    (void)m_engine->httpAnswer(now(), id, status,
                                               std::string_view(headers.constData(), std::size_t(headers.size())),
                                               spanOf(body));
                pump();
            },
            [this](std::uint64_t id) {
                if (m_engine)
                    (void)m_engine->httpFailed(now(), id);
                pump();
            },
            [this](std::uint64_t id, const QByteArray& body) {
                if (m_engine)
                    (void)m_engine->rpcAnswer(now(), id, spanOf(body));
                pump();
            },
            [this](std::uint64_t id) {
                if (m_engine)
                    (void)m_engine->rpcFailed(now(), id);
                pump();
            }},
        QStringLiteral("eMuleQt/%1").arg(kAppVersion));
    m_http->setProxy(toNetworkProxy(thePrefs.proxySettings()));
    // ed3k_rpc_endpoints is for ed3k_open engines only: the URL is picked here (startRpc)
    m_creditRpc = settings.creditRpc;
    m_chaintubeRpc = settings.chaintubeRpc;

    // was: ToDo: LAN discovery sockets of mode A (socket-modes.md "Implementer note (LAN
    // discovery)"); until then LAN is off, which ends its first round (bootstrap.md §1)
    // (done: Ed3kLan, pref ed3k.lan; off ends the first round at once as before)
    if (settings.lan && (extensions() & ED3K_EXT_LAN) != 0) {
        m_lan = std::make_unique<Ed3kLan>(*this, port, settings.lanHooks);
        if (!m_lan->start())
            m_lan.reset();
    }
    if (!m_lan)
        (void)m_engine->lanEnable(now(), false);
    // the state of the last run, before the first datagram
    loadState();
    // was: ToDo: node list sources (bootstrap.md §3: bootstrapConfigure, the last
    // BootstrapStarted) and a pref for them (done: Ed3kBootstrap, pref ed3k.nodeLists)
    // was: ToDo: upload/download limits through BandwidthCoordinator (setLimits or grants,
    // noteBytesSentOutsideTheLoop), socket-modes.md "Mode A in eMuleQt" (done: setLimits()
    // from BandwidthCoordinator, which also reports traffic() to the throttler)
    applyLimits();
    // was: ToDo: an upload queue and slots for ed3k (done: the engine's policy, eMule's score;
    // its slot count from BandwidthCoordinator, counted with eD2K's)
    applyUploadSlots();
    m_logLevel = -1;
    applyLogLevel();

    connect(m_socket.get(), &QUdpSocket::readyRead, this, &Ed3kSession::onReadyRead);
    if (settings.bindAddress == QHostAddress::Any || settings.bindAddress == QHostAddress::AnyIPv6)
        m_pinTimer.start();
    pump();
    if (!m_engine)
        return false;

    logEd3k(QStringLiteral("ed3k node %1 started on UDP port %2").arg(nodeIdHex()).arg(m_port));
    emit statusChanged();
    // datagrams that came before the connect
    if (m_socket->hasPendingDatagrams())
        onReadyRead();
    return m_engine != nullptr;
}

void Ed3kSession::stop()
{
    if (!m_engine)
        return;
    m_timer.stop();
    m_badSaveTimer.stop();
    m_pinTimer.stop();
    saveState(true);
    m_lan.reset();
    if (m_http)
        m_http->clear();
    m_http.reset();
    m_posted.clear();
    // the counters of this engine stay in traffic()
    const Ed3kTraffic t = traffic();
    m_trafficBase = t;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
    }
    m_socket.reset();
    m_engine.reset();
    m_port = 0;
    logEd3k(QStringLiteral("ed3k node stopped"));
    emit statusChanged();
}

std::uint16_t Ed3kSession::udpPort() const
{
    return m_engine ? m_port : 0;
}

std::optional<ffi::NodeId> Ed3kSession::nodeId() const
{
    if (!m_engine)
        return std::nullopt;
    auto id = m_engine->nodeId();
    if (!id)
        return std::nullopt;
    return *id;
}

QByteArray Ed3kSession::portTestProbe(std::uint64_t nonce)
{
    if (!m_engine)
        return {};
    auto datagram = m_engine->portTestProbe(now(), nonce);
    if (!datagram) {
        logEd3kWarning(QStringLiteral("ed3k: port test probe not made (error %1)").arg(errorName(datagram.error())));
        return {};
    }
    const auto bytes = datagram->bytes();
    return {reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())};
}

int Ed3kSession::portTestSeen(std::uint64_t nonce)
{
    if (!m_engine)
        return 0;
    const auto seen = m_engine->portTestSeen(nonce);
    return seen ? int(*seen) : 0;
}

QString Ed3kSession::nodeIdHex() const
{
    const auto id = nodeId();
    return id ? hex(id->bytes) : QString();
}

std::uint64_t Ed3kSession::contactCount() const
{
    if (!m_engine)
        return 0;
    const auto s = m_engine->stats(now());
    return s ? s->contacts : 0;
}

Ed3kStatus Ed3kSession::status() const
{
    Ed3kStatus st;
    if (!m_engine)
        return st;
    st.running = true;
    st.udpPort = m_port;
    st.nodeId = nodeIdHex();
    if (const auto s = m_engine->stats(now())) {
        st.stats = *s;
        st.contacts = s->contacts;
        st.class4 = s->class4;
        st.class6 = s->class6;
        st.router = s->router != 0;
    }
    return st;
}

Ed3kTraffic Ed3kSession::traffic() const
{
    Ed3kTraffic t = m_trafficBase;
    if (!m_engine)
        return t;
    if (const auto s = m_engine->stats(now())) {
        t.sent += s->wire_sent;
        t.recv += s->wire_recv;
    }
    return t;
}

void Ed3kSession::setLimits(std::uint64_t upBytesPerSec, std::uint64_t downBytesPerSec)
{
    if (m_limits.up == upBytesPerSec && m_limits.down == downBytesPerSec)
        return;
    m_limits = {upBytesPerSec, downBytesPerSec};
    applyLimits();
    pump();
}

void Ed3kSession::setUploadSlots(std::uint32_t count)
{
    if (m_uploadSlots == count)
        return;
    m_uploadSlots = count;
    applyUploadSlots();
    pump();
}

std::uint64_t Ed3kSession::extensions() const
{
    if (!m_engine)
        return 0;
    const auto bits = m_engine->extensions();
    return bits ? *bits : 0;
}

std::optional<ffi::QueueStats> Ed3kSession::uploadQueueStats()
{
    if (!m_engine)
        return std::nullopt;
    auto s = m_engine->queueStats();
    return s ? std::optional(*s) : std::nullopt;
}

void Ed3kSession::setRpcEndpoints(ffi::RpcClass cls, const QStringList& urls)
{
    (cls == ffi::RpcClass::Credit ? m_creditRpc : m_chaintubeRpc) = urls;
}

ffi::Now Ed3kSession::now() const
{
    // +1: mono 0 never reaches the core
    const auto mono = m_clock.isValid() ? std::uint64_t(m_clock.nsecsElapsed() / 1000) : 0;
    return ffi::Now{mono + 1, std::uint64_t(QDateTime::currentSecsSinceEpoch())};
}

void Ed3kSession::post(std::function<void(ffi::Engine&, ffi::Now)> cmd)
{
    if (!m_engine || !cmd)
        return;
    if (m_pumping) {
        m_posted.push_back(std::move(cmd));
        m_pumpAgain = true;
        return;
    }
    cmd(*m_engine, now());
    pump();
}

void Ed3kSession::pump()
{
    if (!m_engine)
        return;
    if (m_pumping) {
        m_pumpAgain = true;   // a slot called a command: loop once more
        return;
    }
    m_pumping = true;
    std::array<std::byte, 1500> buf{};
    do {
        m_pumpAgain = false;
        for (bool busy = true; busy && m_engine;) {
            busy = false;
            for (;;) {
                auto d = m_engine->datagramOut(now(), buf);
                if (!d) {
                    die(d.error());
                    break;
                }
                if (!*d)
                    break;
                busy = true;
                const auto& dg = **d;
                // at once and in this order: control before data (socket-modes.md "Priority")
                writeDatagram(reinterpret_cast<const char*>(buf.data()), qint64(dg.len), dg.to);
            }
            while (m_engine) {
                auto ev = m_engine->pollEvent();
                if (!ev) {
                    die(ev.error());
                    break;
                }
                if (!*ev)
                    break;
                busy = true;
                dispatch(**ev);
            }
            while (m_engine && !m_posted.empty()) {
                auto cmds = std::move(m_posted);
                m_posted.clear();
                for (auto& cmd : cmds)
                    if (m_engine)
                        cmd(*m_engine, now());
                busy = true;
            }
        }
    } while (m_pumpAgain && m_engine);
    m_pumping = false;
    // LAN discovery's datagrams of whatever ran (Ed3kLan::datagramIn pumps again)
    if (m_lan && m_engine)
        m_lan->drain();
    armTimer();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kSession::onReadyRead()
{
    while (m_engine && m_socket && m_socket->hasPendingDatagrams()) {   // drain fully, then pump once
        const QNetworkDatagram d = m_socket->receiveDatagram();
        if (!d.isValid())
            break;
        const QByteArray data = d.data();
        const auto ours = m_engine->datagramIn(now(), toAddress(d.senderAddress(), std::uint16_t(d.senderPort())),
                                               spanOf(data));
        if (!ours) {
            if (ours.error() == ffi::Error::Dead) {
                die(ours.error());
                return;
            }
            continue;
        }
        if (!*ours)
            ++m_notOurs;   // not ed3k: its own socket, so dropped
    }
    pump();
}

void Ed3kSession::onTimer()
{
    if (!m_engine)
        return;
    if (auto ok = m_engine->timeout(now()); !ok && ok.error() == ffi::Error::Dead) {
        die(ok.error());
        return;
    }
    applyLogLevel();   // the filter rules may have changed (verbose, kadVerboseLog)
    followIPv6Pin();
    pump();
}

void Ed3kSession::dispatch(ffi::Event& ev)
{
    namespace e = ffi::event;
    if (const auto* s = std::get_if<e::StateChanged>(&ev)) {
        switch (s->slot) {
        case ffi::State::BadBlocks:
            // at most once a minute
            m_badDirty = true;
            if (!m_badSaveTimer.isActive())
                m_badSaveTimer.start(kBadSaveEvery);
            break;
        case ffi::State::KadContacts:
            saveSlot(ffi::State::KadContacts, kKadFile);
            emit statusChanged();
            break;
        case ffi::State::SetGrants:
            saveSlot(ffi::State::SetGrants, kSetsFile);
            break;
        case ffi::State::Rooms:
            saveSlot(ffi::State::Rooms, kRoomsFile);
            break;
        case ffi::State::Names:
            saveSlot(ffi::State::Names, kNamesFile);
            break;
        case ffi::State::Paid:
            saveSlot(ffi::State::Paid, kPaidFile);
            break;
        }
    } else if (std::holds_alternative<e::FriendChanged>(ev)) {
        saveFriends();
    } else if (std::holds_alternative<e::FilterChanged>(ev)) {
        saveExport(m_engine->filterExportState(), kFilterFile);
    } else if (const auto* h = std::get_if<e::HttpRequest>(&ev)) {
        m_http->start(*h);
        return;
    } else if (const auto* c = std::get_if<e::IoCancel>(&ev)) {
        m_http->cancel(c->id);   // HTTP and RPC ids alike
        return;
    } else if (const auto* rpc = std::get_if<e::RpcRequest>(&ev)) {
        if (startRpc(*rpc))
            return;   // else a slot may answer it, or answerUnhandled fails it
    } else if (const auto* l = std::get_if<e::Log>(&ev)) {
        logEngineLine(*l);
        return;
    } else if (const auto* b = std::get_if<e::KadBootstrapped>(&ev)) {
        logEd3kKad(QStringLiteral("ed3k: Kad bootstrap (IPv%1): %2 contacts").arg(b->family).arg(b->contacts));
        emit statusChanged();
    } else if (const auto* ks = std::get_if<e::KadStored>(&ev)) {
        logEd3kKadDebug(QStringLiteral("ed3k: Kad store %1 (kind %2): %3 nodes")
                            .arg(ks->lookup).arg(ks->kind).arg(ks->nodes));
    } else if (const auto* sp = std::get_if<e::SourcesPublished>(&ev)) {
        logEd3kKadDebug(QStringLiteral("ed3k: sources published: %1 of %2 nodes stored (%3 again)")
                            .arg(sp->stored).arg(sp->nodes).arg(sp->again));
    } else if (const auto* kp = std::get_if<e::KeywordsPublished>(&ev)) {
        logEd3kKadDebug(QStringLiteral("ed3k: keywords of %1 file(s) published: %2 stored, %3 full, %4 rejected, %5 unanswered")
                            .arg(kp->files).arg(kp->stored).arg(kp->full).arg(kp->rejected).arg(kp->unanswered));
    } else if (const auto* n = std::get_if<e::NatClass>(&ev)) {
        logEd3kDebug(QStringLiteral("ed3k: IPv%1 class %2").arg(n->family).arg(n->natClass));
        emit statusChanged();
    } else if (const auto* r = std::get_if<e::Router>(&ev)) {
        logEd3kDebug(QStringLiteral("ed3k: router %1").arg(r->on ? 1 : 0));
        emit statusChanged();
    }

    bool handled = false;
    emit eventPolled(ev, &handled);
    if (!handled && m_engine)
        answerUnhandled(ev);
}

bool Ed3kSession::startRpc(const ffi::event::RpcRequest& r)
{
    const QStringList& urls = r.endpointClass == ED3K_RPC_CREDIT      ? m_creditRpc
                              : r.endpointClass == ED3K_RPC_CHAINTUBE ? m_chaintubeRpc
                                                                      : QStringList{};
    // operator 0: any (the first); n: the n-th (packmule_udp HttpConfig::endpoint)
    const qsizetype index = r.op == 0 ? 0 : qsizetype(r.op) - 1;
    if (!m_http || index >= urls.size())
        return false;
    const QByteArray body(reinterpret_cast<const char*>(r.body.data()), qsizetype(r.body.size()));
    m_http->startRpc(r.id, urls.at(index), body);
    return true;
}

void Ed3kSession::applyLimits()
{
    if (!m_engine)
        return;
    // 0 = none; others at least 16 KiB/s (ed3k_set_limits)
    static constexpr std::uint64_t kFloor = 16 * 1024;
    const auto floored = [](std::uint64_t v) { return v == 0 ? 0 : std::max(v, kFloor); };
    const ffi::Limits l{floored(m_limits.up), floored(m_limits.down)};
    if (auto ok = m_engine->setLimits(now(), l); !ok)
        logEd3kWarning(QStringLiteral("ed3k: limits not set (error %1)").arg(errorName(ok.error())));
}

void Ed3kSession::applyUploadSlots()
{
    if (!m_engine)
        return;
    if (auto ok = m_engine->queueAuto(now(), true, m_uploadSlots); !ok)
        logEd3kWarning(QStringLiteral("ed3k: upload queue not set (error %1)").arg(errorName(ok.error())));
}

void Ed3kSession::applyLogLevel()
{
    if (!m_engine)
        return;
    const bool verbose = lcEmuleEd3k().isDebugEnabled() || lcEmuleEd3kKad().isDebugEnabled();
    const auto level = verbose ? ffi::LogLevel::Verbose : ffi::LogLevel::Info;
    if (m_logLevel == int(level))
        return;
    if (m_engine->logLevel(level))
        m_logLevel = int(level);
}

void Ed3kSession::logEngineLine(const ffi::event::Log& line)
{
    const QString text = QStringLiteral("ed3k: ") + QString::fromStdString(line.text);
    const bool verbose = line.level == ffi::LogLevel::Verbose;
    if (line.area == ffi::LogArea::Kad)
        verbose ? logEd3kKadDebug(text) : logEd3kKad(text);
    else
        verbose ? logEd3kDebug(text) : logEd3k(text);
}

void Ed3kSession::answerUnhandled(const ffi::Event& ev)
{
    namespace e = ffi::event;
    const ffi::Now t = now();
    if (const auto* w = std::get_if<e::WantBlock>(&ev)) {
        // was: ToDo: serve eMule shared files over ed3k (done: Ed3kShared); here only roots nobody serves
        (void)m_engine->blockFail(t, w->id);
        logEd3kDebug(QStringLiteral("ed3k: block %1 refused, root not shared").arg(w->id));
    } else if (const auto* v = std::get_if<e::VerifyBlock>(&ev)) {
        (void)m_engine->blockFail(t, v->id);
    } else if (const auto* r = std::get_if<e::RpcRequest>(&ev)) {
        // was: ToDo: chain RPC endpoints (credit, chaintube) (done: ed3k.creditRpc /
        // ed3k.chaintubeRpc, startRpc); here none is configured for its class and operator
        (void)m_engine->rpcFailed(t, r->id);
    } else if (const auto* a = std::get_if<e::BootstrapAsk>(&ev)) {
        // was: ToDo: ask the user (GUI) before a node list from a link is used (done:
        // Ed3kBootstrap asks through the GUI); here nobody took the ask: refused
        (void)m_engine->bootstrapAnswer(t, a->ask, false);
    } else if (const auto* u = std::get_if<e::UnpackRead>(&ev)) {
        (void)m_engine->unpackReadFailed(t, u->id);
    } else if (const auto* k = std::get_if<e::UnpackAsk>(&ev)) {
        (void)m_engine->unpackEnd(t, k->id, ED3K_UNPACK_UNSUPPORTED);
    }
    // ToDo: downloads: events only logged for now (was: GUI/IPC status, done:
    // Ed3kBridge GetEd3kStatus/PushEd3kStatus; was: chat, friends, done: Ed3kFriends)
}

void Ed3kSession::loadState()
{
    const ffi::Now t = now();
    const auto put = [&](ffi::State slot, const QString& file) {
        QByteArray blob = readBlob(filePath(file));
        if (blob.isEmpty())
            return false;
        const auto ok = m_engine->statePut(t, slot, spanOf(blob));
        wipe(blob);   // the rooms' slot holds keys
        if (!ok) {
            // a blob that does not parse is dropped, never a reason not to start
            logEd3kWarning(QStringLiteral("ed3k: %1 dropped (error %2)").arg(file, errorName(ok.error())));
            return false;
        }
        return true;
    };
    put(ffi::State::BadBlocks, kBadFile);
    put(ffi::State::SetGrants, kSetsFile);
    put(ffi::State::Rooms, kRoomsFile);
    put(ffi::State::Names, kNamesFile);
    put(ffi::State::Paid, kPaidFile);
    QByteArray friends = readBlob(filePath(kFriendFile));
    if (!friends.isEmpty()) {
        if (auto ok = m_engine->friendImportState(t, spanOf(friends)); !ok && ok.error() != ffi::Error::Unsupported)
            logEd3kWarning(QStringLiteral("ed3k: %1 dropped (error %2)").arg(kFriendFile, errorName(ok.error())));
        wipe(friends);
    }
    // the filter lists' state and the entry hops (hops.md) of the last run
    if (const QByteArray filter = readBlob(filePath(kFilterFile)); !filter.isEmpty())
        if (auto ok = m_engine->filterImportState(t, spanOf(filter)); !ok && ok.error() != ffi::Error::Unsupported)
            logEd3kWarning(QStringLiteral("ed3k: %1 dropped (error %2)").arg(kFilterFile, errorName(ok.error())));
    if (const QByteArray hops = readBlob(filePath(kHopsFile)); !hops.isEmpty())
        if (auto ok = m_engine->hopsImportState(spanOf(hops)); !ok && ok.error() != ffi::Error::Unsupported)
            logEd3kWarning(QStringLiteral("ed3k: %1 dropped (error %2)").arg(kHopsFile, errorName(ok.error())));
    // the KAD blob counts as loaded; without one the contacts are loaded all the same
    if (!put(ffi::State::KadContacts, kKadFile))
        (void)m_engine->contactsLoaded(t);
}

void Ed3kSession::saveState(bool all)
{
    if (!m_engine)
        return;
    if (all || m_badDirty) {
        m_badDirty = false;
        saveSlot(ffi::State::BadBlocks, kBadFile);
    }
    if (!all)
        return;
    // an empty table does not replace the contacts of an earlier run
    if (contactCount() > 0)
        saveSlot(ffi::State::KadContacts, kKadFile);
    saveSlot(ffi::State::SetGrants, kSetsFile);
    saveSlot(ffi::State::Rooms, kRoomsFile);
    saveSlot(ffi::State::Names, kNamesFile);
    saveSlot(ffi::State::Paid, kPaidFile);
    saveFriends();
    // was: ToDo: filter and hops state (filterExportState, hopsExportState) once used (done:
    // filter.dat also on FilterChanged, hops.dat; loaded in loadState)
    saveExport(m_engine->filterExportState(), kFilterFile);
    saveExport(m_engine->hopsExportState(), kHopsFile);
}

void Ed3kSession::saveSlot(ffi::State slot, const QString& file)
{
    auto blob = m_engine->stateGet(now(), slot);
    if (!blob && blob.error() == ffi::Error::Unsupported)
        return;   // its extension is off
    if (!blob) {
        logEd3kWarning(QStringLiteral("ed3k: state %1 not read (error %2)").arg(file, errorName(blob.error())));
        return;
    }
    if (blob->empty())
        return;
    if (!writeBlob(filePath(file), blob->bytes()))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(filePath(file)));
}

void Ed3kSession::saveFriends()
{
    auto blob = m_engine->friendExportState();
    if (!blob || blob->empty())
        return;
    if (!writeBlob(filePath(kFriendFile), blob->bytes()))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(filePath(kFriendFile)));
}

void Ed3kSession::saveExport(const ffi::Result<ffi::Buffer>& blob, const QString& file)
{
    // Unsupported: the extension is off; empty: nothing to keep (an old file stays)
    if (!blob || blob->empty())
        return;
    if (!writeBlob(filePath(file), blob->bytes()))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(filePath(file)));
}

void Ed3kSession::followIPv6Pin()
{
    if (!m_engine)
        return;
    const QHostAddress& bind = m_settings.bindAddress;
    if (bind != QHostAddress::Any && bind != QHostAddress::AnyIPv6)
        return;
    const QHostAddress pin = advertisedIPv6(bind, ipv6Pin());
    if (pin == m_ipv6Source)
        return;
    m_ipv6Source = pin;
    const ffi::Address local = pin.isNull() ? ffi::Address{{}, m_port} : toAddress(pin, m_port);
    if (auto ok = m_engine->setLocalAddr(now(), 6, local); !ok)
        logEd3kWarning(QStringLiteral("ed3k: IPv6 address not set (error %1)").arg(errorName(ok.error())));
    else
        logEd3kDebug(QStringLiteral("ed3k: IPv6 source %1").arg(pin.isNull() ? QStringLiteral("[::]") : pin.toString()));
}

void Ed3kSession::writeDatagram(const char* data, qint64 len, const ffi::Address& to)
{
    const QHostAddress ip = toQHostAddress(to);
    // §8.3: global IPv6 peers see the address advertised; loopback, link-local and
    // unique-local ones get the system's source
    if (!m_ipv6Source.isNull() && !to.isV4() && Address::fromQHostAddress(ip).isPublicIP()) {
        QNetworkDatagram dg(QByteArray(data, qsizetype(len)), ip, to.port);
        dg.setSender(m_ipv6Source);
        if (m_socket->writeDatagram(dg) >= 0)
            return;
        // the address went away: unpinned this once, the timer looks again
    }
    m_socket->writeDatagram(data, len, ip, to.port);
}

void Ed3kSession::die(ffi::Error error)
{
    if (!m_engine)
        return;
    logEd3kError(QStringLiteral("ed3k: the engine stopped (error %1); ed3k is off until restarted").arg(errorName(error)));
    m_timer.stop();
    m_badSaveTimer.stop();
    m_pinTimer.stop();
    if (m_http)
        m_http->clear();
    m_posted.clear();
    if (m_socket)
        m_socket->disconnect(this);
    // its sockets may be in a call up the stack: freed later
    if (m_lan) {
        m_lan->detach();
        m_lan.release()->deleteLater();
    }
    // the socket and HTTP client may be in a call up the stack: freed later
    if (m_socket)
        m_socket.release()->deleteLater();
    if (m_http)
        m_http.release()->deleteLater();
    m_trafficBase = traffic();   // if the dead engine still reads
    m_engine.reset();
    m_port = 0;
    emit statusChanged();
}

void Ed3kSession::armTimer()
{
    if (!m_engine) {
        m_timer.stop();
        return;
    }
    const auto at = m_engine->nextTimeout();
    if (!at) {
        m_timer.stop();
        return;
    }
    const std::uint64_t mono = now().monoUs;
    const std::uint64_t waitUs = *at > mono ? *at - mono : 0;
    const std::uint64_t ms = std::min<std::uint64_t>((waitUs + 999) / 1000, std::numeric_limits<int>::max());
    m_timer.start(int(ms));
}

QString Ed3kSession::filePath(const QString& name) const
{
    return QDir(m_dir).filePath(name);
}

} // namespace eMule::ed3k
