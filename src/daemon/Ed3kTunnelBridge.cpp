/// @file Ed3kTunnelBridge.cpp
/// @brief Paired phones over the ed3k tunnel: preamble, pair proof, the loopback pipe into
/// IpcServer, pair secrets and the peer list (see the header).

#include "Ed3kTunnelBridge.h"
#include "utils/ArgStr.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCborArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

#include <openssl/crypto.h>

#ifdef EMULE_HAVE_ED3K
#include "DaemonApp.h"
#include "Ed3kFriends.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "IpcServer.h"

#include <QUrl>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <map>
#include <memory>
#endif

namespace eMule::Ed3kTunnelBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr qint64 kSecretLifeMs = 10 * 60 * 1000;
constexpr int kSecretsMax = 4;
constexpr int kPreambleWaitMs = 15000;
constexpr int kPeersMax = 32;
/// Kad contacts handed to a phone as seeds of its leaf (finding the core again)
constexpr int kSeedsMax = 8;

/// "<node id 32 hex>[/<node key 64 hex>] <name>" of ipc.tunnelPeers; empty id when malformed
struct Peer {
    QByteArray id;
    QString name;
    QByteArray key;   ///< 32 bytes once known (tunnel or RegisterTunnelPeer): an ed3k friend
};

Peer parsePeer(const QString& line)
{
    const qsizetype sp = line.indexOf(u' ');
    const QString head = (sp < 0 ? line : line.left(sp)).trimmed().toLower();
    const qsizetype slash = head.indexOf(u'/');
    const QString hex = slash < 0 ? head : head.left(slash);
    const QByteArray id = QByteArray::fromHex(hex.toLatin1());
    if (id.size() != 16 || hex.size() != 32)
        return {};
    QByteArray key;
    if (slash >= 0) {
        const QString keyHex = head.mid(slash + 1);
        key = QByteArray::fromHex(keyHex.toLatin1());
        if (key.size() != 32 || keyHex.size() != 64)
            key.clear();
    }
    return {id, sp < 0 ? QString() : line.mid(sp + 1).trimmed(), key};
}

QList<Peer> peers()
{
    QList<Peer> out;
    for (const QString& line : thePrefs.ipcTunnelPeers())
        if (Peer p = parsePeer(line); !p.id.isEmpty())
            out.append(p);
    return out;
}

void savePeers(const QList<Peer>& list)
{
    QStringList lines;
    for (const Peer& p : list)
        lines << QString::fromLatin1(p.id.toHex())
                     + (p.key.size() == 32 ? u'/' + QString::fromLatin1(p.key.toHex()) : QString()) + u' ' + p.name;
    thePrefs.setIpcTunnelPeers(lines);
    thePrefs.saveInBackground();
}

/// "Phone N" with the lowest N not taken
QString nextName(const QList<Peer>& list)
{
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("Phone %1").arg(n);
        if (std::ranges::none_of(list, [&](const Peer& p) { return p.name == name; }))
            return name;
    }
}

/// Pair secrets out now, with their deadline (ms since epoch)
std::map<QByteArray, qint64>& secrets()
{
    static std::map<QByteArray, qint64> s;
    return s;
}

void dropExpiredSecrets()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::erase_if(secrets(), [now](const auto& kv) { return kv.second <= now; });
}

bool sameBytes(const QByteArray& a, const QByteArray& b)
{
    return a.size() == b.size() && CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size())) == 0;
}

/// A secret whose proof for @p node is @p proof: used up and true
bool takeSecretFor(const QByteArray& node, const QByteArray& proof)
{
    dropExpiredSecrets();
    for (auto it = secrets().begin(); it != secrets().end(); ++it) {
        if (sameBytes(pairProof(it->first, node), proof)) {
            secrets().erase(it);
            return true;
        }
    }
    return false;
}

#ifdef EMULE_HAVE_ED3K

namespace ffi = ::ed3k;

QString hexOf(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

QPointer<ed3k::Ed3kFriends>& friendsPtr()
{
    static QPointer<ed3k::Ed3kFriends> f;
    return f;
}

std::optional<ffi::Key32> keyOf(const QByteArray& raw)
{
    return raw.size() == 32 ? ed3k::Ed3kFriends::keyFrom(raw) : std::nullopt;
}

/// The phone as an ed3k friend of the node (friend.md §1): the node stores a sealed record of
/// its addresses for it, so the phone finds the core again after its address and relays changed
void befriend(const Peer& p)
{
    auto* f = friendsPtr().data();
    const auto key = keyOf(p.key);
    if (!f || !key || !f->isRunning())
        return;
    for (const auto& known : f->friends())
        if (known.key == *key)
            return;
    const QString link = QStringLiteral("ed3k:friend?key=%1&dn=%2")
                             .arg(ed3k::base32(*key),
                                  QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("qDonkey ") + p.name)));
    if (auto ok = f->add(link, {}); !ok)
        logWarning(QStringLiteral("ed3k tunnel: %1 not added as ed3k friend (%2)").arg(argStr(p.name), argStr(int(ok.error()))));
    else
        logInfo(QStringLiteral("ed3k tunnel: %1 is an ed3k friend now (finds the core by key)").arg(p.name));
}

void unfriend(const Peer& p)
{
    auto* f = friendsPtr().data();
    if (const auto key = keyOf(p.key); f && key && f->isRunning())
        (void)f->remove(*key);
}

QString endpointText(const ffi::Address& a)
{
    const QHostAddress ip = ed3k::toQHostAddress(a);
    const QString host = ip.protocol() == QAbstractSocket::IPv6Protocol ? u'[' + ip.toString() + u']' : ip.toString();
    return host + u':' + QString::number(a.port);
}

/// One accepted tunnel
struct Pipe {
    std::uint64_t id = 0;
    QByteArray peer;            ///< node ID
    QByteArray key;             ///< node key
    QString remote;
    bool known = false;
    QByteArray pre;             ///< preamble bytes until it is complete
    bool bridged = false;
    QPointer<QTcpSocket> client;   ///< our end of the loopback pipe
    QByteArray toTunnel;        ///< read from IPC, not taken by the tunnel yet (Busy)
    QByteArray toIpc;           ///< from the phone before the pipe connected
    bool ipcClosed = false;
    QElapsedTimer age;
};

/// The tunnel host: one per daemon
class Host : public QObject {
public:
    explicit Host(ed3k::Ed3kSession& session)
        : m_session(session)
    {
        connect(&m_session, &ed3k::Ed3kSession::eventPolled, this,
                [this](const ffi::Event& ev, bool* handled) { onEvent(ev, handled); }, Qt::DirectConnection);
        connect(&m_session, &ed3k::Ed3kSession::statusChanged, this, [this] { apply(); });
        m_sweep.setInterval(5000);
        connect(&m_sweep, &QTimer::timeout, this, [this] { sweep(); });
        m_sweep.start();
        m_loop.setMaxPendingConnections(64);
        connect(&m_loop, &QTcpServer::newConnection, this, [this] { onLoopAccepted(); });
        apply();
    }

    ~Host() override
    {
        for (auto& [id, p] : m_pipes)
            if (p.client)
                p.client->abort();
    }

    /// Listens and allows as prefs, secrets and the node say.
    void apply()
    {
        const bool want = thePrefs.ipcTunnel() && m_session.isRunning() && (!peers().isEmpty() || !secrets().empty());
        auto* engine = m_session.engine();
        if (!engine) {
            m_listening = nullptr;
            m_allowed.clear();
            return;
        }
        if (!want) {
            if (m_listening == engine) {
                m_session.post([](ffi::Engine& e, ffi::Now now) { (void)e.tunnelListen(now, kAlpn, false); });
                logInfo(QStringLiteral("ed3k tunnel: closed (no paired phone)"));
            }
            m_listening = nullptr;
            m_allowed.clear();
            return;
        }
        if (m_listening != engine) {
            m_allowed.clear();
            m_session.post([](ffi::Engine& e, ffi::Now now) {
                if (auto ok = e.tunnelListen(now, kAlpn, true); !ok)
                    logWarning(QStringLiteral("ed3k tunnel: cannot listen (%1)").arg(int(ok.error())));
            });
            m_listening = engine;
            logInfo(QStringLiteral("ed3k tunnel: listening for paired phones (%1)").arg(QString::fromLatin1(kAlpn)));
        }
        QSet<QByteArray> now;
        for (const Peer& p : peers())
            now.insert(p.id);
        for (const QByteArray& id : now)
            if (!m_allowed.contains(id))
                setAllowed(id, true);
        for (const QByteArray& id : QSet<QByteArray>(m_allowed))
            if (!now.contains(id))
                setAllowed(id, false);
        for (const Peer& p : peers())
            befriend(p);
    }

private:
    /// A paired phone's key, from QUIC (it proved it): kept and befriended if new
    void learnKey(const QByteArray& id, const QByteArray& key)
    {
        if (key.size() != 32)
            return;
        QList<Peer> list = peers();
        for (Peer& q : list) {
            if (q.id != id || q.key == key)
                continue;
            q.key = key;
            savePeers(list);
            befriend(q);
            return;
        }
    }

    void setAllowed(const QByteArray& id, bool on)
    {
        ffi::NodeId node;
        std::copy_n(reinterpret_cast<const std::uint8_t*>(id.constData()), 16, node.bytes.begin());
        m_session.post([node, on](ffi::Engine& e, ffi::Now now) { (void)e.tunnelAllow(now, kAlpn, node, on); });
        if (on)
            m_allowed.insert(id);
        else
            m_allowed.remove(id);
    }

    void onEvent(const ffi::Event& ev, bool* handled)
    {
        namespace e = ffi::event;
        if (const auto* o = std::get_if<e::TunnelOpen>(&ev)) {
            if (!o->accepted || o->alpn != kAlpn)
                return;
            *handled = true;
            Pipe p;
            p.id = o->tunnel;
            p.peer = QByteArray(reinterpret_cast<const char*>(o->peer.bytes.data()), 16);
            p.key = QByteArray(reinterpret_cast<const char*>(o->peerKey.data()), 32);
            p.remote = ed3k::toQHostAddress(o->remote).toString();
            p.known = o->known;
            p.age.start();
            logInfo(QStringLiteral("ed3k tunnel %1: phone %2 from %3%4")
                        .arg(argStr(p.id), QString::fromLatin1(p.peer.toHex().left(12)), argStr(p.remote),
                             argStr(p.known ? QString() : QStringLiteral(" (not paired yet)"))));
            if (p.known)
                learnKey(p.peer, p.key);
            m_pipes.emplace(p.id, std::move(p));
        } else if (const auto* d = std::get_if<e::TunnelData>(&ev)) {
            auto it = m_pipes.find(d->tunnel);
            if (it == m_pipes.end())
                return;
            *handled = true;
            const std::uint64_t id = d->tunnel;
            const std::size_t n = d->data.size();
            onData(it->second, QByteArray(reinterpret_cast<const char*>(d->data.data()), qsizetype(n)));
            // copied: the engine may read on
            m_session.post([id, n](ffi::Engine& e, ffi::Now now) { (void)e.tunnelReadDone(now, id, n); });
        } else if (const auto* dr = std::get_if<e::TunnelDrained>(&ev)) {
            if (auto it = m_pipes.find(dr->tunnel); it != m_pipes.end()) {
                *handled = true;
                flushToTunnel(it->second);
            }
        } else if (const auto* f = std::get_if<e::TunnelFinished>(&ev)) {
            if (auto it = m_pipes.find(f->tunnel); it != m_pipes.end()) {
                *handled = true;
                if (it->second.client)
                    it->second.client->disconnectFromHost();
            }
        } else if (const auto* c = std::get_if<e::TunnelClosed>(&ev)) {
            if (auto it = m_pipes.find(c->tunnel); it != m_pipes.end()) {
                *handled = true;
                logInfo(QStringLiteral("ed3k tunnel %1: closed (%2)").arg(c->tunnel).arg(c->code));
                if (it->second.client)
                    it->second.client->abort();
                m_pipes.erase(it);
            }
        }
    }

    void onData(Pipe& p, const QByteArray& data)
    {
        if (p.bridged) {
            if (p.client && p.client->state() == QAbstractSocket::ConnectedState)
                p.client->write(data);
            else
                p.toIpc += data;
            return;
        }
        p.pre += data;
        if (p.pre.size() < kPreambleLen)
            return;
        const QByteArray magic = p.pre.left(16);
        const QByteArray proof = p.pre.mid(16, 32);
        const QByteArray rest = p.pre.mid(kPreambleLen);
        p.pre.clear();
        if (magic != QByteArray(kMagic, 16)) {
            logWarning(QStringLiteral("ed3k tunnel %1: no qDonkey preamble, closed").arg(p.id));
            closeTunnel(p.id);
            return;
        }
        if (!p.known) {
            if (!takeSecretFor(p.peer, proof)) {
                logWarning(QStringLiteral("ed3k tunnel %1: phone %2 not paired and no valid pair proof, closed")
                               .arg(argStr(p.id), QString::fromLatin1(p.peer.toHex().left(12))));
                closeTunnel(p.id);
                return;
            }
            QList<Peer> list = peers();
            if (list.size() >= kPeersMax)
                unfriend(list.takeFirst());
            const QString name = nextName(list);
            list.append({p.peer, name, p.key});
            savePeers(list);
            setAllowed(p.peer, true);
            befriend(list.last());
            p.known = true;
            logInfo(QStringLiteral("ed3k tunnel %1: paired %2 (%3)").arg(argStr(p.id), argStr(name), QString::fromLatin1(p.peer.toHex())));
        }
        bridge(p, rest);
    }

    /// The loopback pipe: our socket here, the accepted one to IpcServer.
    void bridge(Pipe& p, const QByteArray& first)
    {
        auto* da = DaemonApp::instance();
        if (!da || !da->ipcServer()) {
            closeTunnel(p.id);
            return;
        }
        if (!m_loop.isListening() && !m_loop.listen(QHostAddress::LocalHost, 0)) {
            logWarning(QStringLiteral("ed3k tunnel: no loopback pipe (%1)").arg(m_loop.errorString()));
            closeTunnel(p.id);
            return;
        }
        p.bridged = true;
        p.toIpc = first;
        auto* sock = new QTcpSocket(this);
        p.client = sock;
        const std::uint64_t id = p.id;
        // our port first: the accept side finds its pipe by it
        sock->bind(QHostAddress::LocalHost, 0);
        m_byPort.insert(sock->localPort(), id);
        connect(sock, &QTcpSocket::connected, this, [this, id] {
            auto it = m_pipes.find(id);
            if (it == m_pipes.end() || !it->second.client)
                return;
            if (!it->second.toIpc.isEmpty())
                it->second.client->write(std::exchange(it->second.toIpc, {}));
        });
        connect(sock, &QTcpSocket::readyRead, this, [this, id] {
            auto it = m_pipes.find(id);
            if (it == m_pipes.end() || !it->second.client)
                return;
            it->second.toTunnel += it->second.client->readAll();
            flushToTunnel(it->second);
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, id] {
            auto it = m_pipes.find(id);
            if (it == m_pipes.end())
                return;
            it->second.ipcClosed = true;
            flushToTunnel(it->second);
        });
        sock->connectToHost(QHostAddress::LocalHost, m_loop.serverPort());
    }

    void onLoopAccepted()
    {
        while (QTcpSocket* s = m_loop.nextPendingConnection()) {
            const std::uint64_t id = m_byPort.take(s->peerPort());
            auto it = m_pipes.find(id);
            auto* da = DaemonApp::instance();
            if (id == 0 || it == m_pipes.end() || !da || !da->ipcServer()) {
                s->abort();
                s->deleteLater();
                continue;
            }
            da->ipcServer()->adoptTunnel(s, QStringLiteral("%1 (ed3k node %2)")
                                                .arg(it->second.remote, QString::fromLatin1(it->second.peer.toHex().left(12))));
        }
    }

    /// IPC bytes into the tunnel; then its half-close once IPC is gone
    void flushToTunnel(Pipe& p)
    {
        if (!p.toTunnel.isEmpty()) {
            QByteArray data = std::exchange(p.toTunnel, {});
            const std::uint64_t id = p.id;
            // the tunnel holds up to 4 MiB; past that the rest waits for TunnelDrained
            m_session.post([this, id, data](ffi::Engine& e, ffi::Now now) {
                auto ok = e.tunnelSend(now, id, std::as_bytes(std::span(data.constData(), std::size_t(data.size()))));
                if (!ok && ok.error() == ffi::Error::Busy)
                    if (auto it = m_pipes.find(id); it != m_pipes.end())
                        it->second.toTunnel.prepend(data);
            });
        }
        if (p.ipcClosed && p.toTunnel.isEmpty()) {
            const std::uint64_t id = p.id;
            m_session.post([id](ffi::Engine& e, ffi::Now now) { (void)e.tunnelFinish(now, id); });
        }
    }

    void closeTunnel(std::uint64_t id)
    {
        if (auto it = m_pipes.find(id); it != m_pipes.end()) {
            if (it->second.client)
                it->second.client->abort();
            m_pipes.erase(it);
        }
        m_session.post([id](ffi::Engine& e, ffi::Now now) { (void)e.tunnelClose(now, id); });
    }

    /// Preambles that never came; secrets that ran out
    void sweep()
    {
        std::vector<std::uint64_t> stale;
        for (const auto& [id, p] : m_pipes)
            if (!p.bridged && p.age.elapsed() > kPreambleWaitMs)
                stale.push_back(id);
        for (const auto id : stale) {
            logWarning(QStringLiteral("ed3k tunnel %1: no preamble in time, closed").arg(id));
            closeTunnel(id);
        }
        const std::size_t before = secrets().size();
        dropExpiredSecrets();
        if (secrets().size() != before)
            apply();
    }

    ed3k::Ed3kSession& m_session;
    std::map<std::uint64_t, Pipe> m_pipes;
    QHash<quint16, std::uint64_t> m_byPort;
    QTcpServer m_loop;
    QTimer m_sweep;
    const void* m_listening = nullptr;   ///< the engine listened on
    QSet<QByteArray> m_allowed;
};

std::unique_ptr<Host>& host()
{
    static std::unique_ptr<Host> h;
    return h;
}

ed3k::Ed3kSession*& sessionPtr()
{
    static ed3k::Ed3kSession* s = nullptr;
    return s;
}

#endif

} // namespace

void setSession(ed3k::Ed3kSession* session, ed3k::Ed3kFriends* friends)
{
#ifdef EMULE_HAVE_ED3K
    host().reset();
    sessionPtr() = session;
    friendsPtr() = friends;
    if (session)
        host() = std::make_unique<Host>(*session);
    // friends running (or a list change): the paired phones become friends then
    static QMetaObject::Connection onFriends;
    QObject::disconnect(onFriends);
    if (friends)
        onFriends = QObject::connect(friends, &ed3k::Ed3kFriends::friendsChanged, friends, [] { reapply(); });
#else
    Q_UNUSED(session);
    Q_UNUSED(friends);
#endif
}

void reapply()
{
#ifdef EMULE_HAVE_ED3K
    if (host())
        host()->apply();
#endif
}

QByteArray pairProof(const QByteArray& secret, const QByteArray& phoneNode)
{
    const QByteArray message = QByteArrayLiteral("qdonkey-pair") + phoneNode;
    return QMessageAuthenticationCode::hash(message, secret, QCryptographicHash::Sha256);
}

QCborMap identity(bool pair)
{
    QCborMap out;
#ifdef EMULE_HAVE_ED3K
    ed3k::Ed3kSession* s = sessionPtr();
    const bool on = thePrefs.ipcTunnel() && s && s->isRunning();
    out.insert(QStringLiteral("tunnel"), on);
    if (!on)
        return out;
    const ed3k::Ed3kStatus st = s->status();
    out.insert(QStringLiteral("nodeId"), st.nodeId);
    QCborArray eps;
    for (const QString& ep : {st.endpoint4, st.endpoint6})
        if (!ep.isEmpty())
            eps.append(ep);
    out.insert(QStringLiteral("endpoints"), eps);
    QCborArray relays;
    for (qsizetype i = 0; i < st.relays4.size() && i < st.relayIds4.size(); ++i)
        relays.append(QString(st.relayIds4.at(i) + u'@' + st.relays4.at(i)));
    for (qsizetype i = 0; i < st.relays6.size() && i < st.relayIds6.size(); ++i)
        relays.append(QString(st.relayIds6.at(i) + u'@' + st.relays6.at(i)));
    out.insert(QStringLiteral("relays"), relays);
    // the node's own port on this machine's addresses: the LAN fallback
    out.insert(QStringLiteral("udpPort"), static_cast<qint64>(st.udpPort));
    // finding the core when endpoints and relays are stale: its friend link (the key) and Kad
    // contacts the phone's leaf starts its routing table from
    if (auto* f = friendsPtr().data()) {
        if (auto link = f->myLink(false, {}); link)
            out.insert(QStringLiteral("friendLink"), *link);
    }
    if (auto* e = s->engine()) {
        QCborArray seeds;
        if (auto contacts = e->kadContacts(s->now()); contacts) {
            for (const auto& c : *contacts) {
                if (seeds.size() >= kSeedsMax)
                    break;
                if (c.fails != 0)
                    continue;
                ffi::NodeId id;
                std::copy(std::begin(c.id), std::end(c.id), id.bytes.begin());
                seeds.append(QString(hexOf(id.bytes) + u'@' + endpointText(ffi::Address::from(c.addr))));
            }
        }
        out.insert(QStringLiteral("seeds"), seeds);
    }
    if (pair) {
        dropExpiredSecrets();
        while (int(secrets().size()) >= kSecretsMax)
            secrets().erase(secrets().begin());
        QByteArray secret(16, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(secret.data()), 4);
        secrets().emplace(secret, QDateTime::currentMSecsSinceEpoch() + kSecretLifeMs);
        out.insert(QStringLiteral("pairSecret"), QString::fromLatin1(secret.toHex()));
        if (host())
            host()->apply();
    }
#else
    Q_UNUSED(pair);
    out.insert(QStringLiteral("tunnel"), false);
#endif
    return out;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    switch (msg.type()) {
    case IpcMsgType::RegisterTunnelPeer: {
        // [node id hex, name, node key hex?]: a phone paired on the LAN (the token proves it
        // may); the key makes it an ed3k friend (a wrong one only finds nothing)
        Peer p = parsePeer(msg.fieldString(0) + u' ' + msg.fieldString(1));
        if (p.id.isEmpty()) {
            reply(IpcMessage::makeResult(msg.seqId(), false, QStringLiteral("bad node id")));
            return true;
        }
        if (const QByteArray key = QByteArray::fromHex(msg.fieldString(2).toLatin1()); key.size() == 32)
            p.key = key;
        QList<Peer> list = peers();
        QString name = p.name.left(64);
        for (const Peer& q : list) {
            if (q.id != p.id)
                continue;
            if (name.isEmpty())
                name = q.name;   // a refresh keeps the name
            if (p.key.isEmpty())
                p.key = q.key;
        }
        list.removeIf([&](const Peer& q) { return q.id == p.id; });
        if (list.size() >= kPeersMax) {
#ifdef EMULE_HAVE_ED3K
            unfriend(list.first());
#endif
            list.removeFirst();
        }
        list.append({p.id, name.isEmpty() ? nextName(list) : name, p.key});
        savePeers(list);
        reapply();
        reply(IpcMessage::makeResult(msg.seqId(), true, QCborValue()));
        return true;
    }
    case IpcMsgType::GetTunnelPeers: {
        QCborArray rows;
        for (const Peer& p : peers())
            rows.append(QCborMap{{QStringLiteral("nodeId"), QString::fromLatin1(p.id.toHex())},
                                 {QStringLiteral("name"), p.name}});
        reply(IpcMessage::makeResult(msg.seqId(), true, rows));
        return true;
    }
    case IpcMsgType::RemoveTunnelPeer: {
        const QByteArray id = QByteArray::fromHex(msg.fieldString(0).toLatin1());
        QList<Peer> list = peers();
#ifdef EMULE_HAVE_ED3K
        for (const Peer& q : list)
            if (q.id == id)
                unfriend(q);   // its record for the phone is no longer stored
#endif
        const auto n = list.removeIf([&](const Peer& q) { return q.id == id; });
        if (n > 0) {
            savePeers(list);
            reapply();   // its open tunnel closes (packmule takes it off the allow-list)
        }
        reply(IpcMessage::makeResult(msg.seqId(), n > 0, QCborValue()));
        return true;
    }
    default:
        return false;
    }
}

} // namespace eMule::Ed3kTunnelBridge
