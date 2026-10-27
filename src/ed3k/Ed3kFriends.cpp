/// @file Ed3kFriends.cpp
/// @brief ed3k friends and chat in the daemon — implementation.

#include "Ed3kFriends.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kNamesFile = QStringLiteral("friend-names.dat");

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(Ed3kFriends::toBytes(bytes).toHex());
}

/// The query of an `ed3k:friend?...` link; empty for anything else.
QUrlQuery linkQuery(const QString& link)
{
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    if (q < 0 || !link.startsWith(QLatin1String("ed3k:friend"), Qt::CaseInsensitive))
        return {};
    return QUrlQuery(link.mid(q + 1));
}

/// base32 of Protocol.md §13.2 (RFC 4648 alphabet, no padding, either case).
std::optional<QByteArray> base32Decode(const QString& text)
{
    QByteArray out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const QChar c : text) {
        const char ch = char(c.toLower().toLatin1());
        int v = -1;
        if (ch >= 'a' && ch <= 'z')
            v = ch - 'a';
        else if (ch >= '2' && ch <= '7')
            v = ch - '2' + 26;
        if (v < 0)
            return std::nullopt;
        acc = (acc << 5) | std::uint32_t(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((acc >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

Ed3kFriends::Ed3kFriends(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kFriends::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kFriends::onSessionStatus);
    onSessionStatus();
}

Ed3kFriends::~Ed3kFriends() = default;

void Ed3kFriends::setChatOptions(const ChatOptions& options)
{
    m_options = options;
    applyChatOptions();
}

bool Ed3kFriends::isRunning() const
{
    return m_session.isRunning();
}

std::vector<Ed3kFriends::Friend> Ed3kFriends::friends()
{
    std::vector<Friend> out;
    auto* e = m_session.engine();
    if (!e)
        return out;
    auto list = e->friends();
    if (!list)
        return out;
    out.reserve(list->size());
    for (const auto& f : *list) {
        Friend row;
        row.key = f.key;
        row.node = f.node;
        row.mutual = f.mutual;
        row.asking = f.asking;
        row.heldLink = f.heldLink;
        if (const auto n = m_names.find(f.key); n != m_names.end())
            row.name = n->second;
        if (const auto a = m_addr.find(f.node); a != m_addr.end())
            row.addr = a->second;
        if (const auto s = m_seen.find(f.node); s != m_seen.end())
            row.lastSeen = s->second;
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<Ed3kFriends::Request> Ed3kFriends::requests() const
{
    std::vector<Request> out;
    out.reserve(m_requests.size());
    for (const auto& [key, r] : m_requests)
        out.push_back(r);
    return out;
}

bool Ed3kFriends::isFriend(const ffi::NodeId& node)
{
    return keyOfNode(node).has_value();
}

QString Ed3kFriends::nameOf(const ffi::NodeId& node)
{
    // the node ID is derived from the key: the name map is by key
    auto* e = m_session.engine();
    if (!e)
        return {};
    const auto list = e->friends();
    if (!list)
        return {};
    for (const auto& f : *list) {
        if (f.node == node) {
            const auto n = m_names.find(f.key);
            return n == m_names.end() ? QString() : n->second;
        }
    }
    return {};
}

ffi::Result<ffi::Key32> Ed3kFriends::add(const QString& link, const QString& text)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray l = link.trimmed().toUtf8();
    const QByteArray t = text.toUtf8();
    if (auto ok = e->friendAdd(m_session.now(), std::string_view(l.constData(), std::size_t(l.size())),
                               std::string_view(t.constData(), std::size_t(t.size())));
        !ok)
        return std::unexpected(ok.error());
    // the engine took the link, so `key` is valid (friend.md §1)
    const QUrlQuery q = linkQuery(link.trimmed());
    const auto raw = base32Decode(q.queryItemValue(QStringLiteral("key")));
    const auto key = raw ? keyFrom(*raw) : std::nullopt;
    m_session.pump();
    if (!key)
        return std::unexpected(ffi::Error::Malformed);
    const QString name = q.queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded).trimmed();
    if (!name.isEmpty()) {
        m_names[*key] = name;
        saveNames();
    }
    logEd3k(QStringLiteral("ed3k: friend %1 added").arg(hexOf(*key).left(16)));
    emit friendsChanged();
    return *key;
}

ffi::Result<> Ed3kFriends::remove(const ffi::Key32& key)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->friendRemove(m_session.now(), key);
    m_session.pump();
    if (!ok)
        return ok;
    if (m_names.erase(key) > 0)
        saveNames();
    emit friendsChanged();
    return {};
}

ffi::Result<> Ed3kFriends::answerRequest(const ffi::Key32& key, bool accept)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const ffi::Now t = m_session.now();
    auto ok = accept ? e->friendAccept(t, key) : e->friendDecline(t, key);
    m_session.pump();
    // a request the engine no longer has is gone here too
    m_requests.erase(key);
    if (ok && accept)
        logEd3k(QStringLiteral("ed3k: friend request of %1 accepted").arg(hexOf(key).left(16)));
    emit friendsChanged();
    return ok;
}

ffi::Result<QString> Ed3kFriends::myLink(bool withRequest, const QString& name)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray n = name.toUtf8();
    auto link = e->friendMyLink(m_session.now(), withRequest, std::string_view(n.constData(), std::size_t(n.size())));
    m_session.pump();
    if (!link)
        return std::unexpected(link.error());
    const auto text = link->text();
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

ffi::Result<std::vector<std::uint64_t>> Ed3kFriends::sendChat(const ffi::NodeId& node, const QString& text)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = text.toUtf8();
    auto ids = e->chatSend(m_session.now(), sourceOf(node), std::string_view(t.constData(), std::size_t(t.size())));
    m_session.pump();
    if (!ids)
        return ids;
    const std::uint64_t group = ids->empty() ? 0 : ids->front();
    for (const std::uint64_t id : *ids)
        m_out[id] = Outgoing{node, text, group, false};
    return ids;
}

void Ed3kFriends::endChat(const ffi::NodeId& node)
{
    m_waiting.erase(node);
}

QByteArray Ed3kFriends::toBytes(std::span<const std::uint8_t> bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
}

std::optional<ffi::Key32> Ed3kFriends::keyFrom(const QByteArray& bytes)
{
    ffi::Key32 key{};
    if (bytes.size() != qsizetype(key.size()))
        return std::nullopt;
    std::memcpy(key.data(), bytes.constData(), key.size());
    return key;
}

std::optional<ffi::NodeId> Ed3kFriends::nodeFrom(const QByteArray& bytes)
{
    ffi::NodeId node;
    if (bytes.size() != qsizetype(node.bytes.size()))
        return std::nullopt;
    std::memcpy(node.bytes.data(), bytes.constData(), node.bytes.size());
    return node;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kFriends::onEventPolled(const ffi::Event& ev, bool* handled)
{
    namespace e = ffi::event;
    const auto nodeBytes = [](const ffi::NodeId& n) { return toBytes(n.bytes); };
    if (const auto* m = std::get_if<e::ChatMessage>(&ev)) {
        *handled = true;
        const QString text = QString::fromUtf8(m->text.data(), qsizetype(m->text.size()));
        // the engine's policy may lag a pref change: the host checks again
        if ((m_options.friendsOnly && !isFriend(m->node)) || filtered(text)) {
            logEd3kDebug(QStringLiteral("ed3k: chat message of %1 filtered").arg(hexOf(m->node.bytes)));
            return;
        }
        emit chatMessage(nodeBytes(m->node), nameOf(m->node), text, m->id);
    } else if (const auto* s = std::get_if<e::ChatSent>(&ev)) {
        *handled = true;
        m_out.erase(s->id);
        m_suppressed.erase(s->id);
        emit chatSent(nodeBytes(s->node), s->id);
    } else if (const auto* f = std::get_if<e::ChatFailed>(&ev)) {
        *handled = true;
        onChatFailed(*f);
    } else if (const auto* r = std::get_if<e::FriendRequest>(&ev)) {
        *handled = true;
        const QString text = QString::fromUtf8(r->text.data(), qsizetype(r->text.size()));
        m_requests[r->key] = Request{r->key, r->node, text, r->issued};
        logEd3k(QStringLiteral("ed3k: friend request of %1").arg(hexOf(r->node.bytes)));
        emit friendRequest(toBytes(r->key), nodeBytes(r->node), text, r->issued);
        emit friendsChanged();
    } else if (const auto* fo = std::get_if<e::FriendFound>(&ev)) {
        *handled = true;
        onFound(fo->key, fo->node, fo->addr, QDateTime::currentSecsSinceEpoch());
    } else if (const auto* se = std::get_if<e::FriendSeen>(&ev)) {
        *handled = true;
        onFound(se->key, se->node, se->addr, qint64(se->issued));
    } else if (const auto* nf = std::get_if<e::FriendNotFound>(&ev)) {
        *handled = true;
        logEd3kDebug(QStringLiteral("ed3k: friend %1 not found (%2)").arg(hexOf(nf->node.bytes)).arg(int(nf->why)));
        onNotFound(nf->node);
    } else if (const auto* a = std::get_if<e::FriendAccepted>(&ev)) {
        *handled = true;
        logEd3k(QStringLiteral("ed3k: friend %1 accepted our request").arg(hexOf(a->node.bytes)));
        emit friendsChanged();
    } else if (const auto* x = std::get_if<e::FriendRequestExpired>(&ev)) {
        *handled = true;
        logEd3k(QStringLiteral("ed3k: our friend request to %1 went unanswered for 7 days").arg(hexOf(x->node.bytes)));
        emit friendsChanged();
    } else if (std::holds_alternative<e::FriendChanged>(ev)) {
        // the session saves the state (saveFriends)
        *handled = true;
        emit friendsChanged();
    }
}

void Ed3kFriends::onSessionStatus()
{
    if (m_session.isRunning() && !m_attached)
        attach();
    else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        m_requests.clear();
        m_out.clear();
        m_suppressed.clear();
        m_waiting.clear();
        emit friendsChanged();
    }
}

void Ed3kFriends::attach()
{
    m_attached = true;
    loadNames();
    applyChatOptions();
    if (auto* e = m_session.engine()) {
        // presence looks: a friend's address is known before a chat needs it
        if (auto ok = e->friendSetPresence(m_session.now(), true); !ok && ok.error() != ffi::Error::Unsupported)
            logEd3kWarning(QStringLiteral("ed3k: friend presence not set (error %1)").arg(int(ok.error())));
    }
    m_session.pump();
    emit friendsChanged();
}

void Ed3kFriends::applyChatOptions()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto policy = m_options.friendsOnly ? ffi::ChatPolicy::FriendsOnly : ffi::ChatPolicy::Everyone;
    (void)e->chatSetPolicy(policy, ffi::ChatBlock::Deny);
    // chat.md §2: 20 a minute is the engine's default
    (void)e->chatSetRate(m_options.rateLimit ? std::optional<std::uint32_t>{20} : std::optional<std::uint32_t>{});
}

void Ed3kFriends::onChatFailed(const ffi::event::ChatFailed& f)
{
    // a piece of a send that waits for a look already
    if (m_suppressed.erase(f.id) > 0)
        return;
    const auto it = m_out.find(f.id);
    std::optional<Outgoing> sent;
    if (it != m_out.end()) {
        sent = it->second;
        m_out.erase(it);
    }
    const bool noRoute = f.failure.why == ffi::AskFailure::Unreachable || f.failure.why == ffi::AskFailure::Lost;
    const auto key = keyOfNode(f.node);
    if (sent && noRoute && !sent->retried && key) {
        // the whole text once more after a look; the other pieces of that send are dropped
        auto& waiting = m_waiting[f.node];
        const bool looking = !waiting.empty();
        waiting.push_back(Outgoing{f.node, sent->text, sent->group, true});
        for (auto o = m_out.begin(); o != m_out.end();) {
            if (o->second.group == sent->group) {
                m_suppressed.insert(o->first);
                o = m_out.erase(o);
            } else {
                ++o;
            }
        }
        if (!looking) {
            if (auto* e = m_session.engine())
                (void)e->friendFind(m_session.now(), *key);
            emit chatProgress(toBytes(f.node.bytes), int(Progress::Searching));
        }
        return;
    }
    emit chatFailed(toBytes(f.node.bytes), f.id, int(f.failure.why), f.failure.retryAfter);
}

void Ed3kFriends::onFound(const ffi::Key32& key, const ffi::NodeId& node, const ffi::Address& addr, qint64 when)
{
    (void)key;
    m_addr[node] = addr;
    m_seen[node] = when;
    const auto waiting = m_waiting.find(node);
    if (waiting != m_waiting.end()) {
        std::vector<Outgoing> sends = std::move(waiting->second);
        m_waiting.erase(waiting);
        emit chatProgress(toBytes(node.bytes), int(Progress::Found));
        auto* e = m_session.engine();
        for (const Outgoing& o : sends) {
            const QByteArray t = o.text.toUtf8();
            ffi::Result<std::vector<std::uint64_t>> ids = std::unexpected(ffi::Error::State);
            if (e)
                ids = e->chatSend(m_session.now(), sourceOf(node), std::string_view(t.constData(), std::size_t(t.size())));
            if (!ids) {
                emit chatFailed(toBytes(node.bytes), o.group, int(ffi::AskFailure::Unreachable), 0);
                continue;
            }
            for (const std::uint64_t id : *ids)
                m_out[id] = Outgoing{node, o.text, ids->empty() ? 0 : ids->front(), true};
        }
    }
    emit friendsChanged();
}

void Ed3kFriends::onNotFound(const ffi::NodeId& node)
{
    const auto waiting = m_waiting.find(node);
    if (waiting == m_waiting.end())
        return;
    const std::vector<Outgoing> sends = std::move(waiting->second);
    m_waiting.erase(waiting);
    emit chatProgress(toBytes(node.bytes), int(Progress::Failed));
    for (const Outgoing& o : sends)
        emit chatFailed(toBytes(node.bytes), o.group, int(ffi::AskFailure::Unreachable), 0);
}

bool Ed3kFriends::filtered(const QString& text) const
{
    return std::ranges::any_of(m_options.filter, [&](const QString& word) {
        const QString w = word.trimmed();
        return !w.isEmpty() && text.contains(w, Qt::CaseInsensitive);
    });
}

ffi::Source Ed3kFriends::sourceOf(const ffi::NodeId& node) const
{
    ffi::Source s{node, false, std::nullopt, std::nullopt, {}};
    if (const auto a = m_addr.find(node); a != m_addr.end()) {
        if (a->second.isV4())
            s.ep4 = a->second;
        else
            s.ep6 = a->second;
    }
    return s;
}

std::optional<ffi::Key32> Ed3kFriends::keyOfNode(const ffi::NodeId& node)
{
    auto* e = m_session.engine();
    if (!e)
        return std::nullopt;
    const auto list = e->friends();
    if (!list)
        return std::nullopt;
    for (const auto& f : *list)
        if (f.node == node)
            return f.key;
    return std::nullopt;
}

void Ed3kFriends::loadNames()
{
    m_names.clear();
    const QByteArray blob = readBlob(namesPath());
    for (const QByteArray& line : blob.split('\n')) {
        const qsizetype tab = line.indexOf('\t');
        if (tab <= 0)
            continue;
        const auto key = keyFrom(QByteArray::fromHex(line.left(tab)));
        if (key)
            m_names[*key] = QString::fromUtf8(line.mid(tab + 1));
    }
}

void Ed3kFriends::saveNames() const
{
    if (m_session.stateDir().isEmpty())
        return;
    QByteArray blob;
    for (const auto& [key, name] : m_names) {
        QString clean = name;
        clean.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' '));
        blob += toBytes(key).toHex() + '\t' + clean.toUtf8() + '\n';
    }
    if (!writeBlob(namesPath(), {reinterpret_cast<const std::byte*>(blob.constData()), std::size_t(blob.size())}))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(namesPath()));
}

QString Ed3kFriends::namesPath() const
{
    return QDir(m_session.stateDir()).filePath(kNamesFile);
}

} // namespace eMule::ed3k
