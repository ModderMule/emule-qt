/// @file Ed3kUsenetJoin.cpp
/// @brief A Usenet download joined with ed3k — implementation.

#include "Ed3kUsenetJoin.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "nzb/NzbInfo.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <set>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

/// a file whose articles all resolved waits this long for ed3k to fill its holes
constexpr int kSealHoldMs = 10 * 60 * 1000;
constexpr int kClaimSearchesPerPost = 4;
/// reads given by block_read in one go; a guard, the core gives few
constexpr int kMaxReadsPerCall = 1 << 16;
/// member index of a post set's article list fetched into memory (the manifest: kSetManifest)
constexpr int kArticleList = -2;

void le32(QByteArray& out, std::size_t v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((std::uint32_t(v) >> (8 * i)) & 0xff));
}

/// UsenetQueue as the join sees it.
class QueueAdapter final : public UsenetJoinQueue {
public:
    explicit QueueAdapter(usenet::UsenetQueue& q) : m_q(q) {}
    const usenet::UsenetQueueItem* findItem(const QString& id) const override { return m_q.findItem(id); }
    QStringList itemIds() const override
    {
        QStringList out;
        for (const auto* item : m_q.items())
            out.append(item->id);
        return out;
    }
    int refetchRange(const QString& itemId, int fileIndex, qint64 offset, qint64 length,
                     const QString& excludeServerKey) override
    {
        return m_q.refetchRange(itemId, fileIndex, offset, length, excludeServerKey);
    }
    int acceptExternalRange(const QString& itemId, int fileIndex, qint64 offset, qint64 length) override
    {
        return m_q.acceptExternalRange(itemId, fileIndex, offset, length);
    }
    void setSealHold(SealHold hold) override { m_q.setSealHold(std::move(hold)); }
    void releaseSealHold(const QString& itemId, int fileIndex) override { m_q.releaseSealHold(itemId, fileIndex); }

private:
    usenet::UsenetQueue& m_q;
};

} // namespace

// One post: the core's join and what the host keeps per member.
struct Ed3kUsenetJoin::Post {
    explicit Post(ffi::UsenetJoin j) : join(std::move(j)) {}
    ffi::UsenetJoin join;
    std::optional<ffi::UsenetSearch> search;   ///< for blame and ban (§8)
    struct Member {
        ffi::Bytes hashes;     ///< block values: own hash, or of a download's Hashes event
        bool hashing = false;
        quint64 generation = 0;
        std::optional<QDeadlineTimer> holdUntil;
        bool claimSearched = false;
        int rehashTries = 0;
        /// ACCEPT_RANGEs merged: they come per block, an article across two blocks is
        /// only accepted inside their union (acceptExternalRange: wholly inside)
        QList<QPair<qint64, qint64>> accepted;
    };
    std::vector<Member> members;
    std::deque<int> claimQueue;
    int claimsRunning = 0;
    bool finished = false;   ///< the item finished; dropped once no hash is pending
    /// §4.1: the post root of <meta type="ed3k">
    std::optional<ffi::Root> link;
    /// the post set whose article list is fetched: its manifest and publishers
    std::optional<ffi::Root> setRoot;
    QByteArray setFile;
    std::vector<ffi::PublisherKey> setPublishers;
    bool setFromLink = false;
    std::optional<ffi::Root> listRoot;
};

Ed3kUsenetJoin::Ed3kUsenetJoin(Ed3kSession& session, UsenetJoinQueue& queue, const QString& configDir,
                               QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("usenet"), parent)
    , m_queue(queue)
{
}

Ed3kUsenetJoin::~Ed3kUsenetJoin()
{
    stop();
}

std::unique_ptr<UsenetJoinQueue> Ed3kUsenetJoin::queueAdapter(usenet::UsenetQueue& queue)
{
    return std::make_unique<QueueAdapter>(queue);
}

void Ed3kUsenetJoin::connectQueue(usenet::UsenetQueue& queue)
{
    using Q = usenet::UsenetQueue;
    connect(&queue, &Q::itemAdded, this, &Ed3kUsenetJoin::onItemAdded);
    connect(&queue, &Q::itemRemoved, this, &Ed3kUsenetJoin::onItemRemoved);
    connect(&queue, &Q::itemFinished, this, &Ed3kUsenetJoin::onItemFinished);
    connect(&queue, &Q::articleDecoded, this, &Ed3kUsenetJoin::onArticleDecoded);
    connect(&queue, &Q::filesRepaired, this, &Ed3kUsenetJoin::onFilesRepaired);
}

void Ed3kUsenetJoin::start()
{
    if (m_started || !m_session.isRunning())
        return;
    beginStart();
    QDir().mkpath(usenetDir());
    m_queue.setSealHold([this](const QString& itemId, int fileIndex) { return sealHold(itemId, fileIndex); });
    for (const QString& id : m_queue.itemIds())
        (void)ensurePost(id);
    // what was verified or fetched before the restart: post sets, members
    shareKept();
    pumpSession();
}

void Ed3kUsenetJoin::stop()
{
    if (!m_started)
        return;
    for (const auto& [id, post] : m_posts)
        savePost(id);

    // files held unsealed for ed3k: sealed now
    QList<std::pair<QString, int>> held;
    for (const auto& [id, post] : m_posts)
        for (std::size_t f = 0; f < post->members.size(); ++f)
            if (post->members[f].holdUntil)
                held.append({id, int(f)});

    if (auto* e = m_session.engine())
        for (const auto& [search, member] : m_claimSearches)
            (void)e->aliasSearchEnd(search);
    closeAll();
    m_claimSearches.clear();
    m_posts.clear();
    m_noJoin.clear();
    m_queue.setSealHold({});
    for (const auto& [id, f] : held)
        m_queue.releaseSealHold(id, f);
    pumpSession();
}

std::vector<ffi::NzbFileSegments> Ed3kUsenetJoin::nzbSegments(const usenet::NzbInfo& nzb)
{
    std::vector<ffi::NzbFileSegments> out;
    out.reserve(std::size_t(nzb.files.size()));
    for (const auto& file : nzb.files) {
        ffi::NzbFileSegments segs;
        for (const auto& s : file.nzbOrderSegments()) {
            // numberText empty: an old sidecar (plain decimal) or no attribute
            const QString number = !s.numberText.isEmpty() ? s.numberText
                                   : s.number > 0          ? QString::number(s.number)
                                                           : QString();
            // messageId is mᵢ already (usenet-join-host.md): passed as it is
            segs.push_back(ffi::NzbSegment{number.toStdString(), s.messageId.toStdString()});
        }
        out.push_back(std::move(segs));
    }
    return out;
}

QByteArray Ed3kUsenetJoin::nzbBuffer(const usenet::NzbInfo& nzb)
{
    const auto files = nzbSegments(nzb);
    QByteArray out;
    le32(out, files.size());
    for (const auto& f : files) {
        le32(out, f.size());
        for (const auto& s : f) {
            le32(out, s.number.size());
            out.append(s.number.data(), qsizetype(s.number.size()));
            le32(out, s.messageId.size());
            out.append(s.messageId.data(), qsizetype(s.messageId.size()));
        }
    }
    return out;
}

std::optional<ffi::Root> Ed3kUsenetJoin::metaRoot(const usenet::NzbInfo& nzb)
{
    // was: parsed here; now metaLink (its size is needed to fetch the set)
    const auto parsed = metaLink(nzb);
    if (!parsed)
        return std::nullopt;
    return parsed->root;
}

std::uint64_t Ed3kUsenetJoin::serverTag(const QString& serverKey)
{
    if (serverKey.isEmpty())
        return 0;
    // stable across restarts (the join blob keeps it); one-way, never the account
    const QByteArray in = QByteArray("emuleqt usenet server\n").append(serverKey.toUtf8());
    const QByteArray d = QCryptographicHash::hash(in,
                                                  QCryptographicHash::Sha256);
    std::uint64_t tag = 0;
    for (int i = 0; i < 8; ++i)
        tag |= std::uint64_t(quint8(d.at(i))) << (8 * i);
    return tag == 0 ? 1 : tag;
}

bool Ed3kUsenetJoin::hasJoin(const QString& itemId) const
{
    return m_posts.contains(itemId);
}

std::optional<ffi::UsenetMember> Ed3kUsenetJoin::member(const QString& itemId, int fileIndex) const
{
    const auto post = findPost(itemId);
    if (!post || fileIndex < 0)
        return std::nullopt;
    auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m)
        return std::nullopt;
    return *m;
}

QString Ed3kUsenetJoin::joinPath(const QString& itemId) const
{
    return QDir(usenetDir()).filePath(itemId + QStringLiteral(".join"));
}

bool Ed3kUsenetJoin::fileClaim(const QString& itemId, int fileIndex, const ffi::Root& root, std::uint64_t size,
                               std::span<const ffi::PublisherKey> publishers)
{
    const auto post = ensurePost(itemId);
    if (!post || fileIndex < 0)
        return false;
    if (auto ok = post->join.fileClaim(std::uint64_t(fileIndex), root, size, publishers); !ok) {
        logEd3kDebug(QStringLiteral("ed3k usenet: claim %1 for file %2 of %3 refused (error %4)")
                         .arg(hexOf(root.bytes), QString::number(fileIndex), itemId, errorName(ok.error())));
        return false;
    }
    scheduleSave(itemId);
    startDownload(itemId, fileIndex);
    if (auto* e = m_session.engine(); e) {
        if (auto reads = post->join.pendingReads(e); reads && !reads->empty())
            serveReads(itemId, std::move(*reads));
    }
    drainEvents(itemId);
    pumpSession();
    return true;
}

bool Ed3kUsenetJoin::setManifest(const QString& itemId, std::span<const std::byte> manifest,
                                 std::span<const std::byte> list, std::span<const ffi::PublisherKey> publishers)
{
    const auto post = ensurePost(itemId);
    if (!post)
        return false;
    if (auto ok = post->join.setManifest(manifest, list, publishers); !ok) {
        logEd3kWarning(QStringLiteral("ed3k usenet: post set of %1 refused (error %2)").arg(itemId, errorName(ok.error())));
        return false;
    }
    scheduleSave(itemId);
    const auto info = post->join.info();
    for (std::uint64_t f = 0; info && f < info->members; ++f)
        startDownload(itemId, int(f));
    if (auto* e = m_session.engine(); e) {
        if (auto reads = post->join.pendingReads(e); reads && !reads->empty())
            serveReads(itemId, std::move(*reads));
    }
    drainEvents(itemId);
    pumpSession();
    return true;
}

void Ed3kUsenetJoin::addSource(const QString& itemId, int fileIndex, const ffi::Source& source)
{
    const auto post = findPost(itemId);
    if (!post || !m_session.engine())
        return;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || !m->size)
        return;
    startDownload(itemId, fileIndex);
    Ed3kJoinBase::addSource(*m->root, source);
}

bool Ed3kUsenetJoin::trySet(const QString& itemId, const ffi::Root& root, std::uint64_t size,
                            std::span<const ffi::PublisherKey> publishers)
{
    const auto post = ensurePost(itemId);
    if (!post || !wantsSet(itemId) || post->setRoot)
        return false;
    const bool ok = trySetCandidate(itemId, root, size, publishers);
    pumpSession();
    return ok;
}

void Ed3kUsenetJoin::addBlobSource(const ffi::Root& root, const ffi::Source& source)
{
    Ed3kJoinBase::addSource(root, source);
}

void Ed3kUsenetJoin::onItemAdded(const QString& itemId)
{
    if (m_started)
        (void)ensurePost(itemId);
    pumpSession();
}

void Ed3kUsenetJoin::onItemRemoved(const QString& itemId)
{
    dropPost(itemId, /*keepBlob*/ false);
    m_noJoin.remove(itemId);
    // its files are gone or the user does not want them: not shared any more
    unshareOwner(itemId);
    dropKept(itemId);
    pumpSession();
}

void Ed3kUsenetJoin::onItemFinished(const QString& itemId, bool success, const QString& message)
{
    Q_UNUSED(success);
    Q_UNUSED(message);
    const auto post = findPost(itemId);
    if (!post)
        return;
    m_noJoin.insert(itemId);
    const bool hashing = std::ranges::any_of(post->members, [](const Post::Member& m) { return m.hashing; });
    if (hashing) {
        // a repaired member is still hashed: its result may verify and complete the post
        post->finished = true;
        return;
    }
    dropPost(itemId, /*keepBlob*/ false);
    pumpSession();
}

void Ed3kUsenetJoin::onArticleDecoded(const QString& itemId, int fileIndex, qint64 offset, qint64 length, bool crcOk,
                                      int partNumber, const QByteArray& rawYbeginLine, const QString& serverKey,
                                      const QString& messageId)
{
    const auto post = ensurePost(itemId);
    auto* e = m_session.engine();
    const auto* item = m_queue.findItem(itemId);
    if (!post || !e || !item || fileIndex < 0 || fileIndex >= item->nzb.files.size() || offset < 0 || length <= 0)
        return;

    // mᵢ of the article: the first <segment> of its number in NZB order (only that one
    // counts, usenet.md §1). An article fetched under a second ID of that number is not mᵢ
    // (was: ToDo: the queue could hand the message ID itself; a duplicated number with two
    // fetched IDs is reported under the first one (done: messageId from the queue))
    bool first = false;
    for (const auto& s : item->nzb.files.at(fileIndex).nzbOrderSegments()) {
        if (s.number == partNumber) {
            first = s.messageId == messageId;
            break;
        }
    }
    if (!first || messageId.isEmpty())
        return;
    const QByteArray id = messageId.toUtf8();
    const std::string_view idView(id.constData(), std::size_t(id.size()));

    const std::uint64_t tag = serverTag(serverKey);
    if (tag != 0)
        m_serverKeys.insert(tag, serverKey);

    if (crcOk && !rawYbeginLine.isEmpty()) {
        const auto ok = post->join.ybegin(std::uint64_t(fileIndex), idView,
                                          std::string_view(rawYbeginLine.constData(), std::size_t(rawYbeginLine.size())));
        if (!ok && ok.error() != ffi::Error::Stale)
            logEd3kDebug(QStringLiteral("ed3k usenet: =ybegin of part %1 not taken (error %2)")
                             .arg(partNumber).arg(errorName(ok.error())));
    }
    auto reads = post->join.article(e, std::uint64_t(fileIndex), idView, std::uint64_t(offset), std::uint64_t(length),
                                    crcOk, tag);
    if (!reads) {
        if (reads.error() != ffi::Error::Stale)
            logEd3kDebug(QStringLiteral("ed3k usenet: article %1 of file %2 not taken (error %3)")
                             .arg(partNumber).arg(fileIndex).arg(errorName(reads.error())));
        drainEvents(itemId);
        pumpSession();
        return;
    }
    serveReads(itemId, std::move(*reads));
    scheduleSave(itemId);
    // the queue asked the hold before this article reached the join: ask again
    if (const auto p = findPost(itemId);
        p && std::size_t(fileIndex) < p->members.size() && p->members[std::size_t(fileIndex)].holdUntil)
        m_queue.releaseSealHold(itemId, fileIndex);
    pumpSession();
}

void Ed3kUsenetJoin::onFilesRepaired(const QString& itemId, const QList<int>& fileIndices)
{
    const auto post = findPost(itemId);
    auto* e = m_session.engine();
    if (!post || !e)
        return;
    for (const int f : fileIndices) {
        auto p = findPost(itemId);
        if (!p)
            break;
        auto reads = p->join.memberRepaired(e, std::uint64_t(f));
        if (!reads) {
            logEd3kDebug(QStringLiteral("ed3k usenet: repaired file %1 of %2 not taken (error %3)")
                             .arg(f).arg(itemId, errorName(reads.error())));
            continue;
        }
        // was: ToDo: archive volumes are deleted right after this signal (consumed by the
        // unpack), so their hash at MEMBER_COMPLETE fails; hash them here, synchronously?
        // (done: m_hashNow, MEMBER_COMPLETE of these hashed on this thread)
        m_hashNow = true;
        m_readsNow = true;   // inline too: the volumes go right after this signal
        serveReads(itemId, std::move(*reads));
        m_readsNow = false;
        m_hashNow = false;
    }
    // hashes still running on a worker: done here too, before the volumes go
    std::vector<int> running;
    if (const auto p = findPost(itemId))
        for (std::size_t f = 0; f < p->members.size(); ++f)
            if (p->members[f].hashing)
                running.push_back(int(f));
    m_hashNow = true;
    for (const int f : running) {
        const auto p = findPost(itemId);
        if (!p || std::size_t(f) >= p->members.size())
            break;
        p->members[std::size_t(f)].hashing = false;   // the worker's result is stale now
        onMemberComplete(itemId, f);
    }
    m_hashNow = false;
    scheduleSave(itemId);
    pumpSession();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kUsenetJoin::onOtherEvent(const ffi::Event& event)
{
    if (const auto* af = std::get_if<ffi::event::AliasFound>(&event))
        onAliasFound(*af);
}

void Ed3kUsenetJoin::onDownloadHashes(const Download& d, const ffi::Bytes& blob)
{
    const QString itemId = d.owner;
    const int f = d.member;
    if (const auto post = findPost(itemId)) {
        if (f >= 0 && std::size_t(f) < post->members.size() && post->members[std::size_t(f)].hashes.empty())
            post->members[std::size_t(f)].hashes = blob;
        // block values are there now: articles can be checked
        if (auto reads = post->join.pendingReads(m_session.engine()); reads && !reads->empty())
            serveReads(itemId, std::move(*reads));
    }
}

bool Ed3kUsenetJoin::wantsSet(const QString& owner) const
{
    const auto post = findPost(owner);
    if (!post)
        return false;
    const auto info = post->join.info();
    return info && !info->manifest;
}

void Ed3kUsenetJoin::onSetManifest(const QString& owner, const ffi::Root& root, const QByteArray& data)
{
    // §4.2 step 2: a set an `nzb` record names
    fetchList(owner, root, data, setPublishers(owner), false);
}

void Ed3kUsenetJoin::onSetSearchDone(const QString& owner)
{
    // §4.2 step 1: `nzb` gave no post set that passes §3: `nzb-file` now
    logEd3kDebug(QStringLiteral("ed3k usenet: no post set for %1, searching its files").arg(owner));
    startClaimSearches(owner);
}

bool Ed3kUsenetJoin::keepsOwner(const QString& owner) const
{
    return m_queue.findItem(owner) != nullptr;
}

QString Ed3kUsenetJoin::keptMemberPath(const QString& owner, int member) const
{
    return memberPath(owner, member);
}

bool Ed3kUsenetJoin::hasOwner(const QString& owner) const
{
    return m_posts.contains(owner);
}

void Ed3kUsenetJoin::saveOwner(const QString& owner)
{
    savePost(owner);
}

std::shared_ptr<Ed3kUsenetJoin::Post> Ed3kUsenetJoin::ensurePost(const QString& itemId)
{
    if (auto post = findPost(itemId))
        return post;
    auto* e = m_session.engine();
    if (!m_started || !e || m_noJoin.contains(itemId))
        return nullptr;
    const auto* item = m_queue.findItem(itemId);
    if (!item)
        return nullptr;
    if (item->status == usenet::UsenetItemStatus::Complete || item->status == usenet::UsenetItemStatus::Failed
        || item->nzb.files.isEmpty()) {
        m_noJoin.insert(itemId);
        return nullptr;
    }

    const auto nzb = nzbSegments(item->nzb);
    const auto linked = metaLink(item->nzb);
    const auto link = linked ? std::optional(linked->root) : std::nullopt;
    std::optional<ffi::UsenetJoin> join;
    const QByteArray blob = readBlob(joinPath(itemId));
    if (!blob.isEmpty()) {
        auto loaded = ffi::UsenetJoin::load(*e, nzb, link, spanOf(blob));
        if (loaded)
            join.emplace(std::move(*loaded));
        else
            logEd3kWarning(QStringLiteral("ed3k usenet: saved join of \"%1\" dropped (error %2)")
                               .arg(item->name, errorName(loaded.error())));
    }
    if (!join) {
        auto created = ffi::UsenetJoin::create(*e, nzb, link);
        if (!created) {
            logEd3kWarning(QStringLiteral("ed3k usenet: no join for \"%1\" (error %2)").arg(item->name, errorName(created.error())));
            m_noJoin.insert(itemId);
            return nullptr;
        }
        join.emplace(std::move(*created));
    }

    auto post = std::make_shared<Post>(std::move(*join));
    const auto info = post->join.info();
    post->members.resize(info ? std::size_t(info->members) : 0);
    post->link = link;
    if (auto search = ffi::UsenetSearch::create(post->join))
        post->search.emplace(std::move(*search));
    m_posts.emplace(itemId, post);
    logEd3k(QStringLiteral("ed3k usenet: \"%1\" joined (%2 file(s), %3)")
                .arg(item->name)
                .arg(post->members.size())
                .arg(link ? QStringLiteral("from its ed3k link") : QStringLiteral("NZB alone")));

    // members that have a root already (a restart): their downloads go on
    for (std::size_t f = 0; f < post->members.size(); ++f)
        startDownload(itemId, int(f));
    // members complete but not hashed before the restart (unreadable then): hashed again
    for (std::size_t f = 0; f < post->members.size(); ++f)
        if (const auto m = post->join.member(f); m && m->status == ffi::MemberStatus::Complete)
            onMemberComplete(itemId, int(f));
    if (info && info->manifest) {
        // a restart: the set is known
    } else if (link) {
        // was: ToDo: §4.1 fetch the post set (manifest and article list) of the link over ed3k,
        // then setManifest(); until then the link only makes the core the BLAKE3 judge
        // (done: fetchBlob → onBlobFetched → fetchList → onListFetched → setManifest)
        if (linked->size == 0 || !fetchBlob(*link, linked->size, itemId, kSetManifest))
            logEd3kDebug(QStringLiteral("ed3k usenet: post set %1 of \"%2\" not fetched").arg(hexOf(link->bytes), item->name));
    } else {
        startPostSearch(itemId);
    }
    scheduleSave(itemId);
    return post;
}

std::shared_ptr<Ed3kUsenetJoin::Post> Ed3kUsenetJoin::findPost(const QString& itemId) const
{
    const auto it = m_posts.find(itemId);
    return it == m_posts.end() ? nullptr : it->second;
}

void Ed3kUsenetJoin::dropPost(const QString& itemId, bool keepBlob)
{
    const auto it = m_posts.find(itemId);
    if (it != m_posts.end()) {
        stopDownloads(itemId);
        endSetSearches(itemId);
        auto* e = m_session.engine();
        for (auto s = m_claimSearches.begin(); s != m_claimSearches.end();) {
            if (s->second.first == itemId) {
                if (e)
                    (void)e->aliasSearchEnd(s->first);
                s = m_claimSearches.erase(s);
            } else {
                ++s;
            }
        }
        m_posts.erase(it);
    }
    dropDirty(itemId);
    if (!keepBlob)
        QFile::remove(joinPath(itemId));
}

void Ed3kUsenetJoin::serveReads(const QString& itemId, std::vector<ffi::TorrentRead> reads)
{
    if (reads.empty() || !findPost(itemId)) {
        drainEvents(itemId);
        return;
    }
    // was: ToDo: one block (256 KiB) read on the daemon thread; move to a worker if
    // reads ever come in large batches (done: readInBackground; inline while m_readsNow)
    std::vector<FileRead> files;
    files.reserve(reads.size());
    for (const auto& r : reads)
        files.push_back(FileRead{memberPath(itemId, int(r.member)), r.offset, r.len});
    readInBackground(itemId, std::move(files), [this, itemId, reads = std::move(reads)](ReadResults bytes) {
        std::vector<ffi::TorrentRead> more;
        for (std::size_t i = 0; i < reads.size() && i < bytes.size(); ++i) {
            const auto post = findPost(itemId);
            if (!post)
                return;
            const auto& r = reads[i];
            // sealed or moved since the path was taken: once more under the path now
            auto data = bytes[i] ? std::move(bytes[i]) : readMember(itemId, int(r.member), r.offset, r.len);
            if (!data) {
                (void)post->join.readFailed(r);
                continue;
            }
            auto next = post->join.blockRead(m_session.engine(), r, spanOf(*data));
            if (!next) {
                logEd3kDebug(QStringLiteral("ed3k usenet: block %1 of file %2 not taken (error %3)")
                                 .arg(r.offset).arg(r.member).arg(errorName(next.error())));
                continue;
            }
            more.insert(more.end(), next->begin(), next->end());
        }
        if (!more.empty())
            serveReads(itemId, std::move(more));   // the next batch of this item
        else
            drainEvents(itemId);
        pumpSession();
    });
}

void Ed3kUsenetJoin::drainEvents(const QString& itemId)
{
    for (int guard = 0; guard < kMaxReadsPerCall; ++guard) {
        const auto post = findPost(itemId);
        if (!post)
            return;
        auto ev = post->join.poll();
        if (!ev)
            break;
        handleEvent(itemId, *ev);
    }
    scheduleSave(itemId);
}

void Ed3kUsenetJoin::handleEvent(const QString& itemId, const ffi::UsenetEvent& event)
{
    namespace u = ffi::uev;
    if (const auto* h = std::get_if<u::HaveChanged>(&event)) {
        onHaveChanged(itemId, int(h->member));
    } else if (const auto* a = std::get_if<u::AcceptRange>(&event)) {
        auto [from, to] = std::pair{qint64(a->offset), qint64(a->offset + a->len)};
        if (const auto post = findPost(itemId); post && a->member < post->members.size()) {
            auto& acc = post->members[std::size_t(a->member)].accepted;
            for (qsizetype i = 0; i < acc.size();) {
                if (acc.at(i).second < from || acc.at(i).first > to) {
                    ++i;
                    continue;
                }
                from = std::min(from, acc.at(i).first);
                to = std::max(to, acc.at(i).second);
                acc.removeAt(i);
            }
            acc.append({from, to});
        }
        const int n = m_queue.acceptExternalRange(itemId, int(a->member), from, to - from);
        logEd3kDebug(QStringLiteral("ed3k usenet: bytes %1+%2 of file %3 from ed3k: %4 article(s)")
                         .arg(a->offset).arg(a->len).arg(a->member).arg(n));
        m_queue.releaseSealHold(itemId, int(a->member));
    } else if (const auto* r = std::get_if<u::RefetchRange>(&event)) {
        // an unknown tag (a restart before that server was seen again): nobody excluded
        const QString exclude = m_serverKeys.value(r->server);
        (void)m_queue.refetchRange(itemId, int(r->member), qint64(r->offset), qint64(r->len), exclude);
    } else if (const auto* c = std::get_if<u::MemberComplete>(&event)) {
        onMemberComplete(itemId, int(c->member));
    } else if (const auto* v = std::get_if<u::MemberVerified>(&event)) {
        onMemberVerified(itemId, int(v->member), v->root);
    } else if (const auto* rj = std::get_if<u::MemberRejected>(&event)) {
        const auto* item = m_queue.findItem(itemId);
        const QString file = item ? item->bestFileName(int(rj->member)) : QString::number(rj->member);
        if (const auto m = member(itemId, int(rj->member)); m && m->root)
            stopDownload(*m->root);
        logEd3kWarning(QStringLiteral("ed3k usenet: file %1 of %2 is not the post set's; not shared").arg(rj->member).arg(itemId));
        reportProblem(itemId, int(rj->member),
                      tr("ed3k: \"%1\" does not match the ed3k link of its post; it is not shared").arg(file));
    } else if (const auto* cf = std::get_if<u::ClaimFalse>(&event)) {
        onClaimFalse(itemId, *cf);
    } else if (const auto* lm = std::get_if<u::LinkMismatch>(&event)) {
        const auto* item = m_queue.findItem(itemId);
        logEd3kWarning(QStringLiteral("ed3k usenet: %1: link root %2, the post gives %3")
                           .arg(itemId, hexOf(lm->link.bytes), hexOf(lm->computed.bytes)));
        reportProblem(itemId, -1, tr("ed3k: \"%1\" does not match its ed3k link").arg(item ? item->name : itemId));
    } else if (std::holds_alternative<u::PostComplete>(event)) {
        onPostComplete(itemId);
    }
}

void Ed3kUsenetJoin::onMemberComplete(const QString& itemId, int fileIndex)
{
    const auto post = findPost(itemId);
    if (!post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return;
    if (const auto m = member(itemId, fileIndex); m && m->root)
        stopDownload(*m->root);
    Post::Member& m = post->members[std::size_t(fileIndex)];
    if (m.hashing)
        return;
    m.hashing = true;
    m.generation = ++m_hashGeneration;
    const QString path = memberPath(itemId, fileIndex);
    const quint64 generation = m.generation;
    if (m_hashNow) {
        onMemberHashed(itemId, fileIndex, generation, path, hashNow(path));
        return;
    }
    hashInBackground(path, [this, itemId, fileIndex, generation, path](std::optional<FileHash> hashed) {
        onMemberHashed(itemId, fileIndex, generation, path, hashed);
    });
}

void Ed3kUsenetJoin::onMemberHashed(const QString& itemId, int fileIndex, quint64 generation, const QString& path,
                                    const std::optional<FileHash>& hashed)
{
    const auto post = findPost(itemId);
    if (!post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return;
    Post::Member& m = post->members[std::size_t(fileIndex)];
    if (m.generation != generation)
        return;
    m.hashing = false;
    if (!hashed && memberPath(itemId, fileIndex) != path) {
        // sealed or published while hashed: again at its new place
        onMemberComplete(itemId, fileIndex);
        return;
    }
    if (!hashed) {
        // was: ToDo: hash again later (the file is gone or unreadable) (done: after
        // kRehashMs, and at the next start in ensurePost)
        logEd3kWarning(QStringLiteral("ed3k usenet: cannot hash file %1 of %2 (%3)")
                           .arg(fileIndex).arg(itemId, memberPath(itemId, fileIndex)));
        if (++m.rehashTries < kRehashTries)
            QTimer::singleShot(kRehashMs, this, [this, itemId, fileIndex] {
                const auto p = findPost(itemId);
                const auto again = member(itemId, fileIndex);
                if (p && !p->finished && again && again->status == ffi::MemberStatus::Complete)
                    onMemberComplete(itemId, fileIndex);
            });
    } else {
        m.hashes = hashed->hashes;
        logEd3kDebug(QStringLiteral("ed3k usenet: file %1 of %2 hashed: %3, %4 bytes")
                         .arg(fileIndex).arg(itemId, hexOf(hashed->root.bytes)).arg(hashed->size));
        if (auto done = post->join.memberHashed(std::uint64_t(fileIndex), hashed->hashes); !done)
            logEd3kWarning(QStringLiteral("ed3k usenet: hash of file %1 of %2 not taken (error %3)")
                               .arg(fileIndex).arg(itemId, errorName(done.error())));
        drainEvents(itemId);
    }
    m_queue.releaseSealHold(itemId, fileIndex);
    if (const auto again = findPost(itemId); again && again->finished
        && std::ranges::none_of(again->members, [](const Post::Member& x) { return x.hashing; }))
        dropPost(itemId, /*keepBlob*/ false);
    pumpSession();
}

void Ed3kUsenetJoin::onMemberVerified(const QString& itemId, int fileIndex, const ffi::Root& root)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    if (!e || !post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return;
    stopDownload(root);
    const auto m = post->join.member(std::uint64_t(fileIndex));
    const auto* item = m_queue.findItem(itemId);
    const std::uint64_t size = (m && m->size) ? *m->size : 0;
    std::string name = m ? m->name : std::string{};
    if (name.empty() && item)
        name = item->bestFileName(fileIndex).toStdString();
    const auto t = m_session.now();

    // §6: a member verified is shared at once
    const auto& hashes = post->members[std::size_t(fileIndex)].hashes;
    if (!shareMember(ffi::SharedFile{root, size, name, std::nullopt, std::nullopt, ffi::Have::complete(), std::span{hashes}},
                     itemId, fileIndex)) {
        logEd3kWarning(QStringLiteral("ed3k usenet: cannot share file %1 of %2").arg(fileIndex).arg(itemId));
        return;
    }
    const ffi::FileInfo info{root, size, name, std::nullopt, 0, 0, 0, 0, 0, 0};
    if (auto ok = e->publish(t, info, m_holdBack ? ffi::Publish::Source : ffi::Publish::Both); !ok)
        logEd3kDebug(QStringLiteral("ed3k usenet: publish of file %1 of %2 failed (error %3)")
                         .arg(fileIndex).arg(itemId, errorName(ok.error())));
    if (auto alias = post->join.memberAlias(std::uint64_t(fileIndex), m_holdBack); alias && *alias) {
        const std::vector<ffi::Bytes> one{**alias};
        if (auto ok = e->publishAlias(t, one); !ok)
            logEd3kDebug(QStringLiteral("ed3k usenet: nzb-file record of file %1 of %2 not published (error %3)")
                             .arg(fileIndex).arg(itemId, errorName(ok.error())));
        keepRecords(itemId, std::nullopt, {}, one);
    }
    keepMember(itemId, fileIndex, root, size, QString::fromStdString(name));
    logEd3k(QStringLiteral("ed3k usenet: \"%1\" verified and shared as %2")
                .arg(QString::fromStdString(name), hexOf(root.bytes)));
    emit memberVerified(itemId, fileIndex);
}

void Ed3kUsenetJoin::onHaveChanged(const QString& itemId, int fileIndex)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    if (!e || !post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || !m->size)
        return;
    const auto have = post->join.memberHave(std::uint64_t(fileIndex));
    if (!have)
        return;
    ffi::Have h;
    h.kind = static_cast<ffi::Have::Kind>(have->first);
    for (const auto b : have->second)
        h.bits.push_back(std::to_integer<std::uint8_t>(b));
    if (!isShared(*m->root) && h.kind == ffi::Have::Kind::Nothing)
        return;
    // the pieces verified so far; served only within servable() (WantBlock)
    const auto& hashes = post->members[std::size_t(fileIndex)].hashes;
    (void)shareMember(ffi::SharedFile{*m->root, *m->size, m->name, std::nullopt, std::nullopt, h, std::span{hashes}}, itemId, fileIndex);
}

void Ed3kUsenetJoin::onClaimFalse(const QString& itemId, const ffi::uev::ClaimFalse& ev)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    if (!post)
        return;
    // §8: the publishers and every record that named that root are ignored from now on
    std::vector<ffi::Key32> banned(ev.publishers.begin(), ev.publishers.end());
    if (post->search) {
        (void)post->search->ban(ev.publishers);
        for (const auto& k : post->search->blame(ev.root))
            banned.push_back(k);
    }
    if (e && !banned.empty())
        (void)e->aliasBan(m_session.now(), banned);
    stopDownload(ev.root);
    unshare(ev.root);
    // those bytes came from ed3k and do not count any more
    for (const auto& r : ev.drop)
        if (r.end > r.start)
            (void)m_queue.refetchRange(itemId, int(ev.member), qint64(r.start), qint64(r.end - r.start), QString());

    const auto* item = m_queue.findItem(itemId);
    const QString file = item ? item->bestFileName(int(ev.member)) : QString::number(ev.member);
    logEd3kWarning(QStringLiteral("ed3k usenet: false claim %1 for file %2 of %3, %4 publisher(s) banned")
                       .arg(hexOf(ev.root.bytes)).arg(ev.member).arg(itemId).arg(banned.size()));
    reportProblem(itemId, int(ev.member),
                  tr("ed3k: a false claim for \"%1\" was found and ignored; the file continues from Usenet").arg(file));
}

void Ed3kUsenetJoin::onPostComplete(const QString& itemId)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    const auto* item = m_queue.findItem(itemId);
    if (!e || !post)
        return;
    const QString name = item ? item->name : itemId;
    const QByteArray nameUtf8 = name.toUtf8();
    auto done = post->join.complete(m_holdBack, std::string_view(nameUtf8.constData(), std::size_t(nameUtf8.size())));
    if (!done) {
        logEd3kWarning(QStringLiteral("ed3k usenet: \"%1\" complete, nothing to publish (error %2)").arg(name, errorName(done.error())));
        return;
    }
    const auto t = m_session.now();
    if (done->root && done->shareList && !done->manifest.empty()) {
        const QByteArray manifest(reinterpret_cast<const char*>(done->manifest.data()), qsizetype(done->manifest.size()));
        const QByteArray list(reinterpret_cast<const char*>(done->list.data()), qsizetype(done->list.size()));
        // was: ToDo: the manifest and article list are kept in memory only; after a restart
        // they are not served any more (done: keepBlob, shared again at start)
        keepBlob(itemId, manifest, name, done->root);
        keepRecords(itemId, *done->root, name, {});
        if (!list.isEmpty())
            keepBlob(itemId, list, name + QStringLiteral(".articles"), std::nullopt);
        // MEMBER rows (file-set.md §6)
        if (auto ok = e->setPublish(t, done->manifest, *done->root, std::string_view(nameUtf8.constData(), std::size_t(nameUtf8.size())));
            !ok)
            logEd3kDebug(QStringLiteral("ed3k usenet: set of \"%1\" not published (error %2)").arg(name, errorName(ok.error())));
    }
    if (!done->aliases.empty()) {
        keepRecords(itemId, std::nullopt, {}, done->aliases);
        if (auto ok = e->publishAlias(t, done->aliases); !ok)
            logEd3kDebug(QStringLiteral("ed3k usenet: ALIAS records of \"%1\" not published (error %2)").arg(name, errorName(ok.error())));
    }
    logEd3k(QStringLiteral("ed3k usenet: \"%1\" complete%2").arg(name, done->root ? QStringLiteral(", post root ") + hexOf(done->root->bytes) : QString()));
    emit postCompleted(itemId, done->root.has_value());
}

void Ed3kUsenetJoin::reportProblem(const QString& itemId, int fileIndex, const QString& message)
{
    // the Usenet tab shows emule.usenet: where the user sees the item's messages
    logUsenetWarning(message);
    emit memberProblem(itemId, fileIndex, message);
}

bool Ed3kUsenetJoin::sealHold(const QString& itemId, int fileIndex)
{
    if (!m_started || !m_session.isRunning())
        return false;
    const auto post = findPost(itemId);
    if (!post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return false;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || m->status != ffi::MemberStatus::Open)
        return false;
    // only while an ed3k download of it runs
    if (!downloading(*m->root))
        return false;
    const auto missing = post->join.missing(std::uint64_t(fileIndex));
    if (!missing || missing->empty())
        return false;
    Post::Member& pm = post->members[std::size_t(fileIndex)];
    if (!pm.holdUntil) {
        pm.holdUntil = QDeadlineTimer(kSealHoldMs);
        QTimer::singleShot(kSealHoldMs + 1000, this, [this, itemId, fileIndex] {
            m_queue.releaseSealHold(itemId, fileIndex);
        });
        logEd3k(QStringLiteral("ed3k usenet: file %1 of %2 kept open for ed3k (%3 range(s) missing)")
                    .arg(fileIndex).arg(itemId).arg(missing->size()));
    }
    return !pm.holdUntil->hasExpired();
}

QString Ed3kUsenetJoin::memberPath(const QString& itemId, int fileIndex) const
{
    const auto* item = m_queue.findItem(itemId);
    if (!item || fileIndex < 0 || fileIndex >= item->files.size())
        return {};
    const auto& st = item->files.at(fileIndex);
    // resolved on every call: sealFile() renames tempPath, publishing moves it again
    if (!st.finalPath.isEmpty() && QFileInfo::exists(st.finalPath))
        return st.finalPath;
    return st.tempPath;
}

std::optional<QByteArray> Ed3kUsenetJoin::readMember(const QString& itemId, int fileIndex, std::uint64_t offset,
                                                     std::uint32_t len) const
{
    return readFile(FileRead{memberPath(itemId, fileIndex), offset, len});
}

std::optional<Ed3kJoinBase::FileRead> Ed3kUsenetJoin::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    // only bytes verified under the root (usenet-join.md: servable)
    const auto post = findPost(s.owner);
    if (post && !post->join.servable(std::uint64_t(s.member), offset, len))
        return std::nullopt;
    return FileRead{memberPath(s.owner, s.member), offset, len};
}

bool Ed3kUsenetJoin::sharedGone(const Shared& s) const
{
    // the item is done and its file gone (unpacked volumes, moved)
    return !findPost(s.owner) && !QFileInfo::exists(memberPath(s.owner, s.member));
}

void Ed3kUsenetJoin::startDownload(const QString& itemId, int fileIndex)
{
    const auto post = findPost(itemId);
    if (!m_session.engine() || !post || fileIndex < 0)
        return;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || !m->size || m->status != ffi::MemberStatus::Open || downloading(*m->root))
        return;
    const auto missing = post->join.missing(std::uint64_t(fileIndex));
    if (!missing || missing->empty())
        return;
    (void)openDownload(*m->root, *m->size, itemId, fileIndex);
}

std::vector<std::uint64_t> Ed3kUsenetJoin::wantedBlocks(const Download& d) const
{
    const auto post = findPost(d.owner);
    const auto* item = m_queue.findItem(d.owner);
    if (!post || !item || d.member >= item->files.size() || item->files.at(d.member).finalized)
        return {};
    const auto missing = post->join.missing(std::uint64_t(d.member));
    if (!missing)
        return {};
    std::vector<std::uint64_t> wanted;   // block offsets overlapping a missing range
    for (const auto& r : *missing) {
        const std::uint64_t end = std::min(r.end, d.size);
        for (std::uint64_t off = (r.start / kBlock) * kBlock; off < end; off += kBlock)
            if (wanted.empty() || wanted.back() != off)
                wanted.push_back(off);
    }
    return wanted;
}

void Ed3kUsenetJoin::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    const QString itemId = d.owner;
    const int fileIndex = d.member;
    const auto* item = m_queue.findItem(itemId);
    const auto post = findPost(itemId);
    if (!item || !post || fileIndex >= item->files.size())
        return;
    const auto& st = item->files.at(fileIndex);
    // a sealed file is the queue's and post-processing's now
    if (st.finalized || st.tempPath.isEmpty())
        return;
    QFile f(st.tempPath);
    if (!f.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) || !f.seek(qint64(b.offset))
        || f.write(reinterpret_cast<const char*>(b.data.data()), qint64(b.data.size())) != qint64(b.data.size())) {
        logEd3kWarning(QStringLiteral("ed3k usenet: cannot write a block into %1").arg(st.tempPath));
        return;
    }
    f.close();
    if (auto ok = post->join.ed3kBlock(std::uint64_t(fileIndex), b.offset, std::uint32_t(b.data.size())); !ok)
        logEd3kDebug(QStringLiteral("ed3k usenet: block %1 of file %2 not taken (error %3)")
                         .arg(b.offset).arg(fileIndex).arg(errorName(ok.error())));
    drainEvents(itemId);
}

void Ed3kUsenetJoin::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    // was: ToDo: §4.1 the post set of a link (fetchBlob) is not asked for yet (done: here)
    const auto post = findPost(owner);
    if (!post)
        return;
    if (member == kSetManifest && post->link && *post->link == root)
        fetchList(owner, root, data, {}, true);   // §4.1 step 1: the link's manifest
    else if (member == kArticleList)
        onListFetched(owner, root, data);
    pumpSession();
}

void Ed3kUsenetJoin::onAliasFound(const ffi::event::AliasFound& found)
{
    auto* e = m_session.engine();
    const auto it = m_claimSearches.find(found.search);
    if (!e || it == m_claimSearches.end())
        return;
    const auto [itemId, fileIndex] = it->second;
    m_claimSearches.erase(it);
    const auto post = findPost(itemId);
    if (post)
        post->claimsRunning = std::max(0, post->claimsRunning - 1);
    for (int tries = 0; post && tries < 8; ++tries) {
        auto next = e->aliasSearchNext(found.search);
        if (!next || !*next)
            break;
        const auto& c = **next;
        // nzb-file names one file: relation SAME (asked for), with a size
        // was: if (c.rel != 0 || c.size == 0) — 0 asked for any relation then
        if (c.size == 0)
            continue;
        if (fileClaim(itemId, fileIndex, c.root, c.size, c.publishers))
            break;
        (void)e->aliasSearchBlame(m_session.now(), found.search, c.root);
    }
    (void)e->aliasSearchEnd(found.search);
    startClaimSearches(itemId);
}

void Ed3kUsenetJoin::startClaimSearches(const QString& itemId)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    if (!e || !post || !m_searchEnabled)
        return;
    const auto info = post->join.info();
    if (!info || info->judge != ffi::UsenetJudge::Articles)
        return;
    // §4.2 step 1: `nzb-file` only when `nzb` gave no post set that passes §3
    if (info->manifest || setSearching(itemId) || post->setRoot)
        return;
    // was: §4.2: ToDo the `nzb` search of the post id first (sets need their manifest and
    // article list fetched over ed3k); for now straight to `nzb-file`, whose claims the
    // articles test either way (done: startPostSearch, the base's set search)
    // was: ToDo: FFI: alias search rel 0 means "any", SAME (also 0) cannot be asked for
    // (done: ED3K_REL_SAME, ED3K_REL_ANY is "any" now)
    if (post->claimQueue.empty()) {
        std::set<std::array<std::uint8_t, 32>> seen;
        for (std::size_t f = 0; f < post->members.size() && seen.size() < 256; ++f) {
            if (post->members[f].claimSearched)
                continue;
            const auto m = post->join.member(f);
            if (!m || m->root || !m->fileId || m->status != ffi::MemberStatus::Open || !seen.insert(*m->fileId).second)
                continue;
            post->members[f].claimSearched = true;
            post->claimQueue.push_back(int(f));
        }
    }
    while (post->claimsRunning < kClaimSearchesPerPost && !post->claimQueue.empty()) {
        const int f = post->claimQueue.front();
        post->claimQueue.pop_front();
        const auto m = post->join.member(std::uint64_t(f));
        if (!m || !m->fileId || m->root)
            continue;
        auto search = e->aliasSearchStart(m_session.now(), "nzb-file", std::span<const std::uint8_t>(*m->fileId), ED3K_REL_SAME);
        if (!search) {
            logEd3kDebug(QStringLiteral("ed3k usenet: nzb-file search not started (error %1)").arg(errorName(search.error())));
            break;
        }
        m_claimSearches[*search] = {itemId, f};
        ++post->claimsRunning;
    }
}

void Ed3kUsenetJoin::startPostSearch(const QString& itemId)
{
    const auto post = findPost(itemId);
    if (!post)
        return;
    const auto info = post->join.info();
    if (!info || info->judge != ffi::UsenetJudge::Articles || info->manifest)
        return;
    // §4.2 step 1: ALIAS records of `nzb` name post sets (relation SET), at most 3 tried
    if (!info->postId || !startSetSearch(itemId, "nzb", std::span<const std::uint8_t>(*info->postId), ED3K_REL_SET))
        startClaimSearches(itemId);
}

void Ed3kUsenetJoin::fetchList(const QString& itemId, const ffi::Root& root, const QByteArray& manifest,
                               std::vector<ffi::PublisherKey> publishers, bool fromLink)
{
    const auto post = findPost(itemId);
    if (!post)
        return;
    // §3: the article list the manifest names; one above 32 MiB is not fetched
    const auto list = ffi::usenetSetList(spanOf(manifest));
    if (!list) {
        logEd3kDebug(QStringLiteral("ed3k usenet: set %1 of %2 names no article list (error %3)")
                         .arg(hexOf(root.bytes), itemId, errorName(list.error())));
        setRefused(itemId, root, fromLink);
        return;
    }
    post->setRoot = root;
    post->setFile = manifest;
    post->setPublishers = std::move(publishers);
    post->setFromLink = fromLink;
    post->listRoot = list->root;
    if (!fetchBlob(list->root, list->size, itemId, kArticleList)) {
        logEd3kDebug(QStringLiteral("ed3k usenet: article list %1 of %2 not fetched").arg(hexOf(list->root.bytes), itemId));
        setRefused(itemId, root, fromLink);
    }
}

void Ed3kUsenetJoin::onListFetched(const QString& itemId, const ffi::Root& root, const QByteArray& list)
{
    const auto post = findPost(itemId);
    if (!post || !post->setRoot || post->listRoot != root)
        return;
    const QByteArray manifest = std::exchange(post->setFile, {});
    const ffi::Root setRoot = *std::exchange(post->setRoot, std::nullopt);
    const auto publishers = std::exchange(post->setPublishers, {});
    const bool fromLink = post->setFromLink;
    post->listRoot.reset();
    if (!setManifest(itemId, spanOf(manifest), spanOf(list), publishers)) {
        setRefused(itemId, setRoot, fromLink);
        return;
    }
    logEd3k(QStringLiteral("ed3k usenet: \"%1\" has its post set %2").arg(postName(itemId), hexOf(setRoot.bytes)));
    // §6 held back: neither the post set nor the article list is shared
    if (!m_holdBack) {
        keepBlob(itemId, manifest, postName(itemId), setRoot);
        keepBlob(itemId, list, postName(itemId) + QStringLiteral(".articles"), root);
    }
}

void Ed3kUsenetJoin::setRefused(const QString& itemId, const ffi::Root& root, bool fromLink)
{
    if (const auto post = findPost(itemId)) {
        post->setRoot.reset();
        post->setFile.clear();
        post->setPublishers.clear();
        post->listRoot.reset();
    }
    if (fromLink) {
        // §4.1: nobody to blame; the post goes on from Usenet, BLAKE3 still the judge
        reportProblem(itemId, -1, tr("ed3k: the ed3k link of \"%1\" names no post set of it").arg(postName(itemId)));
        return;
    }
    // §4.2 step 2, §8: its publishers blamed, the next set tried; none left: `nzb-file`
    if (!refuseSet(itemId, root) && !setSearching(itemId))
        startClaimSearches(itemId);
}

QString Ed3kUsenetJoin::postName(const QString& itemId) const
{
    const auto* item = m_queue.findItem(itemId);
    return item && !item->name.isEmpty() ? item->name : itemId;
}

std::optional<ffi::FileLink> Ed3kUsenetJoin::metaLink(const usenet::NzbInfo& nzb)
{
    const QString link = nzb.metaValue(QStringLiteral("ed3k")).trimmed();
    if (link.isEmpty())
        return std::nullopt;
    const QByteArray text = link.toUtf8();
    auto parsed = ffi::parseFileLink(std::string_view(text.constData(), std::size_t(text.size())));
    if (!parsed)
        return std::nullopt;
    return *parsed;
}

void Ed3kUsenetJoin::savePost(const QString& itemId)
{
    const auto post = findPost(itemId);
    if (!post)
        return;
    auto blob = post->join.save();
    if (!blob) {
        logEd3kWarning(QStringLiteral("ed3k usenet: join of %1 not saved (error %2)").arg(itemId, errorName(blob.error())));
        return;
    }
    QDir().mkpath(usenetDir());
    if (!writeBlob(joinPath(itemId), *blob))
        logEd3kWarning(QStringLiteral("ed3k usenet: cannot write %1").arg(joinPath(itemId)));
}

QString Ed3kUsenetJoin::usenetDir() const
{
    return joinDir(QStringLiteral("usenet"));
}

} // namespace eMule::ed3k
