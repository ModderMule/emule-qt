/// @file Ed3kUploadsBridge.cpp
/// @brief The ed3k upload queue in GetUploads — implementation.

#include "Ed3kUploadsBridge.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QHostAddress>
#include <QRegularExpression>

#include <algorithm>
#include <set>
#include <span>

#include "BtBridge.h"
#include "Ed3kHosterBridge.h"
#include "UsenetBridge.h"
#include "app/AppContext.h"
#include "files/PartFile.h"
#include "geo/IP2Country.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"

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

// Hello.caps bits (Protocol.md §2)
constexpr std::uint64_t kCapRouter = 1u << 0;   // in routing tables: open and routing
constexpr std::uint64_t kCapIndex = 1u << 2;    // index node (§11)
constexpr std::uint64_t kCapEd2k = 1u << 6;     // speaks eD2K too

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

constexpr std::uint64_t kPiece = 4 * 1024 * 1024;
constexpr std::uint64_t kBlock = 256 * 1024;   ///< ed3k block (§12.2)

QCborMap rowToCbor(const ed3k::Ed3kUploads::Row& r, qint64 nowMs)
{
    const QString node = hexOf(r.node.bytes);
    const qint64 held = std::max<qint64>(0, nowMs - r.since);
    QCborMap m{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        // Hello.nick: the peer's user name (its Options); else the node ID's start
        {QStringLiteral("userName"), r.nick.isEmpty() ? node.left(8) : r.nick},
        {QStringLiteral("nick"), r.nick},
        {QStringLiteral("userHash"), node},
        {QStringLiteral("software"), softwareOf(r.agent)},
        // Hello seen: caps and agent are known
        {QStringLiteral("helloKnown"), !r.agent.isEmpty() || r.caps != 0},
        {QStringLiteral("ed2k"), (r.caps & kCapEd2k) != 0},
        {QStringLiteral("router"), (r.caps & kCapRouter) != 0},
        {QStringLiteral("indexNode"), (r.caps & kCapIndex) != 0},
        {QStringLiteral("uploadState"), r.holding ? QStringLiteral("Transferring") : QStringLiteral("OnQueue")},
        {QStringLiteral("uploadFileName"), r.fileName},
        {QStringLiteral("uploadFileSize"), qint64(std::min<std::uint64_t>(r.fileSize, INT64_MAX))},
        {QStringLiteral("root"), hexOf(r.root.bytes)},
        {QStringLiteral("addr"), r.address},
        {QStringLiteral("port"), int(r.port)},
        {QStringLiteral("cc"), r.address.isEmpty() ? QString() : countryCodeOf(QHostAddress(r.address))},
        {QStringLiteral("sessionUp"), qint64(std::min<std::uint64_t>(r.bytes, INT64_MAX))},
        {QStringLiteral("upDatarate"), qint64(std::min<std::uint64_t>(r.rate, INT64_MAX))},
        {QStringLiteral("waitStartTime"), r.holding ? qint64(0) : held},
        {QStringLiteral("uploadStartDelay"), r.holding ? held : qint64(0)},
        {QStringLiteral("queueRank"), r.place},
        // was: {QStringLiteral("isConnected"), true},
        {QStringLiteral("isConnected"), r.connected},
        {QStringLiteral("lan"), r.lan},
        {QStringLiteral("due"), r.due},
        // hops.md §1: "Anonymous" rows have no address; the GUI shows them as anonymous
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(r.key)},
        // the root belongs to a torrent/usenet/hoster join: the GUI badges the node icon
        {QStringLiteral("join"), s_uploads ? ed3k::Ed3kJoinBase::networkJoinAt(s_uploads->session(), r.root) : QString()},
    };
    // Uploaded / Downloaded columns: file bytes with the node this daemon run, the running turn too
    if (s_uploads) {
        const auto [up, down] = s_uploads->peerBytes(r.key, r.node);
        m.insert(QStringLiteral("uploadedTotal"), qint64(std::min<std::uint64_t>(up + (r.holding ? r.bytes : 0), INT64_MAX)));
        m.insert(QStringLiteral("downloadedTotal"), qint64(std::min<std::uint64_t>(down, INT64_MAX)));
    }
    // Upload Status bar: the downloader's pieces (FileRequest.have), 4 MiB parts; a slot holder's
    // next and sent blocks as eD2K draws them
    const bool sending = r.holding && (r.nextOffset || !r.sent.empty());
    if ((r.haveKnown || sending) && r.fileSize > 0) {
        const auto pieces = qint64(std::min<std::uint64_t>((r.fileSize + kPiece - 1) / kPiece, INT32_MAX));
        QCborMap bar{
            {QStringLiteral("fileSize"), qint64(std::min<std::uint64_t>(r.fileSize, INT64_MAX))},
            {QStringLiteral("partSize"), qint64(kPiece)},
            {QStringLiteral("parts"), r.have},
        };
        if (sending) {
            // whole parts about to go out: the next block and the latest one sent
            QCborArray next;
            if (r.nextOffset)
                next.append(qint64(*r.nextOffset / kPiece));
            if (!r.sent.empty())
                next.append(qint64(r.sent.back() / kPiece));
            bar.insert(QStringLiteral("next"), next);
            // blocks of the stream, merged into inclusive byte ranges
            std::vector<std::uint64_t> offsets = r.sent;
            std::ranges::sort(offsets);
            QCborArray sent;
            std::uint64_t start = 0, end = 0;
            bool open = false;
            for (const std::uint64_t at : offsets) {
                if (at >= r.fileSize)
                    continue;
                const std::uint64_t last = std::min(at + kBlock, r.fileSize) - 1;
                if (open && at <= end + 1) {
                    end = std::max(end, last);
                    continue;
                }
                if (open)
                    sent.append(QCborArray{qint64(start), qint64(end)});
                start = at, end = last, open = true;
            }
            if (open)
                sent.append(QCborArray{qint64(start), qint64(end)});
            bar.insert(QStringLiteral("sent"), sent);
        }
        m.insert(QStringLiteral("upPartCount"), pieces);
        m.insert(QStringLiteral("upStatus"), bar);
    }
    return m;
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

QString keyOf(::ed3k::KeyKind key, const ::ed3k::NodeId& node)
{
    return (key == ::ed3k::KeyKind::Anon ? QStringLiteral("ed3k:anon:") : QStringLiteral("ed3k:")) + hexOf(node.bytes);
}

/// pieces of the file the source holds (FileStatus.have: a missing bit is held)
std::pair<int, int> piecesOf(const SourceInfo& s)
{
    const auto pieces = int(std::min<std::uint64_t>(s.size / kPiece + (s.size % kPiece != 0 ? 1 : 0), INT32_MAX));
    if (!s.haveKnown)
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
    if (s.haveKnown) {   // was: !have.empty() (an empty bitmap is the complete file)
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
        // Completed of the row: this file only (transferredDown: all files, Client Details)
        {QStringLiteral("fileDown"), qint64(std::min<std::uint64_t>(s_uploads->peerFileDown(s.key, s.node, s.root), INT64_MAX))},
        // only a held slot moves bytes; the 10 s average lingers after it ends
        {QStringLiteral("datarate"), s.holding && !s.otherFile ? qint64(s_uploads->downRate(s.key, s.node)) : 0},
        {QStringLiteral("availPartCount"), held},
        {QStringLiteral("partCount"), pieces},
        {QStringLiteral("addr"), seen.address},
        {QStringLiteral("port"), seen.port},
        {QStringLiteral("cc"), seen.address.isEmpty() ? QString() : countryCodeOf(QHostAddress(seen.address))},
        {QStringLiteral("key"), ed3k::Ed3kHide::keyName(s.key)},
        {QStringLiteral("sourceFrom"), -1},   // no eD2K source kind ("?")
    };
    if (s.join == QLatin1String("torrent") || s.join == QLatin1String("usenet") || s.join == QLatin1String("hoster"))
        m.insert(QStringLiteral("join"), s.join);
    if (!partMap.isEmpty())
        m.insert(QStringLiteral("sourcePartMap"), partMap);
    // as eD2K's: other files of ours wanting this node (A4AF mark + tooltip)
    if (!s.otherWants.isEmpty()) {
        m.insert(QStringLiteral("hasOtherRequests"), true);
        m.insert(QStringLiteral("a4afFiles"), QCborArray::fromStringList(s.otherWants));
    }
    return m;
}

/// details() of a download source that is not in our upload queue
QCborMap sourceDetailsOf(const SourceInfo& s)
{
    const Seen seen = seenOf(s);
    const bool anon = s.key == ::ed3k::KeyKind::Anon;
    const QString node = hexOf(s.node.bytes);
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
        {QStringLiteral("router"), (seen.caps & kCapRouter) != 0},
        {QStringLiteral("indexNode"), (seen.caps & kCapIndex) != 0},
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
        {QStringLiteral("haveKnown"), s.haveKnown},
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
    // slots counted with eD2K's on one line (UploadQueue::setForeignSlots): numbered after them
    qint64 slot = 0;
    for (const auto& v : std::as_const(uploading))
        slot = std::max(slot, v.toMap().value(QStringLiteral("slot")).toInteger());
    for (const auto& r : s_uploads->uploading()) {
        QCborMap m = rowToCbor(r, now);
        m.insert(QStringLiteral("slot"), ++slot);
        uploading.append(m);
    }
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
        // was: raw.begin(): signed char vs uint8, a node byte >= 0x80 never matched (Details empty)
        return r.key == wanted
            && std::equal(r.node.bytes.begin(), r.node.bytes.end(), reinterpret_cast<const std::uint8_t*>(raw.constData()));
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

#ifdef EMULE_HAVE_ED3K
namespace {

/// {network, name, id[, host]} of job @p owner of the join tagged @p tag; empty: unknown
QCborMap jobAlias(const QString& tag, const QString& owner)
{
    if (tag == QLatin1String("torrent"))
        return BtBridge::aliasOf(owner);
    if (tag == QLatin1String("usenet"))
        return UsenetBridge::aliasOf(owner);
    if (tag == QLatin1String("hoster"))
        return Ed3kHosterBridge::aliasOf(owner);
    if (tag != QLatin1String("ed2k"))
        return {};
    // owner: the eD2K hash (Ed3kEd2kJoin::ownerOf)
    QCborMap m{{QStringLiteral("network"), QStringLiteral("ed2k")}, {QStringLiteral("id"), owner}};
    const QByteArray md4 = QByteArray::fromHex(owner.toLatin1());
    if (theApp.downloadQueue && md4.size() == 16)
        if (const auto* pf = theApp.downloadQueue->fileByID(reinterpret_cast<const uint8*>(md4.constData())))
            m.insert(QStringLiteral("name"), pf->fileName());
    return m;
}

} // namespace
#endif

QCborArray aliasesOf([[maybe_unused]] const QByteArray& root)
{
    QCborArray out;
#ifdef EMULE_HAVE_ED3K
    ::ed3k::Root r;
    if (!s_uploads || root.size() != qsizetype(r.bytes.size()))
        return out;
    std::copy(root.begin(), root.end(), r.bytes.begin());
    for (const auto& job : ed3k::Ed3kJoinBase::aliasJobsAt(s_uploads->session(), r))
        if (const QCborMap m = jobAlias(job.tag, job.owner); !m.isEmpty())
            out.append(m);
#endif
    return out;
}

QCborArray joinsOf([[maybe_unused]] const QByteArray& root)
{
    QCborArray out;
#ifdef EMULE_HAVE_ED3K
    ::ed3k::Root r;
    if (!s_uploads || root.size() != qsizetype(r.bytes.size()))
        return out;
    std::copy(root.begin(), root.end(), r.bytes.begin());
    const auto states = ed3k::Ed3kJoinBase::joinStatesAt(s_uploads->session(), r);
    // every network, also one whose join does not run (BitTorrent off, not built)
    for (const QString tag : {QStringLiteral("ed2k"), QStringLiteral("torrent"), QStringLiteral("usenet"), QStringLiteral("hoster")}) {
        using Kind = ed3k::Ed3kJoinBase::JoinState::Kind;
        QCborMap row{{QStringLiteral("network"), tag}};
        const auto st = std::ranges::find(states, tag, &ed3k::Ed3kJoinBase::JoinState::tag);
        if (st == states.end() || st->kind == Kind::Off) {
            row.insert(QStringLiteral("state"), QStringLiteral("off"));
            row.insert(QStringLiteral("reason"), QStringLiteral("notRunning"));
            out.append(row);
            continue;
        }
        static constexpr const char* kStates[] = {"joined", "skipped", "available", "off"};
        row.insert(QStringLiteral("state"), QString::fromLatin1(kStates[int(st->kind)]));
        if (!st->reason.isEmpty())
            row.insert(QStringLiteral("reason"), st->reason);
        if (st->anon)
            row.insert(QStringLiteral("key"), QStringLiteral("anon"));
        QCborArray jobs;
        for (const QString& owner : st->owners)
            if (const QCborMap m = jobAlias(tag, owner); !m.isEmpty())
                jobs.append(m);
        if (!jobs.isEmpty())
            row.insert(QStringLiteral("jobs"), jobs);
        out.append(row);
    }
#endif
    return out;
}

void appendNetworkDownloading([[maybe_unused]] QCborArray& out)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return;
    // single transfer mode: every job fetching now (unjoined ones get paused), also joined ones
    // whose ed3k member is not open yet (set still searched) — as appendNetworkUploading
    QStringList torrents = thePrefs.btSingleTransferOn() ? BtBridge::jobIds(true) : QStringList();
    QStringList usenet = thePrefs.usenetSingleTransferOn() ? UsenetBridge::jobIds(true) : QStringList();
    QStringList hoster = thePrefs.hosterSingleTransferOn() ? Ed3kHosterBridge::owners() : QStringList();
    for (const auto& job : ed3k::Ed3kJoinBase::networkJobsAt(s_uploads->session())) {
        QStringList* ids = job.tag == QLatin1String("torrent") ? &torrents
                         : job.tag == QLatin1String("usenet")  ? &usenet
                         : job.tag == QLatin1String("hoster")  ? &hoster
                                                               : nullptr;
        if (ids && !ids->contains(job.owner))
            *ids << job.owner;
    }
    for (const QString& id : std::as_const(torrents))
        BtBridge::appendDownloading(out, id);
    for (const QString& id : std::as_const(usenet))
        UsenetBridge::appendDownloading(out, id);
    for (const QString& id : std::as_const(hoster))
        Ed3kHosterBridge::appendDownloading(out, id);
#endif
}

void appendNetworkUploading([[maybe_unused]] QCborArray& uploading)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return;
    QStringList ids = thePrefs.btSingleTransferOn() ? BtBridge::jobIds(false) : QStringList();
    for (const auto& job : ed3k::Ed3kJoinBase::networkJobsAt(s_uploads->session()))
        if (job.tag == QLatin1String("torrent") && !ids.contains(job.owner))
            ids << job.owner;
    const qsizetype first = uploading.size();
    for (const QString& id : std::as_const(ids))
        BtBridge::appendUploading(uploading, id);
    // numbered after eD2K's and ed3k's slots, as ed3k's after eD2K's
    qint64 slot = 0;
    for (qsizetype i = 0; i < first; ++i)
        slot = std::max(slot, uploading.at(i).toMap().value(QStringLiteral("slot")).toInteger());
    for (qsizetype i = first; i < uploading.size(); ++i) {
        QCborMap m = uploading.at(i).toMap();
        m.insert(QStringLiteral("slot"), ++slot);
        uploading[i] = m;
    }
#endif
}

QCborArray jobSources([[maybe_unused]] const QString& tag, [[maybe_unused]] const QString& owner)
{
    QCborArray out;
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return out;
    for (const auto& s : ed3k::Ed3kJoinBase::jobSourcesAt(s_uploads->session(), tag, owner)) {
        QCborMap m = sourceToCbor(s);
        m.insert(QStringLiteral("fileName"), s.fileName);
        out.append(m);
    }
#endif
    return out;
}

bool jobJoined([[maybe_unused]] const QString& tag, [[maybe_unused]] const QString& owner)
{
#ifdef EMULE_HAVE_ED3K
    if (s_uploads)
        return ed3k::Ed3kJoinBase::joinedAt(s_uploads->session(), tag, owner).value_or(false);
#endif
    return false;
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
