/// @file Ed3kJoinBase.cpp
/// @brief The ed3k side shared by every join — implementation.

#include "Ed3kJoinBase.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "files/PartFile.h"
#include "prefs/Preferences.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/FileRelocate.h"
#include "utils/ReadOnceHint.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/PathUtils.h"
#include "utils/SafeRemove.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <iterator>
#include <chrono>
#include <cstring>

namespace eMule::ed3k {

namespace {

/// What comes of a source of Ed3kDownloadsWeb (a fetch, its blocks, its end): the anonymous
/// node's too (http-source.md §3.2)
bool isHttpSourceEvent(const ffi::Event& event)
{
    namespace ev = ffi::event;
    return std::holds_alternative<ev::HttpBlock>(event) || std::holds_alternative<ev::HttpUnverified>(event)
           || std::holds_alternative<ev::HttpWhole>(event) || std::holds_alternative<ev::HttpFetched>(event)
           || std::holds_alternative<ev::HttpDropped>(event);
}

constexpr int kSaveDelayMs = 5000;
/// queued priority switches at one node at least this far apart (Protocol.md §12.5, 30 min)
constexpr auto kQueuedSwitchGap = std::chrono::minutes(30);
/// read queue of the blocks served (WantBlock, VerifyBlock); never an owner id
const QString kUploadReads = QStringLiteral("\x01up");
constexpr std::size_t kMaxStreams = 4;
constexpr qint64 kHashChunk = 1 << 20;
/// sources searched again while no stream holds a slot
constexpr int kResearchMs = 10 * 60 * 1000;
/// candidates of a set search looked at per try (the engine gives at most 3, alias.md §4)
constexpr int kSetCandidates = 8;
const QString kKeptIndex = QStringLiteral("kept.tsv");
/// ALIAS records to publish again: u32-LE length + Alias, as publishAlias takes them
const QString kKeptRecords = QStringLiteral("records.bin");
/// alias.md §3 "Deleted file": one row per file, per network
const QString kDeletedFile = QStringLiteral("deleted.tsv");
constexpr int kDeletedTimerMs = 60 * 60 * 1000;
/// alias.md §3: 365 days at most
constexpr int kDeletedDaysMax = 36500;   // was 365: the age limit is a MAY (alias.md §3)
/// a kept member's block hashes with the size and mtime they were read at: a restart
/// shares it without reading it again (report 23: every seeding torrent read at each start)
constexpr quint32 kKeptHashesMagic = 0x454B4831;   // "EKH1"

/// Own threads for hashing: they run at lowered priority, which is never undone (#136).
QThreadPool* hashPool()
{
    static QThreadPool* pool = [] {
        // the app's child: waited for at exit, as the global pool was
        auto* p = new QThreadPool(QCoreApplication::instance());
        // was: p->setMaxThreadCount(2);
        // ed3k's share list hashes one file per disk (as eD2K, up to 4 disks); the joins too
        p->setMaxThreadCount(4);
        return p;
    }();
    return pool;
}

/// eD2K file hash: MD4 per 9,728,000 B part; more than one part (or exactly one full part):
/// MD4 of the part hashes, a size on a part boundary adding the MD4 of an empty part.
class Ed2kHasher {
public:
    void update(const char* data, qint64 len)
    {
        while (len > 0) {
            const qint64 take = std::min<qint64>(len, qint64(PARTSIZE) - m_inPart);
            m_part.addData(QByteArrayView(data, take));
            m_inPart += take;
            data += take;
            len -= take;
            if (m_inPart == qint64(PARTSIZE)) {
                m_parts += m_part.result();
                m_part.reset();
                m_inPart = 0;
            }
        }
    }
    [[nodiscard]] QByteArray finish()
    {
        if (m_parts.isEmpty())
            return m_part.result();   // one part under PARTSIZE (or empty)
        m_parts += m_part.result();   // the rest, or MD4("") on a part boundary
        return QCryptographicHash::hash(m_parts, QCryptographicHash::Md4);
    }

private:
    QCryptographicHash m_part{QCryptographicHash::Md4};
    QByteArray m_parts;
    qint64 m_inPart = 0;
};

/// Hashes a file on a worker; nullopt when unreadable or cancelled. @p ed2k: its eD2K hash too.
std::optional<std::pair<ffi::Hashed, ffi::Bytes>> hashFile(const QString& path, const std::atomic<bool>& cancel,
                                                          QByteArray* ed2k = nullptr,
                                                          std::atomic<int>* permille = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    ffi::Hasher h;
    Ed2kHasher md4;
    QByteArray chunk;
    // #136: under the eD2K hashing disk limit too; it read at full speed beside it
    DiskLoadLimiter& diskLoad = DiskLoadLimiter::thisThread();
    diskLoad.setFile(path);   // one budget with eD2K's hashing of the same disk
    ReadOnceHint readOnce(f);
    while (!f.atEnd()) {
        if (cancel.load())
            return std::nullopt;
        {
            const DiskLoadLimiter::Read timed(diskLoad);
            chunk = f.read(kHashChunk);
        }
        if (chunk.isEmpty() && f.error() != QFileDevice::NoError)
            return std::nullopt;
        if (!h.update({reinterpret_cast<const std::byte*>(chunk.constData()), std::size_t(chunk.size())}))
            return std::nullopt;
        if (ed2k)
            md4.update(chunk.constData(), chunk.size());
        readOnce.readUpTo(f.pos());
        if (permille && f.size() > 0)
            permille->store(int(f.pos() * 1000 / f.size()), std::memory_order_relaxed);
    }
    if (ed2k)
        *ed2k = md4.finish();
    auto done = h.finish();
    if (!done)
        return std::nullopt;
    const auto bytes = done->hashes.bytes();
    ffi::Bytes hashes(bytes.begin(), bytes.end());
    return std::pair{std::move(*done), std::move(hashes)};
}

QString blobFile(const ffi::Root& root)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(root.bytes.data()), qsizetype(root.bytes.size())).toHex())
           + QStringLiteral(".blob");
}

QString keptHashesFile(int member)
{
    return QStringLiteral("m%1.hashes").arg(member);
}

QByteArray keptHashesBlob(std::uint64_t size, qint64 mtimeMs, const ffi::Root& root, const ffi::Bytes& hashes)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << kKeptHashesMagic << quint64(size) << qint64(mtimeMs)
      << QByteArray(reinterpret_cast<const char*>(root.bytes.data()), qsizetype(root.bytes.size()))
      << QByteArray(reinterpret_cast<const char*>(hashes.data()), qsizetype(hashes.size()));
    return out;
}

/// The hashes in @p blob when they are of @p root and the file is still @p size, @p mtimeMs.
std::optional<ffi::Bytes> keptHashesOf(const QByteArray& blob, std::uint64_t size, qint64 mtimeMs, const ffi::Root& root)
{
    if (blob.isEmpty())
        return std::nullopt;
    QDataStream s(blob);
    s.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint64 keptSize = 0;
    qint64 keptMtime = 0;
    QByteArray keptRoot;
    QByteArray hashes;
    s >> magic >> keptSize >> keptMtime >> keptRoot >> hashes;
    if (s.status() != QDataStream::Ok || magic != kKeptHashesMagic || keptSize != size || keptMtime != mtimeMs
        || hashes.isEmpty() || keptRoot.size() != qsizetype(root.bytes.size())
        || std::memcmp(keptRoot.constData(), root.bytes.data(), root.bytes.size()) != 0)
        return std::nullopt;
    const auto* b = reinterpret_cast<const std::byte*>(hashes.constData());
    return ffi::Bytes(b, b + hashes.size());
}

std::optional<ffi::Root> rootOfHex(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    if (raw.size() != 32)
        return std::nullopt;
    ffi::Root r;
    std::memcpy(r.bytes.data(), raw.constData(), r.bytes.size());
    return r;
}

std::vector<ffi::Bytes> readRecords(const QString& path)
{
    const QByteArray raw = readBlob(path);
    std::vector<ffi::Bytes> out;
    qsizetype at = 0;
    while (at + 4 <= raw.size()) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(raw.constData() + at);
        const qsizetype len = qsizetype(std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24));
        at += 4;
        if (len < 0 || at + len > raw.size())
            break;   // damaged tail: what came before stays
        const auto* b = reinterpret_cast<const std::byte*>(raw.constData() + at);
        out.emplace_back(b, b + len);
        at += len;
    }
    return out;
}

/// On the background writer (report 43); readRecords reads a queued copy back.
void writeRecords(const QString& path, const std::vector<ffi::Bytes>& records)
{
    std::vector<std::byte> out;
    for (const auto& r : records) {
        const auto len = std::uint32_t(r.size());
        for (int i = 0; i < 4; ++i)
            out.push_back(std::byte((len >> (8 * i)) & 0xff));
        out.insert(out.end(), r.begin(), r.end());
    }
    writeBlobLater(path, out);
}

/// a name in one TSV field
QString field(QString s)
{
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s;
}

} // namespace

// The set tried for an owner (§4.2): from which search, and its publishers.
struct Ed3kJoinBase::SetTry {
    // was: std::uint32_t search = 0; but the engine's first search is ID 0 (tryNextSet recursed forever)
    std::optional<std::uint32_t> search;
    std::optional<ffi::Root> root;
    std::vector<ffi::PublisherKey> publishers;
};

// What is kept of an owner for a restart (kept.tsv: "b root name", "m member root size name").
struct Ed3kJoinBase::Kept {
    struct Blob {
        ffi::Root root;
        QString name;
    };
    struct Member {
        int member = -1;
        ffi::Root root;
        std::uint64_t size = 0;
        QString name;
        QString path;   ///< keepMemberPaths; empty: the job's own path (keptMemberPath)
    };
    struct Set {
        ffi::Root root;   ///< the manifest, kept as a blob
        QString name;
    };
    std::vector<Blob> blobs;
    std::vector<Member> members;
    /// the set published at completion (MEMBER rows, file-set.md §6)
    std::optional<Set> set;
};

// Protocol.md §12.5 "One entry per downloader": at most one file stream per uploader, across
// every join of a session (a second FileRequest to the node replaces the first stream).
// Per key of this machine (hops.md §1): the uploader sees the node and the anonymous node as two
// downloaders. Daemon thread only.
struct Ed3kJoinBase::NodeTurns {
    struct Holder {
        Ed3kJoinBase* join = nullptr;
        ffi::Root root;
        std::uint64_t stream = 0;
        ffi::Source source;
    };
    struct Want {
        Ed3kJoinBase* join = nullptr;
        ffi::Root root;
        ffi::Source source;
        int priority = 0;
        std::uint64_t seq = 0;
        bool swapped = false;   ///< swapTurnsTo: first, before any priority
    };
    // was: std::map<ffi::NodeId, Holder> holders; (and waiting, queuedSwitch by NodeId)
    std::map<TurnNode, Holder> holders;
    std::map<TurnNode, std::vector<Want>> waiting;
    std::uint64_t seq = 0;
    int users = 0;
    /// every join of the session (the shared block-request depth)
    std::set<Ed3kJoinBase*> joins;
    /// last priority switch while queued, per node: §12.5 counts it in the uploader's 30 min
    /// budget, a 2nd one within it is BUSY and the uploader keeps the old stream
    std::map<TurnNode, std::chrono::steady_clock::time_point> queuedSwitch;
    /// #224: files found on disk by root (addFoundCopy)
    struct Found {
        QString path;
        std::uint64_t size = 0;
        qint64 mtimeMs = -1;
    };
    std::map<ffi::Root, Found> found;
};

Ed3kJoinBase::Ed3kJoinBase(Ed3kSession& session, const QString& configDir, QString tag, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_configDir(configDir)
    , m_tag(std::move(tag))
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &Ed3kJoinBase::saveDirty);
    m_researchTimer.setInterval(kResearchMs);
    connect(&m_researchTimer, &QTimer::timeout, this, &Ed3kJoinBase::onResearchTimer);
    m_deletedTimer.setInterval(kDeletedTimerMs);
    connect(&m_deletedTimer, &QTimer::timeout, this, &Ed3kJoinBase::onDeletedTimer);
    // was: connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kJoinBase::onEventPolled, Qt::DirectConnection);
    // the node key's events as before; the anonymous node's from keyedEventPolled (only an
    // anonCapable() join takes them): each event once
    connect(&m_session, &Ed3kSession::eventPolled, this,
            [this](const ffi::Event& ev, bool* handled) { onEventPolled(ev, ffi::KeyKind::Node, handled); },
            Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::keyedEventPolled, this,
            [this](const ffi::Event& ev, ffi::KeyKind key, bool* handled) {
                if (key == ffi::KeyKind::Anon)
                    onEventPolled(ev, key, handled);
            },
            Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kJoinBase::onSessionStatus);
    // declined-records.md §2: a list change takes back (or puts out again) what is published
    connect(&m_session, &Ed3kSession::declinedChanged, this, [this] {
        if (m_started)
            onDeclinedChanged();
    });
    connect(&m_session.hide(), &Ed3kHide::started, this, [this] { hideChanged(true); });
    connect(&m_session.hide(), &Ed3kHide::stopped, this, [this] { hideChanged(false); });
    // hops.md §1: moved to the node key (the user asked): a member left out joins again
    connect(&m_session.hide(), &Ed3kHide::keyMoved, this, [this](const ffi::Root& root, ffi::KeyKind key) {
        if (key == ffi::KeyKind::Node && m_started && !anonCapable())
            rejoinSkipped(root);
    });
    ++turns().users;
    turns().joins.insert(this);
}

Ed3kJoinBase::~Ed3kJoinBase()
{
    m_cancel->store(true);
    dropTurns(false);
    turns().joins.erase(this);
    if (--turns().users <= 0)
        turnsMap().erase(&m_session);
}

void Ed3kJoinBase::setHoldBack(bool on)
{
    if (on == m_holdBack)
        return;
    const bool running = m_started;
    if (running)
        stop();   // unshares; start() shares the kept items as the new setting says
    m_holdBack = on;
    if (on)
        withdrawKeptRecords();
    if (running)
        start();
}

void Ed3kJoinBase::setAnonKey(bool on)
{
    if (on == m_anonKey)
        return;
    const bool running = m_started;
    if (running) {
        stop();   // unshares; start() shares the kept items under their key again
        // published under the old key: taken back, they go out again under the new one
        withdrawKeptSets();
        withdrawKeptRecords();
    }
    m_anonKey = on;
    logInfo(QStringLiteral("%1 files %2 the hiding switch (hops.md §1)").arg(m_tag, on ? QStringLiteral("follow") : QStringLiteral("no longer follow")));
    if (running)
        start();
}

void Ed3kJoinBase::setSourceResearch(int everyMs, int minGapMs)
{
    m_researchTimer.setInterval(std::max(1, everyMs));
    m_researchGapMs = std::max(0, minGapMs);
}

bool Ed3kJoinBase::openDownload(const ffi::Root& root, std::uint64_t size, const QString& owner, int member)
{
    return open(root, size, owner, member, false);
}

bool Ed3kJoinBase::fetchBlob(const ffi::Root& root, std::uint64_t size, const QString& owner, int member)
{
    return open(root, size, owner, member, true);
}

void Ed3kJoinBase::stopDownload(const ffi::Root& root)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return;
    // its waits for a turn end; its nodes go to the next waiting download
    auto& turnsNow = turns();
    for (auto& [node, q] : turnsNow.waiting)
        std::erase_if(q, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; });
    std::erase_if(turnsNow.waiting, [](const auto& q) { return q.second.empty(); });
    const auto streams = it->second->streams;
    std::erase_if(m_streams, [&](const auto& s) { return s.second == root; });
    std::erase_if(m_sourceLookups, [&](const auto& s) { return s.second == root; });
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [stream, slot] : streams) {
            // was: (void)e->streamClose(t, stream);
            // switched in the turn (§12.5 "Several files in one turn"): the next file's
            // FileRequest replaces the stream at the core, the slot or position stays
            if (const auto node = nodeHeldBy(stream); node && passTurn(*node))
                continue;
            (void)e->streamClose(t, stream);
        }
        (void)e->downloadClose(t, root);
    }
    // was: std::erase_if(m_streams / m_sourceLookups) here (now above, before the turns pass)
    if (const auto again = m_downloads.find(root); again != m_downloads.end())
        m_downloads.erase(again);
}

void Ed3kJoinBase::stopDownloads(const QString& owner)
{
    std::vector<ffi::Root> roots;
    for (const auto& [root, d] : m_downloads)
        if (d->owner == owner)
            roots.push_back(root);
    for (const auto& root : roots)
        stopDownload(root);
    dropSkipped(owner);
}

const Ed3kJoinBase::Download* Ed3kJoinBase::downloadOf(const ffi::Root& root) const
{
    const auto it = m_downloads.find(root);
    return it == m_downloads.end() ? nullptr : it->second.get();
}

void Ed3kJoinBase::addSource(const ffi::Root& root, const ffi::Source& source)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end())
        return;
    Download& d = *it->second;
    // was: ToDo: turns at a source node (§12.5) for the anonymous node's downloads (done:
    // NodeTurns by key and node, the anonymous node's downloads take turns like the node's)
    // a node streaming this file already: e.g. a leaf with new relays since it was asked
    if (refreshStream(root, source))
        return;
    // was: if (d.streams.size() >= kMaxStreams || d.asked.contains(source.id)) (now by priority)
    if (d.streams.size() >= streamCap(d) || d.asked.contains(source.id))
        return;
    // one file stream per uploader (§12.5): another download holds the node, this one waits
    if (waitTurn(root, source)) {
        d.asked.insert(source.id);
        return;
    }
    (void)openStream(root, source);
    // was: the FileRequest here (now openStream, also for a turn granted)
    // auto stream = e->fileRequest(m_session.now(), source, ffi::Request{root, d.size, ffi::Have{}, 0, false, false});
    // if (!stream) {
    //     logDebug(QStringLiteral("source refused (error %1)").arg(errorName(stream.error())));
    //     return;
    // }
    // d.asked.insert(source.id);
    // d.streams[*stream] = false;
    // m_streams[*stream] = root;
    // pumpSession();
}

void Ed3kJoinBase::requestBlocks(const ffi::Root& root)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end())
        return;
    Download& d = *it->second;
    // #224: a local copy is read first; the network gets what it lacks after (endLocalCopy)
    if (d.copying)
        return;

    std::vector<std::uint64_t> wanted;
    if (d.inMemory) {
        for (std::uint64_t off = 0; off < d.size; off += kBlock)
            if (!d.got.contains(off))
                wanted.push_back(off);
    } else {
        wanted = wantedBlocks(d);
    }
    std::erase_if(wanted, [&](std::uint64_t off) { return d.outstanding.contains(off); });
    // nothing left for another stream: an idle slot goes to a download waiting for its node
    if (wanted.empty()) {
        std::vector<std::uint64_t> idle;
        for (const auto& [stream, slot] : d.streams)
            if (slot && std::ranges::none_of(d.outstanding, [s = stream](const auto& o) { return o.second == s; }))
                idle.push_back(stream);
        for (const auto stream : idle)
            (void)switchTurn(root, stream);
        return;
    }
    std::erase_if(wanted, [&](std::uint64_t off) { return !e->canVerify(root, off); });
    // was: std::erase_if(wanted, [&](std::uint64_t off) { return d.outstanding.contains(off) || !e->canVerify(root, off); });
    std::size_t granted = 0;   // not `slots`: a Qt macro
    for (const auto& [stream, slot] : d.streams)
        granted += slot ? 1 : 0;
    if (wanted.empty() || granted == 0)
        return;
    // was: std::clamp<std::size_t>(e->rangesHint(std::uint32_t(granted)), 1, 16) (each download
    // alone); now the session's depth shared by priority: a higher one asks deeper
    const std::size_t perStream = rangesPerStream(d, granted);
    auto next = wanted.begin();
    const auto t = m_session.now();
    for (const auto& [stream, slot] : d.streams) {
        if (!slot)
            continue;
        const auto busy = std::size_t(std::ranges::count_if(d.outstanding, [s = stream](const auto& o) { return o.second == s; }));
        std::vector<ffi::Range> ranges;
        for (; next != wanted.end() && busy + ranges.size() < perStream; ++next)
            ranges.push_back(ffi::Range{*next, std::uint32_t(std::min<std::uint64_t>(kBlock, d.size - *next))});
        if (ranges.empty())
            continue;
        if (auto ok = e->blockRequest(t, stream, ranges); ok) {
            for (const auto& r : ranges)
                d.outstanding[r.offset] = stream;
        }
    }
}

void Ed3kJoinBase::priorityChanged(const ffi::Root& root)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return;
    const int priority = downloadPriority(*it->second);
    auto& t = turns();
    std::vector<TurnNode> nodes;
    for (auto& [node, q] : t.waiting)
        for (auto& w : q)
            if (w.join == this && w.root == root) {
                w.priority = priority;
                nodes.push_back(node);
            }
    for (const auto& [node, h] : t.holders)
        if (h.join == this && h.root == root)
            nodes.push_back(node);
    // a node waited for whose holder is lower now, or one held with a higher waiter
    for (const auto& node : nodes)
        (void)yieldToHigher(node);
    // the depth of every download of the session follows the weights
    for (Ed3kJoinBase* j : t.joins) {
        std::vector<ffi::Root> roots;
        for (const auto& [r, d] : j->m_downloads)
            roots.push_back(r);
        for (const auto& r : roots)
            j->requestBlocks(r);
    }
    pumpSession();
}

void Ed3kJoinBase::sourceLimitChanged(const ffi::Root& root)
{
    if (m_downloads.contains(root))
        searchSourcesAgain(root, false);
}

int Ed3kJoinBase::swapTurnsTo(const ffi::Root& root)
{
    if (!m_downloads.contains(root))
        return 0;
    auto& t = turns();
    std::vector<TurnNode> nodes;
    for (auto& [node, q] : t.waiting) {
        const bool here = std::ranges::any_of(q, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; });
        if (!here)
            continue;
        // the latest swap wins at a node
        for (auto& w : q)
            w.swapped = w.join == this && w.root == root;
        nodes.push_back(node);
    }
    const auto now = std::chrono::steady_clock::now();
    for (const auto& node : nodes) {
        const auto h = t.holders.find(node);
        if (h == t.holders.end())
            continue;
        const NodeTurns::Holder holder = h->second;
        const auto held = holder.join->m_downloads.find(holder.root);
        if (held == holder.join->m_downloads.end())
            continue;
        // a slot holder keeps its turn (eD2K never swaps a downloading source)
        const auto slot = held->second->streams.find(holder.stream);
        if (slot != held->second->streams.end() && slot->second)
            continue;
        const auto last = t.queuedSwitch.find(node);
        if (!switchAllowed(true, last == t.queuedSwitch.end() ? std::nullopt : std::optional(last->second), now))
            continue;   // next in line there
        if (holder.join->switchTurn(holder.root, holder.stream))
            t.queuedSwitch[node] = now;
    }
    pumpSession();
    return int(nodes.size());
}

bool Ed3kJoinBase::switchAllowed(bool queued, std::optional<std::chrono::steady_clock::time_point> last,
                                 std::chrono::steady_clock::time_point now)
{
    return !queued || !last || now - *last >= kQueuedSwitchGap;
}

std::vector<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::sourcesOf(const ffi::Root& root) const
{
    std::vector<SourceInfo> out;
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return out;
    const Download& d = *it->second;
    const NodeTurns& t = turns();
    for (const auto& [node, h] : t.holders)
        if (h.join == this && h.root == root)
            if (auto info = holderInfo(node, h.root, h.stream, h.source))
                out.push_back(std::move(*info));
    // §12.5: its one stream serves another download of ours; this one waits for the turn
    for (const auto& [node, wants] : t.waiting) {
        const bool here = std::ranges::any_of(wants, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; });
        if (!here)
            continue;
        SourceInfo s;
        s.key = node.first;
        s.node = node.second;
        s.root = root;
        s.join = m_tag;
        s.fileName = downloadName(root, d);
        s.size = d.size;
        s.otherFile = true;
        if (const auto h = t.holders.find(node); h != t.holders.end()) {
            if (const auto held = h->second.join ? h->second.join->holderInfo(node, h->second.root, h->second.stream, h->second.source) : std::nullopt)
                s.otherName = held->fileName;
            if (node.first == ffi::KeyKind::Node)
                s.endpoint = h->second.source.ep4 ? h->second.source.ep4 : h->second.source.ep6;
        }
        out.push_back(std::move(s));
    }
    return out;
}

int Ed3kJoinBase::waitingFor(const ffi::Root& root) const
{
    return int(std::ranges::count_if(turns().waiting, [&](const auto& q) {
        return std::ranges::any_of(q.second, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; });
    }));
}

std::vector<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::sourcesAt(const Ed3kSession& session, const ffi::Root& root)
{
    std::vector<SourceInfo> out;
    for (const Ed3kJoinBase* join : turnsOf(session).joins)
        std::ranges::move(join->sourcesOf(root), std::back_inserter(out));
    return out;
}

std::optional<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::sourceAt(const Ed3kSession& session, ffi::KeyKind key,
                                                               const ffi::NodeId& node)
{
    const NodeTurns& t = turnsOf(session);
    const auto h = t.holders.find(TurnNode{key, node});
    if (h == t.holders.end() || !h->second.join)
        return std::nullopt;
    return h->second.join->holderInfo(h->first, h->second.root, h->second.stream, h->second.source);
}

std::vector<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::downloadingAt(const Ed3kSession& session)
{
    std::vector<SourceInfo> out;
    for (const auto& [node, h] : turnsOf(session).holders)
        if (h.join)
            if (auto info = h.join->holderInfo(node, h.root, h.stream, h.source); info && info->holding)
                out.push_back(std::move(*info));
    return out;
}

namespace {

bool isNetworkTag(const QString& t)
{
    return t == QLatin1String("torrent") || t == QLatin1String("usenet") || t == QLatin1String("hoster");
}

} // namespace

QString Ed3kJoinBase::networkJoinAt(const Ed3kSession& session, const ffi::Root& root)
{
    for (const Ed3kJoinBase* join : turnsOf(session).joins)
        if (isNetworkTag(join->m_tag) && (join->sharesRoot(root) || join->m_downloads.contains(root)))
            return join->m_tag;
    return {};
}

std::vector<Ed3kJoinBase::JoinOwner> Ed3kJoinBase::aliasJobsAt(const Ed3kSession& session, const ffi::Root& root)
{
    std::vector<JoinOwner> out;
    const auto add = [&out](const QString& tag, const QString& owner) {
        JoinOwner j{tag, owner};
        // find, not ranges::contains: GCC 12 (Ubuntu 22.04 build) lacks it
        if (!owner.isEmpty() && std::ranges::find(out, j) == out.end())
            out.push_back(std::move(j));
    };
    for (const Ed3kJoinBase* join : turnsOf(session).joins) {
        if (!isNetworkTag(join->m_tag) && join->m_tag != QLatin1String("ed2k"))
            continue;
        if (const auto s = join->m_shared.find(root); s != join->m_shared.end() && s->second.member >= 0)
            add(join->m_tag, s->second.owner);
        if (const auto d = join->m_downloads.find(root); d != join->m_downloads.end() && d->second->member >= 0)
            add(join->m_tag, d->second->owner);
    }
    return out;
}

std::vector<Ed3kJoinBase::JoinState> Ed3kJoinBase::joinStatesAt(const Ed3kSession& session, const ffi::Root& root)
{
    std::vector<JoinState> out;
    for (const Ed3kJoinBase* join : turnsOf(session).joins) {
        if (!isNetworkTag(join->m_tag) && join->m_tag != QLatin1String("ed2k"))
            continue;
        JoinState st;
        st.tag = join->m_tag;
        if (!join->m_started) {
            out.push_back(std::move(st));
            continue;
        }
        const auto addOwner = [&st](const QString& owner) {
            if (!owner.isEmpty() && !st.owners.contains(owner))
                st.owners << owner;
        };
        if (const auto k = join->m_skipped.find(root); k != join->m_skipped.end()) {
            st.kind = JoinState::Kind::Skipped;
            st.reason = QStringLiteral("anonKey");
            addOwner(k->second.owner);
        } else {
            if (const auto sh = join->m_shared.find(root); sh != join->m_shared.end() && sh->second.member >= 0)
                addOwner(sh->second.owner);
            if (const auto d = join->m_downloads.find(root); d != join->m_downloads.end() && d->second->member >= 0)
                addOwner(d->second->owner);
            if (!st.owners.isEmpty()) {
                st.kind = JoinState::Kind::Joined;
                st.anon = join->anonCapable() && join->keyOf(root) == ffi::KeyKind::Anon;
            } else if (join->anonHeld(root)) {
                // none of it holds the file; one that came would be left out
                st.kind = JoinState::Kind::Skipped;
                st.reason = QStringLiteral("anonKey");
            } else {
                st.kind = JoinState::Kind::Available;
            }
        }
        out.push_back(std::move(st));
    }
    return out;
}

std::vector<Ed3kJoinBase::JoinOwner> Ed3kJoinBase::networkJobsAt(const Ed3kSession& session)
{
    std::vector<JoinOwner> out;
    const auto add = [&out](const QString& tag, const QString& owner) {
        JoinOwner j{tag, owner};
        // find, not ranges::contains: GCC 12 (Ubuntu 22.04 build) lacks it
        if (!owner.isEmpty() && std::ranges::find(out, j) == out.end())
            out.push_back(std::move(j));
    };
    for (const Ed3kJoinBase* join : turnsOf(session).joins) {
        if (!isNetworkTag(join->m_tag))
            continue;
        for (const auto& [root, d] : join->m_downloads)
            if (d->member >= 0)
                add(join->m_tag, d->owner);
        for (const auto& [root, s] : join->m_shared)
            if (s.member >= 0)
                add(join->m_tag, s.owner);
    }
    return out;
}

std::vector<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::jobSourcesAt(const Ed3kSession& session, const QString& tag,
                                                                const QString& owner)
{
    std::vector<SourceInfo> out;
    for (const Ed3kJoinBase* join : turnsOf(session).joins) {
        if (join->m_tag != tag)
            continue;
        for (const auto& [root, d] : join->m_downloads) {
            if (d->owner != owner)
                continue;
            for (auto& s : join->sourcesOf(root)) {
                // a node serving two files of the job: one row, the slot holder wins
                const auto same = std::ranges::find_if(out, [&s](const SourceInfo& o) {
                    return o.key == s.key && o.node.bytes == s.node.bytes;
                });
                if (same == out.end())
                    out.push_back(std::move(s));
                else if (s.holding && !same->holding)
                    *same = std::move(s);
            }
        }
    }
    return out;
}

std::optional<bool> Ed3kJoinBase::joinedAt(const Ed3kSession& session, const QString& tag, const QString& owner)
{
    for (const Ed3kJoinBase* join : turnsOf(session).joins)
        if (join->m_tag == tag)
            return join->m_started ? std::optional(join->hasOwner(owner)) : std::nullopt;
    return std::nullopt;
}

std::vector<std::uint8_t> Ed3kJoinBase::pieceAvailability(const Download& d)
{
    constexpr std::uint64_t kPiece = 16 * kBlock;
    if (d.sourceHave.empty() || d.size == 0)
        return {};
    const auto pieces = std::size_t(d.size / kPiece + (d.size % kPiece != 0 ? 1 : 0));
    std::vector<std::uint8_t> out(pieces, 0);
    for (const auto& [stream, have] : d.sourceHave) {
        if (!d.streams.contains(stream))
            continue;
        for (std::size_t p = 0; p < pieces; ++p) {
            // missing bits are 1 (Protocol.md FileStatus.have)
            const bool held = p / 8 >= have.size() || ((have[p / 8] >> (p % 8)) & 1u) != 0;
            if (held && out[p] < 255)
                ++out[p];
        }
    }
    return out;
}

int Ed3kJoinBase::priorityWeight(const Download& d) const
{
    return std::clamp(downloadPriority(d), -2, 2) + 3;
}

std::optional<ffi::Root> Ed3kJoinBase::shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect,
                                                ffi::KeyKind key)
{
    auto* e = m_session.engine();
    if (!e || data.isEmpty())
        return std::nullopt;
    const bool anon = key == ffi::KeyKind::Anon;
    // hops.md §1: nothing under the anonymous key while hiding is off
    if (anon && !m_session.hide().hiding())
        return std::nullopt;
    ffi::Hasher h;
    if (!h.update(spanOf(data)))
        return std::nullopt;
    auto hashed = h.finish();
    if (!hashed)
        return std::nullopt;
    if (expect && hashed->root != *expect)
        logWarning(QStringLiteral("\"%1\" is not the root it should be").arg(name));
    const QByteArray n = name.toUtf8();
    const ffi::SharedFile f{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt,
                            std::nullopt, ffi::Have::complete(), hashed->hashes.bytes()};
    const auto t = m_session.now();
    // was: if (auto ok = e->fileOpen(t, f); !ok && !m_shared.contains(hashed->root)) {
    if (auto ok = anon ? e->anonFileOpen(t, f) : e->fileOpen(t, f); !ok && !m_shared.contains(hashed->root)) {
        logDebug([&]() -> QString { return QStringLiteral("cannot share \"%1\" (error %2)").arg(name, errorName(ok.error())); });
        return std::nullopt;
    }
    if (anonCapable())
        m_session.hide().record(hashed->root, key);
    m_shared[hashed->root] = Shared{QString(), -1, hashed->size, data};
    // sources 1: this node, the file held complete (Protocol.md §10.7)
    const ffi::FileInfo info{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt, 1, 0, 0, 0, 0, 0};
    // was: (void)e->publish(t, info, ffi::Publish::Source);
    (void)(anon ? e->anonPublish(t, info, ffi::Publish::Source) : e->publish(t, info, ffi::Publish::Source));
    return hashed->root;
}

bool Ed3kJoinBase::shareMember(const ffi::SharedFile& file, const QString& owner, int member)
{
    auto* e = m_session.engine();
    if (!e)
        return false;
    const auto t = m_session.now();
    if (m_shared.contains(file.root)) {
        (void)e->fileHave(t, file.root, file.have);
        return true;
    }
    // hops.md §1: a network that is not anonymous leaves a file of the anonymous key out
    // (never opened under the node key beside it, nor under the anonymous key for it)
    // (skipped once: each have change of a partial member comes here again)
    if (!anonCapable() && (m_skipped.contains(file.root) || anonHeld(file.root))) {
        skipRoot(file.root, Skipped{owner, member});
        return false;
    }
    // hops.md §1: under the key it is held under; a new one as the hiding switch says
    const ffi::KeyKind key = keyOf(file.root);
    if (key == ffi::KeyKind::Anon && !m_session.hide().hiding()) {
        logDebug([&]() -> QString { return QStringLiteral("member %1 of %2 is held under the anonymous key: not shared while hiding is off")
                     .arg(member).arg(owner); });
        return false;
    }
    // was: if (auto ok = e->fileOpen(t, file); !ok) {
    // a torrent/usenet/hoster member of the same root opened first under the node key
    if (key == ffi::KeyKind::Anon)
        takeFromNodeJoins(file.root);
    if (auto ok = key == ffi::KeyKind::Anon ? e->anonFileOpen(t, file) : e->fileOpen(t, file); !ok) {
        logDebug([&]() -> QString { return QStringLiteral("member %1 of %2 not shared (error %3)").arg(member).arg(owner, errorName(ok.error())); });
        return false;
    }
    // the key this join chose, never one another join opened it under (keyOf)
    if (anonCapable())
        m_session.hide().record(file.root, key);
    m_skipped.erase(file.root);
    m_shared[file.root] = Shared{owner, member, file.size, {}};
    // the engine's queue policy ranks its downloaders by it (eMule's score)
    (void)e->uploadPriority(file.root, uploadPriority(m_shared[file.root]));
    return true;
}

void Ed3kJoinBase::updateUploadPriority(const ffi::Root& root)
{
    auto* e = m_session.engine();
    const auto it = m_shared.find(root);
    if (e && it != m_shared.end())
        (void)e->uploadPriority(root, uploadPriority(it->second));
}

void Ed3kJoinBase::unshare(const ffi::Root& root)
{
    const auto it = m_shared.find(root);
    if (it == m_shared.end())
        return;
    if (auto* e = m_session.engine())
        (void)e->fileClose(m_session.now(), root);
    m_shared.erase(it);
    m_publishInfo.erase(root);
    m_declinedOut.erase(root);
}

void Ed3kJoinBase::unshareOwner(const QString& owner)
{
    dropSkipped(owner);
    auto* e = m_session.engine();
    for (auto it = m_shared.begin(); it != m_shared.end();) {
        if (it->second.blob.isEmpty() && it->second.owner == owner) {
            if (e)
                (void)e->fileClose(m_session.now(), it->first);
            it = m_shared.erase(it);
        } else {
            ++it;
        }
    }
}

bool Ed3kJoinBase::hasLocalCopy(const ffi::Root& root, const QString& owner) const
{
    for (const Ed3kJoinBase* join : turnsOf(m_session).joins) {
        const auto s = join->m_shared.find(root);
        // a blob is in memory (manifest), no file; our own member is what is missing
        if (s != join->m_shared.end() && s->second.blob.isEmpty() && !(join == this && s->second.owner == owner))
            return true;
    }
    return turnsOf(m_session).found.contains(root);
}

std::optional<Ed3kJoinBase::FileRead> Ed3kJoinBase::localCopyRead(const ffi::Root& root, std::uint64_t offset, std::uint32_t len,
                                                                  const QString& owner) const
{
    for (const Ed3kJoinBase* join : turnsOf(m_session).joins) {
        const auto s = join->m_shared.find(root);
        if (s == join->m_shared.end() || !s->second.blob.isEmpty() || (join == this && s->second.owner == owner))
            continue;
        // the sharer's own rule: verified bytes only, size and date as hashed
        if (auto r = join->sharedRead(s->second, offset, len); r && !r->path.isEmpty())
            return r;
    }
    // a file a folder scan found (size and date checked by the read)
    const auto& found = turnsOf(m_session).found;
    if (const auto f = found.find(root); f != found.end() && offset + len <= f->second.size)
        return FileRead{f->second.path, offset, len, qint64(f->second.size), f->second.mtimeMs};
    return std::nullopt;
}

bool Ed3kJoinBase::copyLocal(const ffi::Root& root)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end() || it->second->inMemory)
        return false;
    Download& d = *it->second;
    if (d.copying || d.copied || !hasLocalCopy(root, d.owner))
        return false;
    d.copying = true;
    d.copiedBlocks = 0;
    logInfo(QStringLiteral("\"%1\" read from a local copy of %2 first").arg(downloadName(root, d), hexOf(root.bytes)));
    copyLocalBatch(root, 0);
    return true;
}

bool Ed3kJoinBase::copyingLocal(const ffi::Root& root) const
{
    const auto it = m_downloads.find(root);
    return it != m_downloads.end() && it->second->copying;
}

void Ed3kJoinBase::addFoundCopy(const Ed3kSession& session, const ffi::Root& root, const QString& path,
                                std::uint64_t size, qint64 mtimeMs)
{
    auto& t = turnsOf(session);
    t.found[root] = NodeTurns::Found{path, size, mtimeMs};
    // the open downloads of it: from this file now (again, if an earlier copy ended)
    for (Ed3kJoinBase* join : std::vector<Ed3kJoinBase*>(t.joins.begin(), t.joins.end())) {
        const auto it = join->m_downloads.find(root);
        if (it == join->m_downloads.end() || it->second->inMemory || it->second->copying)
            continue;
        it->second->copied = false;
        (void)join->copyLocal(root);
    }
}

int Ed3kJoinBase::foundCopies(const Ed3kSession& session)
{
    return int(turnsOf(session).found.size());
}

std::vector<std::pair<ffi::Root, std::uint64_t>> Ed3kJoinBase::wantedFiles(const Ed3kSession& session)
{
    std::map<ffi::Root, std::uint64_t> out;
    for (const Ed3kJoinBase* join : turnsOf(session).joins)
        for (const auto& [root, d] : join->m_downloads)
            if (!d->inMemory && d->size > 0)
                out.emplace(root, d->size);
    return {out.begin(), out.end()};
}

QString Ed3kJoinBase::localCopyPath(const Ed3kSession& session, const ffi::Root& root, std::uint64_t size)
{
    const auto& t = turnsOf(session);
    for (const Ed3kJoinBase* join : t.joins) {
        const auto s = join->m_shared.find(root);
        if (s == join->m_shared.end() || !s->second.blob.isEmpty() || s->second.size != size)
            continue;
        // the whole of it servable there (len is 32 bit: in steps)
        QString path;
        bool whole = size > 0;
        for (std::uint64_t off = 0; whole && off < size;) {
            const auto len = std::uint32_t(std::min<std::uint64_t>(size - off, 0x80000000ULL));
            const auto r = join->sharedRead(s->second, off, len);
            whole = r && !r->path.isEmpty() && (path.isEmpty() || path == r->path);
            if (whole)
                path = r->path;
            off += len;
        }
        if (whole)
            return path;
    }
    if (const auto f = t.found.find(root); f != t.found.end() && f->second.size == size)
        return f->second.path;
    return {};
}

std::optional<std::pair<ffi::Root, std::uint64_t>> Ed3kJoinBase::hashFileRoot(const QString& path, const std::atomic<bool>& cancel)
{
    auto hashed = hashFile(path, cancel);
    if (!hashed)
        return std::nullopt;
    return std::pair{hashed->first.root, hashed->first.size};
}

void Ed3kJoinBase::withdrawOwner(const QString& owner)
{
    unshareOwner(owner);
    const Kept kept = readKept(owner);
    for (const auto& b : kept.blobs)
        unshare(b.root);
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    // was: if (kept.set) (void)e->setUnpublish(t, kept.set->root);
    if (kept.set)
        unpublishSet(kept.set->root);
    if (const auto records = readRecords(QDir(keptDir(owner)).filePath(kKeptRecords)); !records.empty())
        if (auto ok = e->unpublishAlias(t, records); !ok)
            logDebug([&]() -> QString { return QStringLiteral("ALIAS records of %1 not withdrawn (error %2)").arg(owner, errorName(ok.error())); });
}

std::expected<ffi::Publish, ffi::Error> Ed3kJoinBase::publishShared(const ffi::FileInfo& info)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    // declined-records.md §2: nothing put out for a root (or eD2K hash) this node declines
    if (const auto& d = m_session.declined(); d.listsRoot(info.root) || (info.ed2k && d.listsEd2k(*info.ed2k))) {
        logInfo(QStringLiteral("\"%1\" not published: declined (%2)").arg(QString::fromStdString(info.name), QString::fromLatin1(kDeclineFile)));
        // published once the list lets it (onDeclinedChanged)
        if (m_keepPublishInfo) {
            m_publishInfo.insert_or_assign(info.root, info);
            m_declinedOut.insert(info.root);
        }
        return std::unexpected(ffi::Error::Denied);
    }
    if (m_keepPublishInfo) {
        m_publishInfo.insert_or_assign(info.root, info);
        m_declinedOut.erase(info.root);
    }
    const auto t = m_session.now();
    const auto mode = m_holdBack ? ffi::Publish::Source : ffi::Publish::Both;
    // under the anonymous key: its ANON record and keywords through a last hop (hops.md §1)
    const bool anon = keyOf(info.root) == ffi::KeyKind::Anon;
    const auto publish = [&](ffi::Publish what) { return anon ? e->anonPublish(t, info, what) : e->publish(t, info, what); };
    // was: auto ok = e->publish(t, info, mode);
    auto ok = publish(mode);
    if (ok)
        return mode;
    // a name without a keyword: the engine refuses Both before it takes the source record
    // (packmule-ffi ed3k_publish), so the file would not be found by its root either
    if (mode == ffi::Publish::Both && ok.error() == ffi::Error::Malformed) {
        // was: if (auto src = e->publish(t, info, ffi::Publish::Source); !src)
        if (auto src = publish(ffi::Publish::Source); !src)
            return std::unexpected(src.error());
        return ffi::Publish::Source;
    }
    return std::unexpected(ok.error());
}

void Ed3kJoinBase::unpublishShared(const ffi::Root& root, ffi::Publish mode)
{
    // taken back on purpose: not published again by a list change
    m_publishInfo.erase(root);
    m_declinedOut.erase(root);
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    if (keyOf(root) == ffi::KeyKind::Anon) {
        (void)e->anonUnshare(t, root);
        (void)e->anonUnpublish(t, root);
    } else {
        (void)e->unpublish(t, root, mode);
    }
}

void Ed3kJoinBase::onDeclinedChanged()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto& d = m_session.declined();
    const auto t = m_session.now();
    int out = 0;
    int again = 0;
    std::vector<ffi::FileInfo> republish;
    for (auto it = m_publishInfo.begin(); it != m_publishInfo.end();) {
        const ffi::Root root = it->first;
        // gone meanwhile (forgetShare, hiding off)
        if (!isShared(root)) {
            m_declinedOut.erase(root);
            it = m_publishInfo.erase(it);
            continue;
        }
        const bool listed = d.listsRoot(root) || (it->second.ed2k && d.listsEd2k(*it->second.ed2k));
        const bool isOut = m_declinedOut.contains(root);
        if (listed && !isOut) {
            // source, keywords (Keywords ends its ALIAS records too), note
            const ffi::FileInfo info = it->second;
            const bool anon = keyOf(root) == ffi::KeyKind::Anon;
            unpublishShared(root, ffi::Publish::Both);
            (void)(anon ? e->anonUnpublishNote(t, root) : e->unpublishNote(t, root));
            it = m_publishInfo.insert_or_assign(root, info).first;
            m_declinedOut.insert(root);
            ++out;
        } else if (!listed && isOut) {
            republish.push_back(it->second);
        }
        ++it;
    }
    for (const ffi::FileInfo& info : republish)
        if (publishShared(info))
            ++again;
    if (out > 0 || again > 0)
        logInfo(QStringLiteral("decline list changed: %1 file(s) withdrawn, %2 published again").arg(out).arg(again));
    pumpSession();
}

ffi::KeyKind Ed3kJoinBase::keyFor(const ffi::Root& root) const
{
    return anonCapable() ? m_session.hide().keyFor(root) : ffi::KeyKind::Node;
}

ffi::KeyKind Ed3kJoinBase::keyOf(const ffi::Root& root) const
{
    // was: the engine's key first, for every join
    // if (auto* e = m_session.engine())
    //     if (const auto k = e->fileKey(root); k && *k)
    //         return **k;
    // return keyFor(root);
    // a join of the node key opens nothing under the anonymous key (anonHeld skips it)
    if (!anonCapable())
        return ffi::KeyKind::Node;
    if (const auto k = m_session.hide().recorded(root))
        return *k;
    // held, not recorded (keys.json lost): its key, unless a node-key join opened it
    if (!heldByNodeJoinsOnly(root))
        if (auto* e = m_session.engine())
            if (const auto k = e->fileKey(root); k && *k)
                return **k;
    return m_session.hide().keyFor(root);
}

bool Ed3kJoinBase::anonHeld(const ffi::Root& root) const
{
    const Ed3kHide& hide = m_session.hide();
    if (const auto k = hide.recorded(root))
        return *k == ffi::KeyKind::Anon;
    if (auto* e = m_session.engine())
        if (const auto k = e->fileKey(root); k && *k && **k == ffi::KeyKind::Anon)
            return true;
    // not opened yet: an anon-capable join (shared list) takes it under the anonymous key
    if (!hide.hiding())
        return false;
    return std::ranges::any_of(turnsOf(m_session).joins, [&](const Ed3kJoinBase* j) {
        return j != this && j->m_started && j->anonCapable() && j->claimsRoot(root);
    });
}

bool Ed3kJoinBase::withholdSet(const QString& owner, std::span<const ffi::Root> members) const
{
    // was: if (anonCapable()) return false;
    if (anonCapable())
        return !setKeyOf(owner, members);
    if (std::ranges::any_of(m_skipped, [&](const auto& kv) { return kv.second.owner == owner; }))
        return true;
    return std::ranges::any_of(members, [&](const ffi::Root& r) { return anonHeld(r); });
}

std::optional<ffi::KeyKind> Ed3kJoinBase::setKeyOf(const QString& owner, std::span<const ffi::Root> members) const
{
    if (!anonCapable())
        return withholdSet(owner, members) ? std::nullopt : std::optional(ffi::KeyKind::Node);
    if (std::ranges::any_of(m_skipped, [&](const auto& kv) { return kv.second.owner == owner; }))
        return std::nullopt;
    std::optional<ffi::KeyKind> key;
    for (const ffi::Root& r : members) {
        const ffi::KeyKind k = keyOf(r);
        if (key && *key != k)
            return std::nullopt;   // §1: a set lists files of one key only
        key = k;
    }
    const Ed3kHide& hide = m_session.hide();
    if (!key)
        key = hide.keyForUse();
    // §1: nothing falls back to the node key
    if (*key == ffi::KeyKind::Anon && !hide.hiding())
        return std::nullopt;
    return key;
}

ffi::KeyKind Ed3kJoinBase::blobKeyOf(const QString& owner) const
{
    if (!anonCapable())
        return ffi::KeyKind::Node;
    std::vector<ffi::Root> roots;
    for (const auto& [root, sh] : m_shared)
        if (sh.member >= 0 && sh.owner == owner)
            roots.push_back(root);
    for (const auto& [root, d] : m_downloads)
        if (d->member >= 0 && d->owner == owner)
            roots.push_back(root);
    for (const auto& m : readKept(owner).members)
        if (std::ranges::find(roots, m.root) == roots.end())
            roots.push_back(m.root);
    return setKeyOf(owner, roots).value_or(ffi::KeyKind::Node);
}

std::expected<std::uint32_t, ffi::Error> Ed3kJoinBase::publishAliasRecords(std::span<const ffi::Bytes> records)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto t = m_session.now();
    const bool anon = anonCapable() && m_session.hide().hiding();
    std::uint32_t published = 0;
    ffi::Error last = ffi::Error::State;
    for (const auto& r : records) {
        auto ok = e->publishAlias(t, std::span(&r, 1));
        // State: its root is not shared, or held under the anonymous key
        if (!ok && ok.error() == ffi::Error::State && anon)
            ok = e->anonPublishAlias(t, std::span(&r, 1));
        if (ok)
            ++published;
        else
            last = ok.error();
    }
    if (published == 0 && !records.empty())
        return std::unexpected(last);
    return published;
}

void Ed3kJoinBase::unpublishSet(const ffi::Root& root)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    (void)e->setUnpublish(t, root);   // NotFound: not under this key
    if (m_session.hide().hiding())
        (void)e->setUnpublish(t, root, ffi::KeyKind::Anon);
}

bool Ed3kJoinBase::moveDownloadKey(const ffi::Root& root, ffi::KeyKind key)
{
    const auto it = m_downloads.find(root);
    if (!anonCapable() || it == m_downloads.end() || it->second->key == key)
        return false;
    const QString owner = it->second->owner;
    const int member = it->second->member;
    const std::uint64_t size = it->second->size;
    const bool inMemory = it->second->inMemory;
    // closed under one key, then opened under the other (ffi.md "never under both keys")
    stopDownload(root);
    m_session.hide().record(root, key);
    const bool ok = open(root, size, owner, member, inMemory);
    pumpSession();
    return ok;
}

void Ed3kJoinBase::runHashJob(std::function<std::function<void()>(const std::atomic<bool>& cancel)> work)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    hashPool()->start([self, cancel, work = std::move(work)]() mutable {
        DiskLoadLimiter::lowerThisThread();
        auto then = work(*cancel);
        if (cancel->load() || !then)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, then = std::move(then)] {
                if (self && !cancel->load())
                    then();
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::hashInBackground(const QString& path, std::function<void(std::optional<FileHash>)> done, bool withEd2k,
                                    std::shared_ptr<FileStep> step)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    hashPool()->start([self, cancel, path, withEd2k, step = std::move(step), done = std::move(done)]() mutable {
        DiskLoadLimiter::lowerThisThread();
        QByteArray ed2k;
        auto hashed = hashFile(path, *cancel, withEd2k ? &ed2k : nullptr, step ? &step->permille : nullptr);
        if (cancel->load())
            return;
        // owned copies into the lambda; ffi::Hashed holds a move-only Buffer
        std::optional<FileHash> out;
        if (hashed)
            out = FileHash{hashed->first.root, hashed->first.size, std::move(hashed->second), ed2k};
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, done = std::move(done), out = std::move(out)]() mutable {
                if (self && !cancel->load())
                    done(std::move(out));
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::checkpointInBackground(const QString& path, std::uint64_t upTo,
                                          std::function<void(std::optional<HashCheckpoint>)> done)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    hashPool()->start([self, cancel, path, upTo, done = std::move(done)]() mutable {
        DiskLoadLimiter::lowerThisThread();
        std::optional<HashCheckpoint> out;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            ffi::Hasher h;
            DiskLoadLimiter& diskLoad = DiskLoadLimiter::thisThread();
            diskLoad.setFile(path);
            bool ok = true;
            for (std::uint64_t at = 0; ok && at < upTo && !cancel->load();) {
                QByteArray chunk;
                {
                    const DiskLoadLimiter::Read timed(diskLoad);
                    chunk = f.read(qint64(std::min<std::uint64_t>(kHashChunk, upTo - at)));
                }
                ok = !chunk.isEmpty() && h.update({reinterpret_cast<const std::byte*>(chunk.constData()), std::size_t(chunk.size())});
                at += std::uint64_t(chunk.size());
            }
            if (ok && !cancel->load()) {
                if (auto cp = h.checkpoint()) {
                    const auto b = cp->blob.bytes();
                    out = HashCheckpoint{QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())), cp->offset};
                }
            }
        }
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, done = std::move(done), out = std::move(out)]() mutable {
                if (self && !cancel->load())
                    done(std::move(out));
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::relocateInBackground(const QString& src, const QString& dst, std::uint64_t dataSize,
                                        std::function<void(QString error)> done)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    const std::uint64_t floor = thePrefs.checkDiskspace() ? thePrefs.minFreeDiskSpace() : 0;
    QThreadPool::globalInstance()->start([self, cancel, src, dst, dataSize, floor, done = std::move(done)]() mutable {
        const QString error = relocateFile(src, dst, floor, /*removeSource*/ true, nullptr, dataSize);
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, error, done = std::move(done)] {
                if (self && !cancel->load())
                    done(error);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::copyInBackground(const QString& src, const QString& dst, std::shared_ptr<FileStep> step,
                                    std::function<void(bool ok)> done)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    QThreadPool::globalInstance()->start([self, cancel, src, dst, step = std::move(step), done = std::move(done)]() mutable {
        const bool ok = FileMoveThread::copyThenRename(
            src, dst, [&] { return !cancel->load() && !step->stop.load(); },
            [&](qint64 copied, qint64 total) {
                step->permille.store(total > 0 ? int(copied * 1000 / total) : 1000, std::memory_order_relaxed);
            });
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, ok, done = std::move(done)] {
                if (self && !cancel->load())
                    done(ok);
            },
            Qt::QueuedConnection);
    });
}

std::optional<Ed3kJoinBase::FileHash> Ed3kJoinBase::hashNow(const QString& path)
{
    const std::atomic<bool> never{false};
    auto hashed = hashFile(path, never);
    if (!hashed)
        return std::nullopt;
    return FileHash{hashed->first.root, hashed->first.size, std::move(hashed->second)};
}

std::optional<QByteArray> Ed3kJoinBase::readFile(const FileRead& r)
{
    if (r.path.isEmpty())
        return std::nullopt;
    if (r.size >= 0 || r.mtimeMs >= 0) {
        // changed on disk since it was hashed: its blocks would not verify
        const QFileInfo fi(r.path);
        if (!fi.isFile() || (r.size >= 0 && fi.size() != r.size)
            || (r.mtimeMs >= 0 && fi.lastModified().toMSecsSinceEpoch() != r.mtimeMs))
            return std::nullopt;
    }
    QFile f(r.path);
    // shared delete: a .part moved meanwhile can still go (#187)
    if (!openReadShared(f) || !f.seek(qint64(r.offset)))
        return std::nullopt;
    QByteArray data = f.read(qint64(r.len));
    if (data.size() != qsizetype(r.len))
        return std::nullopt;
    return data;
}

void Ed3kJoinBase::readInBackground(const QString& owner, std::vector<FileRead> reads, std::function<void(ReadResults)> done)
{
    if (m_readsNow) {
        ReadResults out;
        out.reserve(reads.size());
        for (const auto& r : reads)
            out.push_back(readFile(r));
        done(std::move(out));
        return;
    }
    auto& q = m_reads[owner];
    q.batches.push_back(ReadBatch{std::move(reads), std::move(done)});
    if (!q.busy)
        runReads(owner);
}

std::size_t Ed3kJoinBase::readsInFlight() const
{
    std::size_t n = 0;
    for (const auto& [owner, q] : m_reads)
        n += q.batches.size();
    return n;
}

bool Ed3kJoinBase::readsPending(const QString& owner) const
{
    const auto it = m_reads.find(owner);
    return it != m_reads.end() && (it->second.busy || !it->second.batches.empty());
}

bool Ed3kJoinBase::startSetSearch(const QString& owner, std::string_view scheme, std::span<const std::uint8_t> id,
                                  std::uint32_t rel)
{
    auto* e = m_session.engine();
    if (!e || !m_searchEnabled)
        return false;
    auto s = e->aliasSearchStart(m_session.now(), scheme, id, rel);
    if (!s) {
        logDebug([&]() -> QString { return QStringLiteral("%1 search of %2 not started (error %3)")
                     .arg(QString::fromUtf8(scheme.data(), qsizetype(scheme.size())), owner, errorName(s.error())); });
        return false;
    }
    m_setSearches[*s] = {owner, false};
    if (!m_setTries.contains(owner))
        m_setTries.emplace(owner, std::make_unique<SetTry>());
    return true;
}

bool Ed3kJoinBase::trySetCandidate(const QString& owner, const ffi::Root& root, std::uint64_t size,
                                   std::span<const ffi::PublisherKey> publishers)
{
    if (size == 0 || !m_session.engine())
        return false;
    auto& t = m_setTries[owner];
    if (!t)
        t = std::make_unique<SetTry>();
    if (t->root && downloading(*t->root))
        return false;   // one set at a time
    if (!fetchBlob(root, size, owner, kSetManifest))
        return false;
    t->root = root;
    t->publishers.assign(publishers.begin(), publishers.end());
    logInfo(QStringLiteral("trying set %1 of %2").arg(hexOf(root.bytes), owner));
    return true;
}

bool Ed3kJoinBase::refuseSet(const QString& owner, const ffi::Root& root)
{
    const auto it = m_setTries.find(owner);
    if (it == m_setTries.end())
        return false;
    SetTry& t = *it->second;
    if (auto* e = m_session.engine()) {
        const auto now = m_session.now();
        // §8: the publishers of the records that named it
        if (t.search)
            (void)e->aliasSearchBlame(now, *t.search, root);
        else if (!t.publishers.empty())
            (void)e->aliasBan(now, t.publishers);
    }
    logInfo(QStringLiteral("set %1 of %2 refused").arg(hexOf(root.bytes), owner));
    if (t.root && *t.root == root) {
        stopDownload(root);
        t.root.reset();
        t.publishers.clear();
    }
    return tryNextSet(owner);
}

std::optional<ffi::Root> Ed3kJoinBase::setTrying(const QString& owner) const
{
    const auto it = m_setTries.find(owner);
    return it == m_setTries.end() ? std::nullopt : it->second->root;
}

std::vector<ffi::PublisherKey> Ed3kJoinBase::setPublishers(const QString& owner) const
{
    const auto it = m_setTries.find(owner);
    return it == m_setTries.end() ? std::vector<ffi::PublisherKey>{} : it->second->publishers;
}

void Ed3kJoinBase::endSetSearches(const QString& owner)
{
    auto* e = m_session.engine();
    for (auto it = m_setSearches.begin(); it != m_setSearches.end();) {
        if (it->second.first == owner) {
            if (e)
                (void)e->aliasSearchEnd(it->first);
            it = m_setSearches.erase(it);
        } else {
            ++it;
        }
    }
    if (const auto t = m_setTries.find(owner); t != m_setTries.end()) {
        if (t->second->root)
            stopDownload(*t->second->root);
        m_setTries.erase(t);
    }
}

bool Ed3kJoinBase::setSearching(const QString& owner) const
{
    return std::ranges::any_of(m_setSearches, [&](const auto& s) { return s.second.first == owner; });
}

void Ed3kJoinBase::keepBlob(const QString& owner, const QByteArray& data, const QString& name, std::optional<ffi::Root> expect)
{
    // was: const auto root = shareBlob(data, name, expect);
    const auto root = shareBlob(data, name, expect, blobKeyOf(owner));   // hops.md §1: its members' key
    if (!root)
        return;
    Kept kept = readKept(owner);
    if (std::ranges::any_of(kept.blobs, [&](const Kept::Blob& b) { return b.root == *root; }))
        return;
    const QString dir = keptDir(owner);
    QDir().mkpath(dir);
    if (!writeBlob(QDir(dir).filePath(blobFile(*root)), spanOf(data))) {
        logWarning(QStringLiteral("cannot keep \"%1\" in %2").arg(name, dir));
        return;
    }
    kept.blobs.push_back({*root, name});
    writeKept(owner, kept);
}

void Ed3kJoinBase::keepMember(const QString& owner, int member, const ffi::Root& root, std::uint64_t size, const QString& name)
{
    Kept kept = readKept(owner);
    QString path;   // kept across a member hashed again
    std::erase_if(kept.members, [&](const Kept::Member& m) {
        if (m.member == member && m.root == root)
            path = m.path;
        return m.member == member;
    });
    kept.members.push_back({member, root, size, name, path});
    writeKept(owner, kept);
}

void Ed3kJoinBase::keepMemberPaths(const QString& owner, const QHash<int, QString>& paths)
{
    Kept kept = readKept(owner);
    bool changed = false;
    for (auto& m : kept.members)
        if (const auto it = paths.constFind(m.member); it != paths.cend() && !it->isEmpty() && *it != m.path) {
            m.path = *it;
            changed = true;
        }
    if (changed)
        writeKept(owner, kept);
}

QHash<int, QString> Ed3kJoinBase::keptMemberPaths(const QString& owner) const
{
    QHash<int, QString> out;
    for (const auto& m : readKept(owner).members)
        if (!m.path.isEmpty())
            out.insert(m.member, m.path);
    return out;
}

void Ed3kJoinBase::setDeletedDays(int days)
{
    m_deletedDays = std::clamp(days, -1, kDeletedDaysMax);
    if (m_started)
        onDeletedTimer();
}

bool Ed3kJoinBase::keepDeleted(const DeletedFile& f)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_deletedDays < 0 || deletedOver(f, now))
        return false;
    if (auto* e = m_session.engine()) {
        if (auto ok = e->aliasKeepDeleted(m_session.now(), f.root, std::uint64_t(std::max<qint64>(0, f.deletedAt / 1000))); !ok) {
            logDebug([&]() -> QString { return QStringLiteral("deleted \"%1\" of %2 not kept (error %3)").arg(f.name, f.owner, errorName(ok.error())); });
            return false;
        }
    }
    auto& rows = deletedRows();
    std::erase_if(rows, [&](const DeletedFile& r) { return r.root == f.root; });
    rows.push_back(f);
    saveDeleted();
    logDebug([&]() -> QString { return QStringLiteral("\"%1\" of %2 deleted: its records stay out (%3)").arg(f.name, f.owner, m_deletedDays > 0 ? QStringLiteral("%1 day(s) at most").arg(m_deletedDays) : QStringLiteral("no age limit")); });
    return true;
}

void Ed3kJoinBase::endDeleted(const ffi::Root& root, const QString& why)
{
    auto& rows = deletedRows();
    const auto it = std::ranges::find(rows, root, &DeletedFile::root);
    if (it == rows.end())
        return;
    const DeletedFile f = *it;
    rows.erase(it);
    saveDeleted();
    if (auto* e = m_session.engine())
        (void)e->aliasEndDeleted(m_session.now(), root);
    logInfo(QStringLiteral("deleted \"%1\" no longer published: %2").arg(f.name, why));
    onDeletedEnded(f);
    // the owner's set and records too, once nothing else keeps it
    if (!hasDeleted(f.owner) && !keepsOwner(f.owner) && QFileInfo::exists(keptDir(f.owner))) {
        withdrawOwner(f.owner);
        dropKept(f.owner);
    }
    pumpSession();
}

bool Ed3kJoinBase::isDeleted(const ffi::Root& root) const
{
    const auto& rows = deletedRows();
    return std::ranges::find(rows, root, &DeletedFile::root) != rows.end();
}

bool Ed3kJoinBase::hasDeleted(const QString& owner) const
{
    const auto& rows = deletedRows();
    return std::ranges::find(rows, owner, &DeletedFile::owner) != rows.end();
}

std::vector<Ed3kJoinBase::DeletedFile> Ed3kJoinBase::deletedOf(const QString& owner) const
{
    std::vector<DeletedFile> out;
    std::ranges::copy_if(deletedRows(), std::back_inserter(out), [&](const DeletedFile& f) { return f.owner == owner; });
    return out;
}

void Ed3kJoinBase::deletedChecked(const ffi::Root& root, qint64 at)
{
    auto& rows = deletedRows();
    if (const auto it = std::ranges::find(rows, root, &DeletedFile::root); it != rows.end() && it->checkedAt != at) {
        it->checkedAt = at;
        saveDeleted();
    }
}

void Ed3kJoinBase::loadDeleted()
{
    m_deleted.reset();   // read again: another run may have changed it
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const DeletedFile& f : deletedRows())
            (void)e->aliasKeepDeleted(t, f.root, std::uint64_t(std::max<qint64>(0, f.deletedAt / 1000)));
    }
    m_deletedTimer.start();
    onDeletedTimer();
}

void Ed3kJoinBase::onDeletedTimer()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::vector<ffi::Root> over;
    for (const DeletedFile& f : deletedRows())
        if (m_deletedDays < 0 || deletedOver(f, now))
            over.push_back(f.root);
    for (const auto& root : over)
        endDeleted(root, m_deletedDays > 0 ? QStringLiteral("%1 day(s) since deleted").arg(m_deletedDays) : QStringLiteral("switched off"));
    if (m_started && !deletedRows().empty())
        checkDeleted(now);
}

bool Ed3kJoinBase::deletedOver(const DeletedFile& f, qint64 now) const
{
    return m_deletedDays > 0 && now - f.deletedAt >= qint64(m_deletedDays) * 24 * 3600 * 1000;
}

std::vector<Ed3kJoinBase::DeletedFile>& Ed3kJoinBase::deletedRows() const
{
    if (m_deleted)
        return *m_deleted;
    m_deleted.emplace();
    // root owner member size deletedAt checkedAt name check(space separated)
    const QByteArray raw = readBlob(deletedPath());
    for (const QString& line : QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() < 7)
            continue;
        const auto root = rootOfHex(f.at(0));
        if (!root)
            continue;
        DeletedFile d;
        d.root = *root;
        d.owner = QUrl::fromPercentEncoding(f.at(1).toLatin1());
        d.member = f.at(2).toInt();
        d.size = f.at(3).toULongLong();
        d.deletedAt = f.at(4).toLongLong();
        d.checkedAt = f.at(5).toLongLong();
        d.name = f.at(6);
        d.check = f.value(7).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m_deleted->push_back(std::move(d));
    }
    return *m_deleted;
}

void Ed3kJoinBase::saveDeleted() const
{
    QString out;
    for (const DeletedFile& d : deletedRows())
        out += QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\n")
                   .arg(hexOf(d.root.bytes), QString::fromLatin1(QUrl::toPercentEncoding(d.owner)))
                   .arg(d.member)
                   .arg(d.size)
                   .arg(d.deletedAt)
                   .arg(d.checkedAt)
                   .arg(field(d.name), d.check.join(QLatin1Char(' ')));
    QDir().mkpath(joinDir(m_tag));
    const QByteArray utf8 = out.toUtf8();
    writeBlobLater(deletedPath(), spanOf(utf8));
}

std::optional<ffi::Root> Ed3kJoinBase::rootFromHex(const QString& hex)
{
    return rootOfHex(hex);
}

QString Ed3kJoinBase::deletedPath() const
{
    return QDir(joinDir(m_tag)).filePath(kDeletedFile);
}

void Ed3kJoinBase::republishKeptAliases(const QString& owner)
{
    if (!m_session.engine() || m_holdBack)
        return;
    // one by one: a record of a file neither shared nor deleted is refused (State) alone
    // was: (void)e->publishAlias(t, std::span(&r, 1)); (node key only)
    for (const auto& r : readRecords(QDir(keptDir(owner)).filePath(kKeptRecords)))
        (void)publishAliasRecords(std::span(&r, 1));
    pumpSession();
}

void Ed3kJoinBase::keepRecords(const QString& owner, std::optional<ffi::Root> set, const QString& name,
                               const std::vector<ffi::Bytes>& aliases)
{
    if (set) {
        Kept kept = readKept(owner);
        kept.set = Kept::Set{*set, name};
        writeKept(owner, kept);
    }
    if (aliases.empty())
        return;
    const QString path = QDir(keptDir(owner)).filePath(kKeptRecords);
    auto records = readRecords(path);
    for (const auto& a : aliases)
        if (std::ranges::find(records, a) == records.end())
            records.push_back(a);
    QDir().mkpath(keptDir(owner));
    writeRecords(path, records);
}

void Ed3kJoinBase::shareKept()
{
    const QDir dir(joinDir(m_tag));
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString owner = QUrl::fromPercentEncoding(sub.toLatin1());
        if (!keepsOwner(owner)) {
            logDebug([&]() -> QString { return QStringLiteral("%1 is gone: what was kept of it is dropped").arg(owner); });
            forgetBlobWrites(dir.filePath(sub));
            removeDirTree(dir.filePath(sub));
            continue;
        }
        shareKeptOwner(owner);
    }
}

void Ed3kJoinBase::shareKeptOwner(const QString& owner)
{
    if (!sharesKeptOwner(owner))
        return;
    // was: kept.tsv and the .blob files read here, on the daemon thread: a busy disk
    // stalled the core 8 s at start (report 58, a torrent's .blob); now on the read pool
    const QString dir = keptDir(owner);
    const bool withBlobs = shareKeptBlobs();
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    m_session.readPool().start([self, cancel, owner, dir, withBlobs] {
        if (!QFileInfo::exists(dir))
            return;
        Kept kept = readKeptIn(dir);
        std::vector<QByteArray> blobs;
        if (withBlobs)
            for (const auto& b : kept.blobs)
                blobs.push_back(readBlob(QDir(dir).filePath(blobFile(b.root))));
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, owner, kept = std::move(kept), blobs = std::move(blobs)] {
                if (self && !cancel->load())
                    self->shareKeptRead(owner, kept, blobs);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::shareKeptRead(const QString& owner, const Kept& kept, const std::vector<QByteArray>& blobs)
{
    // dropped or switched off while the pool read
    if (!sharesKeptOwner(owner))
        return;
    if (shareKeptBlobs() && blobs.size() == kept.blobs.size()) {
        // hops.md §1: under the key of its members (blobKeyOf)
        const ffi::KeyKind blobKey = blobKeyOf(owner);
        for (std::size_t i = 0; i < kept.blobs.size(); ++i) {
            const auto& b = kept.blobs[i];
            const QByteArray& data = blobs[i];
            if (data.isEmpty())
                logDebug([&]() -> QString { return QStringLiteral("kept \"%1\" of %2 is gone").arg(b.name, owner); });
            else
                // was: (void)shareBlob(data, b.name, b.root);
                (void)shareBlob(data, b.name, b.root, blobKey);
        }
    }
    // the records go out once every member had its first hash (publishAlias needs the
    // roots shared)
    m_keptPending[owner] = int(kept.members.size());
    if (kept.members.empty())
        publishKeptRecords(owner);
    for (const auto& m : kept.members)
        shareKeptMember(owner, m.member, m.root, m.size, m.name, 0);
}

void Ed3kJoinBase::dropKept(const QString& owner)
{
    forgetBlobWrites(keptDir(owner));
    removeDirTree(keptDir(owner));
}

std::optional<ffi::Root> Ed3kJoinBase::keptSet(const QString& owner) const
{
    if (const auto set = readKept(owner).set)
        return set->root;
    return std::nullopt;
}

QString Ed3kJoinBase::keptDir(const QString& owner) const
{
    // reversible and safe as a file name (':' of torrent ids, '/')
    return QDir(joinDir(m_tag)).filePath(QString::fromLatin1(QUrl::toPercentEncoding(owner)));
}

void Ed3kJoinBase::scheduleSave(const QString& owner)
{
    scheduleSave(owner, kSaveDelayMs);
}

void Ed3kJoinBase::scheduleSave(const QString& owner, int delayMs)
{
    if (!hasOwner(owner))
        return;
    m_dirty.insert(owner);
    // a save due sooner stays sooner
    if (!m_saveTimer.isActive() || m_saveTimer.remainingTime() > delayMs)
        m_saveTimer.start(delayMs);
}

void Ed3kJoinBase::beginStart()
{
    m_started = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_researchTimer.start();
}

void Ed3kJoinBase::closeAll()
{
    // the nodes held go to downloads of the other joins (a restart of this one alone)
    dropTurns(true);
    m_cancel->store(true);
    m_reads.clear();   // their results are dropped (the cancel flag)
    m_saveTimer.stop();
    m_researchTimer.stop();
    m_deletedTimer.stop();
    m_dirty.clear();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [search, owner] : m_setSearches)
            (void)e->aliasSearchEnd(search);
        for (const auto& [root, d] : m_downloads) {
            for (const auto& [stream, slot] : d->streams)
                (void)e->streamClose(t, stream);
            (void)e->downloadClose(t, root);
        }
        for (const auto& [root, shared] : m_shared)
            (void)e->fileClose(t, root);
    }
    m_downloads.clear();
    m_streams.clear();
    m_sourceLookups.clear();
    m_setSearches.clear();
    m_setTries.clear();
    m_shared.clear();
    m_skipped.clear();
    m_publishInfo.clear();
    m_declinedOut.clear();
    m_started = false;
}

void Ed3kJoinBase::pumpSession()
{
    if (m_session.isRunning())
        m_session.pump();
}

QString Ed3kJoinBase::joinDir(const QString& sub) const
{
    return QDir(stateDir(m_configDir)).filePath(sub);
}

void Ed3kJoinBase::logDebug(const QString& msg) const
{
    qCDebug(logCategory()).noquote() << logPrefix() + msg;
}

void Ed3kJoinBase::logWarning(const QString& msg) const
{
    qCWarning(logCategory()).noquote() << logPrefix() + msg;
}

void Ed3kJoinBase::logInfo(const QString& msg) const
{
    qCInfo(logCategory()).noquote() << logPrefix() + msg;
}

Ed3kJoinBase::LogCategory Ed3kJoinBase::logCategory() const
{
    return &lcEmuleEd3k;
}

QString Ed3kJoinBase::hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

QString Ed3kJoinBase::errorName(ffi::Error e)
{
    return QString::number(static_cast<int>(e));
}

std::span<const std::byte> Ed3kJoinBase::spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

QString Ed3kJoinBase::safeName(QString name, const QString& fallback)
{
    name = QFileInfo(name.replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    static const QString bad = QStringLiteral("<>:\"|?*");
    for (QChar& c : name)
        if (c.unicode() < 0x20 || bad.contains(c))
            c = QLatin1Char('_');
    name = name.trimmed();
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    return name.isEmpty() ? fallback : name.left(200);
}

QStringList Ed3kJoinBase::metFilesIn(const QStringList& dirs, QStringView suffix)
{
    QStringList out;
    QStringList seen;
    for (const QString& dir : dirs) {
        const QString abs = QDir(dir).absolutePath();
        if (dir.isEmpty() || seen.contains(abs))
            continue;
        seen << abs;
        const QDir d(abs);
        for (const QString& name : d.entryList({QStringLiteral("*") + suffix}, QDir::Files, QDir::Name))
            out << d.filePath(name);
    }
    return out;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kJoinBase::onEventPolled(const ffi::Event& event, ffi::KeyKind key, bool* handled)
{
    namespace ev = ffi::event;
    if (!m_started)
        return;
    const bool anon = key == ffi::KeyKind::Anon;
    // hops.md §1: a join of the node key never takes the anonymous node's events for its own
    if (anon && !anonCapable())
        return;
    if (const auto* w = std::get_if<ev::WantBlock>(&event)) {
        if (answerBlock(w->id, w->root, w->offset, w->len) && handled)
            *handled = true;
    } else if (const auto* v = std::get_if<ev::VerifyBlock>(&event)) {
        if (answerBlock(v->id, v->root, v->offset, std::nullopt) && handled)
            *handled = true;
    } else if (const auto* r = std::get_if<ev::FileRequested>(&event)) {
        if (m_shared.contains(r->root)) {
            // was: ToDo: an upload queue and slots for ed3k (bandwidth, fairness); every
            // downloader of a joined member gets a slot at once for now (done: the engine's
            // queue policy, ed3k_queue_auto, answers it; a slot at once only without it)
            // the anonymous node's downloader is answered there (by node ID, ffi.md hiding)
            if (r->first && !r->autoAnswered)
                (void)(anon ? m_session.engine()->anonQueueSet(m_session.now(), r->node, 0, 0)
                            : m_session.engine()->queueSet(m_session.now(), r->node, 0, 0));
            if (handled)
                *handled = true;
        }
    } else if (const auto* b = std::get_if<ev::Block>(&event)) {
        onBlock(*b);
    } else if (const auto* q = std::get_if<ev::QueueRank>(&event)) {
        if (const auto it = m_streams.find(q->stream); it != m_streams.end()) {
            const ffi::Root root = it->second;
            bool turnEnded = false;
            if (const auto d = m_downloads.find(root); d != m_downloads.end()) {
                auto& slot = d->second->streams[q->stream];
                turnEnded = slot && q->rank > 0;
                slot = q->rank == 0;
            }
            // the turn ended, still connected: the next file waiting for this node asks now
            // (§12.5: another file keeps the position); with recheck > 0 it would be an early
            // return, so the stream stays
            if (turnEnded && q->recheck == 0)
                (void)switchTurn(root, q->stream);
            requestBlocks(root);
        }
    } else if (const auto* h = std::get_if<ev::Hashes>(&event)) {
        if (const auto d = m_downloads.find(h->root); d != m_downloads.end()) {
            d->second->hashes = true;
            if (!d->second->inMemory)
                onDownloadHashes(*d->second, h->blob);
            requestBlocks(h->root);
        }
    } else if (const auto* bf = std::get_if<ev::BlockFailed>(&event)) {
        if (const auto d = m_downloads.find(bf->root); d != m_downloads.end()) {
            d->second->outstanding.erase(bf->offset);
            requestBlocks(bf->root);
        }
    } else if (const auto* rv = std::get_if<ev::RangesVoid>(&event)) {
        if (const auto it = m_streams.find(rv->stream); it != m_streams.end()) {
            const ffi::Root root = it->second;
            if (const auto d = m_downloads.find(root); d != m_downloads.end())
                for (const auto off : rv->offsets)
                    d->second->outstanding.erase(off);
        }
    } else if (const auto* fs = std::get_if<ev::FileStatus>(&event)) {
        if (const auto it = m_streams.find(fs->stream); it != m_streams.end() && fs->have)
            if (const auto d = m_downloads.find(it->second); d != m_downloads.end())
                d->second->sourceHave[fs->stream] = *fs->have;   // absent: unchanged
        const bool ours = m_streams.contains(fs->stream);
        if (ours && !fs->usable)
            (void)m_session.engine()->streamClose(m_session.now(), fs->stream);
        if (ours)
            onStreamStatus(*fs);
    } else if (const auto* sc = std::get_if<ev::StreamClosed>(&event)) {
        if (const auto it = m_streams.find(sc->stream); it != m_streams.end()) {
            const ffi::Root root = it->second;
            m_streams.erase(it);
            // its node goes to the next waiting download (none of them reaches an unreachable one)
            if (const auto node = nodeHeldBy(sc->stream)) {
                if (sc->why == ffi::Closed::Unreachable) {
                    turns().holders.erase(*node);
                    dropWaiters(*node);
                } else {
                    (void)passTurn(*node);
                }
            }
            if (const auto d = m_downloads.find(root); d != m_downloads.end()) {
                d->second->streams.erase(sc->stream);
                d->second->sourceHave.erase(sc->stream);
                std::erase_if(d->second->outstanding, [&](const auto& o) { return o.second == sc->stream; });
                // was: ToDo: search sources again when the last stream closed (done: searchSourcesAgain)
                if (d->second->streams.empty())
                    searchSourcesAgain(root, false);
            }
            onStreamClosed(*sc);
        }
    } else if (const auto* kf = std::get_if<ev::KadFound>(&event)) {
        onKadFound(*kf, key);
    } else if (const auto* af = std::get_if<ev::AliasFound>(&event); af && !anon && m_setSearches.contains(af->search)) {
        onSetAliasFound(*af);
    } else if (const auto* wh = std::get_if<ev::WantHashes>(&event)) {
        // stored values of a shared root: read back (else answerUnhandled fails it)
        if (m_shared.contains(wh->root) && answerHashes(wh->id, wh->root) && handled)
            *handled = true;
    } else if (const auto* hl = std::get_if<ev::HashesLost>(&event)) {
        if (m_shared.contains(hl->root))
            onHashesLost(hl->root);
    } else if (!anon || isHttpSourceEvent(event)) {
        // the joins' own events are of the node key (sets, aliases, ...); HTTP sources of an
        // anonymous download come from the anonymous node (http-source.md §3.2)
        onOtherEvent(event);
    }
}

void Ed3kJoinBase::onSessionStatus()
{
    if (m_session.isRunning() && !m_started)
        start();
    else if (!m_session.isRunning() && m_started)
        stop();
}

void Ed3kJoinBase::hideChanged(bool hiding)
{
    if (!m_started || !anonCapable())
        return;
    if (!hiding) {
        // the anonymous node ended with its downloads and shares (ffi.md hiding): forgotten
        // here; they wait for hiding to come back, never go to the node key
        for (auto it = m_shared.begin(); it != m_shared.end();)
            it = m_session.hide().recorded(it->first) == ffi::KeyKind::Anon ? m_shared.erase(it) : std::next(it);
        for (auto& [root, d] : m_downloads) {
            if (d->key != ffi::KeyKind::Anon)
                continue;
            for (const auto& [stream, slot] : d->streams)
                m_streams.erase(stream);
            d->streams.clear();
            d->outstanding.clear();
            d->sourceHave.clear();
            d->asked.clear();
            d->anonOpened = false;
        }
        std::erase_if(m_sourceLookups, [](const auto& l) { return (l.first >> 63) != 0; });
        // its turns at source nodes went with it
        auto& t = turns();
        std::erase_if(t.holders, [this](const auto& h) { return h.first.first == ffi::KeyKind::Anon && h.second.join == this; });
        for (auto& [node, q] : t.waiting)
            if (node.first == ffi::KeyKind::Anon)
                std::erase_if(q, [this](const NodeTurns::Want& w) { return w.join == this; });
        std::erase_if(t.waiting, [](const auto& q) { return q.second.empty(); });
    } else {
        for (const auto& [root, d] : m_downloads)
            if (d->key == ffi::KeyKind::Anon)
                searchSourcesAgain(root, true);
    }
    onHideChanged(hiding);
    pumpSession();
}

std::uint64_t Ed3kJoinBase::lookupKey(std::uint64_t lookup, ffi::KeyKind key)
{
    return key == ffi::KeyKind::Anon ? (lookup | (std::uint64_t{1} << 63)) : lookup;
}

bool Ed3kJoinBase::answerBlock(std::uint64_t id, const ffi::Root& root, std::uint64_t offset, std::optional<std::uint32_t> len)
{
    auto* e = m_session.engine();
    const auto it = m_shared.find(root);
    if (!e || it == m_shared.end())
        return false;
    const Shared& s = it->second;
    if (offset >= s.size) {
        (void)e->blockFail(m_session.now(), id);
        return true;
    }
    const auto n = std::uint32_t(len.value_or(std::uint32_t(std::min<std::uint64_t>(kBlock, s.size - offset))));
    if (!s.blob.isEmpty()) {
        std::optional<QByteArray> data;
        if (offset + n <= std::uint64_t(s.blob.size()))
            data = s.blob.mid(qsizetype(offset), qsizetype(n));
        finishBlock(id, root, std::move(data));
        return true;
    }
    auto read = sharedRead(s, offset, n);
    if (!read) {
        finishBlock(id, root, std::nullopt);
        return true;
    }
    // on a worker; the engine waits for the answer by id
    readInBackground(kUploadReads, {std::move(*read)}, [this, id, root](ReadResults r) {
        finishBlock(id, root, r.empty() ? std::nullopt : std::move(r.front()));
        pumpSession();
    });
    return true;
}

void Ed3kJoinBase::finishBlock(std::uint64_t id, const ffi::Root& root, std::optional<QByteArray> data)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    if (!data) {
        (void)e->blockFail(t, id);
        const auto it = m_shared.find(root);
        if (it != m_shared.end() && it->second.blob.isEmpty() && sharedGone(it->second)) {
            logDebug([&]() -> QString { return QStringLiteral("%1 is gone, not shared any more").arg(hexOf(root.bytes)); });
            (void)e->fileClose(t, root);
            m_shared.erase(it);
        }
        return;
    }
    auto buf = ffi::Buffer::alloc(std::size_t(data->size()));
    if (!buf) {
        (void)e->blockFail(t, id);
        return;
    }
    std::memcpy(buf->bytes().data(), data->constData(), std::size_t(data->size()));
    (void)e->blockProvide(t, id, std::move(*buf));
}

void Ed3kJoinBase::runReads(const QString& owner)
{
    const auto it = m_reads.find(owner);
    if (it == m_reads.end())
        return;
    if (it->second.batches.empty()) {
        m_reads.erase(it);
        return;
    }
    it->second.busy = true;
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    m_session.readPool().start([self, cancel, owner, reads = it->second.batches.front().reads]() {
        ReadResults out;
        out.reserve(reads.size());
        for (const auto& r : reads) {
            if (cancel->load())
                return;
            out.push_back(readFile(r));
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, owner, out = std::move(out)]() mutable {
                if (self && !cancel->load())
                    self->readsDone(owner, std::move(out));
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::readsDone(const QString& owner, ReadResults results)
{
    const auto it = m_reads.find(owner);
    if (it == m_reads.end() || it->second.batches.empty())
        return;
    auto done = std::move(it->second.batches.front().done);
    it->second.batches.pop_front();
    it->second.busy = false;
    done(std::move(results));   // may queue more of this owner (and start it)
    if (const auto again = m_reads.find(owner); again != m_reads.end() && !again->second.busy)
        runReads(owner);
}

void Ed3kJoinBase::onBlock(const ffi::event::Block& b)
{
    const auto it = m_downloads.find(b.root);
    if (it == m_downloads.end())
        return;
    Download& d = *it->second;
    d.outstanding.erase(b.offset);
    // per-peer bytes (client details): the holder of this stream
    for (const auto& [node, h] : turns().holders)
        if (h.join == this && h.stream == b.stream) {
            m_session.addPeerBytes(node.first, node.second, 0, b.data.size());
            m_session.addPeerFileDown(node.first, node.second, b.root, b.data.size());
            break;
        }
    if (!d.inMemory) {
        onFileBlock(d, b);
        // the handler may have stopped it
        requestBlocks(b.root);
        return;
    }
    if (b.offset + b.data.size() > d.size)
        return;
    if (d.memory.size() != qsizetype(d.size))
        d.memory.resize(qsizetype(d.size));
    std::memcpy(d.memory.data() + b.offset, b.data.data(), b.data.size());
    d.got.insert(b.offset);
    if (d.got.size() < std::size_t((d.size + kBlock - 1) / kBlock)) {
        requestBlocks(b.root);
        return;
    }
    const QString owner = d.owner;
    const int member = d.member;
    const QByteArray data = d.memory;
    const ffi::Root root = b.root;
    stopDownload(root);
    if (member == kSetManifest && setTrying(owner) == root) {
        onSetManifest(owner, root, data);
        return;
    }
    onBlobFetched(owner, member, root, data);
}

void Ed3kJoinBase::onKadFound(const ffi::event::KadFound& found, ffi::KeyKind key)
{
    // was: const auto it = m_sourceLookups.find(found.lookup);
    const auto it = m_sourceLookups.find(lookupKey(found.lookup, key));
    if (it == m_sourceLookups.end())
        return;
    const ffi::Root root = it->second;
    for (const auto& s : found.sources)
        addSource(root, s);
    // anonymous sources (hops.md §4): the ID alone, reached through their gates; only the
    // anonymous node asks them (ffi.md: ed3k_anon_file_request)
    if (key == ffi::KeyKind::Anon)
        for (const auto& id : found.anon)
            addSource(root, ffi::Source{id, false, std::nullopt, std::nullopt, {}});
}

bool Ed3kJoinBase::open(const ffi::Root& root, std::uint64_t size, const QString& owner, int member, bool inMemory)
{
    auto* e = m_session.engine();
    if (!e || size == 0 || m_downloads.contains(root))
        return false;
    const auto t = m_session.now();
    // hops.md §1: a network that is not anonymous downloads no file of the anonymous key on
    // ed3k (its own network still does)
    if (!anonCapable() && anonHeld(root)) {
        skipRoot(root, Skipped{owner, member});
        return false;
    }
    // hops.md §1: the key it is held under, a new one as the hiding switch says
    // was: const ffi::KeyKind key = anonCapable() ? keyOf(root) : ffi::KeyKind::Node;
    const ffi::KeyKind key = keyOf(root);
    if (key == ffi::KeyKind::Anon)
        takeFromNodeJoins(root);
    if (anonCapable())
        m_session.hide().record(root, key);
    // the anonymous key opens it with the first anonFileRequest (downloadOpen here would hold
    // it for the node key)
    if (key == ffi::KeyKind::Node)
        if (auto ok = e->downloadOpen(t, root, size); !ok)
            logDebug([&]() -> QString { return QStringLiteral("download of %1 not opened (error %2)").arg(hexOf(root.bytes), errorName(ok.error())); });
    auto d = std::make_unique<Download>();
    d->key = key;
    d->owner = owner;
    d->member = member;
    d->size = size;
    d->inMemory = inMemory;
    m_downloads.emplace(root, std::move(d));
    // was: ToDo: search again every so often while no stream holds a slot (done: m_researchTimer)
    searchSourcesAgain(root, true);
    return true;
}

void Ed3kJoinBase::copyLocalBatch(const ffi::Root& root, std::uint64_t from)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end() || !it->second->copying)
        return;
    Download& d = *it->second;
    auto wanted = wantedBlocks(d);
    std::ranges::sort(wanted);
    std::vector<FileRead> reads;
    std::vector<std::uint64_t> offsets;
    std::uint64_t next = d.size;
    for (const std::uint64_t off : wanted) {
        if (off < from || off >= d.size)
            continue;
        if (reads.size() >= kCopyBatch) {
            next = off;
            break;
        }
        const auto len = std::uint32_t(std::min(kBlock, d.size - off));
        // not servable there (unverified, changed): the network gets it
        if (auto r = localCopyRead(root, off, len, d.owner)) {
            reads.push_back(*r);
            offsets.push_back(off);
        }
    }
    if (reads.empty()) {
        endLocalCopy(root);
        return;
    }
    readInBackground(d.owner, std::move(reads), [this, root, offsets = std::move(offsets), next](ReadResults bytes) {
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            const auto at = m_downloads.find(root);
            if (at == m_downloads.end() || !at->second->copying)
                return;   // stopped, or done (the handler may stop it)
            if (i >= bytes.size() || !bytes[i]) {
                logWarning(QStringLiteral("local copy of %1 not readable at %2; downloaded instead").arg(hexOf(root.bytes)).arg(offsets[i]));
                endLocalCopy(root);
                return;
            }
            const QByteArray& b = *bytes[i];
            const bool taken = takeLocalBlock(*at->second, offsets[i],
                                              std::span{reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())});
            if (const auto again = m_downloads.find(root); taken && again != m_downloads.end())
                ++again->second->copiedBlocks;
        }
        copyLocalBatch(root, next);
        pumpSession();
    });
}

void Ed3kJoinBase::endLocalCopy(const ffi::Root& root)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end() || !it->second->copying)
        return;
    Download& d = *it->second;
    d.copying = false;
    d.copied = true;
    logDebug([&]() -> QString { return QStringLiteral("local copy of %1 read: %2 block(s)").arg(hexOf(root.bytes)).arg(d.copiedBlocks); });
    onLocalCopyDone(d, d.copiedBlocks);
    // the blocks still missing: from the network now
    if (downloading(root))
        requestBlocks(root);
    pumpSession();
}

void Ed3kJoinBase::saveDirty()
{
    const auto dirty = m_dirty;
    m_dirty.clear();
    for (const QString& owner : dirty)
        saveOwner(owner);
}

void Ed3kJoinBase::searchSourcesAgain(const ffi::Root& root, bool force)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || !m_searchEnabled || it == m_downloads.end())
        return;
    Download& d = *it->second;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && d.searchedAt != 0 && now - d.searchedAt < m_researchGapMs)
        return;
    d.searchedAt = now;
    // nodes asked before may have it now (none of them streams any more)
    if (d.streams.empty())
        d.asked.clear();
    if (d.key == ffi::KeyKind::Anon) {
        // the anonymous node searches through a last hop (hops.md §4.1); nothing while it is off
        if (!m_session.hide().hiding())
            return;
        ++m_sourceSearches;
        if (auto lookup = e->anonSearchSources(m_session.now(), root))
            m_sourceLookups[lookupKey(*lookup, ffi::KeyKind::Anon)] = root;
        return;
    }
    ++m_sourceSearches;
    if (auto lookup = e->searchSources(m_session.now(), root)) {
        m_sourceLookups[lookupKey(*lookup, ffi::KeyKind::Node)] = root;
        // the Kad window lists it by name; a lookup that ended within its start may be gone
        if (const QString name = downloadName(root, d); !name.isEmpty())
            (void)e->kadLookupName(*lookup, name.toStdString());
    }
}

void Ed3kJoinBase::onResearchTimer()
{
    std::vector<ffi::Root> idle;
    for (const auto& [root, d] : m_downloads)
        if (std::ranges::none_of(d->streams, [](const auto& s) { return s.second; }))
            idle.push_back(root);
    for (const auto& root : idle)
        searchSourcesAgain(root, false);
    if (!idle.empty())
        pumpSession();
}

void Ed3kJoinBase::onSetAliasFound(const ffi::event::AliasFound& found)
{
    const auto it = m_setSearches.find(found.search);
    if (it == m_setSearches.end())
        return;
    it->second.second = true;
    const QString owner = it->second.first;
    logDebug([&]() -> QString { return QStringLiteral("%1 set(s) claimed for %2").arg(found.roots).arg(owner); });
    auto& t = m_setTries[owner];
    if (!t)
        t = std::make_unique<SetTry>();
    // the first search with roots is tried; the others wait
    if (!t->search && !t->root && wantsSet(owner)) {
        t->search = found.search;
        (void)tryNextSet(owner);
    }
}

bool Ed3kJoinBase::tryNextSet(const QString& owner)
{
    auto* e = m_session.engine();
    const auto it = m_setTries.find(owner);
    if (!e || it == m_setTries.end())
        return false;
    SetTry& t = *it->second;
    if (t.search) {
        for (int tries = 0; tries < kSetCandidates; ++tries) {
            auto next = e->aliasSearchNext(*t.search);
            if (!next || !*next)
                break;
            const auto& c = **next;
            if (c.size == 0)
                continue;
            if (!fetchBlob(c.root, c.size, owner, kSetManifest))
                continue;
            t.root = c.root;
            t.publishers.assign(c.publishers.begin(), c.publishers.end());
            logInfo(QStringLiteral("trying set %1 of %2 (%3 site(s))").arg(hexOf(c.root.bytes), owner).arg(c.sites));
            return true;
        }
        (void)e->aliasSearchEnd(*t.search);
        m_setSearches.erase(*t.search);
        t.search.reset();
    }
    // another search of the owner that found roots (a hybrid torrent's second hash)
    for (const auto& [search, s] : m_setSearches) {
        if (s.first == owner && s.second) {
            t.search = search;
            return tryNextSet(owner);
        }
    }
    if (!setSearching(owner) && wantsSet(owner))
        onSetSearchDone(owner);
    return false;
}

Ed3kJoinBase::Kept Ed3kJoinBase::readKept(const QString& owner) const
{
    return readKeptIn(keptDir(owner));
}

Ed3kJoinBase::Kept Ed3kJoinBase::readKeptIn(const QString& dir)
{
    Kept kept;
    const QByteArray raw = readBlob(QDir(dir).filePath(kKeptIndex));
    for (const QString& line : QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() >= 3 && f.at(0) == QLatin1String("b")) {
            if (const auto root = rootOfHex(f.at(1)))
                kept.blobs.push_back({*root, f.at(2)});
        } else if (f.size() >= 3 && f.at(0) == QLatin1String("s")) {
            if (const auto root = rootOfHex(f.at(1)))
                kept.set = Kept::Set{*root, f.at(2)};
        } else if (f.size() >= 5 && f.at(0) == QLatin1String("m")) {
            bool okMember = false;
            bool okSize = false;
            const int member = f.at(1).toInt(&okMember);
            const std::uint64_t size = f.at(3).toULongLong(&okSize);
            if (const auto root = rootOfHex(f.at(2)); root && okMember && okSize && member >= 0)
                kept.members.push_back({member, *root, size, f.at(4), f.value(5)});
        }
    }
    return kept;
}

void Ed3kJoinBase::writeKept(const QString& owner, const Kept& kept)
{
    QString out;
    for (const auto& b : kept.blobs)
        out += QStringLiteral("b\t%1\t%2\n").arg(hexOf(b.root.bytes), field(b.name));
    for (const auto& m : kept.members)
        // was: m member root size name; a sixth column (path) older readers skip
        out += m.path.isEmpty()
            ? QStringLiteral("m\t%1\t%2\t%3\t%4\n").arg(m.member).arg(hexOf(m.root.bytes)).arg(m.size).arg(field(m.name))
            : QStringLiteral("m\t%1\t%2\t%3\t%4\t%5\n").arg(m.member).arg(hexOf(m.root.bytes)).arg(m.size).arg(field(m.name), field(m.path));
    if (kept.set)
        out += QStringLiteral("s\t%1\t%2\n").arg(hexOf(kept.set->root.bytes), field(kept.set->name));
    const QString dir = keptDir(owner);
    QDir().mkpath(dir);
    const QByteArray utf8 = out.toUtf8();
    writeBlobLater(QDir(dir).filePath(kKeptIndex), spanOf(utf8));
}

void Ed3kJoinBase::shareKeptMember(const QString& owner, int member, const ffi::Root& root, std::uint64_t size,
                                   const QString& name, int tries)
{
    const QString path = keptMemberPath(owner, member);
    if (path.isEmpty()) {
        if (tries == 0)
            keptMemberDone(owner);
        return;
    }
    const QString cache = QDir(keptDir(owner)).filePath(keptHashesFile(member));
    // was: the file's stat and the saved hashes read here, on the daemon thread: a busy disk
    // stalled the core 8 s at start (m0.hashes, QIODevice::readAll); now on the read pool
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    m_session.readPool().start([self, cancel, owner, member, root, size, name, tries, path, cache] {
        const QFileInfo before(path);
        const qint64 mtimeMs = before.lastModified().toMSecsSinceEpoch();
        std::optional<ffi::Bytes> hashes;
        if (before.isFile() && std::uint64_t(before.size()) == size)
            hashes = keptHashesOf(readBlob(cache), size, mtimeMs, root);
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, owner, member, root, size, name, tries, path, cache, mtimeMs, hashes = std::move(hashes)]() mutable {
                if (self && !cancel->load())
                    self->shareKeptChecked(owner, member, root, size, name, tries, path, cache, mtimeMs, std::move(hashes));
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::shareKeptChecked(const QString& owner, int member, const ffi::Root& root, std::uint64_t size,
                                    const QString& name, int tries, const QString& path, const QString& cache, qint64 mtimeMs,
                                    std::optional<ffi::Bytes> hashes)
{
    if (hashes) {
        logDebug([&]() -> QString { return QStringLiteral("kept member %1 of %2 unchanged: not hashed again").arg(member).arg(owner); });
        shareKeptHashed(owner, member, root, size, name, tries, path, FileHash{root, size, std::move(*hashes)});
        if (tries == 0)
            keptMemberDone(owner);
        return;
    }
    hashInBackground(path, [this, owner, member, root, size, name, tries, path, cache, mtimeMs](std::optional<FileHash> hashed) {
        if (hashed && hashed->root == root && hashed->size == size) {
            // the file as it was read: changed meanwhile, the next start hashes it again
            const QFileInfo after(path);
            if (after.lastModified().toMSecsSinceEpoch() == mtimeMs && std::uint64_t(after.size()) == size) {
                const QByteArray blob = keptHashesBlob(size, mtimeMs, root, hashed->hashes);
                writeBlobLater(cache, spanOf(blob));
            }
        }
        shareKeptHashed(owner, member, root, size, name, tries, path, hashed);
        // a first try decides, shared or not; a later one shares, the records are out already
        if (tries == 0)
            keptMemberDone(owner);
    });
}

void Ed3kJoinBase::shareKeptHashed(const QString& owner, int member, const ffi::Root& root, std::uint64_t size,
                                   const QString& name, int tries, const QString& path, const std::optional<FileHash>& hashed)
{
    {
        if (!hashed) {
            // gone or unreadable now: again later, and at the next start
            // was: retried when gone too; a PAR2 volume is deleted after repair: once per start
            if (!QFileInfo::exists(path)) {
                logDebug([&]() -> QString { return QStringLiteral("kept member %1 of %2 gone from disk (%3)").arg(member).arg(owner, path); });
                return;
            }
            logDebug([&]() -> QString { return QStringLiteral("kept member %1 of %2 not readable (%3)").arg(member).arg(owner, path); });
            if (tries + 1 < kRehashTries)
                QTimer::singleShot(kRehashMs, this, [this, owner, member, root, size, name, tries] {
                    if (m_started && keepsOwner(owner))
                        shareKeptMember(owner, member, root, size, name, tries + 1);
                });
            return;
        }
        if (hashed->root != root || hashed->size != size) {
            logInfo(QStringLiteral("kept member %1 of %2 changed: not shared any more (%3 B, %4; kept %5 B, %6)")
                        .arg(member).arg(owner).arg(hashed->size).arg(hexOf(hashed->root.bytes)).arg(size).arg(hexOf(root.bytes)));
            Kept kept = readKept(owner);
            std::erase_if(kept.members, [&](const Kept::Member& m) { return m.member == member; });
            writeKept(owner, kept);
            const QString cache = QDir(keptDir(owner)).filePath(keptHashesFile(member));
            forgetBlobWrites(cache);
            QFile::remove(cache);
            return;
        }
        const QByteArray n = name.toUtf8();
        const std::string fileName(n.constData(), std::size_t(n.size()));
        if (!shareMember(ffi::SharedFile{root, size, fileName, std::nullopt, std::nullopt, ffi::Have::complete(), std::span{hashed->hashes}},
                         owner, member))
            return;
        // was: ToDo: the ALIAS and MEMBER records of a kept post or torrent are not published
        // again (done: keepRecords, publishKeptRecords after the members)
        if (auto* e = m_session.engine()) {
            // sources 1: this node, the file held complete (Protocol.md §10.7)
            const ffi::FileInfo info{root, size, fileName, std::nullopt, 1, 0, 0, 0, 0, 0};
            // was: (void)e->publish(m_session.now(), info, m_holdBack ? ffi::Publish::Source : ffi::Publish::Both);
            Q_UNUSED(e)
            (void)publishShared(info);
        }
        logDebug([&]() -> QString { return QStringLiteral("kept member %1 of %2 shared again as %3").arg(member).arg(owner, hexOf(root.bytes)); });
        pumpSession();
    }
}

void Ed3kJoinBase::keptMemberDone(const QString& owner)
{
    const auto it = m_keptPending.find(owner);
    if (it == m_keptPending.end())
        return;
    if (--it->second > 0)
        return;
    m_keptPending.erase(it);
    if (m_started && keepsOwner(owner))
        publishKeptRecords(owner);
}

void Ed3kJoinBase::publishKeptRecords(const QString& owner)
{
    m_keptPending.erase(owner);
    if (!m_session.engine())
        return;
    // was: kept.tsv, the set manifest (.blob) and the records read here, on the daemon
    // thread (report 58: 8 s stall on a busy disk); now on the read pool
    const QString dir = keptDir(owner);
    const bool withSet = shareKeptBlobs();
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    m_session.readPool().start([self, cancel, owner, dir, withSet] {
        Kept kept = readKeptIn(dir);
        QByteArray manifest;
        if (kept.set && withSet)
            manifest = readBlob(QDir(dir).filePath(blobFile(kept.set->root)));
        auto records = readRecords(QDir(dir).filePath(kKeptRecords));
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, owner, kept = std::move(kept), manifest = std::move(manifest), records = std::move(records)] {
                if (self && !cancel->load())
                    self->publishKeptRead(owner, kept, manifest, records);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kJoinBase::publishKeptRead(const QString& owner, const Kept& kept, const QByteArray& manifest,
                                   const std::vector<ffi::Bytes>& records)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    // §6: the set is not shared while holding back (its manifest names the article list)
    std::vector<ffi::Root> memberRoots;
    for (const auto& m : kept.members)
        memberRoots.push_back(m.root);
    // was: if (kept.set && shareKeptBlobs()) {
    // hops.md §1: under its members' one key (setKeyOf)
    const auto setKey = setKeyOf(owner, memberRoots);
    if (kept.set && shareKeptBlobs() && !setKey)
        logDebug([&]() -> QString { return QStringLiteral("kept set of %1 not published: its members are not under one key it may use").arg(owner); });
    else if (kept.set && shareKeptBlobs()) {
        const QByteArray n = kept.set->name.toUtf8();
        if (manifest.isEmpty())
            logDebug([&]() -> QString { return QStringLiteral("kept set of %1 is gone: not published").arg(owner); });
        // was: e->setPublish(t, spanOf(manifest), kept.set->root, name)
        else if (auto ok = e->setPublish(t, spanOf(manifest), kept.set->root, std::string_view(n.constData(), std::size_t(n.size())),
                                         ffi::SetSigner::Unsigned, nullptr, *setKey);
                 !ok)
            logDebug([&]() -> QString { return QStringLiteral("kept set of %1 not published (error %2)").arg(owner, errorName(ok.error())); });
    }
    int published = 0;
    // §6: no ALIAS records while holding back
    int unshared = 0;
    if (!m_holdBack && !records.empty()) {
        // was: one batch; it stopped at the first record of a member gone from disk (State)
        // if (auto ok = e->publishAlias(t, records); !ok) ... else published = int(records.size());
        for (const auto& r : records) {
            // was: e->publishAlias(t, std::span(&r, 1)) (node key only)
            if (auto ok = publishAliasRecords(std::span(&r, 1)); ok)
                ++published;
            else if (ok.error() == ffi::Error::State)
                ++unshared;   // its file is not shared (any more)
            else
                logDebug([&]() -> QString { return QStringLiteral("kept ALIAS record of %1 not published (error %2)").arg(owner, errorName(ok.error())); });
        }
    }
    logDebug([&]() -> QString {
        return QStringLiteral("kept records of %1 published again (%2 alias(es), %3 of unshared files skipped)").arg(owner).arg(published).arg(unshared);
    });
    pumpSession();
    emit keptRecordsPublished(owner, published);
}

void Ed3kJoinBase::withdrawKeptSets()
{
    const QDir dir(joinDir(m_tag));
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (const Kept kept = readKept(QUrl::fromPercentEncoding(sub.toLatin1())); kept.set)
            unpublishSet(kept.set->root);
    pumpSession();
}

void Ed3kJoinBase::withdrawKeptRecords()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const QDir dir(joinDir(m_tag));
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto records = readRecords(QDir(dir.filePath(sub)).filePath(kKeptRecords));
        if (records.empty())
            continue;
        if (auto ok = e->unpublishAlias(m_session.now(), records); !ok)
            logDebug([&]() -> QString { return QStringLiteral("kept ALIAS records of %1 not withdrawn (error %2)")
                         .arg(QUrl::fromPercentEncoding(sub.toLatin1()), errorName(ok.error())); });
    }
    pumpSession();
}

std::map<const Ed3kSession*, Ed3kJoinBase::NodeTurns>& Ed3kJoinBase::turnsMap()
{
    static std::map<const Ed3kSession*, NodeTurns> map;
    return map;
}

Ed3kJoinBase::NodeTurns& Ed3kJoinBase::turnsOf(const Ed3kSession& session)
{
    return turnsMap()[&session];
}

Ed3kJoinBase::NodeTurns& Ed3kJoinBase::turns() const
{
    return turnsOf(m_session);
}

bool Ed3kJoinBase::openStream(const ffi::Root& root, const ffi::Source& source)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end())
        return false;
    Download& d = *it->second;
    const bool anon = d.key == ffi::KeyKind::Anon;
    if (anon && !m_session.hide().hiding())
        return false;
    const ffi::Request req{root, d.size, ffi::Have{}, 0, false, false};
    // was: auto stream = e->fileRequest(m_session.now(), source, req);
    auto stream = anon ? e->anonFileRequest(m_session.now(), source, req) : e->fileRequest(m_session.now(), source, req);
    if (!stream) {
        logDebug([&]() -> QString { return QStringLiteral("source refused (error %1)").arg(errorName(stream.error())); });
        return false;
    }
    // the anonymous node holds the root now: downloadOpen goes there (ffi.md hiding)
    if (anon && !d.anonOpened) {
        if (auto ok = e->downloadOpen(m_session.now(), root, d.size); !ok)
            logDebug([&]() -> QString { return QStringLiteral("anonymous download of %1 not opened (error %2)").arg(hexOf(root.bytes), errorName(ok.error())); });
        d.anonOpened = true;
    }
    d.asked.insert(source.id);
    d.streams[*stream] = false;
    m_streams[*stream] = root;
    // was: if (!anon) turns().holders[source.id] = ...
    turns().holders[TurnNode{d.key, source.id}] = NodeTurns::Holder{this, root, *stream, source};
    onStreamOpened(root, *stream, source);
    pumpSession();
    return true;
}

bool Ed3kJoinBase::waitTurn(const ffi::Root& root, const ffi::Source& source)
{
    auto& t = turns();
    const TurnNode node = turnNode(root, source.id);
    const auto h = t.holders.find(node);
    if (h == t.holders.end())
        return false;
    const NodeTurns::Holder& holder = h->second;
    if (holder.join == this && holder.root == root)
        return false;
    // a holder whose stream is gone without passing (should not happen): free
    if (!holder.join->m_streams.contains(holder.stream)) {
        t.holders.erase(h);
        return false;
    }
    auto& q = t.waiting[node];
    if (std::ranges::none_of(q, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; })) {
        const auto d = m_downloads.find(root);
        const int priority = d == m_downloads.end() ? 0 : downloadPriority(*d->second);
        q.push_back(NodeTurns::Want{this, root, source, priority, ++t.seq});
        logDebug([&]() -> QString { return QStringLiteral("%1 waits for its turn at %2").arg(hexOf(root.bytes), hexOf(source.id.bytes)); });
        // a higher priority asks first: the holder gives the node up now (its position stays, §12.5)
        (void)yieldToHigher(node);
    }
    return true;
}

std::optional<Ed3kJoinBase::TurnNode> Ed3kJoinBase::nodeHeldBy(std::uint64_t stream) const
{
    for (const auto& [node, h] : turns().holders)
        if (h.join == this && h.stream == stream)
            return node;
    return std::nullopt;
}

bool Ed3kJoinBase::passTurn(const TurnNode& node)
{
    auto& t = turns();
    t.holders.erase(node);
    for (;;) {
        const auto w = t.waiting.find(node);
        if (w == t.waiting.end())
            return false;
        if (w->second.empty()) {
            t.waiting.erase(w);
            return false;
        }
        // the highest priority first; the same in arrival order (round robin: a download
        // that switched away waits at the end)
        const auto best = std::ranges::max_element(w->second, [](const NodeTurns::Want& a, const NodeTurns::Want& b) {
            if (a.swapped != b.swapped)
                return b.swapped;
            return a.priority != b.priority ? a.priority < b.priority : a.seq > b.seq;
        });
        const NodeTurns::Want next = *best;
        w->second.erase(best);
        if (w->second.empty())
            t.waiting.erase(w);
        if (next.join->grantTurn(next.root, next.source))
            return true;
    }
}

bool Ed3kJoinBase::grantTurn(const ffi::Root& root, const ffi::Source& source)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return false;
    // was: if (it->second->streams.size() >= kMaxStreams) (now by priority)
    if (it->second->streams.size() >= streamCap(*it->second)) {
        it->second->asked.erase(source.id);   // offered again by a later search
        return false;
    }
    logDebug([&]() -> QString { return QStringLiteral("%1 has its turn at %2").arg(hexOf(root.bytes), hexOf(source.id.bytes)); });
    if (openStream(root, source))
        return true;
    it->second->asked.erase(source.id);
    return false;
}

bool Ed3kJoinBase::switchTurn(const ffi::Root& root, std::uint64_t stream)
{
    auto& t = turns();
    const auto node = nodeHeldBy(stream);
    const auto it = m_downloads.find(root);
    if (!node || it == m_downloads.end() || !t.waiting.contains(*node))
        return false;
    Download& d = *it->second;
    const NodeTurns::Holder holder = t.holders[*node];
    const auto slot = d.streams.find(stream);
    const bool held = slot != d.streams.end() && slot->second;
    // replaced at the core by the next file's stream (StreamClosed Replaced: not ours any more)
    d.streams.erase(stream);
    m_streams.erase(stream);
    std::erase_if(d.outstanding, [&](const auto& o) { return o.second == stream; });
    if (!passTurn(*node)) {
        // nobody took it: the stream stays
        d.streams[stream] = held;
        m_streams[stream] = root;
        t.holders[*node] = holder;
        return false;
    }
    d.sourceHave.erase(stream);
    // back in line for the node
    t.waiting[*node].push_back(NodeTurns::Want{this, root, holder.source, downloadPriority(d), ++t.seq});
    logDebug([&]() -> QString { return QStringLiteral("%1 gave its turn at %2 to the next file").arg(hexOf(root.bytes), hexOf(node->second.bytes)); });
    return true;
}

void Ed3kJoinBase::dropWaiters(const TurnNode& node)
{
    auto& t = turns();
    const auto w = t.waiting.find(node);
    if (w == t.waiting.end())
        return;
    const auto waiters = std::move(w->second);
    t.waiting.erase(w);
    for (const auto& x : waiters)
        x.join->turnLost(x.root, node.second);
}

void Ed3kJoinBase::turnLost(const ffi::Root& root, const ffi::NodeId& node)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return;
    it->second->asked.erase(node);
    if (it->second->streams.empty())
        searchSourcesAgain(root, false);
}

void Ed3kJoinBase::dropTurns(bool pass)
{
    auto& t = turns();
    for (auto& [node, q] : t.waiting)
        std::erase_if(q, [this](const NodeTurns::Want& w) { return w.join == this; });
    std::erase_if(t.waiting, [](const auto& q) { return q.second.empty(); });
    std::vector<TurnNode> held;
    for (const auto& [node, h] : t.holders)
        if (h.join == this)
            held.push_back(node);
    for (const auto& node : held) {
        if (pass) {
            // the next waiter's FileRequest replaces the stream (closeAll closes the rest)
            (void)passTurn(node);
            continue;
        }
        // destroyed: no engine calls; the waiters are asked again by a later search
        t.holders.erase(node);
        if (const auto w = t.waiting.find(node); w != t.waiting.end()) {
            for (const auto& x : w->second)
                if (const auto d = x.join->m_downloads.find(x.root); d != x.join->m_downloads.end())
                    d->second->asked.erase(node.second);
            t.waiting.erase(w);
        }
    }
}

bool Ed3kJoinBase::yieldToHigher(const TurnNode& node)
{
    auto& t = turns();
    const auto h = t.holders.find(node);
    const auto w = t.waiting.find(node);
    if (h == t.holders.end() || w == t.waiting.end())
        return false;
    const NodeTurns::Holder holder = h->second;
    const auto held = holder.join->m_downloads.find(holder.root);
    if (held == holder.join->m_downloads.end())
        return false;
    const int priority = holder.join->downloadPriority(*held->second);
    if (std::ranges::none_of(w->second, [&](const NodeTurns::Want& x) { return x.priority > priority; }))
        return false;
    // queued (no slot): at most one switch per 30 min at a node (Protocol.md §12.5 budget)
    const auto slot = held->second->streams.find(holder.stream);
    const bool queued = slot == held->second->streams.end() || !slot->second;
    const auto now = std::chrono::steady_clock::now();
    const auto last = t.queuedSwitch.find(node);
    if (!switchAllowed(queued, last == t.queuedSwitch.end() ? std::nullopt : std::optional(last->second), now))
        return false;
    // the best waiter (passTurn: highest first) takes it; the holder waits again
    // was: return holder.join->switchTurn(holder.root, holder.stream);
    const bool switched = holder.join->switchTurn(holder.root, holder.stream);
    if (switched && queued)
        t.queuedSwitch[node] = now;
    return switched;
}

std::size_t Ed3kJoinBase::streamCap(const Download& d) const
{
    if (const int limit = downloadSourceLimit(d); limit > 0)
        return std::size_t(limit);
    // very low 1, low 2, normal kMaxStreams (4), high 5, very high 6
    return std::max<std::size_t>(1, kMaxStreams * std::size_t(priorityWeight(d)) / 3);
}

std::size_t Ed3kJoinBase::rangesPerStream(const Download& d, std::size_t granted) const
{
    auto* e = m_session.engine();
    if (!e || granted == 0)
        return 1;
    const auto weight = std::uint64_t(priorityWeight(d));
    // every granted stream of the session, and their weights
    std::uint64_t all = 0;
    std::uint64_t weighted = 0;
    for (const Ed3kJoinBase* j : turns().joins)
        for (const auto& [root, other] : j->m_downloads) {
            const auto g = std::uint64_t(std::ranges::count_if(other->streams, [](const auto& s) { return s.second; }));
            all += g;
            weighted += g * std::uint64_t(j->priorityWeight(*other));
        }
    if (all < granted || weighted == 0) {
        // not registered (should not happen): alone
        all = granted;
        weighted = granted * weight;
    }
    const auto hint = std::uint64_t(std::clamp<std::uint32_t>(e->rangesHint(std::uint32_t(std::min<std::uint64_t>(all, 1u << 16))), 1, 16));
    // the session's ranges (hint × all) shared by weight (granted × weight / weighted of
    // them are this one's); per stream, rounded up
    const std::uint64_t share = (hint * all * weight + weighted - 1) / weighted;
    return std::clamp<std::size_t>(std::size_t(share), 1, 16);
}

bool Ed3kJoinBase::refreshStream(const ffi::Root& root, const ffi::Source& source)
{
    auto& t = turns();
    const auto h = t.holders.find(turnNode(root, source.id));
    if (h == t.holders.end() || h->second.join != this || !(h->second.root == root))
        return false;
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end() || !it->second->streams.contains(h->second.stream))
        return false;
    Download& d = *it->second;
    // same request as openStream: packmule hands back the stream it has
    // was: auto stream = e->fileRequest(m_session.now(), source, ffi::Request{root, d.size, ffi::Have{}, 0, false, false});
    const ffi::Request req{root, d.size, ffi::Have{}, 0, false, false};
    auto stream = d.key == ffi::KeyKind::Anon ? e->anonFileRequest(m_session.now(), source, req)
                                               : e->fileRequest(m_session.now(), source, req);
    if (!stream)
        return true;
    h->second.source = source;
    if (*stream != h->second.stream) {
        // the old one ended unseen: track the new one like openStream
        d.streams[*stream] = false;
        m_streams[*stream] = root;
        h->second.stream = *stream;
    }
    pumpSession();
    return true;
}

Ed3kJoinBase::TurnNode Ed3kJoinBase::turnNode(const ffi::Root& root, const ffi::NodeId& node) const
{
    const auto it = m_downloads.find(root);
    return TurnNode{it == m_downloads.end() ? ffi::KeyKind::Node : it->second->key, node};
}

QString Ed3kJoinBase::logPrefix() const
{
    // own tab: the tag alone; in the ed3k tab: "ed3k <tag>"
    return logCategory() == &lcEmuleEd3k ? QStringLiteral("ed3k %1: ").arg(m_tag)
                                        : m_tag + QStringLiteral(": ");
}

std::optional<Ed3kJoinBase::SourceInfo> Ed3kJoinBase::holderInfo(const TurnNode& node, const ffi::Root& root,
                                                                 std::uint64_t stream, const ffi::Source& source) const
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return std::nullopt;
    const Download& d = *it->second;
    SourceInfo s;
    s.key = node.first;
    s.node = node.second;
    s.root = root;
    s.join = m_tag;
    s.fileName = downloadName(root, d);
    s.size = d.size;
    // the anonymous key never learns where its uploader is (hops.md §1)
    if (node.first == ffi::KeyKind::Node)
        s.endpoint = source.ep4 ? source.ep4 : source.ep6;
    if (const auto st = d.streams.find(stream); st != d.streams.end())
        s.holding = st->second;
    if (const auto have = d.sourceHave.find(stream); have != d.sourceHave.end()) {
        s.have = have->second;
        s.haveKnown = true;
    }
    // our pieces: all but those with a block still wanted
    constexpr std::uint64_t kPiece = 16 * kBlock;
    if (d.size > 0) {
        s.ownPieces.assign(std::size_t((d.size + kPiece - 1) / kPiece), true);
        std::vector<std::uint64_t> wanted;
        if (d.inMemory) {
            for (std::uint64_t off = 0; off < d.size; off += kBlock)
                if (!d.got.contains(off))
                    wanted.push_back(off);
        } else {
            wanted = wantedBlocks(d);
        }
        for (const auto off : wanted)
            if (const auto p = std::size_t(off / kPiece); p < s.ownPieces.size())
                s.ownPieces[p] = false;
        // outstanding blocks are wanted too (wantedBlocks keeps them)
        for (const auto& [off, st] : d.outstanding) {
            if (const auto p = std::size_t(off / kPiece); p < s.ownPieces.size())
                s.ownPieces[p] = false;
            if (st == stream)
                s.asked.push_back(off);
        }
    }
    // the files of ours waiting for this node (A4AF files of the source)
    if (const auto w = turns().waiting.find(node); w != turns().waiting.end())
        for (const auto& x : w->second)
            if (const auto o = x.join->m_downloads.find(x.root); o != x.join->m_downloads.end())
                if (const QString name = x.join->downloadName(x.root, *o->second); !name.isEmpty() && !s.otherWants.contains(name))
                    s.otherWants << name;
    s.otherWants.sort(Qt::CaseInsensitive);
    return s;
}

void Ed3kJoinBase::skipRoot(const ffi::Root& root, Skipped s)
{
    const auto [it, added] = m_skipped.insert_or_assign(root, std::move(s));
    if (!added)
        return;
    // was: "%1 of %2 not joined on ed3k: its file is under the anonymous key and this network is not anonymous (hops.md §1)"
    // hops.md §1: no error, the user's hiding wins; the switch to keep it joined is named (#232)
    const QString what = it->second.member >= 0 ? QStringLiteral("file %1 of %2").arg(it->second.member).arg(it->second.owner)
                                                : QStringLiteral("a file of %1").arg(it->second.owner);
    const QString sw = anonSwitchName();
    logInfo(sw.isEmpty()
                ? QStringLiteral("%1 left out of this join: hiding shares it under the anonymous key").arg(what)
                : QStringLiteral("%1 left out of this join: hiding shares it under the anonymous key, %2 stay under the "
                                 "node key. To keep them joined, check \"%2 under the anonymous key\" in Options › ed3k › "
                                 "Extended › Kad publishing.").arg(what, sw));
}

QString Ed3kJoinBase::anonSwitchName() const
{
    // the per-network switches of Options › ed3k › Extended › Kad publishing
    if (m_tag == QLatin1String("torrent"))
        return QStringLiteral("Torrents");
    if (m_tag == QLatin1String("usenet"))
        return QStringLiteral("Usenet downloads");
    if (m_tag == QLatin1String("hoster"))
        return QStringLiteral("Hoster downloads");
    if (m_tag == QLatin1String("ed2k"))
        return QStringLiteral("eD2K downloads");
    return {};
}

void Ed3kJoinBase::takeFromNodeJoins(const ffi::Root& root)
{
    if (!anonCapable())
        return;
    // copied: yieldToAnon changes no registry, but stay safe against a join going away
    const auto joins = turnsOf(m_session).joins;
    for (Ed3kJoinBase* j : joins)
        if (j != this && !j->anonCapable() && (j->m_shared.contains(root) || j->m_downloads.contains(root)))
            j->yieldToAnon(root);
}

void Ed3kJoinBase::yieldToAnon(const ffi::Root& root)
{
    if (const auto it = m_shared.find(root); it != m_shared.end()) {
        Skipped s{it->second.owner, it->second.member};
        // its source record and keywords under the node key, then the file (§1: never both keys)
        unpublishShared(root, ffi::Publish::Both);
        unshare(root);
        if (s.member >= 0) {
            const QString owner = s.owner;
            skipRoot(root, std::move(s));
            withdrawSet(owner);
        }
    }
    if (const auto d = m_downloads.find(root); d != m_downloads.end()) {
        Skipped s{d->second->owner, d->second->member};
        stopDownload(root);
        if (s.member >= 0) {
            const QString owner = s.owner;
            skipRoot(root, std::move(s));
            withdrawSet(owner);
        }
    }
}

void Ed3kJoinBase::rejoinSkipped(const ffi::Root& root)
{
    const auto it = m_skipped.find(root);
    if (it == m_skipped.end())
        return;
    const Skipped s = it->second;
    m_skipped.erase(it);
    // a kept member (verified, its path known) is shared as after a restart; anything else
    // joins again with its job (next start, or the network's next verify)
    if (s.member < 0 || !keepsOwner(s.owner))
        return;
    const Kept kept = readKept(s.owner);
    const auto m = std::ranges::find_if(kept.members, [&](const Kept::Member& k) { return k.member == s.member && k.root == root; });
    if (m == kept.members.end())
        return;
    // its first try ends in keptMemberDone: the kept records (the set, withheld so far) go out again
    if (!m_keptPending.contains(s.owner))
        m_keptPending[s.owner] = 1;
    shareKeptMember(s.owner, s.member, root, m->size, m->name, 0);
}

bool Ed3kJoinBase::heldByNodeJoinsOnly(const ffi::Root& root) const
{
    bool nodeJoin = false;
    for (const Ed3kJoinBase* j : turnsOf(m_session).joins) {
        if (!j->m_shared.contains(root) && !j->m_downloads.contains(root))
            continue;
        if (j->anonCapable())
            return false;
        nodeJoin = true;
    }
    return nodeJoin;
}

void Ed3kJoinBase::dropSkipped(const QString& owner)
{
    std::erase_if(m_skipped, [&](const auto& kv) { return kv.second.owner == owner; });
}

void Ed3kJoinBase::withdrawSet(const QString& owner)
{
    auto* e = m_session.engine();
    if (!e || !keepsOwner(owner))
        return;
    if (const Kept kept = readKept(owner); kept.set)
        if (auto ok = e->setUnpublish(m_session.now(), kept.set->root); ok)
            // was: logInfo(... "set of %1 withdrawn: a member went to the anonymous key (hops.md §1)")
            // skipRoot told the user already
            logDebug([&]() -> QString { return QStringLiteral("set of %1 withdrawn: a file of it went to the anonymous key").arg(owner); });
}

} // namespace eMule::ed3k
