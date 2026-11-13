/// @file Ed3kUploadsBridge.cpp
/// @brief The ed3k upload queue in GetUploads — implementation.

#include "Ed3kUploadsBridge.h"

#include <QCborMap>
#include <QDateTime>
#include <QHostAddress>
#include <QRegularExpression>

#include <algorithm>
#include <set>
#include <span>

#include "geo/IP2Country.h"

#ifdef EMULE_HAVE_ED3K
#include "Ed3kHide.h"
#include "Ed3kJoinBase.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kUploads.h"
#endif

namespace eMule::Ed3kUploadsBridge {

namespace {

ed3k::Ed3kUploads* s_uploads = nullptr;

#ifdef EMULE_HAVE_ED3K
/// Hello.agent "eMuleQt 0.7.0" as eD2K software names read: "eMuleQt v0.7.0"
QString softwareOf(const QString& agent)
{
    if (agent.isEmpty())
        return QStringLiteral("ed3k");
    static const QRegularExpression versioned(QStringLiteral("^(.*\\S)\\s+(\\d[^\\s]*)$"));
    const auto m = versioned.match(agent);
    return m.hasMatch() ? QStringLiteral("%1 v%2").arg(m.captured(1), m.captured(2)) : agent;
}

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

QCborMap rowToCbor(const ed3k::Ed3kUploads::Row& r, qint64 nowMs)
{
    const QString node = hexOf(r.node.bytes);
    const qint64 held = std::max<qint64>(0, nowMs - r.since);
    constexpr std::uint64_t kCapEd2k = 1u << 6;   // Protocol.md §2 bit 6 ED2K
    return QCborMap{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        // Hello.nick: the peer's user name (its Options); else the node ID's start
        {QStringLiteral("userName"), r.nick.isEmpty() ? node.left(8) : r.nick},
        {QStringLiteral("nick"), r.nick},
        {QStringLiteral("userHash"), node},
        {QStringLiteral("software"), softwareOf(r.agent)},
        // Hello seen: caps and agent are known
        {QStringLiteral("helloKnown"), !r.agent.isEmpty() || r.caps != 0},
        {QStringLiteral("ed2k"), (r.caps & kCapEd2k) != 0},
        {QStringLiteral("uploadState"), r.holding ? QStringLiteral("Transferring") : QStringLiteral("OnQueue")},
        {QStringLiteral("uploadFileName"), r.fileName},
        {QStringLiteral("uploadFileSize"), qint64(std::min<std::uint64_t>(r.fileSize, INT64_MAX))},
        {QStringLiteral("root"), hexOf(r.root.bytes)},
        {QStringLiteral("addr"), r.address},
        {QStringLiteral("port"), int(r.port)},
        {QStringLiteral("cc"), r.address.isEmpty() ? QString() : countryCodeOf(QHostAddress(r.address))},
        {QStringLiteral("sessionUp"), qint64(std::min<std::uint64_t>(r.bytes, INT64_MAX))},
        {QStringLiteral("waitStartTime"), r.holding ? qint64(0) : held},
        {QStringLiteral("uploadStartDelay"), r.holding ? held : qint64(0)},
        {QStringLiteral("queueRank"), r.place},
        // was: {QStringLiteral("isConnected"), true},
        {QStringLiteral("isConnected"), r.connected},
        {QStringLiteral("lan"), r.lan},
        {QStringLiteral("due"), r.due},
        // hops.md §1: "Anonymous" rows have no address; the GUI shows them as anonymous
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(r.key)},
    };
}

/// rowToCbor plus what only the details show: key, node ID, totals, derived eD2K hash.
QCborMap detailsOf(const ed3k::Ed3kUploads::Row& r, qint64 nowMs)
{
    const bool anon = r.key == ::ed3k::KeyKind::Anon;
    QCborMap m = rowToCbor(r, nowMs);
    const QString node = m.value(QStringLiteral("userHash")).toString();
    m.insert(QStringLiteral("nodeId"), node);
    m.insert(QStringLiteral("userHash"), QString((anon ? QStringLiteral("ed3k:anon:") : QStringLiteral("ed3k:")) + node));
    m.insert(QStringLiteral("anonymous"), anon);
    // this daemon run: finished turns + the running one; blocks from it
    const auto [up, down] = s_uploads->peerBytes(r.key, r.node);
    const std::uint64_t sent = up + (r.holding ? r.bytes : 0);
    m.insert(QStringLiteral("sentTotal"), qint64(std::min<std::uint64_t>(sent, INT64_MAX)));
    m.insert(QStringLiteral("receivedTotal"), qint64(std::min<std::uint64_t>(down, INT64_MAX)));
    // §14.2: the eD2K user hash the peer has in compat mode; the anonymous node has none
    if (!anon)
        m.insert(QStringLiteral("ed2kUserHash"), hexOf(::ed3k::userHash(r.node)));
    return m;
}

using SourceInfo = ed3k::Ed3kJoinBase::SourceInfo;
constexpr std::uint64_t kPiece = 4 * 1024 * 1024;

QString keyOf(::ed3k::KeyKind key, const ::ed3k::NodeId& node)
{
    return (key == ::ed3k::KeyKind::Anon ? QStringLiteral("ed3k:anon:") : QStringLiteral("ed3k:")) + hexOf(node.bytes);
}

/// pieces of the file the source holds (FileStatus.have: a missing bit is held)
std::pair<int, int> piecesOf(const SourceInfo& s)
{
    const auto pieces = int(std::min<std::uint64_t>(s.size / kPiece + (s.size % kPiece != 0 ? 1 : 0), INT32_MAX));
    if (s.have.empty())
        return {0, pieces};
    int held = 0;
    for (int p = 0; p < pieces; ++p)
        if (std::size_t(p / 8) >= s.have.size() || ((s.have[std::size_t(p / 8)] >> (p % 8)) & 1u) != 0)
            ++held;
    return {held, pieces};
}

/// what the node told in its Hello, the address of a direct connection; the stream's endpoint first
struct Seen {
    QString agent, nick;
    std::uint64_t caps = 0;
    bool known = false;
    QString address;
    int port = 0;
};

Seen seenOf(const SourceInfo& s)
{
    Seen seen;
    auto* eng = s_uploads->session().engine();
    if (eng)
        if (const auto hello = eng->peerHello(s.node, s.key)) {
            seen.agent = QString::fromStdString(hello->agent);
            seen.nick = QString::fromStdString(hello->nick);
            seen.caps = hello->caps;
            seen.known = true;
            if (hello->remote && s.key == ::ed3k::KeyKind::Node) {
                seen.address = ed3k::toQHostAddress(*hello->remote).toString();
                seen.port = hello->remote->port;
            }
        }
    if (s.endpoint && s.key == ::ed3k::KeyKind::Node) {
        seen.address = ed3k::toQHostAddress(*s.endpoint).toString();
        seen.port = s.endpoint->port;
    }
    return seen;
}

QCborMap sourceToCbor(const SourceInfo& s)
{
    const Seen seen = seenOf(s);
    const QString node = hexOf(s.node.bytes);
    const auto [held, pieces] = piecesOf(s);
    const auto [up, down] = s_uploads->peerBytes(s.key, s.node);
    Q_UNUSED(up)
    QCborArray partMap;
    if (!s.have.empty()) {
        // buildSourcePartMap's codes: 1 both have, 2 it has / we need, 3 asked, 4 receiving
        std::set<std::size_t> asked;
        for (const auto off : s.asked)
            asked.insert(std::size_t(off / kPiece));
        const std::size_t active = s.holding && !s.asked.empty() ? std::size_t(*std::ranges::min_element(s.asked) / kPiece)
                                                                 : SIZE_MAX;
        for (int p = 0; p < pieces; ++p) {
            const auto i = std::size_t(p);
            const bool has = i / 8 >= s.have.size() || ((s.have[i / 8] >> (i % 8)) & 1u) != 0;
            const bool ours = i < s.ownPieces.size() && s.ownPieces[i];
            // was: partMap.append(has ? 2 : 0) (our own pieces not told)
            partMap.append(!has ? 0 : ours ? 1 : i == active ? 4 : asked.contains(i) ? 3 : 2);
        }
    }
    QCborMap m{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        {QStringLiteral("userName"), seen.nick.isEmpty() ? node.left(8) : seen.nick},
        {QStringLiteral("software"), softwareOf(seen.agent)},
        {QStringLiteral("softwareId"), -1},
        {QStringLiteral("userHash"), keyOf(s.key, s.node)},
        {QStringLiteral("downloadState"), s.otherFile ? QStringLiteral("OnQueue")
                                          : s.holding ? QStringLiteral("Downloading") : QStringLiteral("OnQueue")},
        {QStringLiteral("a4af"), s.otherFile},
        {QStringLiteral("reqFileName"), s.otherName},
        {QStringLiteral("transferredDown"), qint64(std::min<std::uint64_t>(down, INT64_MAX))},
        {QStringLiteral("sessionDown"), qint64(std::min<std::uint64_t>(down, INT64_MAX))},
        {QStringLiteral("availPartCount"), held},
        {QStringLiteral("partCount"), pieces},
        {QStringLiteral("addr"), seen.address},
        {QStringLiteral("port"), seen.port},
        {QStringLiteral("cc"), seen.address.isEmpty() ? QString() : countryCodeOf(QHostAddress(seen.address))},
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(s.key)},
        {QStringLiteral("sourceFrom"), -1},   // no eD2K source kind ("?")
    };
    if (!partMap.isEmpty())
        m.insert(QStringLiteral("sourcePartMap"), partMap);
    return m;
}

/// details() of a download source that is not in our upload queue
QCborMap sourceDetailsOf(const SourceInfo& s)
{
    const Seen seen = seenOf(s);
    const bool anon = s.key == ::ed3k::KeyKind::Anon;
    const QString node = hexOf(s.node.bytes);
    constexpr std::uint64_t kCapEd2k = 1u << 6;   // Protocol.md §2 bit 6 ED2K
    const auto [up, down] = s_uploads->peerBytes(s.key, s.node);
    QCborMap m{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        {QStringLiteral("userName"), seen.nick.isEmpty() ? node.left(8) : seen.nick},
        {QStringLiteral("nick"), seen.nick},
        {QStringLiteral("userHash"), keyOf(s.key, s.node)},
        {QStringLiteral("nodeId"), node},
        {QStringLiteral("anonymous"), anon},
        {QStringLiteral("software"), softwareOf(seen.agent)},
        {QStringLiteral("helloKnown"), seen.known},
        {QStringLiteral("ed2k"), (seen.caps & kCapEd2k) != 0},
        {QStringLiteral("addr"), seen.address},
        {QStringLiteral("port"), seen.port},
        {QStringLiteral("cc"), seen.address.isEmpty() ? QString() : countryCodeOf(QHostAddress(seen.address))},
        {QStringLiteral("isConnected"), seen.known},
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(s.key)},
        {QStringLiteral("sentTotal"), qint64(std::min<std::uint64_t>(up, INT64_MAX))},
        {QStringLiteral("receivedTotal"), qint64(std::min<std::uint64_t>(down, INT64_MAX))},
    };
    if (!anon)
        m.insert(QStringLiteral("ed2kUserHash"), hexOf(::ed3k::userHash(s.node)));
    return m;
}

/// what details() shows of our download from the node
QCborMap sourcePart(const SourceInfo& s)
{
    const auto [held, pieces] = piecesOf(s);
    return QCborMap{
        {QStringLiteral("fileName"), s.fileName},
        {QStringLiteral("fileSize"), qint64(std::min<std::uint64_t>(s.size, INT64_MAX))},
        {QStringLiteral("root"), hexOf(s.root.bytes)},
        {QStringLiteral("state"), s.holding ? QStringLiteral("Downloading") : QStringLiteral("OnQueue")},
        {QStringLiteral("haveKnown"), !s.have.empty()},
        {QStringLiteral("parts"), pieces},
        {QStringLiteral("availParts"), held},
    };
}
#endif

} // namespace

void setUploads(ed3k::Ed3kUploads* uploads)
{
    s_uploads = uploads;
}

void appendRows(QCborArray& uploading, QCborArray& waiting)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& r : s_uploads->uploading())
        uploading.append(rowToCbor(r, now));
    for (const auto& r : s_uploads->waiting())
        waiting.append(rowToCbor(r, now));
#else
    (void)uploading;
    (void)waiting;
#endif
}

bool isDetailsKey(const QString& key)
{
    return key.startsWith(QLatin1String("ed3k:"));
}

std::optional<QCborMap> details(const QString& key)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads || !isDetailsKey(key))
        return std::nullopt;
    QStringView rest = QStringView(key).mid(5);
    const bool anon = rest.startsWith(QLatin1String("anon:"));
    if (anon)
        rest = rest.mid(5);
    const QByteArray raw = QByteArray::fromHex(rest.toLatin1());
    if (raw.size() != 16)
        return std::nullopt;
    const auto wanted = anon ? ::ed3k::KeyKind::Anon : ::ed3k::KeyKind::Node;
    const auto match = [&](const ed3k::Ed3kUploads::Row& r) {
        return r.key == wanted && std::equal(r.node.bytes.begin(), r.node.bytes.end(), raw.begin());
    };
    ::ed3k::NodeId node;
    std::copy(raw.begin(), raw.end(), node.bytes.begin());
    // also a source of ours: what we fetch from it
    const auto source = ed3k::Ed3kJoinBase::sourceAt(s_uploads->session(), wanted, node);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& list : {s_uploads->uploading(), s_uploads->waiting()})
        for (const auto& r : list)
            if (match(r)) {
                QCborMap m = detailsOf(r, now);
                if (source)
                    m.insert(QStringLiteral("source"), sourcePart(*source));
                return m;
            }
    if (!source)
        return std::nullopt;
    QCborMap m = sourceDetailsOf(*source);
    m.insert(QStringLiteral("source"), sourcePart(*source));
    return m;
#else
    (void)key;
    return std::nullopt;
#endif
}

QCborArray downloadSources(const QByteArray& root)
{
    QCborArray out;
#ifdef EMULE_HAVE_ED3K
    ::ed3k::Root r;
    if (!s_uploads || root.size() != qsizetype(r.bytes.size()))
        return out;
    std::copy(root.begin(), root.end(), r.bytes.begin());
    for (const auto& s : ed3k::Ed3kJoinBase::sourcesAt(s_uploads->session(), r))
        out.append(sourceToCbor(s));
#else
    (void)root;
#endif
    return out;
}

void appendDownloading(QCborArray& out)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return;
    for (const auto& s : ed3k::Ed3kJoinBase::downloadingAt(s_uploads->session())) {
        QCborMap m = sourceToCbor(s);
        // GetDownloadClients rows: the plain node ID (parseClient adds the details prefix) and our file
        m.insert(QStringLiteral("userHash"), hexOf(s.node.bytes));
        m.insert(QStringLiteral("reqFileName"), s.fileName);
        m.insert(QStringLiteral("isConnected"), true);
        out.append(m);
    }
#else
    (void)out;
#endif
}

std::optional<QCborMap> detailsByEd2kHash(const QByteArray& userHash)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads || userHash.size() != 16)
        return std::nullopt;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& list : {s_uploads->uploading(), s_uploads->waiting()})
        for (const auto& r : list) {
            if (r.key != ::ed3k::KeyKind::Node)
                continue;
            const auto derived = ::ed3k::userHash(r.node);
            if (std::equal(derived.begin(), derived.end(), reinterpret_cast<const std::uint8_t*>(userHash.constData())))
                return detailsOf(r, now);
        }
    return std::nullopt;
#else
    (void)userHash;
    return std::nullopt;
#endif
}

} // namespace eMule::Ed3kUploadsBridge
