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

constexpr std::uint64_t kBlock = 256 * 1024;
/// a file whose articles all resolved waits this long for ed3k to fill its holes
constexpr int kSealHoldMs = 10 * 60 * 1000;
constexpr int kSaveDelayMs = 5000;
constexpr std::size_t kMaxStreams = 4;
constexpr int kClaimSearchesPerPost = 4;
/// reads given by block_read in one go; a guard, the core gives few
constexpr int kMaxReadsPerCall = 1 << 16;
constexpr qint64 kHashChunk = 1 << 20;

QString hexOf(std::span<const std::uint8_t> bytes)
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
        if (!h.update(spanOf(chunk)))
            return std::nullopt;
    }
    auto done = h.finish();
    if (!done)
        return std::nullopt;
    const auto bytes = done->hashes.bytes();
    ffi::Bytes hashes(bytes.begin(), bytes.end());
    return std::pair{std::move(*done), std::move(hashes)};
}

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
        /// ACCEPT_RANGEs merged: they come per block, an article across two blocks is
        /// only accepted inside their union (acceptExternalRange: wholly inside)
        QList<QPair<qint64, qint64>> accepted;
    };
    std::vector<Member> members;
    std::deque<int> claimQueue;
    int claimsRunning = 0;
    bool finished = false;   ///< the item finished; dropped once no hash is pending
};

// An ed3k download of one member's root.
struct Ed3kUsenetJoin::Download {
    QString itemId;
    int fileIndex = -1;
    std::uint64_t size = 0;
    std::map<std::uint64_t, bool> streams;            ///< stream → holds a slot
    std::map<std::uint64_t, std::uint64_t> outstanding;   ///< offset → stream
    std::set<ffi::NodeId> asked;
    bool hashes = false;
};

Ed3kUsenetJoin::Ed3kUsenetJoin(Ed3kSession& session, UsenetJoinQueue& queue, const QString& configDir,
                               QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_queue(queue)
    , m_configDir(configDir)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &Ed3kUsenetJoin::saveDirty);
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kUsenetJoin::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kUsenetJoin::onSessionStatus);
}

Ed3kUsenetJoin::~Ed3kUsenetJoin()
{
    stop();
    m_cancel->store(true);
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
    m_started = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    QDir().mkpath(usenetDir());
    m_queue.setSealHold([this](const QString& itemId, int fileIndex) { return sealHold(itemId, fileIndex); });
    for (const QString& id : m_queue.itemIds())
        (void)ensurePost(id);
    pumpSession();
}

void Ed3kUsenetJoin::stop()
{
    if (!m_started)
        return;
    m_cancel->store(true);
    m_saveTimer.stop();
    for (const auto& [id, post] : m_posts)
        savePost(id);
    m_dirty.clear();

    // files held unsealed for ed3k: sealed now
    QList<std::pair<QString, int>> held;
    for (const auto& [id, post] : m_posts)
        for (std::size_t f = 0; f < post->members.size(); ++f)
            if (post->members[f].holdUntil)
                held.append({id, int(f)});

    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [root, d] : m_downloads) {
            for (const auto& [stream, slot] : d->streams)
                (void)e->streamClose(t, stream);
            (void)e->downloadClose(t, root);
        }
        for (const auto& [search, member] : m_claimSearches)
            (void)e->aliasSearchEnd(search);
        for (const auto& [root, shared] : m_shared)
            (void)e->fileClose(t, root);
    }
    m_downloads.clear();
    m_streams.clear();
    m_claimSearches.clear();
    m_sourceLookups.clear();
    m_shared.clear();
    m_posts.clear();
    m_noJoin.clear();
    m_started = false;
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
    const QString link = nzb.metaValue(QStringLiteral("ed3k")).trimmed();
    if (link.isEmpty())
        return std::nullopt;
    const QByteArray text = link.toUtf8();
    auto parsed = ffi::parseFileLink(std::string_view(text.constData(), std::size_t(text.size())));
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
    auto* e = m_session.engine();
    if (!post || !e)
        return;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || !m->size)
        return;
    startDownload(itemId, fileIndex);
    const auto it = m_downloads.find(*m->root);
    if (it == m_downloads.end())
        return;
    Download& d = *it->second;
    if (d.streams.size() >= kMaxStreams || d.asked.contains(source.id))
        return;
    auto stream = e->fileRequest(m_session.now(), source, ffi::Request{*m->root, d.size, ffi::Have{}, 0, false, false});
    if (!stream) {
        logEd3kDebug(QStringLiteral("ed3k usenet: source refused (error %1)").arg(errorName(stream.error())));
        return;
    }
    d.asked.insert(source.id);
    d.streams[*stream] = false;
    m_streams[*stream] = *m->root;
    pumpSession();
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
    auto* e = m_session.engine();
    for (auto it = m_shared.begin(); it != m_shared.end();) {
        if (it->second.itemId == itemId) {
            if (e)
                (void)e->fileClose(m_session.now(), it->first);
            it = m_shared.erase(it);
        } else {
            ++it;
        }
    }
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
                                      int partNumber, const QByteArray& rawYbeginLine, const QString& serverKey)
{
    const auto post = ensurePost(itemId);
    auto* e = m_session.engine();
    const auto* item = m_queue.findItem(itemId);
    if (!post || !e || !item || fileIndex < 0 || fileIndex >= item->nzb.files.size() || offset < 0 || length <= 0)
        return;

    // mᵢ of the article: the first <segment> of its number in NZB order (only that one
    // counts, usenet.md §1; a report for another one would be STALE anyway).
    // ToDo: the queue could hand the message ID itself; a duplicated number with two
    // fetched IDs is reported under the first one
    QString messageId;
    bool found = false;
    for (const auto& s : item->nzb.files.at(fileIndex).nzbOrderSegments()) {
        if (s.number == partNumber) {
            messageId = s.messageId;
            found = true;
            break;
        }
    }
    if (!found || messageId.isEmpty())
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
        // ToDo: archive volumes are deleted right after this signal (consumed by the
        // unpack), so their hash at MEMBER_COMPLETE fails; hash them here, synchronously?
        serveReads(itemId, std::move(*reads));
    }
    scheduleSave(itemId);
    pumpSession();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kUsenetJoin::onEventPolled(const ffi::Event& event, bool* handled)
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
            // ToDo: an upload queue and slots for ed3k (bandwidth, fairness); every
            // downloader of a usenet member gets a slot at once for now
            if (r->first)
                (void)m_session.engine()->queueSet(m_session.now(), r->node, 0, 0);
            if (handled)
                *handled = true;
        }
    } else if (const auto* b = std::get_if<ev::Block>(&event)) {
        onBlock(*b);
    } else if (const auto* q = std::get_if<ev::QueueRank>(&event)) {
        if (const auto it = m_streams.find(q->stream); it != m_streams.end()) {
            const ffi::Root root = it->second;
            if (const auto d = m_downloads.find(root); d != m_downloads.end())
                d->second->streams[q->stream] = q->rank == 0;
            requestBlocks(root);
        }
    } else if (const auto* h = std::get_if<ev::Hashes>(&event)) {
        if (const auto d = m_downloads.find(h->root); d != m_downloads.end()) {
            d->second->hashes = true;
            const QString itemId = d->second->itemId;
            const int f = d->second->fileIndex;
            if (const auto post = findPost(itemId)) {
                if (f >= 0 && std::size_t(f) < post->members.size() && post->members[std::size_t(f)].hashes.empty())
                    post->members[std::size_t(f)].hashes = h->blob;
                // block values are there now: articles can be checked
                if (auto reads = post->join.pendingReads(m_session.engine()); reads && !reads->empty())
                    serveReads(itemId, std::move(*reads));
            }
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
        if (m_streams.contains(fs->stream) && !fs->usable)
            (void)m_session.engine()->streamClose(m_session.now(), fs->stream);
    } else if (const auto* sc = std::get_if<ev::StreamClosed>(&event)) {
        if (const auto it = m_streams.find(sc->stream); it != m_streams.end()) {
            const ffi::Root root = it->second;
            m_streams.erase(it);
            if (const auto d = m_downloads.find(root); d != m_downloads.end()) {
                d->second->streams.erase(sc->stream);
                std::erase_if(d->second->outstanding, [&](const auto& o) { return o.second == sc->stream; });
                // ToDo: search sources again when the last stream closed
            }
        }
    } else if (const auto* kf = std::get_if<ev::KadFound>(&event)) {
        onKadFound(*kf);
    } else if (const auto* af = std::get_if<ev::AliasFound>(&event)) {
        onAliasFound(*af);
    }
}

void Ed3kUsenetJoin::onSessionStatus()
{
    if (m_session.isRunning() && !m_started)
        start();
    else if (!m_session.isRunning() && m_started)
        stop();
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
    const auto link = metaRoot(item->nzb);
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
    if (link) {
        // ToDo: §4.1 fetch the post set (manifest and article list) of the link over ed3k,
        // then setManifest(); until then the link only makes the core the BLAKE3 judge
        logEd3kDebug(QStringLiteral("ed3k usenet: post root %1 of \"%2\": fetching its set is not implemented")
                         .arg(hexOf(link->bytes), item->name));
    } else {
        startClaimSearches(itemId);
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
        std::vector<ffi::Root> roots;
        for (const auto& [root, d] : m_downloads)
            if (d->itemId == itemId)
                roots.push_back(root);
        for (const auto& root : roots)
            stopDownload(root);
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
    m_dirty.remove(itemId);
    if (!keepBlob)
        QFile::remove(joinPath(itemId));
}

void Ed3kUsenetJoin::serveReads(const QString& itemId, std::vector<ffi::TorrentRead> reads)
{
    std::deque<ffi::TorrentRead> todo(reads.begin(), reads.end());
    for (int guard = 0; !todo.empty() && guard < kMaxReadsPerCall; ++guard) {
        const ffi::TorrentRead r = todo.front();
        todo.pop_front();
        const auto post = findPost(itemId);
        if (!post)
            return;
        // ToDo: one block (256 KiB) read on the daemon thread; move to a worker if
        // reads ever come in large batches
        const auto bytes = readMember(itemId, int(r.member), r.offset, r.len);
        if (!bytes) {
            (void)post->join.readFailed(r);
            continue;
        }
        auto more = post->join.blockRead(m_session.engine(), r, spanOf(*bytes));
        if (!more) {
            logEd3kDebug(QStringLiteral("ed3k usenet: block %1 of file %2 not taken (error %3)")
                             .arg(r.offset).arg(r.member).arg(errorName(more.error())));
            continue;
        }
        todo.insert(todo.end(), more->begin(), more->end());
    }
    drainEvents(itemId);
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
    QPointer<Ed3kUsenetJoin> self(this);
    const auto cancel = m_cancel;
    QThreadPool::globalInstance()->start([self, cancel, path, itemId, fileIndex, generation] {
        auto hashed = hashFile(path, *cancel);
        if (cancel->load())
            return;
        // owned copies into the lambda; ffi::Hashed holds a move-only Buffer
        const bool ok = hashed.has_value();
        const ffi::Root root = ok ? hashed->first.root : ffi::Root{};
        const std::uint64_t size = ok ? hashed->first.size : 0;
        ffi::Bytes hashes = ok ? std::move(hashed->second) : ffi::Bytes{};
        QMetaObject::invokeMethod(
            self.data(),
            [self, itemId, fileIndex, generation, path, ok, root, size, hashes = std::move(hashes)] {
                if (self)
                    self->onMemberHashed(itemId, fileIndex, generation, path, ok, root, size, hashes);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kUsenetJoin::onMemberHashed(const QString& itemId, int fileIndex, quint64 generation, const QString& path, bool ok,
                                    const ffi::Root& root, std::uint64_t size, const ffi::Bytes& hashes)
{
    const auto post = findPost(itemId);
    if (!post || fileIndex < 0 || std::size_t(fileIndex) >= post->members.size())
        return;
    Post::Member& m = post->members[std::size_t(fileIndex)];
    if (m.generation != generation)
        return;
    m.hashing = false;
    if (!ok && memberPath(itemId, fileIndex) != path) {
        // sealed or published while hashed: again at its new place
        onMemberComplete(itemId, fileIndex);
        return;
    }
    if (!ok) {
        // ToDo: hash again later (the file is gone or unreadable)
        logEd3kWarning(QStringLiteral("ed3k usenet: cannot hash file %1 of %2 (%3)")
                           .arg(fileIndex).arg(itemId, memberPath(itemId, fileIndex)));
    } else {
        m.hashes = hashes;
        logEd3kDebug(QStringLiteral("ed3k usenet: file %1 of %2 hashed: %3, %4 bytes")
                         .arg(fileIndex).arg(itemId, hexOf(root.bytes)).arg(size));
        if (auto done = post->join.memberHashed(std::uint64_t(fileIndex), hashes); !done)
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
    if (m_shared.contains(root)) {
        (void)e->fileHave(t, root, ffi::Have::complete());
    } else {
        const auto& hashes = post->members[std::size_t(fileIndex)].hashes;
        const ffi::SharedFile f{root, size, name, std::nullopt, std::nullopt, ffi::Have::complete(), std::span{hashes}};
        if (auto ok = e->fileOpen(t, f); !ok) {
            logEd3kWarning(QStringLiteral("ed3k usenet: cannot share file %1 of %2 (error %3)")
                               .arg(fileIndex).arg(itemId, errorName(ok.error())));
            return;
        }
        m_shared[root] = Shared{itemId, fileIndex, size, {}};
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
    }
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
    const auto t = m_session.now();
    if (m_shared.contains(*m->root)) {
        (void)e->fileHave(t, *m->root, h);
        return;
    }
    if (h.kind == ffi::Have::Kind::Nothing)
        return;
    // the pieces verified so far; served only within servable() (WantBlock)
    const auto& hashes = post->members[std::size_t(fileIndex)].hashes;
    const ffi::SharedFile f{*m->root, *m->size, m->name, std::nullopt, std::nullopt, h, std::span{hashes}};
    if (auto ok = e->fileOpen(t, f); ok)
        m_shared[*m->root] = Shared{itemId, fileIndex, *m->size, {}};
    else
        logEd3kDebug(QStringLiteral("ed3k usenet: partial share of file %1 of %2 refused (error %3)")
                         .arg(fileIndex).arg(itemId, errorName(ok.error())));
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
    if (const auto s = m_shared.find(ev.root); s != m_shared.end()) {
        if (e)
            (void)e->fileClose(m_session.now(), ev.root);
        m_shared.erase(s);
    }
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
        // ToDo: the manifest and article list are kept in memory only; after a restart
        // they are not served any more
        shareBlob(manifest, name, done->root);
        if (!list.isEmpty())
            shareBlob(list, name + QStringLiteral(".articles"), std::nullopt);
        // MEMBER rows (file-set.md §6)
        if (auto ok = e->setPublish(t, done->manifest, *done->root, std::string_view(nameUtf8.constData(), std::size_t(nameUtf8.size())));
            !ok)
            logEd3kDebug(QStringLiteral("ed3k usenet: set of \"%1\" not published (error %2)").arg(name, errorName(ok.error())));
    }
    if (!done->aliases.empty()) {
        if (auto ok = e->publishAlias(t, done->aliases); !ok)
            logEd3kDebug(QStringLiteral("ed3k usenet: ALIAS records of \"%1\" not published (error %2)").arg(name, errorName(ok.error())));
    }
    logEd3k(QStringLiteral("ed3k usenet: \"%1\" complete%2").arg(name, done->root ? QStringLiteral(", post root ") + hexOf(done->root->bytes) : QString()));
    emit postCompleted(itemId, done->root.has_value());
}

void Ed3kUsenetJoin::shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect)
{
    auto* e = m_session.engine();
    if (!e || data.isEmpty())
        return;
    ffi::Hasher h;
    if (!h.update(spanOf(data)))
        return;
    auto hashed = h.finish();
    if (!hashed)
        return;
    if (expect && hashed->root != *expect)
        logEd3kWarning(QStringLiteral("ed3k usenet: manifest of \"%1\" is not its post root").arg(name));
    const QByteArray n = name.toUtf8();
    const ffi::SharedFile f{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt,
                            std::nullopt, ffi::Have::complete(), hashed->hashes.bytes()};
    const auto t = m_session.now();
    if (auto ok = e->fileOpen(t, f); !ok && !m_shared.contains(hashed->root)) {
        logEd3kDebug(QStringLiteral("ed3k usenet: cannot share \"%1\" (error %2)").arg(name, errorName(ok.error())));
        return;
    }
    m_shared[hashed->root] = Shared{QString(), -1, hashed->size, data};
    const ffi::FileInfo info{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt, 0, 0, 0, 0, 0, 0};
    (void)e->publish(t, info, ffi::Publish::Source);
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
    if (!m_downloads.contains(*m->root))
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
    const QString path = memberPath(itemId, fileIndex);
    if (path.isEmpty())
        return std::nullopt;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(qint64(offset)))
        return std::nullopt;
    QByteArray data = f.read(qint64(len));
    if (data.size() != qsizetype(len))
        return std::nullopt;
    return data;
}

bool Ed3kUsenetJoin::answerBlock(std::uint64_t id, const ffi::Root& root, std::uint64_t offset,
                                 std::optional<std::uint32_t> len)
{
    auto* e = m_session.engine();
    const auto it = m_shared.find(root);
    if (!e || it == m_shared.end())
        return false;
    const Shared& s = it->second;
    const auto t = m_session.now();
    if (offset >= s.size) {
        (void)e->blockFail(t, id);
        return true;
    }
    const auto n = std::uint32_t(len.value_or(std::uint32_t(std::min<std::uint64_t>(kBlock, s.size - offset))));
    std::optional<QByteArray> data;
    if (!s.blob.isEmpty()) {
        if (offset + n <= std::uint64_t(s.blob.size()))
            data = s.blob.mid(qsizetype(offset), qsizetype(n));
    } else {
        // only bytes verified under the root (usenet-join.md: servable)
        const auto post = findPost(s.itemId);
        if (!post || post->join.servable(std::uint64_t(s.fileIndex), offset, n))
            data = readMember(s.itemId, s.fileIndex, offset, n);
    }
    if (!data) {
        (void)e->blockFail(t, id);
        // the item is done and its file gone (unpacked volumes, moved): not shared any more
        if (s.blob.isEmpty() && !findPost(s.itemId) && !QFileInfo::exists(memberPath(s.itemId, s.fileIndex))) {
            logEd3kDebug(QStringLiteral("ed3k usenet: %1 is gone, not shared any more").arg(hexOf(root.bytes)));
            (void)e->fileClose(t, root);
            m_shared.erase(it);
        }
        return true;
    }
    auto buf = ffi::Buffer::alloc(std::size_t(data->size()));
    if (!buf) {
        (void)e->blockFail(t, id);
        return true;
    }
    std::memcpy(buf->bytes().data(), data->constData(), std::size_t(data->size()));
    (void)e->blockProvide(t, id, std::move(*buf));
    return true;
}

void Ed3kUsenetJoin::startDownload(const QString& itemId, int fileIndex)
{
    auto* e = m_session.engine();
    const auto post = findPost(itemId);
    if (!e || !post || fileIndex < 0)
        return;
    const auto m = post->join.member(std::uint64_t(fileIndex));
    if (!m || !m->root || !m->size || m->status != ffi::MemberStatus::Open || m_downloads.contains(*m->root))
        return;
    const auto missing = post->join.missing(std::uint64_t(fileIndex));
    if (!missing || missing->empty())
        return;
    const auto t = m_session.now();
    if (auto ok = e->downloadOpen(t, *m->root, *m->size); !ok)
        logEd3kDebug(QStringLiteral("ed3k usenet: download of %1 not opened (error %2)").arg(hexOf(m->root->bytes), errorName(ok.error())));
    auto d = std::make_unique<Download>();
    d->itemId = itemId;
    d->fileIndex = fileIndex;
    d->size = *m->size;
    m_downloads.emplace(*m->root, std::move(d));
    if (m_searchEnabled) {
        // ToDo: search again every so often while no stream holds a slot
        if (auto lookup = e->searchSources(t, *m->root))
            m_sourceLookups[*lookup] = *m->root;
    }
}

void Ed3kUsenetJoin::stopDownload(const ffi::Root& root)
{
    const auto it = m_downloads.find(root);
    if (it == m_downloads.end())
        return;
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [stream, slot] : it->second->streams)
            (void)e->streamClose(t, stream);
        (void)e->downloadClose(t, root);
    }
    std::erase_if(m_streams, [&](const auto& s) { return s.second == root; });
    std::erase_if(m_sourceLookups, [&](const auto& s) { return s.second == root; });
    m_downloads.erase(it);
}

void Ed3kUsenetJoin::requestBlocks(const ffi::Root& root)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end())
        return;
    Download& d = *it->second;
    const auto post = findPost(d.itemId);
    const auto* item = m_queue.findItem(d.itemId);
    if (!post || !item || d.fileIndex >= item->files.size() || item->files.at(d.fileIndex).finalized)
        return;
    const auto missing = post->join.missing(std::uint64_t(d.fileIndex));
    if (!missing)
        return;

    std::vector<std::uint64_t> wanted;   // block offsets overlapping a missing range
    for (const auto& r : *missing) {
        const std::uint64_t end = std::min(r.end, d.size);
        for (std::uint64_t off = (r.start / kBlock) * kBlock; off < end; off += kBlock) {
            if (!d.outstanding.contains(off) && (wanted.empty() || wanted.back() != off) && e->canVerify(root, off))
                wanted.push_back(off);
        }
    }
    std::size_t granted = 0;   // not `slots`: a Qt macro
    for (const auto& [stream, slot] : d.streams)
        granted += slot ? 1 : 0;
    if (wanted.empty() || granted == 0)
        return;
    const std::size_t perStream = std::clamp<std::size_t>(e->rangesHint(std::uint32_t(granted)), 1, 16);
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

void Ed3kUsenetJoin::onBlock(const ffi::event::Block& b)
{
    const auto it = m_downloads.find(b.root);
    if (it == m_downloads.end())
        return;
    Download& d = *it->second;
    d.outstanding.erase(b.offset);
    const QString itemId = d.itemId;
    const int fileIndex = d.fileIndex;
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
    requestBlocks(b.root);
}

void Ed3kUsenetJoin::onKadFound(const ffi::event::KadFound& found)
{
    const auto it = m_sourceLookups.find(found.lookup);
    if (it == m_sourceLookups.end())
        return;
    const ffi::Root root = it->second;
    const auto d = m_downloads.find(root);
    if (d == m_downloads.end())
        return;
    const QString itemId = d->second->itemId;
    const int fileIndex = d->second->fileIndex;
    for (const auto& s : found.sources)
        addSource(itemId, fileIndex, s);
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
        // nzb-file names one file: relation SAME (0) only, with a size
        if (c.rel != 0 || c.size == 0)
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
    // §4.2: ToDo the `nzb` search of the post id first (sets need their manifest and
    // article list fetched over ed3k); for now straight to `nzb-file`, whose claims the
    // articles test either way
    // ToDo: FFI: alias search rel 0 means "any", SAME (also 0) cannot be asked for
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
        auto search = e->aliasSearchStart(m_session.now(), "nzb-file", std::span<const std::uint8_t>(*m->fileId));
        if (!search) {
            logEd3kDebug(QStringLiteral("ed3k usenet: nzb-file search not started (error %1)").arg(errorName(search.error())));
            break;
        }
        m_claimSearches[*search] = {itemId, f};
        ++post->claimsRunning;
    }
}

void Ed3kUsenetJoin::scheduleSave(const QString& itemId)
{
    if (!m_posts.contains(itemId))
        return;
    m_dirty.insert(itemId);
    if (!m_saveTimer.isActive())
        m_saveTimer.start(kSaveDelayMs);
}

void Ed3kUsenetJoin::saveDirty()
{
    const auto dirty = m_dirty;
    m_dirty.clear();
    for (const QString& id : dirty)
        savePost(id);
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

void Ed3kUsenetJoin::pumpSession()
{
    if (m_session.isRunning())
        m_session.pump();
}

QString Ed3kUsenetJoin::usenetDir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("usenet"));
}

} // namespace eMule::ed3k
