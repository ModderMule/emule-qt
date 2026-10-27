/// @file Ed3kJoinBase.cpp
/// @brief The ed3k side shared by every join — implementation.

#include "Ed3kJoinBase.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace {

constexpr int kSaveDelayMs = 5000;
constexpr std::size_t kMaxStreams = 4;
constexpr qint64 kHashChunk = 1 << 20;

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

} // namespace

Ed3kJoinBase::Ed3kJoinBase(Ed3kSession& session, const QString& configDir, QString tag, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_configDir(configDir)
    , m_tag(std::move(tag))
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &Ed3kJoinBase::saveDirty);
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kJoinBase::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kJoinBase::onSessionStatus);
}

Ed3kJoinBase::~Ed3kJoinBase()
{
    m_cancel->store(true);
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

void Ed3kJoinBase::stopDownloads(const QString& owner)
{
    std::vector<ffi::Root> roots;
    for (const auto& [root, d] : m_downloads)
        if (d->owner == owner)
            roots.push_back(root);
    for (const auto& root : roots)
        stopDownload(root);
}

void Ed3kJoinBase::addSource(const ffi::Root& root, const ffi::Source& source)
{
    auto* e = m_session.engine();
    const auto it = m_downloads.find(root);
    if (!e || it == m_downloads.end())
        return;
    Download& d = *it->second;
    if (d.streams.size() >= kMaxStreams || d.asked.contains(source.id))
        return;
    auto stream = e->fileRequest(m_session.now(), source, ffi::Request{root, d.size, ffi::Have{}, 0, false, false});
    if (!stream) {
        logDebug(QStringLiteral("source refused (error %1)").arg(errorName(stream.error())));
        return;
    }
    d.asked.insert(source.id);
    d.streams[*stream] = false;
    m_streams[*stream] = root;
    pumpSession();
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
    std::erase_if(wanted, [&](std::uint64_t off) { return d.outstanding.contains(off) || !e->canVerify(root, off); });
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

void Ed3kJoinBase::shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect)
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
        logWarning(QStringLiteral("\"%1\" is not the root it should be").arg(name));
    const QByteArray n = name.toUtf8();
    const ffi::SharedFile f{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt,
                            std::nullopt, ffi::Have::complete(), hashed->hashes.bytes()};
    const auto t = m_session.now();
    if (auto ok = e->fileOpen(t, f); !ok && !m_shared.contains(hashed->root)) {
        logDebug(QStringLiteral("cannot share \"%1\" (error %2)").arg(name, errorName(ok.error())));
        return;
    }
    m_shared[hashed->root] = Shared{QString(), -1, hashed->size, data};
    const ffi::FileInfo info{hashed->root, hashed->size, std::string(n.constData(), std::size_t(n.size())), std::nullopt, 0, 0, 0, 0, 0, 0};
    (void)e->publish(t, info, ffi::Publish::Source);
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
    return true;
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
}

void Ed3kJoinBase::closeAll()
{
    m_cancel->store(true);
    m_saveTimer.stop();
    m_dirty.clear();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
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
            // ToDo: an upload queue and slots for ed3k (bandwidth, fairness); every
            // downloader of a joined member gets a slot at once for now
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
        data = readShared(s, offset, n);
    }
    if (!data) {
        (void)e->blockFail(t, id);
        if (s.blob.isEmpty() && sharedGone(s)) {
            logDebug(QStringLiteral("%1 is gone, not shared any more").arg(hexOf(root.bytes)));
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
    if (m_searchEnabled) {
        // ToDo: search again every so often while no stream holds a slot
        if (auto lookup = e->searchSources(t, root))
            m_sourceLookups[*lookup] = root;
    }
    return true;
}

void Ed3kJoinBase::saveDirty()
{
    const auto dirty = m_dirty;
    m_dirty.clear();
    for (const QString& owner : dirty)
        saveOwner(owner);
}

} // namespace eMule::ed3k
