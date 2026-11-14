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
constexpr qint64 kRateWindowMs = 10000;   // downRate() average

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

/// "ip:port", IPv6 in brackets
QString endpointText(const ffi::Address& a)
{
    const QHostAddress ip = toQHostAddress(a);
    return a.isV4() ? QStringLiteral("%1:%2").arg(ip.toString()).arg(a.port)
                    : QStringLiteral("[%1]:%2").arg(ip.toString()).arg(a.port);
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
    , m_hide(std::make_unique<Ed3kHide>(*this))
{
    m_readPool.setMaxThreadCount(2);
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kSession::onTimer);
    // the anonymous node's queue policy runs from its start (hops.md §1)
    connect(m_hide.get(), &Ed3kHide::started, this, &Ed3kSession::applyUploadSlots);
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

    // the socket first: the engine is told its port. A shared one is bound and pinned by its
    // owner (eD2K UDP on the same port, §14.1)
    std::unique_ptr<QUdpSocket> socket;
    std::uint16_t port = settings.udpPort;
    if (!settings.sharedSocket) {
        socket = std::make_unique<QUdpSocket>();
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
        port = socket->localPort();
    } else if (port == 0) {
        logEd3kError(QStringLiteral("ed3k: a shared socket needs its port"));
        wipe(*seed);
        return false;
    }

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
    // forceOpen on a wildcard: the public interface address, else no IPv4 endpoint (and no
    // own entry in an exported node list) until 3 sites reported one
    const QHostAddress open4 = settings.forceOpen ? publicIPv4() : QHostAddress();
    const ffi::Address any4 = open4.isNull() ? ffi::Address::v4(0, 0, 0, 0, port) : toAddress(open4, port);
    if (bind == QHostAddress::Any) {
        // cfg.local4 = ffi::Address::v4(0, 0, 0, 0, port);
        cfg.local4 = any4;
        // was: ToDo: the stable IPv6 source address (IPv6SourcePin) instead of [::], socket-modes.md
        // §8.3 (done: advertised and sent from, followIPv6Pin; [::] while none applies)
        // cfg.local6 = ffi::Address{{}, port};
        m_ipv6Source = advertisedIPv6(bind, ipv6Pin());
        cfg.local6 = m_ipv6Source.isNull() ? ffi::Address{{}, port} : toAddress(m_ipv6Source, port);
    } else if (bind == QHostAddress::AnyIPv4) {
        // cfg.local4 = ffi::Address::v4(0, 0, 0, 0, port);
        cfg.local4 = any4;
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
    cfg.smallNetS = settings.smallNetS;
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
    m_ownSocket = std::move(socket);
    m_socket = m_ownSocket ? m_ownSocket.get() : settings.sharedSocket.data();
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
    applyIndexLinks();
    m_logLevel = -1;
    applyLogLevel();
    // hiding (hops.md §1): after hops.dat (loadState), live from the prefs
    m_hide->attach();
    applyHide();

    if (m_ownSocket)
        connect(m_ownSocket.get(), &QUdpSocket::readyRead, this, &Ed3kSession::onReadyRead);
    if (settings.bindAddress == QHostAddress::Any || settings.bindAddress == QHostAddress::AnyIPv6)
        m_pinTimer.start();
    pump();
    if (!m_engine)
        return false;

    logEd3k(QStringLiteral("ed3k node %1 started on UDP port %2%3").arg(nodeIdHex()).arg(m_port)
                .arg(m_ownSocket ? QString() : QStringLiteral(" (shared with eD2K)")));
    emit statusChanged();
    // datagrams that came before the connect
    if (m_ownSocket && m_ownSocket->hasPendingDatagrams())
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
    if (m_ownSocket) {
        m_ownSocket->disconnect(this);
        m_ownSocket->close();
    }
    m_ownSocket.reset();
    m_socket.clear();
    m_engine.reset();
    m_port = 0;
    m_endpoint4.clear();
    m_endpoint6.clear();
    for (QStringList* l : {&m_relays4, &m_relays6, &m_relayIds4, &m_relayIds6})
        l->clear();
    m_hide->detach();
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
        st.kadOffline = s->kad_offline;
        st.natChecking = s->nat_checking;
        st.kadConnected = s->kad_connected != 0;
    }
    st.hide = m_hide->status();
    st.endpoint4 = m_endpoint4;
    st.endpoint6 = m_endpoint6;
    st.relays4 = m_relays4;
    st.relays6 = m_relays6;
    st.relayIds4 = m_relayIds4;
    st.relayIds6 = m_relayIds6;
    if (const auto leaves = m_engine->relaysServed())
        for (const auto& l : *leaves)
            st.leavesServed.push_back({endpointText(ffi::Address::from(l.addr)), hex(l.id), l.hop != 0});
    if (const auto links = m_engine->indexLinks(now()))
        for (const auto& l : *links) {
            const auto text = [](const std::uint8_t (&b)[64]) {
                const auto n = std::find(std::begin(b), std::end(b), std::uint8_t{0}) - std::begin(b);
                return QString::fromUtf8(reinterpret_cast<const char*>(b), qsizetype(n));
            };
            st.indexLinks.push_back({endpointText(ffi::Address::from(l.node.addr)), hex(l.node.id), l.state,
                                     l.current != 0, l.tries, l.up_s, l.rtt_ms, text(l.agent), text(l.nick)});
        }
    if (const auto u = m_engine->kadUsers()) {
        st.usersByTable = u->by_table;
        st.usersByLookups = u->by_lookups;
        st.leafPct = u->leaf_pct;
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

bool Ed3kSession::addIndexLink(const QString& link)
{
    const QString trimmed = link.trimmed();
    const QByteArray text = trimmed.toUtf8();
    if (!ffi::parseNodeLink(std::string_view(text.constData(), std::size_t(text.size())))) {
        logEd3kDebug(QStringLiteral("ed3k: index node link of a server not usable: %1").arg(trimmed));
        return false;
    }
    if (m_indexLinks.contains(trimmed))
        return true;
    m_indexLinks.append(trimmed);
    logEd3k(QStringLiteral("ed3k: index node named by an eD2K server: %1%2")
                .arg(trimmed, m_autoIndex ? QString() : QStringLiteral(" (auto connect off)")));
    if (m_engine && m_autoIndex) {
        applyIndexLinks();
        pump();
    }
    return true;
}

bool Ed3kSession::removeIndexLink(const QString& link)
{
    const QString trimmed = link.trimmed();
    const QByteArray text = trimmed.toUtf8();
    const auto parsed = ffi::parseNodeLink(std::string_view(text.constData(), std::size_t(text.size())));
    if (!parsed)
        return false;
    m_indexLinks.removeAll(trimmed);
    logEd3k(QStringLiteral("ed3k: index node of a disconnected eD2K server dropped: %1").arg(trimmed));
    if (!m_engine)
        return true;
    // NOT_FOUND: never dialled (auto connect off); the next of the list only with auto connect
    if (auto ok = m_engine->removeIndexNode(now(), parsed->id, m_autoIndex); !ok && ok.error() != ffi::Error::NotFound)
        logEd3kWarning(QStringLiteral("ed3k: index node not removed (error %1)").arg(errorName(ok.error())));
    pump();
    emit statusChanged();
    return true;
}

void Ed3kSession::setAutoIndexNodes(bool on)
{
    if (m_autoIndex == on)
        return;
    m_autoIndex = on;
    if (on && m_engine) {
        applyIndexLinks();
        pump();
    }
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
    if (!s)
        return std::nullopt;
    // hops.md §1: the anonymous node's queue too; both take slots from the one upload limit
    if (m_hide->hiding())
        if (auto a = m_engine->queueStats(ffi::KeyKind::Anon)) {
            s->slotCount = std::max(s->slotCount, a->slotCount);
            s->holders += a->holders;
            s->waiting += a->waiting;
        }
    return *s;
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

void Ed3kSession::savePaidState()
{
    if (m_engine)
        saveSlot(ffi::State::Paid, kPaidFile);
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
                // was: auto ev = m_engine->pollEvent(); (drops which node had it)
                auto ev = m_engine->pollKeyedEvent();
                if (!ev) {
                    die(ev.error());
                    break;
                }
                if (!*ev)
                    break;
                busy = true;
                dispatch((*ev)->event, (*ev)->key);
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

void Ed3kSession::addPeerBytes(::ed3k::KeyKind key, const ::ed3k::NodeId& node, std::uint64_t up, std::uint64_t down)
{
    PeerBytes& b = m_peerBytes[{key, node}];
    b.up += up;
    b.down += down;
    if (down == 0)
        return;
    if (!m_rateClock.isValid())
        m_rateClock.start();
    const qint64 now = m_rateClock.elapsed();
    // drop peers gone quiet, at most once a second
    if (now - m_recentPruned >= 1000) {
        m_recentPruned = now;
        std::erase_if(m_recentDown, [&](const auto& kv) {
            return kv.second.blocks.empty() || now - kv.second.blocks.back().first > kRateWindowMs;
        });
    }
    RecentDown& r = m_recentDown[{key, node}];
    while (!r.blocks.empty() && now - r.blocks.front().first > kRateWindowMs)
        r.blocks.pop_front();
    if (r.blocks.empty())
        r.since = now;
    r.blocks.emplace_back(now, down);
}

std::uint64_t Ed3kSession::downRate(::ed3k::KeyKind key, const ::ed3k::NodeId& node) const
{
    const auto it = m_recentDown.find({key, node});
    if (it == m_recentDown.end() || !m_rateClock.isValid())
        return 0;
    const qint64 now = m_rateClock.elapsed();
    std::uint64_t sum = 0;
    for (const auto& [at, bytes] : it->second.blocks)
        if (now - at <= kRateWindowMs)
            sum += bytes;
    if (sum == 0)
        return 0;
    // a fresh burst: over its age, not the whole window (no slow ramp-up); 1 s floor
    const qint64 span = std::clamp<qint64>(now - it->second.since, 1000, kRateWindowMs);
    return sum * 1000 / std::uint64_t(span);
}

Ed3kSession::PeerBytes Ed3kSession::peerBytes(::ed3k::KeyKind key, const ::ed3k::NodeId& node) const
{
    const auto it = m_peerBytes.find({key, node});
    return it == m_peerBytes.end() ? PeerBytes{} : it->second;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kSession::onReadyRead()
{
    while (m_engine && m_ownSocket && m_ownSocket->hasPendingDatagrams()) {   // drain fully, then pump once
        const QNetworkDatagram d = m_ownSocket->receiveDatagram();
        if (!d.isValid())
            break;
        if (peerFiltered(d.senderAddress())) {
            ++m_filtered;
            continue;
        }
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

bool Ed3kSession::offerDatagram(const QNetworkDatagram& d)
{
    if (!m_engine || m_ownSocket)
        return false;
    // the owner (ClientUDPSocket) drops it with its own filter check and counts it
    if (peerFiltered(d.senderAddress()))
        return false;
    const QByteArray data = d.data();
    const auto ours = m_engine->datagramIn(now(), toAddress(d.senderAddress(), std::uint16_t(d.senderPort())), spanOf(data));
    if (!ours) {
        if (ours.error() == ffi::Error::Dead)
            die(ours.error());
        return false;
    }
    if (!*ours)
        return false;   // eD2K's
    // one pump for a burst: the owner drains its socket first
    if (!m_pumpQueued) {
        m_pumpQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            m_pumpQueued = false;
            if (m_engine)
                pump();
        }, Qt::QueuedConnection);
    }
    return true;
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

void Ed3kSession::dispatch(ffi::Event& ev, ffi::KeyKind key)
{
    namespace e = ffi::event;
    m_eventKey = key;
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
        // the Proxy page as it is now: a change needs no node restart (report 34)
        m_http->setProxy(toNetworkProxy(thePrefs.proxySettings()));
        m_http->setReportProxy(m_hide->settings().reportProxy);
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
        logEd3kKadDebug([&]() -> QString { return QStringLiteral("ed3k: Kad store %1 (kind %2): %3 nodes")
                            .arg(ks->lookup).arg(ks->kind).arg(ks->nodes); });
    } else if (std::get_if<e::SourcesPublished>(&ev)) {
        // packmule logs the batch itself ("sources published <target>: N nodes, x of y along")
    } else if (const auto* kp = std::get_if<e::KeywordsPublished>(&ev)) {
        logEd3kKadDebug([&]() -> QString { return QStringLiteral("ed3k: keywords of %1 file(s) published: %2 stored, %3 full, %4 rejected, %5 unanswered")
                            .arg(kp->files).arg(kp->stored).arg(kp->full).arg(kp->rejected).arg(kp->unanswered); });
    } else if (const auto* n = std::get_if<e::NatClass>(&ev)) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: IPv%1 class %2").arg(n->family).arg(n->natClass); });
        emit statusChanged();
    } else if (const auto* ep = std::get_if<e::Endpoint>(&ev)) {
        const QString text = ep->endpoint ? endpointText(*ep->endpoint) : QString();
        (ep->family == 6 ? m_endpoint6 : m_endpoint4) = text;
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: IPv%1 endpoint %2").arg(ep->family)
                            .arg(text.isEmpty() ? QStringLiteral("none") : text); });
        emit statusChanged();
    } else if (const auto* rl = std::get_if<e::Relays>(&ev)) {
        // §9.4: what a Kad buddy is to a firewalled eD2K client
        const auto fill = [](const std::vector<ffi::Node>& nodes, QStringList& addrs, QStringList& ids) {
            addrs.clear();
            ids.clear();
            for (const ffi::Node& n : nodes) {
                addrs << endpointText(n.addr);
                ids << hex(n.id.bytes);
            }
        };
        fill(rl->relays4, m_relays4, m_relayIds4);
        fill(rl->relays6, m_relays6, m_relayIds6);
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: relays IPv4 [%1] IPv6 [%2]")
                            .arg(m_relays4.join(u' '), m_relays6.join(u' ')); });
        emit statusChanged();
    } else if (const auto* r = std::get_if<e::Router>(&ev)) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: router %1").arg(r->on ? 1 : 0); });
        emit statusChanged();
    } else if (const auto* ko = std::get_if<e::KadOffline>(&ev)) {
        // Protocol.md §10 "Own network down": no store, no answer from Kad of that family
        // regular ed3k log: the contact table is frozen while offline
        if (ko->offline)
            logEd3kWarning(QStringLiteral("ed3k: IPv%1 network seems offline (no answers, several sites "
                                          "failed) — Kad contacts kept, not evicted").arg(ko->family));
        else
            logEd3k(QStringLiteral("ed3k: IPv%1 network online again — checking Kad contacts").arg(ko->family));
        emit statusChanged();
    } else if (std::holds_alternative<e::HopsPathBuilt>(ev) || std::holds_alternative<e::HopsPathFailed>(ev)
               || std::holds_alternative<e::HopsPathClosed>(ev)) {
        m_hide->onEvent(ev);
    }

    bool handled = false;
    emit keyedEventPolled(ev, key, &handled);
    // a slot of eventPolled knows the node key only (hops.md §1: never mixed up)
    if (key == ffi::KeyKind::Node)
        emit eventPolled(ev, &handled);
    m_eventKey = ffi::KeyKind::Node;
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
    m_http->setProxy(toNetworkProxy(thePrefs.proxySettings()));
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

void Ed3kSession::applyIndexLinks()
{
    if (!m_engine || !m_autoIndex)
        return;
    // packmule keeps one entry per node ID: repeats only refresh the address
    for (const QString& link : std::as_const(m_indexLinks)) {
        const QByteArray text = link.toUtf8();
        const auto parsed = ffi::parseNodeLink(std::string_view(text.constData(), std::size_t(text.size())));
        if (!parsed || parsed->endpoints.empty())
            continue;
        // its IPv4 endpoint first, else the link's first
        const auto v4 = std::ranges::find_if(parsed->endpoints, [](const ffi::Address& a) { return a.isV4(); });
        const ffi::Address& ep = v4 != parsed->endpoints.end() ? *v4 : parsed->endpoints.front();
        if (auto ok = m_engine->addIndexNode(ffi::Node{parsed->id, ep}); !ok)
            logEd3kWarning(QStringLiteral("ed3k: index node not added (error %1)").arg(errorName(ok.error())));
    }
}

void Ed3kSession::applyUploadSlots()
{
    if (!m_engine)
        return;
    if (auto ok = m_engine->queueAuto(now(), true, m_uploadSlots); !ok)
        logEd3kWarning(QStringLiteral("ed3k: upload queue not set (error %1)").arg(errorName(ok.error())));
    // the anonymous node's queue with the full limit too (both count from one upload limit)
    if (m_hide->hiding())
        if (auto ok = m_engine->queueAuto(now(), true, m_uploadSlots, ffi::KeyKind::Anon); !ok)
            logEd3kWarning(QStringLiteral("ed3k: anonymous upload queue not set (error %1)").arg(errorName(ok.error())));
}

void Ed3kSession::applyHide()
{
    m_hide->apply();
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
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: block %1 refused, root not shared").arg(w->id); });
    } else if (const auto* v = std::get_if<e::VerifyBlock>(&ev)) {
        (void)m_engine->blockFail(t, v->id);
    } else if (const auto* wh = std::get_if<e::WantHashes>(&ev)) {
        (void)m_engine->hashesFail(t, wh->id);
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
    } else if (const auto* l = std::get_if<e::LinkResolve>(&ev)) {
        // hoster.md §3: no resolver took the ask (none built in, or the hoster join stopped)
        (void)m_engine->linkAnswer(t, l->link, ED3K_RESOLVER_NO_RESOLVER);
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
    writeBlobLater(filePath(file), blob->bytes());
}

void Ed3kSession::saveFriends()
{
    auto blob = m_engine->friendExportState();
    if (!blob || blob->empty())
        return;
    writeBlobLater(filePath(kFriendFile), blob->bytes());
}

void Ed3kSession::saveExport(const ffi::Result<ffi::Buffer>& blob, const QString& file)
{
    // Unsupported: the extension is off; empty: nothing to keep (an old file stays)
    if (!blob || blob->empty())
        return;
    writeBlobLater(filePath(file), blob->bytes());
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
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: IPv6 source %1").arg(pin.isNull() ? QStringLiteral("[::]") : pin.toString()); });
}

void Ed3kSession::writeDatagram(const char* data, qint64 len, const ffi::Address& to)
{
    if (!m_socket)
        return;   // the shared socket went with the eD2K core
    const QHostAddress ip = toQHostAddress(to);
    // nothing to a filtered address: the engine sees a lost datagram
    if (peerFiltered(ip)) {
        ++m_filtered;
        return;
    }
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
    if (m_ownSocket)
        m_ownSocket->disconnect(this);
    // its sockets may be in a call up the stack: freed later
    if (m_lan) {
        m_lan->detach();
        m_lan.release()->deleteLater();
    }
    // the socket and HTTP client may be in a call up the stack: freed later
    if (m_ownSocket)
        m_ownSocket.release()->deleteLater();
    m_socket.clear();
    if (m_http)
        m_http.release()->deleteLater();
    m_trafficBase = traffic();   // if the dead engine still reads
    m_engine.reset();
    m_port = 0;
    m_endpoint4.clear();
    m_endpoint6.clear();
    for (QStringList* l : {&m_relays4, &m_relays6, &m_relayIds4, &m_relayIds6})
        l->clear();
    m_hide->detach();
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
