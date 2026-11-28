/// @file Ed3kTorrentJoin.cpp
/// @brief A torrent joined with ed3k — implementation.

#include "Ed3kTorrentJoin.h"
#include "utils/ArgStr.h"

#ifdef EMULE_HAVE_BITTORRENT

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "BtSession.h"
#include "post/UsenetUnpacker.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <set>

namespace eMule::ed3k {

namespace {

// was: a local constant; now ED3K_REL_SET of ed3k.h (the base's startSetSearch default)
// /// ALIAS relation SET (alias.md §1): `root` is the manifest of a set
// constexpr std::uint32_t kRelSet = 1;
/// reads and events handled in one go; a guard, the core gives few
constexpr int kMaxPerCall = 1 << 16;
/// sources kept per join while its check runs
constexpr std::size_t kMaxHeldSources = 256;
// was: a local constant; now the base's kSetManifest (the same -1)
// /// member index of a manifest fetched into memory
// constexpr int kManifest = -1;

std::optional<std::vector<std::uint8_t>> hexBytes(const QString& hex, qsizetype len)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    if (raw.size() != len)
        return std::nullopt;
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

} // namespace

// The sinks reach the join through this: closed when the join goes.
struct Ed3kTorrentJoin::Gate {
    std::mutex mutex;
    Ed3kTorrentJoin* join = nullptr;
};

// libtorrent's thread → the daemon thread.
class Ed3kTorrentJoin::Sink final : public bt::BtJoinSink {
public:
    explicit Sink(std::shared_ptr<Gate> gate) : m_gate(std::move(gate)) {}

    void pieceHashed(const QString& id, int piece, QByteArray data, std::function<void(bool)> done) override
    {
        std::scoped_lock lock(m_gate->mutex);
        if (!m_gate->join) {
            done(false);
            return;
        }
        // posting an event is thread-safe; the join cannot go while the gate is held
        QMetaObject::invokeMethod(
            m_gate->join,
            [gate = m_gate, id, piece, data = std::move(data), done = std::move(done)]() mutable {
                Ed3kTorrentJoin* join = gate->join;   // this thread clears it: no lock needed
                if (join)
                    join->onPieceHashed(id, piece, data, std::move(done));
                else
                    done(false);
            },
            Qt::QueuedConnection);
    }

    bool judges(const QString&) const override { return m_judges.load(std::memory_order_relaxed); }
    /// on the daemon thread, from the join's state (updateJudging)
    void setJudges(bool on) { m_judges.store(on, std::memory_order_relaxed); }

private:
    std::shared_ptr<Gate> m_gate;
    std::atomic<bool> m_judges{true};
};

// One torrent: the core's join and what the host keeps.
struct Ed3kTorrentJoin::Join {
    explicit Join(ffi::Torrent t) : torrent(std::move(t)) {}
    ffi::Torrent torrent;
    std::optional<ffi::Root> link;   ///< §4.1: the manifest root of the ed3k:file link
    std::uint64_t linkSize = 0;
    bt::BtLayout layout;
    std::vector<int> memberFile;     ///< member → index in layout.files
    struct Member {
        ffi::Bytes hashes;           ///< block values: own hash, or of a download's Hashes event
        bool hashing = false;
        bool flushWait = false;      ///< hashed once libtorrent's cache is on disk
        quint64 generation = 0;
        int rehashTries = 0;
        std::set<std::uint64_t> received;   ///< ed3k blocks written (offsets)
        // was: copying, copied (#224); now the base's Download
        std::set<std::uint64_t> local;      ///< #224: blocks of received taken from a local copy
    };
    std::vector<Member> members;
    /// pieces libtorrent has (havePieces, then its verdicts): ed3k skips their blocks
    QBitArray have;
    /// pieces whose hash job waits for a verdict (PENDING, held)
    std::map<int, std::function<void(bool)>> waiting;
    // was: the set search state; now the base's (startSetSearch, setTrying, refuseSet)
    // std::uint32_t search = 0;
    // std::optional<ffi::Root> trying;
    // std::vector<ffi::PublisherKey> tryingPublishers;
    QByteArray completedManifest;
    std::shared_ptr<Sink> sink;
    /// TORRENT_COMPLETE came before every member was hashed: complete() after the last
    bool completePending = false;
    /// complete members whose file is gone from disk since the last summary line
    int missing = 0;
    QString firstMissing;
    /// #264: sources found before libtorrent's check, asked once it is done (member, source)
    std::vector<std::pair<int, ffi::Source>> held;
};

Ed3kTorrentJoin::Ed3kTorrentJoin(Ed3kSession& session, bt::BtSession& bt, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("torrent"), parent)
    , m_bt(bt)
    , m_gate(std::make_shared<Gate>())
{
    m_gate->join = this;
    connect(&m_bt, &bt::BtSession::torrentAdded, this, &Ed3kTorrentJoin::onTorrentAdded);
    connect(&m_bt, &bt::BtSession::metadataReceived, this, &Ed3kTorrentJoin::onMetadataReceived);
    connect(&m_bt, &bt::BtSession::torrentRemoved, this, &Ed3kTorrentJoin::onTorrentRemoved);
    connect(&m_bt, &bt::BtSession::pieceResult, this, &Ed3kTorrentJoin::onPieceResult);
    connect(&m_bt, &bt::BtSession::torrentChecked, this, &Ed3kTorrentJoin::onTorrentChecked);
    connect(&m_bt, &bt::BtSession::cacheFlushed, this, &Ed3kTorrentJoin::onCacheFlushed);
}

Ed3kTorrentJoin::~Ed3kTorrentJoin()
{
    stop();
    std::scoped_lock lock(m_gate->mutex);
    m_gate->join = nullptr;
}

void Ed3kTorrentJoin::start()
{
    if (m_started || !m_session.isRunning())
        return;
    beginStart();
    applyPrivateJoins();
    QDir().mkpath(torrentDir());
    for (const auto& row : m_bt.torrents())
        (void)ensureJoin(row.id);
    // what completed before the restart: manifest, info dictionary, verified members
    shareKept();
    pumpSession();
}

void Ed3kTorrentJoin::stop()
{
    if (!m_started)
        return;
    for (const auto& [id, j] : m_joins)
        saveJoin(id);
    flushBlobWrites();   // on disk once stopped
    // the set searches end in closeAll()
    for (auto& [id, j] : m_joins) {
        m_bt.detachJoin(id);
        // libtorrent's verdict stands for every piece still waiting
        for (auto& [piece, done] : j->waiting)
            done(false);
        j->waiting.clear();
    }
    closeAll();
    m_joins.clear();
    m_noJoin.clear();
    m_keptWaiting.clear();
    pumpSession();
}

void Ed3kTorrentJoin::setI2pJoins(bool allowed)
{
    if (allowed == m_i2pJoins)
        return;
    m_i2pJoins = allowed;
    rejoin(allowed, [](const bt::BtTorrentRow& row) { return row.i2p; });
}

void Ed3kTorrentJoin::setPrivateJoins(bool allowed)
{
    if (allowed == m_privateJoins)
        return;
    m_privateJoins = allowed;
    applyPrivateJoins();
    rejoin(allowed, [](const bt::BtTorrentRow& row) { return row.isPrivate; });
}

bool Ed3kTorrentJoin::i2pHeld(const QString& id) const
{
    if (m_i2pJoins)
        return false;
    const auto row = m_bt.torrent(id);
    return row && row->i2p;
}

bool Ed3kTorrentJoin::privateHeld(const QString& id) const
{
    if (m_privateJoins)
        return false;
    const auto row = m_bt.torrent(id);
    return row && row->isPrivate;
}

bool Ed3kTorrentJoin::sharesKeptOwner(const QString& owner) const
{
    // its paths and flags come with the row
    if (!m_bt.torrent(owner)) {
        m_keptWaiting.insert(owner);
        return false;
    }
    return !i2pHeld(owner) && !privateHeld(owner);
}

bool Ed3kTorrentJoin::hasJoin(const QString& id) const
{
    return m_joins.contains(id);
}

std::optional<ffi::TorrentInfo> Ed3kTorrentJoin::info(const QString& id) const
{
    const Join* j = findJoin(id);
    if (!j)
        return std::nullopt;
    auto i = j->torrent.info();
    return i ? std::optional(*i) : std::nullopt;
}

std::optional<ffi::TorrentMember> Ed3kTorrentJoin::member(const QString& id, int index) const
{
    const Join* j = findJoin(id);
    if (!j || index < 0)
        return std::nullopt;
    auto m = j->torrent.member(std::uint64_t(index));
    return m ? std::optional(*m) : std::nullopt;
}

QString Ed3kTorrentJoin::joinPath(const QString& id) const
{
    QString file = id;
    file.replace(QLatin1Char(':'), QLatin1Char('_'));
    return QDir(torrentDir()).filePath(file + QStringLiteral(".join"));
}

bool Ed3kTorrentJoin::setManifest(const QString& id, std::span<const std::byte> manifest,
                                  std::span<const ffi::PublisherKey> publishers)
{
    Join* j = ensureJoin(id);
    if (!j)
        return false;
    if (auto ok = j->torrent.setManifest(manifest, publishers); !ok) {
        logWarning(QStringLiteral("set of %1 refused (error %2)").arg(name(id), errorName(ok.error())));
        return false;
    }
    logInfo(QStringLiteral("\"%1\" has a set now").arg(name(id)));
    scheduleSave(id);
    startDownloads(id);
    drainEvents(id);
    pumpSession();
    return true;
}

void Ed3kTorrentJoin::addSource(const QString& id, int index, const ffi::Source& source)
{
    const auto m = member(id, index);
    if (!m || !m->root)
        return;
    if (!m_bt.filesChecked(id)) {
        // no download open before the check: the base would drop it
        if (Join* j = findJoin(id); j && j->held.size() < kMaxHeldSources)
            j->held.emplace_back(index, source);
        return;
    }
    startDownloads(id);
    Ed3kJoinBase::addSource(*m->root, source);
}

void Ed3kTorrentJoin::addManifestSource(const QString& id, const ffi::Source& source)
{
    const Join* j = findJoin(id);
    if (!j)
        return;
    if (const auto trying = setTrying(id))
        Ed3kJoinBase::addSource(*trying, source);
    else if (j->link)
        Ed3kJoinBase::addSource(*j->link, source);
}

QByteArray Ed3kTorrentJoin::completedManifest(const QString& id) const
{
    const Join* j = findJoin(id);
    return j ? j->completedManifest : QByteArray();
}

void Ed3kTorrentJoin::onTorrentAdded(const QString& id)
{
    if (m_started) {
        (void)ensureJoin(id);
        // resumed after our start: its set, records and members out again
        if (m_keptWaiting.remove(id))
            shareKeptOwner(id);
    }
    pumpSession();
}

void Ed3kTorrentJoin::onMetadataReceived(const QString& id)
{
    if (m_started)
        (void)ensureJoin(id);
    pumpSession();
}

void Ed3kTorrentJoin::onTorrentRemoved(const QString& id)
{
    dropJoin(id, /*keepBlob*/ false);
    m_noJoin.remove(id);
    m_created.remove(id);
    // its files are gone or not wanted: not shared any more
    unshareOwner(id);
    dropKept(id);
    pumpSession();
}

void Ed3kTorrentJoin::onPieceResult(const QString& id, int piece, bool passed)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    if (piece >= 0 && piece < j->have.size())
        j->have.setBit(piece, passed);
    if (!passed) {
        logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" failed in BitTorrent").arg(piece).arg(name(id)); });
        // #224: blocks of a local copy in it are asked for on ed3k (the engine verified the
        // network's; it would not send those again)
        if (const auto extents = j->torrent.extents(std::uint64_t(piece)))
            for (const auto& x : *extents) {
                if (x.member >= j->members.size())
                    continue;
                auto& mem = j->members[std::size_t(x.member)];
                const auto first = mem.local.lower_bound(x.offset - x.offset % kBlock);
                const auto last = mem.local.lower_bound(x.offset + x.len);
                for (auto it = first; it != last; ++it)
                    mem.received.erase(*it);
                mem.local.erase(first, last);
            }
    }
    if (auto ok = j->torrent.pieceResult(std::uint64_t(piece), passed); !ok && ok.error() != ffi::Error::NotFound)
        logDebug([&]() -> QString { return QStringLiteral("verdict on piece %1 of %2 not taken (error %3)").arg(piece).arg(id, errorName(ok.error())); });
    drainEvents(id);
    pumpSession();
}

void Ed3kTorrentJoin::onCacheFlushed(const QString& id)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    for (std::size_t m = 0; m < j->members.size(); ++m)
        if (j->members[m].flushWait)
            hashMember(id, int(m));
}

void Ed3kTorrentJoin::onTorrentChecked(const QString& id)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    // the resume check found pieces: ask ed3k only for the rest
    j->have = m_bt.havePieces(id);
    if (j->torrent.info() && j->torrent.info()->manifest)
        startDownloads(id);
    for (const auto& [index, source] : std::exchange(j->held, {}))
        addSource(id, index, source);
    pumpSession();
}

void Ed3kTorrentJoin::adoptCreated(const QString& id)
{
    m_created.insert(id);
    m_noJoin.remove(id);
    if (m_started)
        (void)ensureJoin(id);   // null until libtorrent has it: onTorrentAdded then
    pumpSession();
}

int Ed3kTorrentJoin::receivedBlocks(const QString& id, int member) const
{
    const Join* j = findJoin(id);
    if (!j || member < 0 || std::size_t(member) >= j->members.size())
        return 0;
    return int(j->members[std::size_t(member)].received.size());
}

bool Ed3kTorrentJoin::hasBlockValues(const QString& id, int member) const
{
    const Join* j = findJoin(id);
    return j && member >= 0 && std::size_t(member) < j->members.size() && !j->members[std::size_t(member)].hashes.empty();
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

QString Ed3kTorrentJoin::downloadName(const ffi::Root& root, const Download& d) const
{
    Q_UNUSED(root)
    return name(d.owner);
}

std::vector<std::uint64_t> Ed3kTorrentJoin::wantedBlocks(const Download& d) const
{
    const Join* j = findJoin(d.owner);
    if (!j || d.member < 0 || std::size_t(d.member) >= j->members.size())
        return {};
    const auto m = j->torrent.member(std::uint64_t(d.member));
    if (!m || m->status != ffi::MemberStatus::Open)
        return {};
    // #264: in a (re)check, nothing: takeBlock would refuse it anyway
    if (!m_bt.filesChecked(d.owner))
        return {};
    // was: ToDo: skip the blocks libtorrent has already (done: Join::have, piecesHad)
    // was: #224 nothing while copying (members[].copying); now the base holds its requests
    const auto& received = j->members[std::size_t(d.member)].received;
    std::vector<std::uint64_t> wanted;
    for (std::uint64_t off = 0; off < d.size; off += kBlock)
        if (!received.contains(off) && !piecesHad(*j, m->start + off, m->start + std::min(off + kBlock, d.size)))
            wanted.push_back(off);
    return wanted;
}

void Ed3kTorrentJoin::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    // was: the write, ed3kBlock and feedPieces here; now takeBlock (a local copy's blocks too)
    // was: takeBlock(d.owner, ...) / drainEvents(d.owner): a completed member stops its
    // download (onMemberComplete), d.owner dangled and the member was never hashed
    const QString id = d.owner;
    if (!takeBlock(id, d.member, b.offset, std::span<const std::byte>{b.data}))
        return;
    drainEvents(id);
}

bool Ed3kTorrentJoin::takeLocalBlock(const Download& d, std::uint64_t offset, std::span<const std::byte> data)
{
    // #224: like a network block; remembered, a failed piece asks ed3k for it again. Copies:
    // the handlers may stop the download (d gone)
    const QString id = d.owner;
    const int member = d.member;
    if (!takeBlock(id, member, offset, data))
        return false;
    if (Join* j = findJoin(id); j && member >= 0 && std::size_t(member) < j->members.size())
        j->members[std::size_t(member)].local.insert(offset);
    drainEvents(id);
    return true;
}

void Ed3kTorrentJoin::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    Join* j = findJoin(owner);
    if (!j || member != kSetManifest)
        return;
    if (j->link && root == *j->link) {
        // §4.1: the manifest of the link
        if (!setManifest(owner, spanOf(data)))
            reportProblem(owner, -1, tr("ed3k: the ed3k link of \"%1\" names no set of this torrent").arg(name(owner)));
        else
            keepBlob(owner, data, name(owner), root);
        return;
    }
    // was: §4.2 a set a record claims, here; now onSetManifest (the base routes it)
}

void Ed3kTorrentJoin::onSetManifest(const QString& owner, const ffi::Root& root, const QByteArray& data)
{
    // §4.2: a set a record claims
    if (setManifest(owner, spanOf(data), setPublishers(owner))) {
        keepBlob(owner, data, name(owner), root);
        return;
    }
    (void)refuseSet(owner, root);
}

bool Ed3kTorrentJoin::wantsSet(const QString& owner) const
{
    const Join* j = findJoin(owner);
    if (!j)
        return false;
    const auto inf = j->torrent.info();
    return inf && !inf->manifest;
}

bool Ed3kTorrentJoin::keepsOwner(const QString& owner) const
{
    // the rows come with each add_torrent_alert after BtSession::start: dropping what was
    // kept before that lost every completed set at a restart (no republish)
    return m_bt.torrent(owner).has_value() || !m_bt.isRunning() || m_bt.isResuming(owner);
}

QString Ed3kTorrentJoin::keptMemberPath(const QString& owner, int member) const
{
    if (const Join* j = findJoin(owner))
        return memberPath(*j, member);
    // no join (complete, its saved join gone): members are the files without padding
    const auto layout = m_bt.layout(owner);
    if (!layout || member < 0)
        return {};
    int index = 0;
    for (const auto& f : layout->files) {
        if (f.pad)
            continue;
        if (index++ == member)
            return f.path;
    }
    return {};
}

void Ed3kTorrentJoin::onDownloadHashes(const Download& d, const ffi::Bytes& blob)
{
    Join* j = findJoin(d.owner);
    if (j && d.member >= 0 && std::size_t(d.member) < j->members.size() && j->members[std::size_t(d.member)].hashes.empty())
        j->members[std::size_t(d.member)].hashes = blob;
}

std::optional<Ed3kJoinBase::FileRead> Ed3kTorrentJoin::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    // only bytes verified under the root: checked-only data stays BitTorrent's (§4.1)
    const Join* j = findJoin(s.owner);
    if (!j)   // a kept member of a torrent without a join: hashed again under its root before it was shared
        return FileRead{keptMemberPath(s.owner, s.member), offset, len};
    if (!j->torrent.servable(std::uint64_t(s.member), offset, len))
        return std::nullopt;
    return FileRead{memberPath(*j, s.member), offset, len};
}

// was: onOtherEvent took the AliasFound of the set searches; now the base's (onSetAliasFound)

bool Ed3kTorrentJoin::hasOwner(const QString& owner) const
{
    return m_joins.contains(owner);
}

void Ed3kTorrentJoin::saveOwner(const QString& owner)
{
    saveJoin(owner);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

Ed3kTorrentJoin::Join* Ed3kTorrentJoin::ensureJoin(const QString& id)
{
    if (Join* j = findJoin(id))
        return j;
    auto* e = m_session.engine();
    if (!m_started || !e || m_noJoin.contains(id))
        return nullptr;
    // an I2P or private torrent's hash stays off ed3k unless allowed; not in m_noJoin: the pref may flip
    if (i2pHeld(id) || privateHeld(id))
        return nullptr;
    const QByteArray infoDict = m_bt.infoSection(id);
    const auto layout = m_bt.layout(id);
    if (infoDict.isEmpty() || !layout)
        return nullptr;   // a magnet without metadata yet: onMetadataReceived

    std::optional<ffi::Root> link;
    std::uint64_t linkSize = 0;
    if (const QString text = m_bt.ed3kLink(id); !text.isEmpty()) {
        const QByteArray utf8 = text.toUtf8();
        if (auto parsed = ffi::parseFileLink(std::string_view(utf8.constData(), std::size_t(utf8.size())))) {
            link = parsed->root;
            linkSize = parsed->size;
        } else {
            // torrent.md §7: an invalid xs is ignored, the magnet goes on
            logDebug([&]() -> QString { return QStringLiteral("ed3k link of \"%1\" ignored (error %2)").arg(name(id), errorName(parsed.error())); });
        }
    }

    std::optional<ffi::Torrent> torrent;
    const QByteArray blob = readBlob(joinPath(id));
    if (!blob.isEmpty()) {
        auto loaded = ffi::Torrent::load(*e, spanOf(infoDict), link, spanOf(blob));
        if (loaded)
            torrent.emplace(std::move(*loaded));
        else
            logWarning(QStringLiteral("saved join of \"%1\" dropped (error %2)").arg(name(id), errorName(loaded.error())));
    }
    if (!torrent) {
        // complete before ed3k saw it: nothing to judge or fetch any more. One made here
        // (#191) joins all the same: its BLAKE3 roots and set come from the join
        if (const auto row = m_bt.torrent(id);
            !m_created.contains(id) && row && row->haveBytes >= row->totalSize && row->totalSize > 0) {
            m_noJoin.insert(id);
            return nullptr;
        }
        auto created = ffi::Torrent::create(*e, spanOf(infoDict), link);
        if (!created) {
            // DENIED: private, not shared (§6.3); UNSUPPORTED: the extension is off
            logDebug([&]() -> QString { return QStringLiteral("no join for \"%1\" (error %2)").arg(name(id), errorName(created.error())); });
            m_noJoin.insert(id);
            return nullptr;
        }
        torrent.emplace(std::move(*created));
    }

    auto j = std::make_unique<Join>(std::move(*torrent));
    j->link = link;
    j->linkSize = linkSize;
    j->layout = *layout;
    for (int f = 0; f < layout->files.size(); ++f)
        if (!layout->files.at(f).pad)
            j->memberFile.push_back(f);
    const auto info = j->torrent.info();
    if (!info || info->members != j->memberFile.size()) {
        logWarning(QStringLiteral("\"%1\": %2 member(s), %3 file(s): no join")
                       .arg(argStr(name(id)), argStr(info ? info->members : 0), argStr(j->memberFile.size())));
        m_noJoin.insert(id);
        return nullptr;
    }
    j->members.resize(j->memberFile.size());
    Join* raw = j.get();
    m_joins.emplace(id, std::move(j));
    raw->sink = std::make_shared<Sink>(m_gate);
    updateJudging(*raw);
    m_bt.attachJoin(id, raw->sink);
    logInfo(QStringLiteral("\"%1\" joined (%2 member(s), %3)")
                .arg(argStr(name(id)),
                     argStr(raw->members.size()),
                     argStr(link ? QStringLiteral("from its ed3k link") : QStringLiteral("torrent alone"))));

    // members complete but not hashed before the restart (unreadable then): hashed again
    for (std::size_t m = 0; m < raw->members.size(); ++m)
        if (const auto tm = raw->torrent.member(m); tm && tm->status == ffi::MemberStatus::Complete)
            onMemberComplete(id, int(m));
    if (m_created.remove(id)) {
        passCreatedPieces(id);   // made here: no set to look for, ours is the first
    } else if (info->manifest) {
        startDownloads(id);   // a restart: the set is known
    } else if (link) {
        if (linkSize == 0 || !fetchBlob(*link, linkSize, id, kSetManifest))
            logDebug([&]() -> QString { return QStringLiteral("set of \"%1\" not fetched").arg(name(id)); });
    } else {
        startSetSearch(id);
    }
    scheduleSave(id);
    return findJoin(id);
}

void Ed3kTorrentJoin::passCreatedPieces(const QString& id)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    // the info dictionary was hashed from these files: every piece passed under the
    // torrent (§4.2), so members complete, get their BLAKE3 roots, and the set follows
    const int pieces = j->layout.pieces;
    j->have.fill(true, pieces);
    for (int p = 0; p < pieces; ++p) {
        if (auto ok = j->torrent.pieceResult(std::uint64_t(p), true); !ok) {
            logWarning(QStringLiteral("\"%1\": piece %2 not taken (error %3)").arg(argStr(name(id)), argStr(p), argStr(errorName(ok.error()))));
            return;
        }
    }
    logInfo(QStringLiteral("\"%1\" made here: hashing its files for ed3k").arg(name(id)));
    drainEvents(id);
}

Ed3kTorrentJoin::Join* Ed3kTorrentJoin::findJoin(const QString& id) const
{
    const auto it = m_joins.find(id);
    return it == m_joins.end() ? nullptr : it->second.get();
}

void Ed3kTorrentJoin::dropJoin(const QString& id, bool keepBlob)
{
    if (const auto it = m_joins.find(id); it != m_joins.end()) {
        m_bt.detachJoin(id);
        for (auto& [piece, done] : it->second->waiting)
            done(false);
        stopDownloads(id);
        endSetSearches(id);
        m_joins.erase(it);
    }
    dropDirty(id);
    if (!keepBlob) {
        forgetBlobWrites(joinPath(id));
        QFile::remove(joinPath(id));
    }
}

void Ed3kTorrentJoin::onPieceHashed(const QString& id, int piece, const QByteArray& data, std::function<void(bool)> done)
{
    Join* j = findJoin(id);
    if (!j || !m_started) {
        done(false);
        return;
    }
    auto check = j->torrent.pieceHashed(m_session.engine(), std::uint64_t(piece), spanOf(data));
    if (!check) {
        logDebug([&]() -> QString { return QStringLiteral("piece %1 of %2 not checked (error %3)").arg(piece).arg(id, errorName(check.error())); });
        done(false);
        return;
    }
    switch (check->verdict) {
    case ffi::PieceVerdict::Pass:
    case ffi::PieceVerdict::Unknown:
    case ffi::PieceVerdict::Pending:
        // only §4.1 holds; §4.2 waited here too, and a slow or failed read of ours failed
        // a piece the torrent passed (report 31)
        if (!check->hold) {
            done(false);
            break;
        }
        if (auto old = j->waiting.find(piece); old != j->waiting.end())
            old->second(false);   // a second hash of the same piece: the first one goes
        j->waiting[piece] = std::move(done);
        break;
    case ffi::PieceVerdict::Fail:
        if (blakeJudges(*j)) {
            logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" failed under BLAKE3").arg(piece).arg(name(id)); });
            done(true);
        } else {
            // §4.2: the torrent's hash judges, the Kad set is only a claim. Spoiling here
            // failed good pieces on every recheck and the claim never proved false (report 31)
            logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" fails the claimed set; the torrent hash decides").arg(piece).arg(name(id)); });
            done(false);
        }
        break;
    }
    if (check->hold) {
        // the blocks it waits for are in the next pieces: ask for those first
        if (auto wanted = j->torrent.wantedPieces(std::uint64_t(piece)); wanted && !wanted->empty()) {
            QList<int> raise;
            for (const auto p : *wanted)
                raise.append(int(p));
            m_bt.raisePieces(id, raise);
        }
        QTimer::singleShot(kHoldTimeoutMs, this, [this, id, piece] { onHoldTimeout(id, piece); });
    }
    serveReads(id, check->reads);
    pumpSession();
}

void Ed3kTorrentJoin::serveReads(const QString& id, const std::vector<ffi::TorrentRead>& reads)
{
    const Join* j = findJoin(id);
    if (!j || reads.empty()) {
        drainEvents(id);
        return;
    }
    // was: ToDo: one block (256 KiB) read on the daemon thread; move to a worker if
    // reads ever come in large batches (done: readInBackground)
    std::vector<FileRead> files;
    files.reserve(reads.size());
    for (const auto& r : reads)
        files.push_back(FileRead{memberPath(*j, int(r.member)), r.offset, r.len});
    readInBackground(id, std::move(files), [this, id, reads](ReadResults bytes) {
        for (std::size_t i = 0; i < reads.size() && i < bytes.size(); ++i) {
            Join* j = findJoin(id);
            if (!j)
                return;
            const auto& r = reads[i];
            if (!bytes[i]) {
                (void)j->torrent.readFailed(r);
                continue;
            }
            if (auto ok = j->torrent.readDone(m_session.engine(), r, spanOf(*bytes[i])); !ok)
                logDebug([&]() -> QString { return QStringLiteral("read %1 of member %2 not taken (error %3)").arg(r.offset).arg(r.member).arg(errorName(ok.error())); });
        }
        drainEvents(id);
        pumpSession();
    });
}

void Ed3kTorrentJoin::drainEvents(QString id)
{
    for (int guard = 0; guard < kMaxPerCall; ++guard) {
        Join* j = findJoin(id);
        if (!j)
            return;
        auto ev = j->torrent.poll();
        if (!ev)
            break;
        handleEvent(id, *ev);
    }
    // a set taken or refused (CLAIM_FALSE) comes through here
    if (Join* j = findJoin(id))
        updateJudging(*j);
    scheduleSave(id);
}

void Ed3kTorrentJoin::updateJudging(Join& j)
{
    // without a claimed set (§4.2) a piece's check is UNKNOWN and changes nothing: piece_result
    // marks its bytes present. A link (§4.1) always judges
    const auto info = j.torrent.info();
    if (j.sink)
        j.sink->setJudges(!info || info->judge == ffi::Judge::Blake3 || info->manifest.has_value());
}

void Ed3kTorrentJoin::handleEvent(const QString& id, const ffi::TorrentEvent& event)
{
    namespace t = ffi::tev;
    if (const auto* v = std::get_if<t::PieceVerdict>(&event)) {
        finishPiece(id, int(v->piece), v->verdict == ffi::PieceVerdict::Fail);
    } else if (const auto* bf = std::get_if<t::BlockFailed>(&event)) {
        // an ed3k block whose pieces failed: fetched again
        if (Join* j = findJoin(id); j && bf->member < j->members.size())
            j->members[std::size_t(bf->member)].received.erase(bf->block * kBlock);
        logDebug([&]() -> QString { return QStringLiteral("ed3k block %1 of member %2 of \"%3\" failed").arg(bf->block).arg(bf->member).arg(name(id)); });
        if (const auto m = member(id, int(bf->member)); m && m->root)
            requestBlocks(*m->root);
    } else if (const auto* h = std::get_if<t::HaveChanged>(&event)) {
        onHaveChanged(id, int(h->member));
    } else if (const auto* c = std::get_if<t::MemberComplete>(&event)) {
        onMemberComplete(id, int(c->member));
    } else if (const auto* mv = std::get_if<t::MemberVerified>(&event)) {
        onMemberVerified(id, int(mv->member), mv->root);
    } else if (const auto* rj = std::get_if<t::MemberRejected>(&event)) {
        if (const auto m = member(id, int(rj->member)); m && m->root)
            stopDownload(*m->root);
        logWarning(QStringLiteral("member %1 of %2 is not the set's; not shared").arg(rj->member).arg(id));
        reportProblem(id, int(rj->member), tr("ed3k: a file of \"%1\" does not match its ed3k set; it is not shared").arg(name(id)));
    } else if (const auto* cf = std::get_if<t::ClaimFalse>(&event)) {
        onClaimFalse(id, *cf);
    } else if (const auto* lm = std::get_if<t::LinkMismatch>(&event)) {
        logWarning(QStringLiteral("%1: link root %2, the torrent gives %3").arg(id, hexOf(lm->link.bytes), hexOf(lm->computed.bytes)));
        reportProblem(id, -1, tr("ed3k: \"%1\" does not match its ed3k link").arg(name(id)));
    } else if (std::holds_alternative<t::TorrentComplete>(event)) {
        onTorrentComplete(id);
    }
}

void Ed3kTorrentJoin::finishPiece(const QString& id, int piece, bool spoil)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    const auto it = j->waiting.find(piece);
    if (it == j->waiting.end())
        return;
    auto done = std::move(it->second);
    j->waiting.erase(it);
    if (spoil)
        logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" failed under BLAKE3").arg(piece).arg(name(id)); });
    done(spoil);
}

void Ed3kTorrentJoin::onHoldTimeout(const QString& id, int piece)
{
    Join* j = findJoin(id);
    if (!j || !j->waiting.contains(piece))
        return;
    // true: it fails (no verdict came); false: not held any more, the verdict is on its way
    if (j->torrent.pieceRelease(std::uint64_t(piece)))
        finishPiece(id, piece, true);
    drainEvents(id);
}

void Ed3kTorrentJoin::onMemberComplete(const QString& id, int index)
{
    Join* j = findJoin(id);
    if (!j || index < 0 || std::size_t(index) >= j->members.size())
        return;
    if (const auto m = member(id, index); m && m->root)
        stopDownload(*m->root);
    Join::Member& m = j->members[std::size_t(index)];
    if (m.hashing || m.flushWait)
        return;
    // its last blocks may still be in libtorrent's disk cache (pread_disk_io): a hash now
    // reads a short or missing file
    if (m_bt.flushCache(id)) {
        m.flushWait = true;
        QTimer::singleShot(kFlushWaitMs, this, [this, id, index] {
            if (Join* w = findJoin(id); w && std::size_t(index) < w->members.size() && w->members[std::size_t(index)].flushWait)
                hashMember(id, index);
        });
        return;
    }
    hashMember(id, index);
}

void Ed3kTorrentJoin::hashMember(const QString& id, int index)
{
    Join* j = findJoin(id);
    if (!j || index < 0 || std::size_t(index) >= j->members.size())
        return;
    Join::Member& m = j->members[std::size_t(index)];
    m.flushWait = false;
    if (m.hashing)
        return;
    // a volume of an archive set: one read for every volume and the contents (hoster.md §6.1)
    const auto row = m_bt.torrent(id);
    if (m_publishContents && row && !row->isPrivate) {
        const std::vector<int> set = archiveSetOf(*j, index);
        bool ready = !set.empty();
        bool alone = set.empty();
        for (const int i : set) {
            const auto tm = j->torrent.member(std::uint64_t(i));
            if (!tm || (tm->status != ffi::MemberStatus::Open && tm->status != ffi::MemberStatus::Complete)
                || j->members[std::size_t(i)].hashing) {
                alone = true;   // hashed already, or never coming: each on its own
                break;
            }
            if (tm->status == ffi::MemberStatus::Open)
                ready = false;
        }
        if (!alone) {
            if (ready)
                hashArchiveSet(id, set);
            return;   // else the last volume to complete starts it
        }
    }
    m.hashing = true;
    m.generation = ++m_hashGeneration;
    const quint64 generation = m.generation;
    hashInBackground(memberPath(*j, index), [this, id, index, generation](std::optional<FileHash> hashed) {
        onMemberHashed(id, index, generation, hashed);
    });
}

void Ed3kTorrentJoin::setArchiveContents(std::function<void(ArchiveListing)> publish, const QStringList& passwords,
                                         const QString& externalTool)
{
    m_publishContents = std::move(publish);
    m_archivePasswords = passwords;
    m_externalTool = externalTool;
}

std::vector<int> Ed3kTorrentJoin::archiveSetOf(const Join& j, int member) const
{
    const QString path = memberPath(j, member);
    const auto pos = usenet::UsenetUnpacker::volumePositionOf(QFileInfo(path).fileName());
    if (pos.index < 0)
        return {};
    const QString dir = QFileInfo(path).path();
    std::map<int, int> byIndex;   // volume index → member
    for (int i = 0; i < int(j.members.size()); ++i) {
        const QString p = memberPath(j, i);
        if (QFileInfo(p).path() != dir)
            continue;
        const auto other = usenet::UsenetUnpacker::volumePositionOf(QFileInfo(p).fileName());
        if (other.index >= 0 && other.baseName == pos.baseName)
            byIndex[other.index] = i;
    }
    // headless or with a gap: not a set to read
    if (byIndex.empty() || byIndex.begin()->first > pos.first)
        return {};
    std::vector<int> set;
    int expect = byIndex.begin()->first;
    for (const auto& [index, m] : byIndex) {
        if (index != expect++)
            return {};
        set.push_back(m);
    }
    return set;
}

void Ed3kTorrentJoin::hashArchiveSet(const QString& id, const std::vector<int>& set)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    QStringList volumes;
    std::vector<quint64> generations;
    for (const int i : set) {
        Join::Member& m = j->members[std::size_t(i)];
        m.flushWait = false;
        m.hashing = true;
        m.generation = ++m_hashGeneration;
        generations.push_back(m.generation);
        volumes << memberPath(*j, i);
    }
    runHashJob([this, id, set, volumes, generations, passwords = m_archivePasswords, tool = m_externalTool,
                publish = m_publishContents](const std::atomic<bool>& cancel) -> std::function<void()> {
        Ed3kContentsTap tap({}, int(volumes.size()));
        const auto password = Ed3kArchiveContents::readSet(volumes, passwords, tool, tap, &cancel);
        if (cancel.load())
            return {};
        std::vector<std::optional<VolumeHash>> hashes;
        for (int v = 0; v < int(volumes.size()); ++v)
            hashes.push_back(tap.volume(v));
        std::optional<ArchiveListing> listing;
        if (password)
            listing = tap.listing(QFileInfo(volumes.first()).fileName(), *password);
        return [this, id, set, generations, hashes, listing, publish] {
            for (std::size_t k = 0; k < set.size(); ++k) {
                std::optional<FileHash> h;
                if (hashes[k])
                    h = FileHash{hashes[k]->root, hashes[k]->size, hashes[k]->hashes};
                onMemberHashed(id, set[k], generations[k], h);
            }
            // published once the volumes verified as the torrent's (memberHashed above)
            if (listing && publish)
                publish(*listing);
        };
    });
}

void Ed3kTorrentJoin::onMemberHashed(const QString& id, int index, quint64 generation, const std::optional<FileHash>& hashed)
{
    Join* j = findJoin(id);
    if (!j || index < 0 || std::size_t(index) >= j->members.size())
        return;
    Join::Member& m = j->members[std::size_t(index)];
    if (m.generation != generation)
        return;
    m.hashing = false;
    if (!hashed) {
        // gone from disk (deleted or moved behind libtorrent's back): no retries, one summary
        // line per torrent; the next start or a BT recheck tries again
        const QString path = memberPath(*j, index);
        if (!QFileInfo::exists(path)) {
            if (j->missing++ == 0) {
                j->firstMissing = path;
                QTimer::singleShot(kMissingLogMs, this, [this, id] { logMissing(id); });
            }
            return;
        }
        // was: ToDo: hash again later (done: after kRehashMs, and at the next start in ensureJoin)
        logWarning(QStringLiteral("cannot hash member %1 of \"%2\" (%3)").arg(index).arg(name(id), path));
        if (++m.rehashTries < kRehashTries)
            QTimer::singleShot(kRehashMs, this, [this, id, index] {
                if (const auto tm = member(id, index); tm && tm->status == ffi::MemberStatus::Complete)
                    onMemberComplete(id, index);
            });
        return;
    }
    m.hashes = hashed->hashes;
    if (auto ok = j->torrent.memberHashed(std::uint64_t(index), hashed->hashes); !ok)
        logWarning(QStringLiteral("hash of member %1 of \"%2\" not taken (error %3)").arg(index).arg(name(id), errorName(ok.error())));
    drainEvents(id);
    if (Join* again = findJoin(id); again && again->completePending)
        onTorrentComplete(id);
    pumpSession();
}

void Ed3kTorrentJoin::onMemberVerified(const QString& id, int index, const ffi::Root& root)
{
    auto* e = m_session.engine();
    Join* j = findJoin(id);
    if (!e || !j || index < 0 || std::size_t(index) >= j->members.size())
        return;
    stopDownload(root);
    const auto m = j->torrent.member(std::uint64_t(index));
    const std::uint64_t size = m ? m->size : 0;
    const QString file = QFileInfo(memberPath(*j, index)).fileName();
    const QByteArray n = file.toUtf8();
    const std::string fileName(n.constData(), std::size_t(n.size()));
    // §6: a member verified is shared at once
    const auto& hashes = j->members[std::size_t(index)].hashes;
    if (!shareMember(ffi::SharedFile{root, size, fileName, std::nullopt, std::nullopt, ffi::Have::complete(), std::span{hashes}}, id, index)) {
        // hops.md §1: left out, its root is under the anonymous key: kept, so it joins once
        // moved to the node key (or the next start); the user is told it is on BitTorrent
        if (skipped(root)) {
            keepMember(id, index, root, size, file);
            emit memberVerified(id, index);
        }
        return;
    }
    // sources 1: this node, the file held complete (Protocol.md §10.7)
    const ffi::FileInfo info{root, size, fileName, std::nullopt, 1, 0, 0, 0, 0, 0};
    // was: e->publish(m_session.now(), info, m_holdBack ? ffi::Publish::Source : ffi::Publish::Both)
    if (auto ok = publishShared(info); !ok)
        logDebug([&]() -> QString { return QStringLiteral("publish of member %1 of \"%2\" failed (error %3)").arg(index).arg(name(id), errorName(ok.error())); });
    keepMember(id, index, root, size, file);
    logInfo(QStringLiteral("\"%1\" verified and shared as %2").arg(file, hexOf(root.bytes)));
    emit memberVerified(id, index);
}

void Ed3kTorrentJoin::onHaveChanged(const QString& id, int index)
{
    Join* j = findJoin(id);
    if (!j || index < 0 || std::size_t(index) >= j->members.size())
        return;
    const auto m = j->torrent.member(std::uint64_t(index));
    if (!m || !m->root || m->size == 0)
        return;
    const auto have = j->torrent.memberHave(std::uint64_t(index));
    if (!have)
        return;
    ffi::Have h;
    h.kind = static_cast<ffi::Have::Kind>(have->first);
    for (const auto b : have->second)
        h.bits.push_back(std::to_integer<std::uint8_t>(b));
    if (!isShared(*m->root) && h.kind == ffi::Have::Kind::Nothing)
        return;
    // the pieces verified so far; served only within servable() (WantBlock)
    const QByteArray n = QFileInfo(memberPath(*j, index)).fileName().toUtf8();
    const auto& hashes = j->members[std::size_t(index)].hashes;
    (void)shareMember(ffi::SharedFile{*m->root, m->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt, std::nullopt, h,
                                      std::span{hashes}},
                      id, index);
}

void Ed3kTorrentJoin::onClaimFalse(const QString& id, const ffi::tev::ClaimFalse& ev)
{
    auto* e = m_session.engine();
    Join* j = findJoin(id);
    if (!j)
        return;
    // §8: its publishers are ignored from now on, and the set is not tried again
    if (e && !ev.publishers.empty())
        (void)e->aliasBan(m_session.now(), ev.publishers);
    // its ed3k data is dropped: the member downloads stop, nothing of them is served
    for (std::size_t m = 0; m < j->members.size(); ++m) {
        if (const auto tm = j->torrent.member(m); tm && tm->root) {
            stopDownload(*tm->root);
            unshare(*tm->root);
        }
        j->members[m].received.clear();
    }
    logWarning(QStringLiteral("false claim %1 for \"%2\", %3 publisher(s) banned")
                   .arg(argStr(hexOf(ev.manifest.bytes)), argStr(name(id)), argStr(ev.publishers.size())));
    reportProblem(id, -1, tr("ed3k: a false set was claimed for \"%1\" and is ignored; the torrent goes on").arg(name(id)));
    // the search's blame, then its next set; none left: searched again
    if (!refuseSet(id, ev.manifest) && !j->link && !setSearching(id))
        startSetSearch(id);
}

void Ed3kTorrentJoin::onTorrentComplete(const QString& id)
{
    auto* e = m_session.engine();
    Join* j = findJoin(id);
    if (!e || !j)
        return;
    // every member hashed first (Torrent::complete)
    for (std::size_t m = 0; m < j->members.size(); ++m) {
        const auto tm = j->torrent.member(m);
        if (tm && (tm->status == ffi::MemberStatus::Open || tm->status == ffi::MemberStatus::Complete)) {
            j->completePending = true;
            return;
        }
    }
    j->completePending = false;
    const QString torrentName = name(id);
    const QByteArray n = torrentName.toUtf8();
    const std::string_view nameView(n.constData(), std::size_t(n.size()));
    auto done = j->torrent.complete(nameView);
    if (!done) {
        logWarning(QStringLiteral("\"%1\" complete, nothing to publish (error %2)").arg(torrentName, errorName(done.error())));
        return;
    }
    const auto t = m_session.now();
    j->completedManifest = QByteArray(reinterpret_cast<const char*>(done->manifest.data()), qsizetype(done->manifest.size()));
    // was: ToDo: the manifest and info dictionary are kept in memory only (done: keepBlob,
    // shared again at start)
    keepBlob(id, j->completedManifest, torrentName, done->root);
    keepBlob(id, m_bt.infoSection(id), torrentName + QStringLiteral(".info"), done->infoRoot);
    // published again after a restart, as below
    keepRecords(id, done->root, torrentName, done->aliases);
    // MEMBER rows (file-set.md §6); not while a member is under the anonymous key (hops.md §1)
    std::vector<ffi::Root> memberRoots;
    for (std::size_t i = 0; i < j->members.size(); ++i)
        if (const auto m = j->torrent.member(i); m && m->root)
            memberRoots.push_back(*m->root);
    // was: if (withholdSet(id, memberRoots)) (node key only); now under the members' one key
    const auto setKey = setKeyOf(id, memberRoots);
    if (!setKey)
        logInfo(QStringLiteral("set of \"%1\" not published: its members are not under one key it may use (hops.md §1)").arg(torrentName));
    // was: else if (auto ok = e->setPublish(t, done->manifest, done->root, nameView); !ok)
    else if (auto ok = e->setPublish(t, done->manifest, done->root, nameView, ffi::SetSigner::Unsigned, nullptr, *setKey); !ok)
        logDebug([&]() -> QString { return QStringLiteral("set of \"%1\" not published (error %2)").arg(torrentName, errorName(ok.error())); });
    if (!m_holdBack && !done->aliases.empty()) {
        // was: if (auto ok = e->publishAlias(t, done->aliases); !ok)
        if (auto ok = publishAliasRecords(done->aliases); !ok)
            logDebug([&]() -> QString { return QStringLiteral("ALIAS records of \"%1\" not published (error %2)").arg(torrentName, errorName(ok.error())); });
    }
    logInfo(QStringLiteral("\"%1\" complete, torrent root %2").arg(torrentName, hexOf(done->root.bytes)));
    emit torrentCompleted(id);
}

void Ed3kTorrentJoin::startDownloads(const QString& id)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    // #264: a torrent queued for its check has no pieces yet: every block would look
    // wanted, and its whole files overwritten. onTorrentChecked starts them
    if (!m_bt.filesChecked(id))
        return;
    j->have = m_bt.havePieces(id);
    for (std::size_t i = 0; i < j->members.size(); ++i) {
        const auto m = j->torrent.member(i);
        // all of it in libtorrent: BitTorrent finishes it, MEMBER_COMPLETE hashes it
        // was: ... && !downloading(*m->root) && !piecesHad(...)) (void)openDownload(...);
        if (!m || !m->root || m->size == 0 || m->status != ffi::MemberStatus::Open || piecesHad(*j, m->start, m->start + m->size))
            continue;
        if (!downloading(*m->root))
            (void)openDownload(*m->root, m->size, id, int(i));
        // #224: the same root here already (Shared Files, a finished download, another
        // torrent): its blocks first, the network gets the rest
        (void)copyLocal(*m->root);
    }
}

void Ed3kTorrentJoin::startSetSearch(const QString& id)
{
    const auto row = m_bt.torrent(id);
    if (!row || privateHeld(id))
        return;
    // completed here, its set kept: the Kad lookup would only find our own set. Not judged
    // by haveBytes: a resumed row has 0 until libtorrent's check (it searched its own set)
    // if (row->totalSize > 0 && row->haveBytes >= row->totalSize) {
    if (const auto set = keptSet(id)) {
        logDebug([&]() -> QString { return QStringLiteral("no set search for \"%1\": set %2 kept").arg(name(id), hexOf(set->bytes)); });
        return;
    }
    // a hybrid torrent: both (§5); relation SET. The candidate loop is the base's (tryNextSet)
    if (const auto v1 = hexBytes(row->hashV1, 20))
        (void)Ed3kJoinBase::startSetSearch(id, "btih", *v1);
    if (const auto v2 = hexBytes(row->hashV2, 32))
        (void)Ed3kJoinBase::startSetSearch(id, "btmh", *v2);
}

void Ed3kTorrentJoin::feedPieces(const QString& id, const std::vector<std::uint64_t>& pieces)
{
    Join* j = findJoin(id);
    if (!j)
        return;
    for (const auto piece : pieces) {
        auto extents = j->torrent.extents(piece);
        const int size = j->layout.pieceSize(int(piece));
        if (!extents || size <= 0)
            continue;
        // the piece's bytes on a worker (a piece may be MiBs), put together here
        std::vector<FileRead> reads;
        std::vector<std::uint64_t> at;
        bool ok = true;
        for (const auto& x : *extents) {
            if (x.at + x.len > std::uint64_t(size) || x.len > std::numeric_limits<std::uint32_t>::max()) {
                ok = false;
                break;
            }
            reads.push_back(FileRead{memberPath(*j, int(x.member)), x.offset, std::uint32_t(x.len)});
            at.push_back(x.at);
        }
        if (!ok) {
            logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" not read for BitTorrent").arg(piece).arg(name(id)); });
            continue;
        }
        readInBackground(id, std::move(reads), [this, id, piece, size, at = std::move(at)](ReadResults bytes) {
            QByteArray data(size, '\0');   // padding between members is zeros
            for (std::size_t i = 0; i < at.size(); ++i) {
                if (i >= bytes.size() || !bytes[i]) {
                    logDebug([&]() -> QString { return QStringLiteral("piece %1 of \"%2\" not read for BitTorrent").arg(piece).arg(name(id)); });
                    return;
                }
                std::memcpy(data.data() + at[i], bytes[i]->constData(), std::size_t(bytes[i]->size()));
            }
            if (findJoin(id))
                (void)m_bt.addPiece(id, int(piece), data);
        });
    }
}

bool Ed3kTorrentJoin::takeBlock(const QString& id, int index, std::uint64_t offset, std::span<const std::byte> data)
{
    Join* j = findJoin(id);
    if (!j || index < 0 || std::size_t(index) >= j->members.size())
        return false;
    // #264: never into files libtorrent has not checked yet (a queued check of existing
    // data), never over pieces it passed: a false set claim (§4.2) overwrote whole data
    if (!m_bt.filesChecked(id)) {
        logDebug([&]() -> QString { return QStringLiteral("block %1 of member %2 not written: \"%3\" not checked yet").arg(offset).arg(index).arg(name(id)); });
        return false;
    }
    const auto tm = j->torrent.member(std::uint64_t(index));
    if (!tm)
        return false;
    j->have = m_bt.havePieces(id);
    const QString path = memberPath(*j, index);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) {
        logWarning(QStringLiteral("cannot write a block into %1").arg(path));
        return false;
    }
    // the parts of the block in pieces libtorrent lacks, in member offsets
    const auto pieceLength = std::uint64_t(j->layout.pieceLength);
    const std::uint64_t end = offset + data.size();
    for (std::uint64_t at = offset; at < end;) {
        std::uint64_t next = end;
        if (pieceLength > 0) {
            const std::uint64_t abs = tm->start + at;
            next = std::min(end, (abs / pieceLength + 1) * pieceLength - tm->start);
        }
        if (!piecesHad(*j, tm->start + at, tm->start + next)
            && (!f.seek(qint64(at))
                || f.write(reinterpret_cast<const char*>(data.data() + (at - offset)), qint64(next - at)) != qint64(next - at))) {
            logWarning(QStringLiteral("cannot write a block into %1").arg(path));
            return false;
        }
        at = next;
    }
    f.close();
    j->members[std::size_t(index)].received.insert(offset);
    auto pieces = j->torrent.ed3kBlock(std::uint64_t(index), offset, std::uint32_t(data.size()));
    if (!pieces) {
        logDebug([&]() -> QString { return QStringLiteral("block %1 of member %2 not taken (error %3)").arg(offset).arg(index).arg(errorName(pieces.error())); });
    } else {
        logDebug([&]() -> QString { return QStringLiteral("ed3k block %1 of member %2: %3 piece(s) for BitTorrent").arg(offset).arg(index).arg(pieces->size()); });
        feedPieces(id, *pieces);
    }
    return true;
}

void Ed3kTorrentJoin::reportProblem(const QString& id, int index, const QString& message)
{
    logEd3kWarning(message);
    emit memberProblem(id, index, message);
}

QString Ed3kTorrentJoin::memberPath(const Join& j, int index) const
{
    if (index < 0 || std::size_t(index) >= j.memberFile.size())
        return {};
    return j.layout.files.at(j.memberFile[std::size_t(index)]).path;
}

QString Ed3kTorrentJoin::name(const QString& id) const
{
    const auto row = m_bt.torrent(id);
    return row && !row->name.isEmpty() ? row->name : id;
}

void Ed3kTorrentJoin::saveJoin(const QString& id)
{
    const Join* j = findJoin(id);
    if (!j)
        return;
    auto blob = j->torrent.save();
    if (!blob) {
        logWarning(QStringLiteral("join of %1 not saved (error %2)").arg(id, errorName(blob.error())));
        return;
    }
    // was: writeBlob on this thread; its sync stalled the core for seconds (report 23)
    writeBlobLater(joinPath(id), *blob);
}

bool Ed3kTorrentJoin::piecesHad(const Join& j, std::uint64_t begin, std::uint64_t end) const
{
    // [begin, end) in the piece space; an empty range or no piece map: not had
    const auto pieceLength = std::uint64_t(j.layout.pieceLength);
    if (end <= begin || pieceLength == 0 || j.have.isEmpty())
        return false;
    const std::uint64_t last = (end - 1) / pieceLength;
    if (last >= std::uint64_t(j.have.size()))
        return false;
    for (std::uint64_t p = begin / pieceLength; p <= last; ++p)
        if (!j.have.testBit(qsizetype(p)))
            return false;
    return true;
}

bool Ed3kTorrentJoin::blakeJudges(const Join& j) const
{
    const auto info = j.torrent.info();
    return info && info->judge == ffi::Judge::Blake3;
}

QString Ed3kTorrentJoin::torrentDir() const
{
    return joinDir(QStringLiteral("torrent"));
}

void Ed3kTorrentJoin::rejoin(bool allowed, const std::function<bool(const bt::BtTorrentRow&)>& held)
{
    if (!m_started)
        return;
    for (const auto& row : m_bt.torrents()) {
        if (!held(row))
            continue;
        if (allowed) {
            m_noJoin.remove(row.id);   // DENIED before
            (void)ensureJoin(row.id);
            shareKeptOwner(row.id);
        } else {
            const bool joined = findJoin(row.id) != nullptr;
            if (joined) {
                saveJoin(row.id);
                dropJoin(row.id, /*keepBlob*/ true);
            }
            // shared before: withdrawn (set and ALIAS records too), still kept on disk
            withdrawOwner(row.id);
            if (joined)
                logInfo(QStringLiteral("\"%1\" left ed3k (not allowed to join any more)").arg(name(row.id)));
        }
    }
    pumpSession();
}

void Ed3kTorrentJoin::applyPrivateJoins()
{
    if (auto* e = m_session.engine())
        (void)e->setTorrentSharePrivate(m_privateJoins);
}

void Ed3kTorrentJoin::logMissing(const QString& id)
{
    Join* j = findJoin(id);
    if (!j || j->missing == 0)
        return;
    logWarning(QStringLiteral("%1 complete member(s) of \"%2\" missing on disk (first: %3), not shared; recheck the torrent")
                   .arg(j->missing).arg(name(id), j->firstMissing));
    j->missing = 0;
    j->firstMissing.clear();
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_BITTORRENT
