/// @file Ed3kDownloadsWeb.cpp
/// @brief Plain ed3k downloads — HTTP sources of a link's `ws=` (http-source.md §2, §3, §5).
///
/// packmule checks and verifies; this side picks what to fetch (host-io.md "HTTP sources in
/// short"): one fetch per download, the first missing blocks (16 at most). Without block
/// values the engine refuses a fetch not at 0 (ERR_STATE): the file is then read whole, in
/// order, as HttpUnverified (written, not marked held) and checked by HttpWhole at the end.
/// How far it got is kept in the .met: after a restart the bytes on disk are hashed again
/// (Hasher::checkpoint) and the read goes on there (httpWholeResume). Block values that come
/// meanwhile (a peer, event::Hashes) check those bytes at once on a worker (webCheckPrefix):
/// matching blocks are held, the rest fetched block by block. `wh=` URLs go to the
/// engine as the source's hash body (§3.1); it reads them when it wants block values.

#include "Ed3kDownloads.h"

#include "Ed3kSession.h"
#include "utils/ArgStr.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace {

namespace ev = ::ed3k::event;

/// blocks per fetch: packmule's FETCH_MAX (4 MiB)
constexpr std::uint64_t kFetchBlocks = 16;
constexpr qsizetype kMaxWebUrls = 4;
constexpr int kBusyRetryMs = 1000;
constexpr int kFailRetryMs = 5000;
constexpr int kFailRetryMaxMs = 300'000;

QString codeName(std::uint32_t code)
{
    switch (code) {
    case ED3K_HTTP_OK: return QStringLiteral("ok");
    case ED3K_HTTP_DISABLED: return QStringLiteral("disabled");
    case ED3K_HTTP_BUSY: return QStringLiteral("busy");
    case ED3K_HTTP_NOT_WANTED: return QStringLiteral("not wanted");
    case ED3K_HTTP_BAD_SOURCE: return QStringLiteral("bad source");
    case ED3K_HTTP_HTTP_FAILED: return QStringLiteral("HTTP failed");
    case ED3K_HTTP_SIZE_MISMATCH: return QStringLiteral("size mismatch");
    case ED3K_HTTP_CORRUPT: return QStringLiteral("corrupt");
    default: return QStringLiteral("code %1").arg(code);
    }
}

} // namespace

std::optional<Ed3kDownloads::WebSourceInfo> Ed3kDownloads::webSourceOf(const ffi::Root& root) const
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end())
        return std::nullopt;
    const Entry& e = it->second;
    if (e.web.source == 0 || e.info.state != Ed3kDownloadInfo::State::Downloading || e.web.urlIndex >= e.webUrls.size())
        return std::nullopt;
    return WebSourceInfo{.url = e.webUrls.at(e.web.urlIndex),
                         .fetching = e.web.fetch.has_value(),
                         .at = e.web.whole ? e.web.wholeNext : e.web.fetchAt,
                         .have = e.have,
                         .unchecked = std::min(e.info.size, e.wholeAt)};
}

void Ed3kDownloads::onOtherEvent(const ::ed3k::Event& event)
{
    if (const auto* b = std::get_if<ev::HttpBlock>(&event)) {
        if (Entry* e = entryOfWebSource(b->source); e && e->info.root == b->root) {
            e->web.whole = false;   // block values are here now
            // was: e->wholeAt = 0; the unchecked bytes before stay until the values check them
            (void)storeBlock(*e, b->offset, b->data);
            webCheckPrefix(*e);
        }
    } else if (const auto* u = std::get_if<ev::HttpUnverified>(&event)) {
        if (Entry* e = entryOfWebSource(u->source); e && e->info.root == u->root)
            webUnverified(*e, u->offset, u->data);
    } else if (const auto* w = std::get_if<ev::HttpWhole>(&event)) {
        if (Entry* e = entryOfWebSource(w->source); e && e->info.root == w->root)
            webWhole(*e, w->code == ED3K_HTTP_OK);
    } else if (const auto* f = std::get_if<ev::HttpFetched>(&event)) {
        if (Entry* e = entryOfWebSource(f->source); e && e->web.fetch == f->fetch)
            webFetched(*e, f->code);
    } else if (const auto* d = std::get_if<ev::HttpDropped>(&event)) {
        if (Entry* e = entryOfWebSource(d->source); e && e->info.root == d->root)
            webDropped(*e, d->code);
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool Ed3kDownloads::mergeWebUrls(QStringList& list, const QStringList& urls)
{
    bool added = false;
    for (const QString& raw : urls) {
        const QString url = raw.trimmed();
        // the engine checks the rest (§5); a broken one is not kept
        const QUrl u(url, QUrl::StrictMode);
        const bool http = u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https");
        if (url.isEmpty() || !u.isValid() || !http || u.host().isEmpty() || list.contains(url) || list.size() >= kMaxWebUrls)
            continue;
        list << url;
        added = true;
    }
    return added;
}

void Ed3kDownloads::webAttach(Entry& e)
{
    if (e.web.source != 0 || e.webUrls.isEmpty() || e.info.state != Ed3kDownloadInfo::State::Downloading || !m_started)
        return;
    auto* eng = m_session.engine();
    if (!eng || !downloading(e.info.root))
        return;
    // hops.md §1: a GET from this address would tie it to the anonymous download; only
    // through the anonymous proxy (Ed3kHttp routes it, never direct)
    const bool anon = keyOf(e.info.root) == ffi::KeyKind::Anon;
    if (anon && !anonWebAllowed()) {
        if (!std::exchange(e.web.loggedAnon, true))
            logInfo(QStringLiteral("\"%1\": HTTP sources not used under the anonymous key").arg(e.info.name));
        return;
    }
    // its fetcher is the anonymous node's: none before that runs (webDrive tries again)
    if (anon && !m_session.hide().hiding())
        return;
    e.web.loggedAnon = false;
    while (e.web.urlIndex < e.webUrls.size()) {
        const QString& url = e.webUrls.at(e.web.urlIndex);
        const QByteArray u8 = url.toUtf8();
        ffi::HttpSource src;
        src.url = std::string(u8.constData(), std::size_t(u8.size()));
        // §3.1: the hash body belongs to the file, any source may name it
        if (!e.webHashUrls.isEmpty()) {
            const QByteArray h8 = e.webHashUrls.constFirst().toUtf8();
            src.hashes = std::string(h8.constData(), std::size_t(h8.size()));
        }
        // http-source.md §3.2: the engine sends its requests only through the anonymous proxy
        const auto id = anon ? eng->httpAddAnonSource(m_session.now(), e.info.root, e.info.size, src)
                             : eng->httpAddUserSource(m_session.now(), e.info.root, e.info.size, src);
        if (id) {
            e.web.source = *id;
            e.web.fetch.reset();
            e.web.whole = false;
            e.web.wholeNext = 0;
            e.web.failures = 0;
            e.web.retryAt = QDeadlineTimer();
            logInfo(QStringLiteral("\"%1\": HTTP source %2%3%4")
                        .arg(e.info.name, url, src.hashes.empty() ? QString() : QStringLiteral(" (with hash body)"),
                             anon ? QStringLiteral(" through the anonymous proxy") : QString()));
            if (e.wholeAt >= kBlock)
                webResume(e);
            QTimer::singleShot(0, this, [this] { webDrive(); });
            return;
        }
        logWarning(QStringLiteral("\"%1\": HTTP source %2 refused (%3)").arg(e.info.name, url, errorName(id.error())));
        ++e.web.urlIndex;
    }
}

void Ed3kDownloads::webDetach(Entry& e)
{
    if (auto* eng = m_session.engine()) {
        if (e.web.fetch)
            (void)eng->httpCancel(m_session.now(), *e.web.fetch);
        if (e.web.source != 0)
            (void)eng->httpDecline(m_session.now(), e.web.source, ED3K_HTTP_NOT_WANTED);
    }
    const int urlIndex = e.web.urlIndex;
    const bool loggedAnon = e.web.loggedAnon;
    e.web = Web{};
    e.web.urlIndex = urlIndex;
    e.web.loggedAnon = loggedAnon;
}

bool Ed3kDownloads::anonWebAllowed() const
{
    const Ed3kHideSettings& s = m_session.hide().settings();
    return s.anonHttp && s.reportProxy.has_value();
}

void Ed3kDownloads::webDrive()
{
    if (!m_started || !m_session.engine())
        return;
    bool fetched = false;
    // the anonymous proxy or its switch went away: those sources stop (rows show them dropped)
    const bool anonAllowed = anonWebAllowed();
    if (anonAllowed != m_anonWebAllowed) {
        m_anonWebAllowed = anonAllowed;
        markChanged();
    }
    for (auto& [root, e] : m_entries) {
        if (e.info.state != Ed3kDownloadInfo::State::Downloading || e.moving)
            continue;
        if (e.web.source != 0 && !anonAllowed && keyOf(root) == ffi::KeyKind::Anon) {
            logInfo(QStringLiteral("\"%1\": HTTP source dropped, no anonymous proxy").arg(e.info.name));
            webDetach(e);
            continue;
        }
        if (e.web.source == 0) {
            webAttach(e);   // a dropped one: the next URL
            continue;
        }
        if (e.web.fetch || e.web.resuming || !e.web.retryAt.hasExpired())
            continue;
        webFetch(e);
        fetched = fetched || e.web.fetch.has_value();
    }
    if (fetched)
        pumpSession();
}

void Ed3kDownloads::webFetch(Entry& e)
{
    auto* eng = m_session.engine();
    if (!eng)
        return;
    const std::uint64_t size = e.info.size;
    std::uint64_t offset = 0;
    std::uint64_t blocks = kFetchBlocks;
    if (e.web.whole) {
        offset = e.web.wholeNext;
    } else {
        // a player's position first (playFrom), else the first missing block; unchecked bytes
        // of a whole read wait for their check (webCheckPrefix), the blocks after go first
        const std::size_t from = e.focus && !e.focusUntil.hasExpired() ? std::size_t(*e.focus / kBlock)
                                                                       : std::size_t(std::min(e.wholeAt, e.info.size) / kBlock);
        auto first = std::find(e.have.begin() + std::ptrdiff_t(std::min(from, e.have.size())), e.have.end(), false);
        if (first == e.have.end())
            first = std::find(e.have.begin(), e.have.end(), false);
        if (first == e.have.end())
            return;
        const auto index = std::size_t(first - e.have.begin());
        blocks = 0;
        while (blocks < kFetchBlocks && index + blocks < e.have.size() && !e.have[index + blocks])
            ++blocks;
        offset = std::uint64_t(index) * kBlock;
    }
    if (offset >= size)
        return;
    const std::uint64_t len = std::min(blocks * kBlock, size - offset);
    const auto fetch = eng->httpFetch(m_session.now(), e.web.source, offset, len);
    if (fetch) {
        e.web.fetch = *fetch;
        e.web.fetchAt = offset;
        return;
    }
    logEd3kDebug([&]() -> QString { return QStringLiteral("\"%1\": HTTP fetch at %2: %3").arg(argStr(e.info.name), argStr(offset), argStr(errorName(fetch.error()))); });
    switch (fetch.error()) {
    case ffi::Error::State:
        // no block values: the whole file in order from 0 (§3 "Verification")
        if (!e.web.whole || e.web.wholeNext != 0) {
            logDebug([&]() -> QString { return QStringLiteral("\"%1\": HTTP source read whole, checked at the end").arg(e.info.name); });
            e.web.whole = true;
            e.web.wholeNext = 0;
            webFetch(e);
            return;
        }
        e.web.retryAt.setRemainingTime(kFailRetryMs);
        break;
    case ffi::Error::Busy:
        e.web.retryAt.setRemainingTime(kBusyRetryMs);
        break;
    case ffi::Error::Stale:
        e.web = Web{.urlIndex = e.web.urlIndex, .loggedAnon = e.web.loggedAnon};   // gone at the engine: added again
        break;
    case ffi::Error::Denied:
        break;   // HttpFetched says why
    default:
        logWarning(QStringLiteral("\"%1\": HTTP fetch at %2 refused (%3)").arg(argStr(e.info.name), argStr(offset), argStr(errorName(fetch.error()))));
        e.web.retryAt.setRemainingTime(kFailRetryMs);
        break;
    }
}

void Ed3kDownloads::webFetched(Entry& e, std::uint32_t code)
{
    e.web.fetch.reset();
    switch (code) {
    case ED3K_HTTP_OK:
    case ED3K_HTTP_NOT_WANTED:
        e.web.failures = 0;
        e.web.retryAt = QDeadlineTimer();
        // the next one at once, not at the next tick
        QTimer::singleShot(0, this, [this] { webDrive(); });
        return;
    case ED3K_HTTP_BUSY:
        e.web.retryAt.setRemainingTime(kBusyRetryMs);
        return;
    case ED3K_HTTP_DISABLED:
        logInfo(QStringLiteral("\"%1\": downloads over HTTP are off").arg(e.info.name));
        e.web.urlIndex = int(e.webUrls.size());
        webDetach(e);
        return;
    case ED3K_HTTP_BAD_SOURCE:
    case ED3K_HTTP_SIZE_MISMATCH:
    case ED3K_HTTP_CORRUPT:
        webDropped(e, code);
        return;
    default:
        break;
    }
    // transport: again later, backing off
    ++e.web.failures;
    const int wait = std::min(kFailRetryMaxMs, kFailRetryMs << std::min(e.web.failures - 1, 6));
    if (e.web.failures == 1 || e.web.failures % 5 == 0)
        logWarning(QStringLiteral("\"%1\": HTTP fetch failed (%2), %3 in a row; again in %4 s")
                       .arg(argStr(e.info.name), argStr(codeName(code)), argStr(e.web.failures), argStr(wait / 1000)));
    e.web.retryAt.setRemainingTime(wait);
}

void Ed3kDownloads::webUnverified(Entry& e, std::uint64_t offset, const ffi::Bytes& data)
{
    if (e.info.state != Ed3kDownloadInfo::State::Downloading || data.empty())
        return;
    const QString path = e.info.partPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) {
        fail(e, QStringLiteral("cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    // verified blocks stay: bytes of this source are not trusted before HttpWhole
    const std::uint64_t end = offset + std::uint64_t(data.size());
    for (std::uint64_t at = offset; at < end;) {
        const std::uint64_t index = at / kBlock;
        const std::uint64_t next = std::min(end, (index + 1) * kBlock);
        if (index < e.have.size() && !e.have[std::size_t(index)]) {
            const auto* p = reinterpret_cast<const char*>(data.data()) + (at - offset);
            if (!f.seek(qint64(at)) || f.write(p, qint64(next - at)) != qint64(next - at)) {
                fail(e, QStringLiteral("cannot write %1: %2").arg(path, f.errorString()));
                return;
            }
        }
        at = next;
    }
    e.web.whole = true;
    e.web.wholeNext = end;
    e.wholeAt = std::max(e.wholeAt, end);
    e.metDirty = true;   // how far it got: the next save
    e.received += std::uint64_t(data.size());
    e.info.transferred += std::uint64_t(data.size());
    markChanged();
}

void Ed3kDownloads::webWhole(Entry& e, bool ok)
{
    if (!ok && e.web.resumed) {
        // the bytes of the earlier run, not the source, may be wrong: once more from 0
        logWarning(QStringLiteral("\"%1\": resumed HTTP read does not give the root: read again from the start").arg(e.info.name));
        // the engine keeps the source (no HttpDropped) and takes a fetch at 0 next
        e.wholeAt = 0;
        e.metDirty = true;
        e.web.whole = true;
        e.web.wholeNext = 0;
        e.web.resumed = false;
        QTimer::singleShot(0, this, [this] { webDrive(); });
        return;
    }
    if (!ok) {
        logWarning(QStringLiteral("\"%1\": the file of the HTTP source is not this root").arg(e.info.name));
        webDropped(e, ED3K_HTTP_CORRUPT);
        return;
    }
    if (e.info.state != Ed3kDownloadInfo::State::Downloading)
        return;
    e.wholeAt = 0;
    logInfo(QStringLiteral("\"%1\": read whole from its HTTP source, root matches").arg(e.info.name));
    std::fill(e.have.begin(), e.have.end(), true);   // not ranges: GCC 12 rejects vector<bool>
    e.info.done = e.info.size;
    emit progress(e.info.root, e.info.done);
    touch(e);
    markChanged();
    verify(e);   // hashed once more, then moved
}

void Ed3kDownloads::webDropped(Entry& e, std::uint32_t code)
{
    if (e.web.urlIndex < e.webUrls.size())
        logWarning(QStringLiteral("\"%1\": HTTP source %2 dropped (%3)").arg(e.info.name, e.webUrls.at(e.web.urlIndex), codeName(code)));
    if (auto* eng = m_session.engine(); eng && e.web.fetch)
        (void)eng->httpCancel(m_session.now(), *e.web.fetch);
    e.web = Web{.urlIndex = e.web.urlIndex + 1, .loggedAnon = e.web.loggedAnon};   // the next URL, if any (webDrive)
    // its bytes are not trusted: another source reads whole from 0
    if (std::exchange(e.wholeAt, 0) > 0)
        e.metDirty = true;
}

void Ed3kDownloads::webResume(Entry& e)
{
    const qint64 onDisk = QFileInfo(e.info.partPath).size();
    const std::uint64_t upTo = std::min(e.wholeAt, std::uint64_t(std::max<qint64>(0, onDisk)));
    if (upTo < kBlock) {
        e.wholeAt = 0;
        return;
    }
    e.web.resuming = true;
    const ffi::Root root = e.info.root;
    const std::uint64_t source = e.web.source;
    checkpointInBackground(e.info.partPath, upTo, [this, root, source](std::optional<HashCheckpoint> cp) {
        webResumed(root, source, cp);
    });
}

void Ed3kDownloads::webResumed(const ffi::Root& root, std::uint64_t source, const std::optional<HashCheckpoint>& cp)
{
    Entry* e = entryOf(root);
    if (!e || e->web.source != source || !e->web.resuming)
        return;   // detached meanwhile
    e->web.resuming = false;
    if (!e->hashes.empty()) {
        // the values came meanwhile: webCheckPrefix() checks the bytes, no whole read
        e->web.whole = false;
        QTimer::singleShot(0, this, [this] { webDrive(); });
        return;
    }
    auto* eng = m_session.engine();
    std::expected<std::uint64_t, ffi::Error> at = std::unexpected(ffi::Error::NotFound);
    if (cp && eng)
        at = eng->httpWholeResume(source, std::span{reinterpret_cast<const std::byte*>(cp->blob.constData()), std::size_t(cp->blob.size())});
    if (at) {
        e->web.whole = true;
        e->web.wholeNext = *at;
        e->web.resumed = true;
        logInfo(QStringLiteral("\"%1\": HTTP read goes on at %2 of %3 B").arg(argStr(e->info.name), argStr(*at), argStr(e->info.size)));
    } else {
        logWarning(QStringLiteral("\"%1\": HTTP read cannot go on (%2): from the start")
                       .arg(e->info.name, cp ? errorName(at.error()) : QStringLiteral("part file unreadable")));
        e->wholeAt = 0;
        e->metDirty = true;
    }
    markChanged();
    QTimer::singleShot(0, this, [this] { webDrive(); });
}

void Ed3kDownloads::webCheckPrefix(Entry& e)
{
    // a file of one block has no values: its whole read ends with the root
    if (e.hashes.empty() || e.have.size() < 2 || e.wholeAt < kBlock || e.prefixChecking
        || e.info.state != Ed3kDownloadInfo::State::Downloading)
        return;
    const qint64 onDisk = QFileInfo(e.info.partPath).size();
    const std::uint64_t upTo = std::min({e.wholeAt, e.info.size, std::uint64_t(std::max<qint64>(0, onDisk))});
    if (upTo < kBlock) {
        e.wholeAt = 0;
        e.metDirty = true;
        return;
    }
    e.prefixChecking = true;
    const ffi::Root root = e.info.root;
    checkpointInBackground(e.info.partPath, upTo, [this, root, upTo](std::optional<HashCheckpoint> cp) {
        webPrefixChecked(root, upTo, cp);
    });
}

void Ed3kDownloads::webPrefixChecked(const ffi::Root& root, std::uint64_t upTo, const std::optional<HashCheckpoint>& cp)
{
    Entry* e = entryOf(root);
    if (!e || !std::exchange(e->prefixChecking, false) || e->info.state != Ed3kDownloadInfo::State::Downloading)
        return;
    // hash blob: magic(4) version(1) size(8) root(32), then 32 B per block;
    // checkpoint: magic(4) version(1) offset(8), then 32 B per whole block
    constexpr qsizetype kHashHead = 45;
    constexpr qsizetype kCpHead = 13;
    constexpr qsizetype kCv = 32;
    const auto values = std::span{e->hashes};
    const std::size_t known = values.size() > std::size_t(kHashHead) ? (values.size() - kHashHead) / kCv : 0;
    const std::size_t taken = cp && cp->blob.size() > kCpHead ? std::size_t((cp->blob.size() - kCpHead) / kCv) : 0;
    const std::size_t count = std::min({known, taken, e->have.size()});
    std::size_t held = 0;
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (e->have[i])
            continue;
        const auto* mine = cp->blob.constData() + kCpHead + qsizetype(i) * kCv;
        const auto* theirs = values.data() + kHashHead + i * kCv;
        if (std::memcmp(mine, theirs, kCv) != 0) {
            ++wrong;   // fetched again, verified
            continue;
        }
        e->have[i] = true;
        e->info.done = std::min(e->info.size, e->info.done + std::min<std::uint64_t>(kBlock, e->info.size - i * kBlock));
        ++held;
    }
    if (!cp)
        logWarning(QStringLiteral("\"%1\": unchecked HTTP bytes unreadable: fetched again").arg(e->info.name));
    else
        logInfo(QStringLiteral("\"%1\": %2 of %3 unchecked HTTP blocks match the block values%4")
                    .arg(argStr(e->info.name), argStr(held), argStr(count),
                         argStr(wrong > 0 ? QStringLiteral(", %1 fetched again").arg(wrong) : QString())));
    // a whole fetch still running wrote more meanwhile: checked once more
    const bool more = e->wholeAt > upTo;
    if (!more)
        e->wholeAt = 0;
    e->web.whole = false;
    e->metDirty = true;
    emit progress(e->info.root, e->info.done);
    touch(*e);
    markChanged();
    if (std::ranges::all_of(e->have, [](bool h) { return h; })) {
        verify(*e);
        return;
    }
    sharePieces(*e);
    if (more)
        webCheckPrefix(*e);
    QTimer::singleShot(0, this, [this] { webDrive(); });
}

Ed3kDownloads::Entry* Ed3kDownloads::entryOfWebSource(std::uint64_t source)
{
    if (source == 0)
        return nullptr;
    for (auto& [root, e] : m_entries)
        if (e.web.source == source)
            return &e;
    return nullptr;
}

} // namespace eMule::ed3k
