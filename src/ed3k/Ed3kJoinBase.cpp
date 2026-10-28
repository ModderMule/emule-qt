/// @file Ed3kJoinBase.cpp
/// @brief The ed3k side shared by every join — implementation.

#include "Ed3kJoinBase.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace eMule::ed3k {

namespace {

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

/// Hashes a file on a worker; nullopt when unreadable or cancelled.
std::optional<std::pair<ffi::Hashed, ffi::Bytes>> hashFile(const QString& path, const std::atomic<bool>& cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    ffi::Hasher h;
    QByteArray chunk;
    while (!f.atEnd()) {
        if (cancel.load())
            return std::nullopt;
        chunk = f.read(kHashChunk);
        if (chunk.isEmpty() && f.error() != QFileDevice::NoError)
            return std::nullopt;
        if (!h.update({reinterpret_cast<const std::byte*>(chunk.constData()), std::size_t(chunk.size())}))
            return std::nullopt;
    }
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

bool writeRecords(const QString& path, const std::vector<ffi::Bytes>& records)
{
    std::vector<std::byte> out;
    for (const auto& r : records) {
        const auto len = std::uint32_t(r.size());
        for (int i = 0; i < 4; ++i)
            out.push_back(std::byte((len >> (8 * i)) & 0xff));
        out.insert(out.end(), r.begin(), r.end());
    }
    return writeBlob(path, out);
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
    std::uint32_t search = 0;
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
// Daemon thread only.
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
    };
    std::map<ffi::NodeId, Holder> holders;
    std::map<ffi::NodeId, std::vector<Want>> waiting;
    std::uint64_t seq = 0;
    int users = 0;
    /// every join of the session (the shared block-request depth)
    std::set<Ed3kJoinBase*> joins;
    /// last priority switch while queued, per node: §12.5 counts it in the uploader's 30 min
    /// budget, a 2nd one within it is BUSY and the uploader keeps the old stream
    std::map<ffi::NodeId, std::chrono::steady_clock::time_point> queuedSwitch;
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
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kJoinBase::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kJoinBase::onSessionStatus);
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
    std::vector<ffi::NodeId> nodes;
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

bool Ed3kJoinBase::switchAllowed(bool queued, std::optional<std::chrono::steady_clock::time_point> last,
                                 std::chrono::steady_clock::time_point now)
{
    return !queued || !last || now - *last >= kQueuedSwitchGap;
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

std::optional<ffi::Root> Ed3kJoinBase::shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect)
{
    auto* e = m_session.engine();
    if (!e || data.isEmpty())
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
    if (auto ok = e->fileOpen(t, f); !ok && !m_shared.contains(hashed->root)) {
        logDebug(QStringLiteral("cannot share \"%1\" (error %2)").arg(name, errorName(ok.error())));
        return std::nullopt;
    }
    m_shared[hashed->root] = Shared{QString(), -1, hashed->size, data};
    const ffi::FileInfo info{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt, 0, 0, 0, 0, 0, 0};
    (void)e->publish(t, info, ffi::Publish::Source);
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
    if (auto ok = e->fileOpen(t, file); !ok) {
        logDebug(QStringLiteral("member %1 of %2 not shared (error %3)").arg(member).arg(owner, errorName(ok.error())));
        return false;
    }
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
}

void Ed3kJoinBase::unshareOwner(const QString& owner)
{
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

std::expected<ffi::Publish, ffi::Error> Ed3kJoinBase::publishShared(const ffi::FileInfo& info)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto t = m_session.now();
    const auto mode = m_holdBack ? ffi::Publish::Source : ffi::Publish::Both;
    auto ok = e->publish(t, info, mode);
    if (ok)
        return mode;
    // a name without a keyword: the engine refuses Both before it takes the source record
    // (packmule-ffi ed3k_publish), so the file would not be found by its root either
    if (mode == ffi::Publish::Both && ok.error() == ffi::Error::Malformed) {
        if (auto src = e->publish(t, info, ffi::Publish::Source); !src)
            return std::unexpected(src.error());
        return ffi::Publish::Source;
    }
    return std::unexpected(ok.error());
}

void Ed3kJoinBase::hashInBackground(const QString& path, std::function<void(std::optional<FileHash>)> done)
{
    QPointer<Ed3kJoinBase> self(this);
    const auto cancel = m_cancel;
    QThreadPool::globalInstance()->start([self, cancel, path, done = std::move(done)]() mutable {
        auto hashed = hashFile(path, *cancel);
        if (cancel->load())
            return;
        // owned copies into the lambda; ffi::Hashed holds a move-only Buffer
        std::optional<FileHash> out;
        if (hashed)
            out = FileHash{hashed->first.root, hashed->first.size, std::move(hashed->second)};
        QMetaObject::invokeMethod(
            self.data(),
            [self, cancel, done = std::move(done), out = std::move(out)]() mutable {
                if (self && !cancel->load())
                    done(std::move(out));
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
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(r.offset)))
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

bool Ed3kJoinBase::startSetSearch(const QString& owner, std::string_view scheme, std::span<const std::uint8_t> id,
                                  std::uint32_t rel)
{
    auto* e = m_session.engine();
    if (!e || !m_searchEnabled)
        return false;
    auto s = e->aliasSearchStart(m_session.now(), scheme, id, rel);
    if (!s) {
        logDebug(QStringLiteral("%1 search of %2 not started (error %3)")
                     .arg(QString::fromUtf8(scheme.data(), qsizetype(scheme.size())), owner, errorName(s.error())));
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
            (void)e->aliasSearchBlame(now, t.search, root);
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
    const auto root = shareBlob(data, name, expect);
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
    std::erase_if(kept.members, [&](const Kept::Member& m) { return m.member == member; });
    kept.members.push_back({member, root, size, name});
    writeKept(owner, kept);
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
    if (!writeRecords(path, records))
        logWarning(QStringLiteral("cannot write %1").arg(path));
}

void Ed3kJoinBase::shareKept()
{
    const QDir dir(joinDir(m_tag));
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString owner = QUrl::fromPercentEncoding(sub.toLatin1());
        if (!keepsOwner(owner)) {
            logDebug(QStringLiteral("%1 is gone: what was kept of it is dropped").arg(owner));
            QDir(dir.filePath(sub)).removeRecursively();
            continue;
        }
        const Kept kept = readKept(owner);
        if (shareKeptBlobs()) {
            for (const auto& b : kept.blobs) {
                const QByteArray data = readBlob(QDir(keptDir(owner)).filePath(blobFile(b.root)));
                if (data.isEmpty())
                    logDebug(QStringLiteral("kept \"%1\" of %2 is gone").arg(b.name, owner));
                else
                    (void)shareBlob(data, b.name, b.root);
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
}

void Ed3kJoinBase::dropKept(const QString& owner)
{
    QDir(keptDir(owner)).removeRecursively();
}

QString Ed3kJoinBase::keptDir(const QString& owner) const
{
    // reversible and safe as a file name (':' of torrent ids, '/')
    return QDir(joinDir(m_tag)).filePath(QString::fromLatin1(QUrl::toPercentEncoding(owner)));
}

void Ed3kJoinBase::scheduleSave(const QString& owner)
{
    if (!hasOwner(owner))
        return;
    m_dirty.insert(owner);
    if (!m_saveTimer.isActive())
        m_saveTimer.start(kSaveDelayMs);
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
    logEd3kDebug(QStringLiteral("ed3k %1: ").arg(m_tag) + msg);
}

void Ed3kJoinBase::logWarning(const QString& msg) const
{
    logEd3kWarning(QStringLiteral("ed3k %1: ").arg(m_tag) + msg);
}

void Ed3kJoinBase::logInfo(const QString& msg) const
{
    logEd3k(QStringLiteral("ed3k %1: ").arg(m_tag) + msg);
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

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kJoinBase::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace ev = ffi::event;
    if (!m_started)
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
            if (r->first && !r->autoAnswered)
                (void)m_session.engine()->queueSet(m_session.now(), r->node, 0, 0);
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
        if (m_streams.contains(fs->stream) && !fs->usable)
            (void)m_session.engine()->streamClose(m_session.now(), fs->stream);
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
        }
    } else if (const auto* kf = std::get_if<ev::KadFound>(&event)) {
        onKadFound(*kf);
    } else if (const auto* af = std::get_if<ev::AliasFound>(&event); af && m_setSearches.contains(af->search)) {
        onSetAliasFound(*af);
    } else {
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
            logDebug(QStringLiteral("%1 is gone, not shared any more").arg(hexOf(root.bytes)));
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

void Ed3kJoinBase::onKadFound(const ffi::event::KadFound& found)
{
    const auto it = m_sourceLookups.find(found.lookup);
    if (it == m_sourceLookups.end())
        return;
    const ffi::Root root = it->second;
    for (const auto& s : found.sources)
        addSource(root, s);
}

bool Ed3kJoinBase::open(const ffi::Root& root, std::uint64_t size, const QString& owner, int member, bool inMemory)
{
    auto* e = m_session.engine();
    if (!e || size == 0 || m_downloads.contains(root))
        return false;
    const auto t = m_session.now();
    if (auto ok = e->downloadOpen(t, root, size); !ok)
        logDebug(QStringLiteral("download of %1 not opened (error %2)").arg(hexOf(root.bytes), errorName(ok.error())));
    auto d = std::make_unique<Download>();
    d->owner = owner;
    d->member = member;
    d->size = size;
    d->inMemory = inMemory;
    m_downloads.emplace(root, std::move(d));
    // was: ToDo: search again every so often while no stream holds a slot (done: m_researchTimer)
    searchSourcesAgain(root, true);
    return true;
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
    ++m_sourceSearches;
    if (auto lookup = e->searchSources(m_session.now(), root))
        m_sourceLookups[*lookup] = root;
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
    logDebug(QStringLiteral("%1 set(s) claimed for %2").arg(found.roots).arg(owner));
    auto& t = m_setTries[owner];
    if (!t)
        t = std::make_unique<SetTry>();
    // the first search with roots is tried; the others wait
    if (t->search == 0 && !t->root && wantsSet(owner)) {
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
    if (t.search != 0) {
        for (int tries = 0; tries < kSetCandidates; ++tries) {
            auto next = e->aliasSearchNext(t.search);
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
        (void)e->aliasSearchEnd(t.search);
        m_setSearches.erase(t.search);
        t.search = 0;
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
    Kept kept;
    const QByteArray raw = readBlob(QDir(keptDir(owner)).filePath(kKeptIndex));
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
                kept.members.push_back({member, *root, size, f.at(4)});
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
        out += QStringLiteral("m\t%1\t%2\t%3\t%4\n").arg(m.member).arg(hexOf(m.root.bytes)).arg(m.size).arg(field(m.name));
    if (kept.set)
        out += QStringLiteral("s\t%1\t%2\n").arg(hexOf(kept.set->root.bytes), field(kept.set->name));
    const QString dir = keptDir(owner);
    QDir().mkpath(dir);
    const QByteArray utf8 = out.toUtf8();
    if (!writeBlob(QDir(dir).filePath(kKeptIndex), spanOf(utf8)))
        logWarning(QStringLiteral("cannot write %1").arg(QDir(dir).filePath(kKeptIndex)));
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
    hashInBackground(path, [this, owner, member, root, size, name, tries, path](std::optional<FileHash> hashed) {
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
            logDebug(QStringLiteral("kept member %1 of %2 not readable (%3)").arg(member).arg(owner, path));
            if (tries + 1 < kRehashTries)
                QTimer::singleShot(kRehashMs, this, [this, owner, member, root, size, name, tries] {
                    if (m_started && keepsOwner(owner))
                        shareKeptMember(owner, member, root, size, name, tries + 1);
                });
            return;
        }
        if (hashed->root != root || hashed->size != size) {
            logInfo(QStringLiteral("kept member %1 of %2 changed: not shared any more").arg(member).arg(owner));
            Kept kept = readKept(owner);
            std::erase_if(kept.members, [&](const Kept::Member& m) { return m.member == member; });
            writeKept(owner, kept);
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
            const ffi::FileInfo info{root, size, fileName, std::nullopt, 0, 0, 0, 0, 0, 0};
            // was: (void)e->publish(m_session.now(), info, m_holdBack ? ffi::Publish::Source : ffi::Publish::Both);
            Q_UNUSED(e)
            (void)publishShared(info);
        }
        logDebug(QStringLiteral("kept member %1 of %2 shared again as %3").arg(member).arg(owner, hexOf(root.bytes)));
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
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    const Kept kept = readKept(owner);
    // §6: the set is not shared while holding back (its manifest names the article list)
    if (kept.set && shareKeptBlobs()) {
        const QByteArray manifest = readBlob(QDir(keptDir(owner)).filePath(blobFile(kept.set->root)));
        const QByteArray n = kept.set->name.toUtf8();
        if (manifest.isEmpty())
            logDebug(QStringLiteral("kept set of %1 is gone: not published").arg(owner));
        else if (auto ok = e->setPublish(t, spanOf(manifest), kept.set->root, std::string_view(n.constData(), std::size_t(n.size()))); !ok)
            logDebug(QStringLiteral("kept set of %1 not published (error %2)").arg(owner, errorName(ok.error())));
    }
    const auto records = readRecords(QDir(keptDir(owner)).filePath(kKeptRecords));
    int published = 0;
    // §6: no ALIAS records while holding back
    if (!m_holdBack && !records.empty()) {
        if (auto ok = e->publishAlias(t, records); !ok)
            logDebug(QStringLiteral("kept ALIAS records of %1 not published (error %2)").arg(owner, errorName(ok.error())));
        else
            published = int(records.size());
    }
    logDebug(QStringLiteral("kept records of %1 published again (%2 alias(es))").arg(owner).arg(published));
    pumpSession();
    emit keptRecordsPublished(owner, published);
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
            logDebug(QStringLiteral("kept ALIAS records of %1 not withdrawn (error %2)")
                         .arg(QUrl::fromPercentEncoding(sub.toLatin1()), errorName(ok.error())));
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
    auto stream = e->fileRequest(m_session.now(), source, ffi::Request{root, d.size, ffi::Have{}, 0, false, false});
    if (!stream) {
        logDebug(QStringLiteral("source refused (error %1)").arg(errorName(stream.error())));
        return false;
    }
    d.asked.insert(source.id);
    d.streams[*stream] = false;
    m_streams[*stream] = root;
    turns().holders[source.id] = NodeTurns::Holder{this, root, *stream, source};
    pumpSession();
    return true;
}

bool Ed3kJoinBase::waitTurn(const ffi::Root& root, const ffi::Source& source)
{
    auto& t = turns();
    const auto h = t.holders.find(source.id);
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
    auto& q = t.waiting[source.id];
    if (std::ranges::none_of(q, [&](const NodeTurns::Want& w) { return w.join == this && w.root == root; })) {
        const auto d = m_downloads.find(root);
        const int priority = d == m_downloads.end() ? 0 : downloadPriority(*d->second);
        q.push_back(NodeTurns::Want{this, root, source, priority, ++t.seq});
        logDebug(QStringLiteral("%1 waits for its turn at %2").arg(hexOf(root.bytes), hexOf(source.id.bytes)));
        // a higher priority asks first: the holder gives the node up now (its position stays, §12.5)
        (void)yieldToHigher(source.id);
    }
    return true;
}

std::optional<ffi::NodeId> Ed3kJoinBase::nodeHeldBy(std::uint64_t stream) const
{
    for (const auto& [node, h] : turns().holders)
        if (h.join == this && h.stream == stream)
            return node;
    return std::nullopt;
}

bool Ed3kJoinBase::passTurn(const ffi::NodeId& node)
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
    logDebug(QStringLiteral("%1 has its turn at %2").arg(hexOf(root.bytes), hexOf(source.id.bytes)));
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
    logDebug(QStringLiteral("%1 gave its turn at %2 to the next file").arg(hexOf(root.bytes), hexOf(node->bytes)));
    return true;
}

void Ed3kJoinBase::dropWaiters(const ffi::NodeId& node)
{
    auto& t = turns();
    const auto w = t.waiting.find(node);
    if (w == t.waiting.end())
        return;
    const auto waiters = std::move(w->second);
    t.waiting.erase(w);
    for (const auto& x : waiters)
        x.join->turnLost(x.root, node);
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
    std::vector<ffi::NodeId> held;
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
                    d->second->asked.erase(node);
            t.waiting.erase(w);
        }
    }
}

bool Ed3kJoinBase::yieldToHigher(const ffi::NodeId& node)
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
    const auto h = t.holders.find(source.id);
    if (h == t.holders.end() || h->second.join != this || !(h->second.root == root))
        return false;
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end() || !it->second->streams.contains(h->second.stream))
        return false;
    Download& d = *it->second;
    // same request as openStream: packmule hands back the stream it has
    auto stream = e->fileRequest(m_session.now(), source, ffi::Request{root, d.size, ffi::Have{}, 0, false, false});
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

} // namespace eMule::ed3k
