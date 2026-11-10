/// @file Ed3kLiveFiles.cpp
/// @brief Channel files of live streams — implementation.

#include "Ed3kLiveFiles.h"

#include "Ed3kProto.h"
#include "Ed3kSession.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace eMule::ed3k {

namespace {

/// §1.2: "ed3klive" ‖ 0x01
constexpr char kMagic[] = "ed3klive\x01";
constexpr qsizetype kMagicLen = 9;
const QString kFetchOwner = QStringLiteral("live-file");

QString textOf(QByteArrayView v)
{
    return QString::fromUtf8(v.data(), v.size());
}

/// The `Channel` message (§1.2) into @p out; false: not framed.
bool readChannel(QByteArrayView body, Ed3kLiveChannelInfo& out)
{
    Ed3kProtoReader r(body);
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return false;
        if (wire == 2 && field >= 1 && field <= 6) {
            QByteArrayView v;
            if (!r.bytes(v))
                return false;
            switch (field) {
            case 1: out.id = v.toByteArray(); break;
            case 2: out.title = textOf(v); break;
            case 3: out.comment = textOf(v); break;
            case 4: out.lang = textOf(v); break;
            case 5: out.media = textOf(v); break;
            default: out.codecs = textOf(v); break;
            }
        } else if (wire == 0 && (field == 7 || field == 8 || field == 10)) {
            std::uint64_t v = 0;
            if (!r.varint(v))
                return false;
            if (field == 7)
                out.kbps = std::uint32_t(std::min<std::uint64_t>(v, UINT32_MAX));
            else if (field == 8)
                out.segmentMs = std::uint32_t(std::min<std::uint64_t>(v, UINT32_MAX));
            else
                out.created = v;
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    return true;
}

} // namespace

Ed3kLiveFiles::Ed3kLiveFiles(Ed3kSession& session, const QString& configDir, const QString& tag, QObject* parent)
    : Ed3kJoinBase(session, configDir, tag, parent)
{
    if (m_session.isRunning())
        start();
}

Ed3kLiveFiles::~Ed3kLiveFiles()
{
    // the waiting callbacks never run (their owners may be gone)
    m_pending.clear();
    if (m_started)
        closeAll();
}

void Ed3kLiveFiles::start()
{
    if (m_started)
        return;
    beginStart();
    // the engine is new: what was published goes out again (the anonymous key's while hiding)
    for (const auto& [root, p] : m_published)
        if (p.key == ffi::KeyKind::Node || m_session.hide().hiding())
            (void)shareOne(p);
}

void Ed3kLiveFiles::stop()
{
    if (!m_started)
        return;
    closeAll();
    // fetches end with the engine
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (auto& [root, p] : pending)
        for (auto& done : p.waiting)
            done(std::nullopt);
}

bool Ed3kLiveFiles::hasMagic(QByteArrayView file)
{
    return file.size() >= kMagicLen && file.first(kMagicLen) == QByteArrayView(kMagic, kMagicLen);
}

std::optional<Ed3kLiveChannelInfo> Ed3kLiveFiles::parse(QByteArrayView file)
{
    if (!hasMagic(file) || std::uint64_t(file.size()) > kFileMax)
        return std::nullopt;
    // LiveFile {body = 1, key = 2, sigs = 3}
    Ed3kProtoReader r(file.sliced(kMagicLen));
    QByteArrayView body;
    QByteArray key;
    bool hasBody = false;
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (wire == 2 && (field == 1 || field == 2)) {
            QByteArrayView v;
            if (!r.bytes(v))
                return std::nullopt;
            if (field == 1) {
                body = v;
                hasBody = true;
            } else {
                key = v.toByteArray();
            }
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    Ed3kLiveChannelInfo out;
    if (!hasBody || key.size() != 32 || !readChannel(body, out))
        return std::nullopt;
    out.key = key;
    return out;
}

QString Ed3kLiveFiles::fileNameOf(const QString& title)
{
    return safeName(title, QStringLiteral("channel")) + kSuffix;
}

std::optional<ffi::Root> Ed3kLiveFiles::publish(const QByteArray& file, const QString& name, ffi::KeyKind key)
{
    const Published p{file, name, key};
    // hops.md §1: its key recorded before it is opened (keyOf follows it)
    ffi::Hasher h;
    if (file.isEmpty() || !h.update(spanOf(file)))
        return std::nullopt;
    const auto hashed = h.finish();
    if (!hashed)
        return std::nullopt;
    m_session.hide().record(hashed->root, key);
    m_published[hashed->root] = p;
    keep(hashed->root, file);
    // the anonymous key's while hiding is off: shared when it runs (onHideChanged)
    if (key == ffi::KeyKind::Anon && !m_session.hide().hiding())
        return hashed->root;
    // was: const auto root = shareOne(p); if (!root) return std::nullopt;
    if (!shareOne(p)) {
        m_published.erase(hashed->root);
        return std::nullopt;
    }
    return hashed->root;
}

void Ed3kLiveFiles::withdraw(const ffi::Root& root)
{
    if (m_published.erase(root) == 0)
        return;
    // its records withdrawn, not served from here any more
    // was: if (auto* e = m_session.engine()) (void)e->unpublish(m_session.now(), root);
    if (isShared(root))
        unpublishShared(root, ffi::Publish::Both);   // under its key
    unshare(root);
    pumpSession();
}

void Ed3kLiveFiles::fetch(const ffi::Root& root, std::uint64_t size, Fetched done, int timeoutMs)
{
    if (auto file = cached(root)) {
        done(std::move(file));
        return;
    }
    if (size == 0 || size > kFileMax || !m_session.engine()) {
        done(std::nullopt);
        return;
    }
    auto& p = m_pending[root];
    p.waiting.push_back(std::move(done));
    if (p.timeout)
        return;   // fetched already
    p.timeout = std::make_unique<QTimer>();
    p.timeout->setSingleShot(true);
    connect(p.timeout.get(), &QTimer::timeout, this, [this, root] {
        stopDownload(root);
        deliver(root, std::nullopt);
    });
    p.timeout->start(std::max(1, timeoutMs));
    if (!fetchBlob(root, size, kFetchOwner, m_nextMember++) && !downloading(root)) {
        // refused: answered from the event loop, never inside the caller
        QTimer::singleShot(0, this, [this, root] { deliver(root, std::nullopt); });
        return;
    }
    pumpSession();
}

std::optional<QByteArray> Ed3kLiveFiles::cached(const ffi::Root& root) const
{
    for (const auto& [r, file] : m_cache)
        if (r == root)
            return file;
    return std::nullopt;
}

std::vector<std::uint64_t> Ed3kLiveFiles::wantedBlocks(const Download& d) const
{
    Q_UNUSED(d)
    return {};
}

void Ed3kLiveFiles::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Q_UNUSED(d) Q_UNUSED(b)
}

void Ed3kLiveFiles::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    Q_UNUSED(member)
    if (owner != kFetchOwner)
        return;
    // verified block by block against the root: the magic says it is a channel file
    if (!hasMagic(data)) {
        logDebug([&]() -> QString { return QStringLiteral("%1 is no channel file").arg(hexOf(root.bytes)); });
        deliver(root, QByteArray());   // fetched, not a channel file: empty
        return;
    }
    keep(root, data);
    deliver(root, data);
}

std::optional<Ed3kJoinBase::FileRead> Ed3kLiveFiles::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    Q_UNUSED(s) Q_UNUSED(offset) Q_UNUSED(len)
    return std::nullopt;   // channel files are blobs in memory
}

bool Ed3kLiveFiles::hasOwner(const QString& owner) const
{
    Q_UNUSED(owner)
    return false;
}

void Ed3kLiveFiles::saveOwner(const QString& owner)
{
    Q_UNUSED(owner)
}

void Ed3kLiveFiles::onHideChanged(bool hiding)
{
    // stopped: the base forgot the anonymous shares; they wait for hiding (never node key)
    if (!hiding)
        return;
    for (const auto& [root, p] : m_published)
        if (p.key == ffi::KeyKind::Anon && !isShared(root))
            (void)shareOne(p);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kLiveFiles::deliver(const ffi::Root& root, std::optional<QByteArray> file)
{
    const auto it = m_pending.find(root);
    if (it == m_pending.end())
        return;
    auto waiting = std::move(it->second.waiting);
    m_pending.erase(it);
    for (auto& done : waiting)
        done(file);
}

void Ed3kLiveFiles::keep(const ffi::Root& root, const QByteArray& file)
{
    std::erase_if(m_cache, [&](const auto& kv) { return kv.first == root; });
    m_cache.emplace_back(root, file);
    while (m_cache.size() > kCacheMax)
        m_cache.pop_front();
}

std::optional<ffi::Root> Ed3kLiveFiles::shareOne(const Published& p)
{
    if (!m_started)
        return std::nullopt;
    // was: const auto root = shareBlob(p.file, p.name, std::nullopt);
    const auto root = shareBlob(p.file, p.name, std::nullopt, p.key);   // hops.md §1: the broadcast's key
    if (!root)
        return std::nullopt;
    const QByteArray n = p.name.toUtf8();
    // kbps of the channel as the bitrate (Protocol.md §5.4: kbit/s)
    const auto info = parse(p.file);
    const ffi::FileInfo fi{*root, std::uint64_t(p.file.size()), std::string(n.constData(), std::size_t(n.size())), std::nullopt, 0, 0, 0,
                           info ? info->kbps : 0, 0, 0};
    if (auto ok = publishShared(fi); !ok)
        logWarning(QStringLiteral("channel file \"%1\" not published (error %2)").arg(p.name, errorName(ok.error())));
    pumpSession();
    return root;
}

} // namespace eMule::ed3k
