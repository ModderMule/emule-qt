/// @file Ed3kEd2kJoin.cpp
/// @brief eD2K downloads fed from ed3k too (§14.2, §14.4) — implementation.

#include "Ed3kEd2kJoin.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"

#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"

#include <QDateTime>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace {

constexpr int kTickMs = 2000;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// ALIAS id of scheme ed2k (alias.md): md4 ‖ size, 8 bytes big endian.
std::vector<std::uint8_t> ed2kAliasId(const QByteArray& md4, std::uint64_t size)
{
    std::vector<std::uint8_t> id(md4.begin(), md4.end());
    for (int i = 7; i >= 0; --i)
        id.push_back(std::uint8_t((size >> (8 * i)) & 0xff));
    return id;
}

ffi::Ed2kHash hashOf(const QByteArray& md4)
{
    ffi::Ed2kHash h{};
    std::memcpy(h.data(), md4.constData(), std::min<std::size_t>(h.size(), std::size_t(md4.size())));
    return h;
}

} // namespace

Ed3kEd2kJoin::Ed3kEd2kJoin(Ed3kSession& session, DownloadQueue& queue, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("ed2k"), parent)
    , m_queue(queue)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &Ed3kEd2kJoin::onTick);
    // a new download is looked up at once; removed / completed ones on the next tick
    connect(&m_queue, &DownloadQueue::fileAdded, this, [this](PartFile*) {
        if (m_started)
            sync();
    });
    connect(&m_queue, &DownloadQueue::fileRemoved, this, [this](PartFile* f) {
        if (f)
            forget(QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16));
    });
    connect(&m_queue, &DownloadQueue::fileCompleted, this, [this](PartFile* f) {
        // Ed3kShared serves it from now on
        if (f)
            forget(QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16));
    });
}

Ed3kEd2kJoin::~Ed3kEd2kJoin()
{
    for (auto& [md4, f] : m_files)
        releaseHints(f);
}

void Ed3kEd2kJoin::start()
{
    if (m_started || !m_enabled || !m_session.isRunning())
        return;
    beginStart();
    m_tick.start();
    sync();
    pumpSession();
}

void Ed3kEd2kJoin::stop()
{
    if (!m_started)
        return;
    m_tick.stop();
    for (auto& [md4, f] : m_files)
        releaseHints(f);
    if (auto* e = m_session.engine())
        for (const auto& [search, md4] : m_searches)
            (void)e->aliasSearchEnd(search);
    m_searches.clear();
    m_files.clear();
    closeAll();
    pumpSession();
}

void Ed3kEd2kJoin::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        stop();
    else
        start();
}

AppContext::Ed3kHint Ed3kEd2kJoin::offerHint(UpDownClient& client, bool wait)
{
    Q_UNUSED(wait)
    if (!client.hasValidHash())
        return AppContext::Ed3kHint::NoHint;
    ffi::Ed2kHash hash{};
    std::memcpy(hash.data(), client.userHash(), hash.size());
    const auto hint = ffi::userHashHint(hash);
    if (!hint)
        return AppContext::Ed3kHint::NoHint;
    if (!m_started || !client.reqFile())
        return AppContext::Ed3kHint::Off;
    File* f = ensureFile(*client.reqFile());
    if (!f)
        return AppContext::Ed3kHint::Off;
    // a LowID hint: no address, but the file is worth looking up (§14.2)
    const Address& ip = client.connectAddress();
    if (ip.isNull() || (client.hasLowID() && !ip.isIPv6()) || client.userPort() == 0) {
        if (!f->root && !f->searching)
            startSearch(*f);
        return AppContext::Ed3kHint::Off;
    }
    // not on ed3k (no root found lately): eD2K alone until the next search
    if (!f->root && !f->searching) {
        if (nowMs() < f->nextSearch)
            return AppContext::Ed3kHint::Off;
        startSearch(*f);
        if (!f->searching)
            return AppContext::Ed3kHint::Off;
    }

    ffi::Source src;
    std::copy(hint->begin(), hint->end(), src.id.bytes.begin());
    src.hint = true;
    // §14.1: ed3k UDP port = advertised eD2K TCP port
    const auto addr = toAddress(ip.toQHostAddress(), client.userPort());
    if (ip.isIPv6())
        src.ep6 = addr;
    else
        src.ep4 = addr;

    auto same = std::ranges::find_if(f->hints, [&](const Hint& h) { return h.source.id == src.id; });
    if (same == f->hints.end()) {
        f->hints.push_back(Hint{});
        same = std::prev(f->hints.end());
    }
    same->source = src;
    same->client = &client;
    same->deadline = nowMs() + m_hintTimeoutMs;
    same->told = false;
    same->answered = false;
    same->stream = 0;
    logDebug(QStringLiteral("eD2K source %1 has a derived user hash, asking over ed3k first")
                 .arg(Endpoint(ip, client.userPort()).toString()));
    if (f->root && downloading(*f->root)) {
        addSource(*f->root, src);
        pumpSession();
    }
    return AppContext::Ed3kHint::Started;
}

void Ed3kEd2kJoin::onPartVerdict(const QByteArray& md4, std::uint32_t part, bool ok)
{
    const auto it = m_files.find(md4);
    if (it == m_files.end() || !it->second.root)
        return;
    File& f = it->second;
    const std::uint64_t from = std::uint64_t(part) * PARTSIZE;
    const std::uint64_t to = std::min<std::uint64_t>(from + PARTSIZE, f.size);   // exclusive
    const auto first = f.blocks.lower_bound(from >= kBlock ? from - (from % kBlock) : 0);
    const bool ours = first != f.blocks.end() && *first < to;
    if (ok) {
        if (ours && f.pair == Pair::Unknown) {
            logInfo(QStringLiteral("\"%1\": ed3k data passed eD2K part %2, pair trusted").arg(f.name).arg(part));
            setPair(f, Pair::Trusted);
        }
        f.shareDirty = true;
        return;
    }
    // the part is downloaded again: what came from ed3k there is not servable any more
    for (auto b = f.blocks.lower_bound(from - (from % kBlock)); b != f.blocks.end() && *b < to;)
        b = f.blocks.erase(b);
    for (auto b = f.whole.lower_bound(from - (from % kBlock)); b != f.whole.end() && *b < to;)
        b = f.whole.erase(b);
    f.shareDirty = true;
    if (!ours || !f.search)
        return;
    auto* e = m_session.engine();
    PartFile* pf = partFile(md4);
    const auto range = ffi::aliasEd2kPartBlocks(f.size, part);
    if (!e || !pf || !range)
        return;
    const auto data = readFile(FileRead{pf->dataFilePath(), range->first, std::uint32_t(range->second - range->first)});
    if (!data)
        return;
    const auto verdict = e->aliasSearchPartFailed(m_session.now(), *f.search, *f.root, part, spanOf(*data));
    if (!verdict) {
        logDebug(QStringLiteral("\"%1\": part %2 not checked under the root (error %3)")
                     .arg(f.name).arg(part).arg(errorName(verdict.error())));
        return;
    }
    if (*verdict == ED3K_PART_CLAIM_FALSE) {
        // §14.4: the pair is false; nobody blamed (ed3k writes carry no sender)
        dropRoot(f, true, QStringLiteral("eD2K part %1 failed, its blocks pass under the root").arg(part));
        takeNextCandidate(f);
    }
    // BLOCKS_FAILED: bad eD2K bytes, eD2K's corruption handling stands; UNKNOWN: no verdict
}

std::optional<ffi::Root> Ed3kEd2kJoin::rootOf(const QByteArray& md4) const
{
    const auto it = m_files.find(md4);
    return it == m_files.end() ? std::nullopt : it->second.root;
}

Ed3kEd2kJoin::Pair Ed3kEd2kJoin::pairOf(const QByteArray& md4) const
{
    const auto it = m_files.find(md4);
    return it == m_files.end() ? Pair::Unknown : it->second.pair;
}

std::uint64_t Ed3kEd2kJoin::ed3kBytes(const QByteArray& md4) const
{
    const auto it = m_files.find(md4);
    return it == m_files.end() ? 0 : it->second.bytes;
}

std::optional<Ed3kEd2kJoin::FileInfo> Ed3kEd2kJoin::infoOf(const QByteArray& md4) const
{
    const auto it = m_files.find(md4);
    if (it == m_files.end())
        return std::nullopt;
    const File& f = it->second;
    FileInfo i{.root = f.root, .pair = f.pair, .searching = f.searching && !f.root, .bytes = f.bytes};
    if (f.root) {
        if (const Download* d = downloadOf(*f.root)) {
            i.sources = int(d->streams.size());
            i.transferring = int(std::ranges::count_if(d->streams, [](const auto& s) { return s.second; }));
        }
    }
    return i;
}

void Ed3kEd2kJoin::sync()
{
    if (!m_started)
        return;
    std::set<QByteArray> live;
    for (PartFile* pf : m_queue.files()) {
        if (!eligible(pf))
            continue;
        if (File* f = ensureFile(*pf))
            live.insert(f->md4);
    }
    std::vector<QByteArray> gone;
    for (const auto& [md4, f] : m_files)
        if (!live.contains(md4))
            gone.push_back(md4);
    for (const auto& md4 : gone)
        forget(md4);

    const qint64 now = nowMs();
    const auto running = std::ranges::count_if(m_files, [](const auto& f) { return f.second.searching; });
    auto budget = kMaxSearches - int(running);
    for (auto& [md4, f] : m_files) {
        if (budget > 0 && !f.root && !f.searching && now >= f.nextSearch) {
            startSearch(f);
            if (f.searching)
                --budget;
        }
        // an unanswered hint: eD2K goes on (§14.2 MUST fall back)
        for (auto& h : f.hints)
            if (!h.told && !h.answered && now >= h.deadline)
                releaseHint(h, Ed3kRoute::Untried);
        std::erase_if(f.hints, [](const Hint& h) { return h.told && !h.answered && h.stream == 0; });
        if (f.shareDirty)
            shareVerified(f);
    }
    pumpSession();
}

// ---------------------------------------------------------------------------
// Ed3kJoinBase
// ---------------------------------------------------------------------------

std::vector<std::uint64_t> Ed3kEd2kJoin::wantedBlocks(const Download& d) const
{
    std::vector<std::uint64_t> out;
    const File* f = fileOfOwner(d.owner);
    const PartFile* pf = f ? partFile(f->md4) : nullptr;
    if (!pf || !eligible(pf))
        return out;
    // blocks over the gaps, rarest work first: parts closest to done (verified sooner)
    std::map<std::uint64_t, std::vector<std::uint64_t>> byPartLeft;
    std::set<std::uint64_t> seen;   // gaps sharing a block
    for (const Gap& g : pf->gapList()) {
        for (std::uint64_t off = g.start - (g.start % kBlock); off <= g.end && off < d.size; off += kBlock) {
            if (!seen.insert(off).second)
                continue;
            const std::uint64_t end = blockEnd(*f, off);
            // eD2K asks for it now: not twice
            if (pf->isAlreadyRequested(off, end))
                continue;
            const auto part = std::uint32_t(off / PARTSIZE);
            byPartLeft[pf->totalGapSizeInPart(part)].push_back(off);
        }
    }
    for (const auto& [left, offs] : byPartLeft)
        for (const auto off : offs) {
            out.push_back(off);
            if (out.size() >= kMaxWanted)
                return out;
        }
    return out;
}

void Ed3kEd2kJoin::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    File* f = fileOfOwner(d.owner);
    PartFile* pf = f ? partFile(f->md4) : nullptr;
    if (!pf || b.data.empty() || b.offset >= f->size)
        return;
    const std::uint64_t start = b.offset;
    const std::uint64_t end = std::min<std::uint64_t>(start + b.data.size(), f->size) - 1;
    // only the missing bytes: never over what eD2K wrote or verified (writeToBuffer refuses
    // ranges touching a complete part)
    std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
    for (const Gap& g : pf->gapList())
        if (g.end >= start && g.start <= end)
            spans.emplace_back(std::max(g.start, start), std::min(g.end, end));
    std::uint64_t written = 0;
    const auto* base = reinterpret_cast<const uint8*>(b.data.data());
    for (const auto& [s, e] : spans)
        written += pf->writeToBuffer(e - s + 1, base + (s - start), s, e, nullptr, Address{});
    if (written == 0)
        return;
    f->blocks.insert(start);
    if (written == end - start + 1)
        f->whole.insert(start);
    f->bytes += written;
    f->shareDirty = true;
}

void Ed3kEd2kJoin::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    Q_UNUSED(owner) Q_UNUSED(member) Q_UNUSED(root) Q_UNUSED(data)
}

void Ed3kEd2kJoin::onDownloadHashes(const Download& d, const ffi::Bytes& blob)
{
    if (File* f = fileOfOwner(d.owner)) {
        f->hashes = blob;
        f->shareDirty = true;
    }
}

std::optional<Ed3kJoinBase::FileRead> Ed3kEd2kJoin::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    const File* f = fileOfOwner(s.owner);
    const PartFile* pf = f ? partFile(f->md4) : nullptr;
    if (!pf || offset >= f->size)
        return std::nullopt;
    for (std::uint64_t off = offset - (offset % kBlock); off < offset + len; off += kBlock)
        if (!blockServable(*f, *pf, off))
            return std::nullopt;
    return FileRead{pf->dataFilePath(), offset, len};
}

void Ed3kEd2kJoin::onOtherEvent(const ffi::Event& event)
{
    if (const auto* af = std::get_if<ffi::event::AliasFound>(&event))
        onAliasFound(*af);
}

void Ed3kEd2kJoin::onStreamOpened(const ffi::Root& root, std::uint64_t stream, const ffi::Source& source)
{
    if (!source.hint)
        return;
    if (File* f = fileOfRoot(root))
        for (auto& h : f->hints)
            if (h.source.id == source.id)
                h.stream = stream;
}

void Ed3kEd2kJoin::onStreamStatus(const ffi::event::FileStatus& status)
{
    File* f = fileOfRoot(status.root);
    if (!f)
        return;
    // §14.4: the uploader pairs this root with another eD2K file: the pair is false
    if (status.ed2k && *status.ed2k != hashOf(f->md4)) {
        dropRoot(*f, true, QStringLiteral("a source names another ed2k hash for the root"));
        takeNextCandidate(*f);
        return;
    }
    for (auto& h : f->hints) {
        // its own stream, or any stream to that node (a Kad source of the same peer)
        const bool sameNode = std::equal(h.source.id.bytes.begin(), h.source.id.bytes.begin() + 12, status.node.bytes.begin());
        if (h.stream != status.stream && !(sameNode && status.usable && !h.answered))
            continue;
        if (!status.usable) {
            releaseHint(h, Ed3kRoute::Failed);
            continue;
        }
        // reached over ed3k (its node ID starts with the hint, the engine checked it):
        // used over ed3k only (§14.4)
        h.answered = true;
        h.told = true;
        if (h.client) {
            logDebug(QStringLiteral("eD2K source %1 answered over ed3k").arg(h.client->userName()));
            h.client->setEd3kRoute(Ed3kRoute::Preferred);
        }
    }
}

void Ed3kEd2kJoin::onStreamClosed(const ffi::event::StreamClosed& closed)
{
    File* f = fileOfRoot(closed.root);
    if (!f)
        return;
    for (auto& h : f->hints) {
        if (h.stream != closed.stream)
            continue;
        h.stream = 0;
        // unreachable, refused, WRONG_PEER, not found: eD2K from now on for this client;
        // a lost / replaced stream: ed3k is asked again at the next re-ask
        const bool gone = closed.why == ffi::Closed::Lost || closed.why == ffi::Closed::Replaced
                       || closed.why == ffi::Closed::Local || closed.why == ffi::Closed::Paused;
        h.told = false;
        releaseHint(h, gone && h.answered ? Ed3kRoute::Untried : Ed3kRoute::Failed);
        h.answered = false;
    }
    std::erase_if(f->hints, [](const Hint& h) { return h.told && h.stream == 0; });
}

bool Ed3kEd2kJoin::hasOwner(const QString& owner) const
{
    return fileOfOwner(owner) != nullptr;
}

void Ed3kEd2kJoin::saveOwner(const QString& owner)
{
    Q_UNUSED(owner)   // the part file is the state; roots are looked up again
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kEd2kJoin::onTick()
{
    sync();
}

PartFile* Ed3kEd2kJoin::partFile(const QByteArray& md4) const
{
    if (md4.size() != 16)
        return nullptr;
    return m_queue.fileByID(reinterpret_cast<const uint8*>(md4.constData()));
}

bool Ed3kEd2kJoin::eligible(const PartFile* f)
{
    if (!f || f->isStopped() || f->isPaused() || uint64(f->fileSize()) == 0)
        return false;
    const auto st = f->status();
    return st != PartFileStatus::Complete && st != PartFileStatus::Completing && st != PartFileStatus::Error;
}

Ed3kEd2kJoin::File* Ed3kEd2kJoin::fileOfOwner(const QString& owner)
{
    const auto it = m_files.find(QByteArray::fromHex(owner.toLatin1()));
    return it == m_files.end() ? nullptr : &it->second;
}

const Ed3kEd2kJoin::File* Ed3kEd2kJoin::fileOfOwner(const QString& owner) const
{
    const auto it = m_files.find(QByteArray::fromHex(owner.toLatin1()));
    return it == m_files.end() ? nullptr : &it->second;
}

Ed3kEd2kJoin::File* Ed3kEd2kJoin::fileOfRoot(const ffi::Root& root)
{
    for (auto& [md4, f] : m_files)
        if (f.root && *f.root == root)
            return &f;
    return nullptr;
}

Ed3kEd2kJoin::File* Ed3kEd2kJoin::ensureFile(PartFile& pf)
{
    if (!m_started || !eligible(&pf))
        return nullptr;
    const QByteArray md4(reinterpret_cast<const char*>(pf.fileHash()), 16);
    auto [it, added] = m_files.try_emplace(md4);
    File& f = it->second;
    if (added) {
        f.md4 = md4;
        f.size = uint64(pf.fileSize());
        f.name = pf.fileName();
    }
    return &f;
}

void Ed3kEd2kJoin::forget(const QByteArray& md4)
{
    const auto it = m_files.find(md4);
    if (it == m_files.end())
        return;
    File& f = it->second;
    releaseHints(f);
    if (f.bytes > 0)
        logInfo(QStringLiteral("\"%1\": %2 bytes came over ed3k").arg(f.name).arg(f.bytes));
    if (f.root) {
        stopDownload(*f.root);
        if (isShared(*f.root))
            unshare(*f.root);
    }
    if (f.search) {
        if (auto* e = m_session.engine())
            (void)e->aliasSearchEnd(*f.search);
        m_searches.erase(*f.search);
    }
    m_files.erase(it);
}

void Ed3kEd2kJoin::startSearch(File& f)
{
    auto* e = m_session.engine();
    if (!e || !m_searchEnabled || f.searching || f.root)
        return;
    if (f.search) {
        (void)e->aliasSearchEnd(*f.search);
        m_searches.erase(*f.search);
        f.search.reset();
    }
    const auto id = ed2kAliasId(f.md4, f.size);
    auto search = e->aliasSearchStart(m_session.now(), "ed2k", id, ED3K_REL_SAME, f.size);
    f.nextSearch = nowMs() + m_searchGapMs;
    if (!search) {
        logDebug(QStringLiteral("\"%1\": ed2k alias search refused (error %2)").arg(f.name, errorName(search.error())));
        return;
    }
    f.search = *search;
    f.searching = true;
    m_searches[*search] = f.md4;
    pumpSession();
}

void Ed3kEd2kJoin::onAliasFound(const ffi::event::AliasFound& found)
{
    const auto s = m_searches.find(found.search);
    if (s == m_searches.end())
        return;
    const auto it = m_files.find(s->second);
    if (it == m_files.end()) {
        m_searches.erase(s);
        return;
    }
    File& f = it->second;
    f.searching = false;
    // a later AliasFound of the search in use: its candidates wait for a false pair
    if (f.root)
        return;
    logDebug(QStringLiteral("\"%1\": %2 ed3k root(s) claimed for its ed2k hash").arg(f.name).arg(found.roots));
    takeNextCandidate(f);
}

void Ed3kEd2kJoin::takeNextCandidate(File& f)
{
    auto* e = m_session.engine();
    if (f.root)
        return;
    if (e && f.search) {
        while (true) {
            auto c = e->aliasSearchNext(*f.search);
            if (!c || !*c)
                break;
            const auto& cand = **c;
            if (cand.size != f.size || f.refused.contains(cand.root))
                continue;
            if (m_rootTaken && m_rootTaken(cand.root)) {
                logDebug(QStringLiteral("\"%1\": root %2 is downloaded over ed3k already").arg(f.name, hexOf(cand.root.bytes)));
                f.refused.insert(cand.root);
                continue;
            }
            f.root = cand.root;
            f.pair = Pair::Unknown;
            openRoot(f);
            return;
        }
    }
    // nothing (left): eD2K alone until the next search
    if (f.search) {
        if (e)
            (void)e->aliasSearchEnd(*f.search);
        m_searches.erase(*f.search);
        f.search.reset();
    }
    f.searching = false;
    f.nextSearch = nowMs() + m_searchGapMs;
    releaseHints(f);
}

void Ed3kEd2kJoin::openRoot(File& f)
{
    if (!f.root)
        return;
    logInfo(QStringLiteral("\"%1\": also downloading over ed3k (root %2)").arg(f.name, hexOf(f.root->bytes)));
    if (!openDownload(*f.root, f.size, ownerOf(f.md4), 0) && !downloading(*f.root)) {
        f.refused.insert(*f.root);
        f.root.reset();
        takeNextCandidate(f);
        return;
    }
    for (auto& h : f.hints)
        if (!h.told)
            addSource(*f.root, h.source);
    pumpSession();
}

void Ed3kEd2kJoin::dropRoot(File& f, bool falsePair, const QString& why)
{
    if (!f.root)
        return;
    const ffi::Root root = *f.root;
    logWarning(QStringLiteral("\"%1\": ed3k root %2 dropped: %3").arg(f.name, hexOf(root.bytes), why));
    stopDownload(root);
    if (isShared(root))
        unshare(root);
    if (falsePair) {
        f.refused.insert(root);
        setPair(f, Pair::False);
    }
    f.root.reset();
    f.blocks.clear();
    f.whole.clear();
    f.hashes.clear();
    releaseHints(f);
}

void Ed3kEd2kJoin::setPair(File& f, Pair pair)
{
    if (f.pair == pair)
        return;
    f.pair = pair;
    f.shareDirty = true;
    emit pairChanged(f.md4, pair);
}

void Ed3kEd2kJoin::releaseHint(Hint& h, Ed3kRoute route)
{
    if (h.told)
        return;
    h.told = true;
    if (h.client && h.client->ed3kRoute() != Ed3kRoute::Failed)
        h.client->setEd3kRoute(route);
}

void Ed3kEd2kJoin::releaseHints(File& f)
{
    for (auto& h : f.hints) {
        // Preferred ones go back to eD2K as well (no ed3k for them now)
        if (h.answered) {
            h.told = false;
            h.answered = false;
        }
        releaseHint(h, Ed3kRoute::Untried);
    }
    f.hints.clear();
}

void Ed3kEd2kJoin::shareVerified(File& f)
{
    f.shareDirty = false;
    const PartFile* pf = partFile(f.md4);
    // values needed to serve more than one block (§7.2)
    if (!m_started || !f.root || !pf || (f.hashes.empty() && f.size > kBlock))
        return;
    constexpr std::uint64_t kPiece = 16 * kBlock;
    const auto pieces = std::size_t((f.size + kPiece - 1) / kPiece);
    std::vector<std::uint8_t> bits((pieces + 7) / 8, 0);
    bool any = false;
    for (std::size_t p = 0; p < pieces; ++p) {
        bool held = true;
        for (std::uint64_t off = p * kPiece; held && off < std::min(f.size, (p + 1) * kPiece); off += kBlock)
            held = blockServable(f, *pf, off);
        if (held) {
            bits[p / 8] = std::uint8_t(bits[p / 8] | (1u << (p % 8)));
            any = true;
        }
    }
    if (!any)
        return;
    const QByteArray n = f.name.toUtf8();
    (void)shareMember(ffi::SharedFile{*f.root, f.size, std::string(n.constData(), std::size_t(n.size())), hashOf(f.md4),
                                      std::nullopt, ffi::Have::pieces(std::move(bits)), std::span{f.hashes}},
                      ownerOf(f.md4), 0);
}

bool Ed3kEd2kJoin::blockServable(const File& f, const PartFile& pf, std::uint64_t off) const
{
    const std::uint64_t end = blockEnd(f, off);
    if (!pf.isComplete(off, end) || !pf.isCompleteBDSafe(off, end))
        return false;
    // verified under BLAKE3 when it came whole from ed3k
    if (f.whole.contains(off))
        return true;
    // eD2K bytes: only once the pair is trusted and the parts passed MD4
    if (f.pair != Pair::Trusted)
        return false;
    return pf.isComplete(std::uint32_t(off / PARTSIZE)) && pf.isComplete(std::uint32_t(end / PARTSIZE));
}

std::uint64_t Ed3kEd2kJoin::blockEnd(const File& f, std::uint64_t off) const
{
    return std::min<std::uint64_t>(off + kBlock, f.size) - 1;
}

} // namespace eMule::ed3k
