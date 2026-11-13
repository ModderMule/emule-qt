/// @file Ed3kShared.cpp
/// @brief eMule's shared files served over ed3k — implementation.

#include "Ed3kShared.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/OwnerOnly.h"
#include "utils/SystemMemory.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

constexpr quint32 kCacheMagic = 0x45335348;   // "E3SH", shared.dat v1 (values inline)
constexpr quint32 kCacheVersion = 1;
constexpr quint32 kIndexMagic = 0x45335349;   // "E3SI", shared-index.dat
constexpr quint32 kIndexVersion = 2;
/// head of shared-hashes.dat; offsets count from the file start
constexpr char kHashesMagic[] = "E3SHASH1";
constexpr qint64 kHashesMagicLen = 8;
/// rewritten when the dead values are at least this much and more than the live ones
constexpr qint64 kCompactMinDead = 4 << 20;
constexpr qint64 kCopyChunk = 1 << 20;
/// the index while hashing: rewritten at most this often (values are on disk at once)
constexpr int kHashRunSaveDelayMs = 30'000;
/// the RAM check (a syscall) at most this often
constexpr qint64 kLowRamCheckMs = 5000;
/// auto budget of the engine's block values: this share of RAM, within the bounds
constexpr std::uint64_t kAutoHashRamPercent = 2;
constexpr std::uint64_t kAutoHashRamMin = 64ull << 20;
constexpr std::uint64_t kAutoHashRamMax = 512ull << 20;
/// read queue of stored block values (WantHashes); never an owner id
const QString kHashReads = QStringLiteral("\x01hashes");
/// guards against a broken file; a share list this big is not served from the cache
constexpr quint32 kMaxCacheEntries = 1u << 20;
/// alias.md §3: `name` at most 255 B of UTF-8
constexpr qsizetype kMaxAliasName = 255;

/// SharedFileList as Ed3kShared sees it.
class ListAdapter final : public SharedFileSource {
public:
    explicit ListAdapter(SharedFileList& list) : m_list(list) {}
    std::vector<SharedEntry> files() const override
    {
        std::vector<SharedEntry> out;
        m_list.forEachFile([&](KnownFile* f) {
            if (f)
                if (auto e = Ed3kShared::entryOf(*f))
                    out.push_back(std::move(*e));
        });
        return out;
    }
    std::optional<SharedEntry> find(const QByteArray& md4) const override
    {
        if (md4.size() != 16)
            return std::nullopt;
        const KnownFile* f = m_list.getFileByID(reinterpret_cast<const uint8*>(md4.constData()));
        return f ? Ed3kShared::entryOf(*f) : std::nullopt;
    }

private:
    SharedFileList& m_list;
};

void varint(ffi::Bytes& out, std::uint64_t v)
{
    while (v >= 0x80) {
        out.push_back(std::byte((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(std::byte(v));
}

void lenField(ffi::Bytes& out, std::uint8_t tag, std::span<const std::uint8_t> bytes)
{
    out.push_back(std::byte(tag));
    varint(out, bytes.size());
    for (const auto b : bytes)
        out.push_back(std::byte(b));
}

std::span<const std::uint8_t> u8(const QByteArray& b)
{
    return {reinterpret_cast<const std::uint8_t*>(b.constData()), std::size_t(b.size())};
}

QByteArray checkOf(std::span<const std::byte> data)
{
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(data.data()), qsizetype(data.size())),
                                    QCryptographicHash::Md5);
}

std::optional<ffi::Ed2kHash> ed2kOf(const QByteArray& md4)
{
    if (md4.size() != 16)
        return std::nullopt;
    ffi::Ed2kHash h{};
    std::memcpy(h.data(), md4.constData(), h.size());
    return h;
}

std::string utf8(const QString& s)
{
    const QByteArray b = s.toUtf8();
    return {b.constData(), std::size_t(b.size())};
}

} // namespace

Ed3kShared::Ed3kShared(Ed3kSession& session, SharedFileSource& source, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("shared"), parent)
    , m_source(source)
{
    m_recheckTimer.setSingleShot(true);
    m_recheckTimer.setInterval(0);
    connect(&m_recheckTimer, &QTimer::timeout, this, &Ed3kShared::recheck);
    // no signal for a priority change (auto priority moves by itself): read once a minute
    m_priorityTimer.setInterval(60 * 1000);
    connect(&m_priorityTimer, &QTimer::timeout, this, [this] {
        refreshPriorities();
        releaseRamHashes();
        applyHashRamBudget();
        logHashProgress();
    });
}

Ed3kShared::~Ed3kShared()
{
    stop();
}

std::unique_ptr<SharedFileSource> Ed3kShared::listAdapter(SharedFileList& list)
{
    return std::make_unique<ListAdapter>(list);
}

std::optional<SharedEntry> Ed3kShared::entryOf(const KnownFile& file)
{
    if (file.isPartFile() || file.hasNullHash() || static_cast<uint64>(file.fileSize()) == 0)
        return std::nullopt;
    // a completed PartFile: filePath() is the final path (dataFilePath() the old .part)
    const auto priority = [&file] {
        using P = ::ed3k::UploadPriority;
        switch (file.upPriority()) {
        case kPrVeryLow: return P::VeryLow;
        case kPrLow: return P::Low;
        case kPrHigh: return P::High;
        case kPrVeryHigh: return P::Release;   // eMule's "Release"
        default: return P::Normal;
        }
    }();
    return SharedEntry{QByteArray(reinterpret_cast<const char*>(file.fileHash()), 16), file.filePath(), file.fileName(),
                       static_cast<uint64>(file.fileSize()), priority};
}

void Ed3kShared::connectList(SharedFileList& list)
{
    // direct: fileRemoved's file may be deleted right after the signal
    connect(&list, &SharedFileList::fileAdded, this, [this](KnownFile* f) {
        if (f)
            if (auto e = entryOf(*f))
                onFileAdded(*e);
    });
    connect(&list, &SharedFileList::fileRemoved, this, [this](KnownFile* f) {
        if (f)
            onFileRemoved(QByteArray(reinterpret_cast<const char*>(f->fileHash()), 16));
    }, Qt::DirectConnection);
    connect(&list, &SharedFileList::fileRelocated, this, [this](KnownFile* f) {
        if (f)
            if (auto e = entryOf(*f))
                onFileRelocated(*e);
    });
    // Queued: fileChanged may fire with the list's lock held, and the lookup takes it
    connect(&list, &SharedFileList::fileChanged, this, &Ed3kShared::onFileChanged,
            Qt::QueuedConnection);
    connect(&list, &SharedFileList::scanned, this, &Ed3kShared::onListScanned);
}

void Ed3kShared::start()
{
    if (m_started || !m_enabled || !m_session.isRunning())
        return;
    beginStart();
    m_hashingVolumes.clear();
    m_scanLogged = false;
    loadCache();
    for (const SharedEntry& e : m_source.files())
        add(e);
    // a disk read folder by folder: fewer seeks between small files
    for (auto& [volume, queue] : m_hashQueues)
        std::ranges::sort(queue, {}, [this](const QByteArray& md4) {
            const auto it = m_files.find(md4);
            return it == m_files.end() ? QString() : it->second.entry.path;
        });
    logInfo(QStringLiteral("%1 file(s) of the share list, %2 to hash on %3 disk(s)")
                .arg(m_files.size()).arg(hashQueueSize()).arg(m_hashQueues.size()));
    m_sideOn->store(true);
    hashNext();
    m_hashRamSent.reset();
    applyHashRamBudget();
    m_priorityTimer.start();
    pumpSession();
}

void Ed3kShared::stop()
{
    if (!m_started)
        return;
    m_priorityTimer.stop();
    m_sideOn->store(false);
    saveCache();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [md4, f] : m_files) {
            if (!f.shared)
                continue;
            if (!f.alias.empty())
                (void)e->unpublishAlias(t, std::span{&f.alias, 1});
            // was: (void)e->unpublish(t, c->second.root, ...) (now under its key)
            if (const auto c = m_cache.find(md4); c != m_cache.end())
                unpublishShared(c->second.root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
        }
    }
    closeAll();
    m_files.clear();
    m_members.clear();
    m_hashQueues.clear();
    m_hashingVolumes.clear();
    m_runTimer.invalidate();
    m_suspects.clear();
    m_recheckTimer.stop();
    pumpSession();
}

void Ed3kShared::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (on)
        start();
    else
        stop();
}

std::optional<ffi::Root> Ed3kShared::rootOf(const QByteArray& md4) const
{
    const auto f = m_files.find(md4);
    if (f == m_files.end() || !f->second.shared)
        return std::nullopt;
    const auto c = m_cache.find(md4);
    return c == m_cache.end() ? std::nullopt : std::optional{c->second.root};
}

std::optional<SharedEntry> Ed3kShared::entryOfRoot(const ffi::Root& root) const
{
    for (const auto& [md4, f] : m_files) {
        if (!f.shared)
            continue;
        if (const auto c = m_cache.find(md4); c != m_cache.end() && c->second.root == root)
            return f.entry;
    }
    return std::nullopt;
}

bool Ed3kShared::holdsRoot(const ffi::Root& root) const
{
    return std::ranges::any_of(m_files, [&](const auto& kv) {
        const auto c = m_cache.find(kv.first);
        return c != m_cache.end() && c->second.root == root;
    });
}

bool Ed3kShared::aliasPublished(const QByteArray& md4) const
{
    const auto f = m_files.find(md4);
    return f != m_files.end() && !f->second.alias.empty();
}

bool Ed3kShared::keywordsPublished(const QByteArray& md4) const
{
    const auto f = m_files.find(md4);
    return f != m_files.end() && f->second.shared && f->second.keywords;
}

std::optional<ffi::KeyKind> Ed3kShared::keyOfFile(const QByteArray& md4) const
{
    const auto root = hashedRootOf(md4);
    return root ? std::optional(keyOf(*root)) : std::nullopt;
}

bool Ed3kShared::tellDownloadElsewhere(const QByteArray& md4, const QString& name, const QString& networks)
{
    const auto root = hashedRootOf(md4);
    return root && keyOf(*root) == ffi::KeyKind::Anon && tellExposed(*root, name, networks);
}

bool Ed3kShared::tellExposed(const ffi::Root& root, const QString& name, const QString& networks)
{
    if (!m_exposedTold.insert(root).second)
        return false;
    logWarning(QStringLiteral("\"%1\" is under the anonymous key and on %2 too: sources there show this "
                              "address (hops.md §1). Remove it there or move it to the node key.").arg(name, networks));
    emit anonExposed(name, networks);
    return true;
}

std::optional<ffi::Root> Ed3kShared::hashedRootOf(const QByteArray& md4) const
{
    const auto c = m_cache.find(md4);
    if (!m_files.contains(md4) || c == m_cache.end())
        return std::nullopt;
    return c->second.root;
}

bool Ed3kShared::moveFileKey(const ffi::Root& root, ffi::KeyKind key)
{
    for (auto& [md4, f] : m_files) {
        const auto c = m_cache.find(md4);
        if (c == m_cache.end() || !(c->second.root == root))
            continue;
        if (keyOf(root) == key)
            return false;
        // hops.md §1: closed under one key (records taken back), then opened under the other
        if (f.shared) {
            if (auto* e = m_session.engine(); e && !f.alias.empty())
                (void)e->unpublishAlias(m_session.now(), std::span{&f.alias, 1});
            unpublishShared(root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
            unshare(root);
            f.shared = false;
            f.keywords = false;
            f.alias.clear();
        }
        m_session.hide().record(root, key);
        share(f, c->second);
        pumpSession();
        return true;
    }
    return false;
}

void Ed3kShared::onHideChanged(bool hiding)
{
    for (auto& [md4, f] : m_files) {
        const auto c = m_cache.find(md4);
        if (c == m_cache.end() || m_session.hide().recorded(c->second.root) != ffi::KeyKind::Anon)
            continue;
        if (!hiding) {
            // gone with the anonymous node (the base forgot the share)
            f.shared = false;
            f.keywords = false;
        } else if (!f.shared && !f.queued) {
            share(f, c->second);
        }
    }
}

QString Ed3kShared::cachePath() const
{
    return joinDir(QStringLiteral("shared-index.dat"));
}

QString Ed3kShared::hashesPath() const
{
    return joinDir(QStringLiteral("shared-hashes.dat"));
}

std::size_t Ed3kShared::hashesInRam() const
{
    return std::size_t(std::ranges::count_if(m_cache, [](const auto& c) { return c.second.inRam; }));
}

void Ed3kShared::setLowRamPercent(int percent)
{
    m_lowRamPercent = percent;
    m_lowRamCheckedMs = 0;
    applyHashRamBudget();
}

void Ed3kShared::setHashRamMiB(int mib)
{
    m_hashRamMiB = std::max(0, mib);
    applyHashRamBudget();
}

std::uint64_t Ed3kShared::hashRamBudget()
{
    if (lowRam())
        return 0;
    if (m_hashRamMiB > 0)
        return std::uint64_t(m_hashRamMiB) << 20;
    const auto mem = systemMemory();
    return autoHashRam(mem ? mem->total : 0);
}

std::uint64_t Ed3kShared::autoHashRam(std::uint64_t totalRam)
{
    if (totalRam == 0)
        return 256ull << 20;   // unknown: a middle value
    return std::clamp(totalRam / 100 * kAutoHashRamPercent, kAutoHashRamMin, kAutoHashRamMax);
}

/// One file's ed3k hash, fed from eD2K's read on its hashing worker.
class Ed3kShared::SideSink final : public SideHash {
public:
    explicit SideSink(QPointer<Ed3kShared> owner) : m_owner(std::move(owner)) {}

    void update(std::span<const std::byte> data) override
    {
        if (m_ok && !m_hasher.update(data))
            m_ok = false;
    }

    void finish(const QByteArray& md4, const QString& path, quint64 size, qint64 mtimeMs) override
    {
        if (!m_ok)
            return;
        auto done = m_hasher.finish();
        if (!done || done->size != size)
            return;
        const auto bytes = done->hashes.bytes();
        FileHash hashed{done->root, done->size, ffi::Bytes(bytes.begin(), bytes.end())};
        // queued to the owner's thread ahead of the share list's hashingFinished
        QMetaObject::invokeMethod(
            m_owner.data(),
            [owner = m_owner, md4, path, mtimeMs, hashed = std::move(hashed)] {
                if (owner)
                    owner->primeHash(md4, path, mtimeMs, hashed);
            },
            Qt::QueuedConnection);
    }

private:
    QPointer<Ed3kShared> m_owner;
    ffi::Hasher m_hasher;
    bool m_ok = true;
};

SideHasherFactory Ed3kShared::sideHasherFactory(quint64 pipeFrom)
{
    QPointer<Ed3kShared> self(this);
    return [self, on = m_sideOn, pipeFrom](const QString& path, quint64 size) -> std::unique_ptr<SideHash> {
        Q_UNUSED(path)
        // a hasher only while serving (size 0: never shared)
        if (!on->load() || size == 0)
            return nullptr;
        auto sink = std::make_unique<SideSink>(self);
        // was: BLAKE3 on the eD2K thread, after MD4/AICH — CPU-bound on an SSD
        if (size >= pipeFrom)
            return std::make_unique<SideHashPipe>(std::move(sink));
        return sink;
    };
}

ffi::Bytes Ed3kShared::ed2kAlias(const QByteArray& md4, std::uint64_t size, const ffi::Root& root, const QString& name)
{
    // Alias (proto): scheme 1, id 2, rel 3 (SAME = 0, left out), root 4, size 5, name 6
    QByteArray id = md4;
    for (int i = 7; i >= 0; --i)
        id.append(char((size >> (8 * i)) & 0xff));
    ffi::Bytes out;
    lenField(out, 0x0a, u8(QByteArrayLiteral("ed2k")));
    lenField(out, 0x12, u8(id));
    lenField(out, 0x22, root.bytes);
    out.push_back(std::byte(0x28));
    varint(out, size);
    // a longer name would get the record REJECTED: left out then (optional field)
    if (const QByteArray n = name.toUtf8(); !n.isEmpty() && n.size() <= kMaxAliasName)
        lenField(out, 0x32, u8(n));
    return out;
}

void Ed3kShared::onFileAdded(const SharedEntry& entry)
{
    if (!m_started)
        return;
    add(entry);
    hashNext();
    pumpSession();
}

void Ed3kShared::onFileRemoved(const QByteArray& md4)
{
    if (!m_started)
        return;
    remove(md4);
    // the hash stays (#122): unshared and shared again (a torrent check, a rescan) it is
    // reused when path, size and date still match (add()); saveCache drops it once gone
    // was: m_cache.erase(md4);
    scheduleSave(kOwner);
    pumpSession();
}

void Ed3kShared::onFileRelocated(const SharedEntry& entry)
{
    if (!m_started)
        return;
    const auto it = m_files.find(entry.md4);
    if (it == m_files.end()) {
        onFileAdded(entry);
        return;
    }
    File& f = it->second;
    if (f.entry.path == entry.path)
        return;
    // a rename keeps size and date: the root stays, reads follow the new path
    if (unchanged(entry.path, f.entry.size, f.mtimeMs)) {
        f.entry = entry;
        if (const auto c = m_cache.find(entry.md4); c != m_cache.end()) {
            c->second.path = entry.path;
            scheduleSave(kOwner);
        }
        return;
    }
    remove(entry.md4);
    onFileAdded(entry);
}

void Ed3kShared::onListScanned(bool full)
{
    if (!m_started || !full || std::exchange(m_scanLogged, true))
        return;
    logInfo(QStringLiteral("%1 file(s) of the share list after its scan, %2 to hash")
                .arg(m_files.size()).arg(hashQueueSize() + m_hashingVolumes.size()));
}

void Ed3kShared::onFileChanged(const QByteArray& md4)
{
    if (!m_started || m_files.contains(md4))
        return;
    if (auto e = m_source.find(md4))
        onFileAdded(*e);
}

// -- base hooks --------------------------------------------------------------

std::vector<std::uint64_t> Ed3kShared::wantedBlocks(const Download& d) const
{
    Q_UNUSED(d)
    return {};   // serves only, downloads nothing
}

void Ed3kShared::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Q_UNUSED(d) Q_UNUSED(b)
}

void Ed3kShared::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    Q_UNUSED(owner) Q_UNUSED(member) Q_UNUSED(root) Q_UNUSED(data)
}

std::optional<Ed3kJoinBase::FileRead> Ed3kShared::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    const File* f = fileOfMember(s.member);
    if (!f || !f->shared)
        return std::nullopt;
    // the worker checks size and date: changed since hashed, its blocks would not verify
    return FileRead{f->entry.path, offset, len, qint64(f->entry.size), f->mtimeMs};
}

::ed3k::UploadPriority Ed3kShared::uploadPriority(const Shared& s) const
{
    const File* f = fileOfMember(s.member);
    return f ? f->entry.priority : ::ed3k::UploadPriority::Normal;
}

bool Ed3kShared::answerHashes(std::uint64_t id, const ffi::Root& root)
{
    auto* e = m_session.engine();
    const auto c = cacheOfRoot(root);
    if (!e || c == m_cache.end())
        return false;
    const Hashed& h = c->second;
    if (h.inRam) {
        (void)e->hashesProvide(m_session.now(), id, std::span<const std::byte>{h.hashes});
        return true;
    }
    if (h.diskOffset < kHashesMagicLen)
        return false;
    // on a worker: a big file's values are MiB, the main thread must not wait for the disk
    const QByteArray md4 = c->first;
    const QByteArray check = h.diskCheck;
    readInBackground(kHashReads, {FileRead{hashesPath(), std::uint64_t(h.diskOffset), h.diskLen}},
                     [this, id, md4, check](ReadResults r) {
        auto* e = m_session.engine();
        if (!e)
            return;
        std::optional<ffi::Bytes> blob;
        if (!r.empty() && r.front()) {
            const auto* b = reinterpret_cast<const std::byte*>(r.front()->constData());
            const std::span<const std::byte> bytes{b, std::size_t(r.front()->size())};
            if (checkOf(bytes) == check)
                blob = ffi::Bytes(bytes.begin(), bytes.end());
        }
        // compacted meanwhile (moved): read again where it is now
        if (const auto c = m_cache.find(md4); !blob && c != m_cache.end())
            blob = loadHashes(c->second);
        const auto t = m_session.now();
        if (blob)
            (void)e->hashesProvide(t, id, std::span<const std::byte>{*blob});
        else
            (void)e->hashesFail(t, id);
        pumpSession();
    });
    return true;
}

void Ed3kShared::onHashesLost(const ffi::Root& root)
{
    const auto c = cacheOfRoot(root);
    if (c == m_cache.end())
        return;
    logWarning(QStringLiteral("stored block hashes of \"%1\" unreadable, hashed again").arg(c->second.path));
    // dropped and taken again on the next turn (recheck): hashed, as a cache miss
    m_suspects.insert(c->first);
    m_recheckTimer.start();
}

bool Ed3kShared::sharedGone(const Shared& s) const
{
    const File* f = fileOfMember(s.member);
    if (!f)
        return true;
    if (unchanged(f->entry.path, f->entry.size, f->mtimeMs))
        return false;   // a read error only
    // the base closes the share; the file is dropped and taken again on the next turn
    m_suspects.insert(f->entry.md4);
    m_recheckTimer.start();
    return true;
}

bool Ed3kShared::hasOwner(const QString& owner) const
{
    return m_started && owner == kOwner;
}

void Ed3kShared::saveOwner(const QString& owner)
{
    if (owner == kOwner)
        saveCache();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kShared::add(const SharedEntry& entry)
{
    if (entry.md4.size() != 16 || entry.size == 0 || entry.path.isEmpty())
        return;
    // #12 MUST: an opened plain copy is not served (its sealed copy is)
    if (m_neverShared && m_neverShared(entry.path))
        return;
    if (const auto it = m_files.find(entry.md4); it != m_files.end()) {
        if (it->second.entry.path != entry.path)
            onFileRelocated(entry);
        return;
    }
    const QFileInfo fi(entry.path);
    if (!fi.isFile() || std::uint64_t(fi.size()) != entry.size) {
        logDebug([&]() -> QString { return QStringLiteral("\"%1\" is not what the share list says, left out").arg(entry.path); });
        return;
    }
    File& f = m_files[entry.md4];
    f.entry = entry;
    f.member = m_nextMember++;
    f.mtimeMs = mtimeOf(entry.path);
    m_members[f.member] = entry.md4;
    const auto c = m_cache.find(entry.md4);
    if (c != m_cache.end() && c->second.path == entry.path && c->second.size == entry.size && c->second.mtimeMs == f.mtimeMs)
        share(f, c->second);
    else
        enqueueHash(f);
}

void Ed3kShared::remove(const QByteArray& md4)
{
    const auto it = m_files.find(md4);
    if (it == m_files.end())
        return;
    File& f = it->second;
    if (f.shared) {
        if (const auto c = m_cache.find(md4); c != m_cache.end()) {
            if (auto* e = m_session.engine()) {
                const auto t = m_session.now();
                if (!f.alias.empty())
                    (void)e->unpublishAlias(t, std::span{&f.alias, 1});
                // was: (void)e->unpublish(t, c->second.root, ...) (now under its key)
                unpublishShared(c->second.root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
            }
            unshare(c->second.root);
        }
    }
    m_members.erase(f.member);
    m_files.erase(it);
    // a queued hash of it finds no file and is skipped
}

void Ed3kShared::share(File& f, Hashed& h)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    // values on disk: the engine asks for them when a peer needs them (answerHashes), so
    // a start reads nothing of shared-hashes.dat
    // was: std::optional<ffi::Bytes> fromDisk;
    // was: if (!h.inRam) {
    // was:     fromDisk = loadHashes(h);
    // was:     if (!fromDisk) {
    const bool stored = h.diskOffset >= kHashesMagicLen;
    if (!h.inRam && !stored) {
        logWarning(QStringLiteral("cached block hashes of \"%1\" unreadable, hashed again").arg(f.entry.name));
        m_cache.erase(f.entry.md4);   // h is gone
        scheduleSave(kOwner);
        enqueueHash(f);
        hashNext();
        return;
    }
    // was: const ffi::Bytes& hashes = fromDisk ? *fromDisk : h.hashes;
    const std::span<const std::byte> hashes = h.inRam ? std::span<const std::byte>{h.hashes} : std::span<const std::byte>{};
    const std::string name = utf8(f.entry.name);
    // hops.md §1: under the anonymous key nothing names it for eD2K (no MD4, no ALIAS)
    const bool anon = keyOf(h.root) == ffi::KeyKind::Anon;
    // was: const auto ed2k = ed2kOf(f.entry.md4);
    const auto ed2k = anon ? std::nullopt : ed2kOf(f.entry.md4);
    // the MD4 is eMule's of the same file (the share list checks size and date)
    // was: if (!shareMember(ffi::SharedFile{h.root, h.size, name, ed2k, std::nullopt, ffi::Have::complete(), std::span{hashes}}, kOwner,
    ffi::SharedFile file{h.root, h.size, name, ed2k, std::nullopt, ffi::Have::complete(), hashes};
    file.hashesStored = stored;
    if (!shareMember(file, kOwner, f.member)) {
        logWarning(QStringLiteral("cannot share \"%1\"").arg(f.entry.name));
        return;
    }
    f.shared = true;
    // no second copy: packmule holds the values within its RAM budget, read back from disk
    // was: if (!m_runTimer.isValid() || lowRam()) {
    // was:     if (h.inRam && h.diskOffset >= 0) { drop } } else if (fromDisk) { keep }
    if (h.inRam && stored) {
        ffi::Bytes().swap(h.hashes);
        h.inRam = false;
    }
    const auto t = m_session.now();
    const ffi::FileInfo info{h.root, h.size, name, ed2k, 0, 0, 0, 0, 0, 0};
    // was: ToDo: keywords (Publish::Both) for files the user wants found over ed3k search
    // (done: every shared file, as eD2K's share list; only the source while holding back)
    // if (auto ok = e->publish(t, info, ffi::Publish::Source); !ok)
    // const auto mode = m_holdBack ? ffi::Publish::Source : ffi::Publish::Both;
    // if (auto ok = e->publish(t, info, mode); !ok)
    // a name without a keyword: its source record alone (publishShared)
    if (auto ok = publishShared(info); !ok)
        logDebug([&]() -> QString { return QStringLiteral("publish of \"%1\" failed (error %2)").arg(f.entry.name, errorName(ok.error())); });
    else
        f.keywords = *ok == ffi::Publish::Both;
    // hops.md §1 SHOULD: every file here is in eD2K's share list too, its sources there show
    // the address and tie the anonymous key to it: the user is told once
    // was: ToDo: the same for a file under the anonymous key downloaded over eD2K or a hoster
    // (done: DaemonApp, DownloadQueue::fileAdded → tellDownloadElsewhere; Ed3kDownloads::add
    // of an anonymous root → setAnonAddedHook → tellExposed)
    // ToDo: BitTorrent / HTTP: a file under the anonymous key downloaded or seeded over
    // BitTorrent (TorrentSession add, by its ed3k root/MD4) or a plain HTTP download is not
    // checked; DaemonApp's anon hooks (next to setAnonAddedHook) are the place
    if (anon)
        (void)tellExposed(h.root, f.entry.name, QStringLiteral("eD2K/Kad"));
    // alias.md §3: the root is shared here and both names come from these bytes; never for a
    // file of the anonymous key (ffi.md: eD2K is never anonymous)
    // was: if (!m_holdBack) {
    if (!m_holdBack && !anon) {
        f.alias = ed2kAlias(f.entry.md4, h.size, h.root, f.entry.name);
        if (auto ok = e->publishAlias(t, std::span{&f.alias, 1}); !ok) {
            logDebug([&]() -> QString { return QStringLiteral("ed2k record of \"%1\" not published (error %2)").arg(f.entry.name, errorName(ok.error())); });
            f.alias.clear();
        }
    }
    emit fileShared(f.entry.md4);
}

void Ed3kShared::enqueueHash(File& f)
{
    if (f.queued)
        return;
    f.queued = true;
    m_hashQueues[volumeOf(f.entry.path)].push_back(f.entry.md4);
}

void Ed3kShared::hashNext()
{
    if (!m_started)
        return;
    // was: one file at a time over all disks; now one per disk, disks in parallel
    for (auto& [volume, queue] : m_hashQueues) {
        if (m_hashingVolumes.contains(volume))
            continue;
        while (!queue.empty()) {
            const QByteArray md4 = queue.front();
            queue.pop_front();
            const auto it = m_files.find(md4);
            if (it == m_files.end() || !it->second.queued)
                continue;
            File& f = it->second;
            f.queued = false;
            m_hashingVolumes.insert(volume);
            ++m_hashCount;
            const QString path = f.entry.path;
            const qint64 mtime = f.mtimeMs;
            if (!m_runTimer.isValid()) {
                m_runTimer.start();
                m_runFiles = 0;
                m_runBytes = 0;
                logInfo(QStringLiteral("hashing %1 file(s)").arg(hashQueueSize() + 1));
            }
            // per file like eD2K's "Hashing file": own switch, a big share floods the log
            if (lcEmuleSharedVerbose().isDebugEnabled())
                logInfo(QStringLiteral("Hashing file: \"%1\"").arg(QDir::toNativeSeparators(path)));
            QElapsedTimer fileTimer;
            fileTimer.start();
            hashInBackground(path, [this, md4, path, mtime, fileTimer, volume](std::optional<FileHash> hashed) {
                m_hashingVolumes.erase(volume);
                if (hashed) {
                    ++m_runFiles;
                    m_runBytes += static_cast<qint64>(hashed->size);
                    if (lcEmuleSharedVerbose().isDebugEnabled())
                        logInfo(QStringLiteral("Hashed \"%1\" (%2 bytes) in %3 ms")
                                    .arg(QDir::toNativeSeparators(path)).arg(hashed->size).arg(fileTimer.elapsed()));
                }
                onHashed(md4, path, mtime, hashed);
                hashNext();
                if (m_hashingVolumes.empty())
                    endHashRun();
                pumpSession();
            });
            break;
        }
    }
    std::erase_if(m_hashQueues, [this](const auto& q) { return q.second.empty() && !m_hashingVolumes.contains(q.first); });
}

void Ed3kShared::onHashed(const QByteArray& md4, const QString& path, qint64 mtimeMs, const std::optional<FileHash>& hashed)
{
    const auto it = m_files.find(md4);
    if (it == m_files.end())
        return;   // removed meanwhile
    File& f = it->second;
    if (f.shared)
        return;
    if (f.entry.path != path) {
        // relocated while hashed: again under the new path
        f.mtimeMs = mtimeOf(f.entry.path);
        enqueueHash(f);
        return;
    }
    if (!hashed) {
        // was: ToDo: hash again later (unreadable now) (done: after kRehashMs, kRehashTries
        // times; and at the next start)
        logWarning(QStringLiteral("cannot hash \"%1\"").arg(path));
        if (++f.rehashTries < kRehashTries)
            QTimer::singleShot(m_rehashMs, this, [this, md4] { rehash(md4); });
        return;
    }
    // written while hashed: the share list rehashes it and adds it again
    if (hashed->size != f.entry.size || !unchanged(path, f.entry.size, mtimeMs)) {
        logDebug([&]() -> QString { return QStringLiteral("\"%1\" changed while hashed, left out").arg(path); });
        return;
    }
    share(f, remember(md4, path, mtimeMs, *hashed));
}

void Ed3kShared::rehash(const QByteArray& md4)
{
    const auto it = m_files.find(md4);
    if (!m_started || it == m_files.end() || it->second.shared)
        return;   // stopped, removed or shared meanwhile
    File& f = it->second;
    f.mtimeMs = mtimeOf(f.entry.path);
    enqueueHash(f);
    hashNext();
    pumpSession();
}

void Ed3kShared::recheck()
{
    const auto suspects = std::exchange(m_suspects, {});
    for (const QByteArray& md4 : suspects) {
        remove(md4);
        m_cache.erase(md4);
        if (auto e = m_source.find(md4))
            add(*e);
    }
    scheduleSave(kOwner);
    hashNext();
    pumpSession();
}

void Ed3kShared::refreshPriorities()
{
    for (auto& [md4, f] : m_files) {
        if (!f.shared)
            continue;
        const auto e = m_source.find(md4);
        if (!e || e->priority == f.entry.priority)
            continue;
        f.entry.priority = e->priority;
        if (const auto c = m_cache.find(md4); c != m_cache.end())
            updateUploadPriority(c->second.root);
    }
}

const Ed3kShared::File* Ed3kShared::fileOfMember(int member) const
{
    const auto m = m_members.find(member);
    if (m == m_members.end())
        return nullptr;
    const auto f = m_files.find(m->second);
    return f == m_files.end() ? nullptr : &f->second;
}

bool Ed3kShared::unchanged(const QString& path, std::uint64_t size, qint64 mtimeMs)
{
    const QFileInfo fi(path);
    return fi.isFile() && std::uint64_t(fi.size()) == size && fi.lastModified().toMSecsSinceEpoch() == mtimeMs;
}

qint64 Ed3kShared::mtimeOf(const QString& path)
{
    return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

void Ed3kShared::loadCache()
{
    m_cache.clear();
    const QByteArray blob = readBlob(cachePath());
    if (blob.isEmpty()) {
        // ToDo: remove the shared.dat (v1) migration later
        if (QFile::exists(joinDir(QStringLiteral("shared.dat"))))
            migrateV1();
        return;
    }
    QDataStream in(blob);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0, count = 0;
    in >> magic >> version >> count;
    if (magic != kIndexMagic || version != kIndexVersion || count > kMaxCacheEntries) {
        logWarning(QStringLiteral("%1 is not a hash cache, ignored").arg(cachePath()));
        return;
    }
    // values stay on disk: read when shared (share())
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        QByteArray md4, root;
        Hashed h;
        quint64 size = 0;
        in >> md4 >> h.path >> size >> h.mtimeMs >> root >> h.diskOffset >> h.diskLen >> h.diskCheck;
        if (in.status() != QDataStream::Ok || md4.size() != 16 || root.size() != qsizetype(h.root.bytes.size()))
            break;
        h.size = size;
        std::memcpy(h.root.bytes.data(), root.constData(), h.root.bytes.size());
        m_cache[md4] = std::move(h);
    }
    if (in.status() != QDataStream::Ok)
        logWarning(QStringLiteral("%1 is cut short, %2 entries kept").arg(cachePath()).arg(m_cache.size()));
    compactHashes();
}

void Ed3kShared::saveCache()
{
    // entries of files not listed (yet) stay while their path exists: the list may load later
    std::erase_if(m_cache, [&](const auto& c) { return !m_files.contains(c.first) && !QFileInfo::exists(c.second.path); });
    const QByteArray blob = indexBlob();
    QDir().mkpath(stateDir(m_configDir));
    writeBlobLater(cachePath(), spanOf(blob));
    dropDirty(kOwner);
}

QByteArray Ed3kShared::indexBlob() const
{
    // the values are in shared-hashes.dat: the index stays small (a few hundred B a file)
    const auto stored = [](const Hashed& h) { return h.diskOffset >= 0; };
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kIndexMagic << kIndexVersion
        << quint32(std::ranges::count_if(m_cache, [&](const auto& c) { return stored(c.second); }));
    for (const auto& [md4, h] : m_cache) {
        if (!stored(h))
            continue;
        const QByteArray root(reinterpret_cast<const char*>(h.root.bytes.data()), qsizetype(h.root.bytes.size()));
        out << md4 << h.path << quint64(h.size) << h.mtimeMs << root << h.diskOffset << h.diskLen << h.diskCheck;
    }
    return blob;
}

bool Ed3kShared::storeHashes(Hashed& h, std::span<const std::byte> data) const
{
    if (data.size() > std::numeric_limits<quint32>::max())
        return false;
    QDir().mkpath(stateDir(m_configDir));
    QFile f(hashesPath());
    const bool fresh = !f.exists();
    if (!f.open(QIODevice::ReadWrite))
        return false;
    if (fresh)
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // a foreign or torn head: started over (entries naming the old values fail their check)
    if (f.size() < kHashesMagicLen || f.read(kHashesMagicLen) != QByteArrayView(kHashesMagic, kHashesMagicLen)) {
        if (!f.resize(0) || !f.seek(0) || f.write(kHashesMagic, kHashesMagicLen) != kHashesMagicLen)
            return false;
    }
    const qint64 at = f.size();
    if (!f.seek(at))
        return false;
    const auto len = qint64(data.size());
    if (f.write(reinterpret_cast<const char*>(data.data()), len) != len) {
        (void)f.resize(at);
        return false;
    }
    f.close();
    if (f.error() != QFileDevice::NoError)
        return false;
#ifdef Q_OS_WIN
    if (fresh)
        (void)makeOwnerOnly(hashesPath());
#endif
    h.diskOffset = at;
    h.diskLen = quint32(len);
    h.diskCheck = checkOf(data);
    return true;
}

std::optional<ffi::Bytes> Ed3kShared::loadHashes(const Hashed& h) const
{
    if (h.diskOffset < kHashesMagicLen)
        return std::nullopt;
    QFile f(hashesPath());
    if (!f.open(QIODevice::ReadOnly) || h.diskOffset + qint64(h.diskLen) > f.size() || !f.seek(h.diskOffset))
        return std::nullopt;
    const QByteArray data = f.read(qint64(h.diskLen));
    if (data.size() != qsizetype(h.diskLen))
        return std::nullopt;
    const auto* b = reinterpret_cast<const std::byte*>(data.constData());
    const std::span<const std::byte> bytes{b, std::size_t(data.size())};
    if (checkOf(bytes) != h.diskCheck)
        return std::nullopt;
    return ffi::Bytes(bytes.begin(), bytes.end());
}

// ToDo: remove migrateV1 + kCacheMagic/kCacheVersion once no profile has shared.dat left
void Ed3kShared::migrateV1()
{
    const QString old = joinDir(QStringLiteral("shared.dat"));
    QFile f(old);
    if (!f.open(QIODevice::ReadOnly))
        return;
    // streamed: one file's values in RAM at a time (a big v1 file on a machine short of RAM)
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0, count = 0;
    in >> magic >> version >> count;
    if (magic != kCacheMagic || version != kCacheVersion || count > kMaxCacheEntries) {
        logWarning(QStringLiteral("%1 is not a hash cache, ignored").arg(old));
        return;
    }
    int moved = 0;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        QByteArray md4, root, hashes;
        Hashed h;
        quint64 size = 0;
        in >> md4 >> h.path >> size >> h.mtimeMs >> root >> hashes;
        if (in.status() != QDataStream::Ok || md4.size() != 16 || root.size() != qsizetype(h.root.bytes.size()))
            break;
        h.size = size;
        std::memcpy(h.root.bytes.data(), root.constData(), h.root.bytes.size());
        if (!storeHashes(h, std::as_bytes(std::span{hashes.constData(), std::size_t(hashes.size())})))
            continue;   // hashed again when listed
        m_cache[md4] = std::move(h);
        ++moved;
    }
    f.close();
    // the index first: a crash before it keeps shared.dat for the next try
    forgetBlobWrites(cachePath());
    if (!writeBlob(cachePath(), spanOf(indexBlob()))) {
        logWarning(QStringLiteral("cannot write %1, %2 kept").arg(cachePath(), old));
        return;
    }
    forgetBlobWrites(old);
    QFile::remove(old);
    logInfo(QStringLiteral("hash cache: %1 of %2 entries moved from shared.dat").arg(moved).arg(count));
}

void Ed3kShared::compactHashes()
{
    QFile src(hashesPath());
    if (!src.open(QIODevice::ReadOnly))
        return;
    std::vector<Hashed*> live;
    qint64 liveBytes = 0;
    for (auto& [md4, h] : m_cache)
        if (h.diskOffset >= kHashesMagicLen && h.diskOffset + qint64(h.diskLen) <= src.size()) {
            live.push_back(&h);
            liveBytes += h.diskLen;
        }
    const qint64 dead = src.size() - kHashesMagicLen - liveBytes;
    if (dead < kCompactMinDead || dead < liveBytes)
        return;
    QElapsedTimer timer;
    timer.start();
    std::ranges::sort(live, {}, &Hashed::diskOffset);
    const QString tmpPath = hashesPath() + QStringLiteral(".tmp");
    QFile dst(tmpPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    dst.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto fail = [&] {
        dst.close();
        QFile::remove(tmpPath);
    };
    if (dst.write(kHashesMagic, kHashesMagicLen) != kHashesMagicLen)
        return fail();
    // chunked copy: never more than kCopyChunk in RAM
    std::vector<std::pair<Hashed*, qint64>> moved;
    moved.reserve(live.size());
    for (Hashed* h : live) {
        const qint64 at = dst.pos();
        if (!src.seek(h->diskOffset))
            return fail();
        for (qint64 left = h->diskLen; left > 0;) {
            const QByteArray chunk = src.read(std::min(left, kCopyChunk));
            if (chunk.isEmpty() || dst.write(chunk) != chunk.size())
                return fail();
            left -= chunk.size();
        }
        moved.emplace_back(h, at);
    }
    dst.close();
    src.close();
    if (dst.error() != QFileDevice::NoError)
        return fail();
    // the index with the new offsets, then the file under them (a crash between: checks fail, hashed again)
    std::vector<qint64> before;
    before.reserve(moved.size());
    for (auto& [h, at] : moved) {
        before.push_back(h->diskOffset);
        h->diskOffset = at;
    }
    forgetBlobWrites(cachePath());
    std::error_code ec;
    if (!writeBlob(cachePath(), spanOf(indexBlob()))
        || (std::filesystem::rename(std::filesystem::path(tmpPath.toStdU16String()),
                                    std::filesystem::path(hashesPath().toStdU16String()), ec),
            ec)) {
        for (std::size_t i = 0; i < moved.size(); ++i)
            moved[i].first->diskOffset = before[i];
        (void)writeBlob(cachePath(), spanOf(indexBlob()));
        logWarning(QStringLiteral("cannot compact %1").arg(hashesPath()));
        return fail();
    }
#ifdef Q_OS_WIN
    (void)makeOwnerOnly(hashesPath());
#endif
    logInfo(QStringLiteral("hash cache compacted: %1 MiB dead dropped in %2 ms").arg(dead >> 20).arg(timer.elapsed()));
}

bool Ed3kShared::lowRam()
{
    if (m_lowRamCheck)
        return m_lowRamCheck();
    if (m_lowRamPercent <= 0)
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lowRamCheckedMs != 0 && now - m_lowRamCheckedMs < kLowRamCheckMs)
        return m_lowRam;
    m_lowRamCheckedMs = now;
    const bool was = m_lowRam;
    m_lowRam = systemMemoryLow(m_lowRamPercent);
    if (m_lowRam != was)
        logInfo(m_lowRam ? QStringLiteral("free RAM below %1 %: only block hashes in use kept in RAM").arg(m_lowRamPercent)
                         : QStringLiteral("free RAM above %1 % again: block hashes kept in RAM within the budget").arg(m_lowRamPercent));
    return m_lowRam;
}

void Ed3kShared::releaseRamHashes()
{
    // was: if (!m_started || !lowRam())
    if (!m_started || (m_runTimer.isValid() && !lowRam()))
        return;
    std::size_t released = 0;
    for (auto& [md4, h] : m_cache)
        if (h.inRam && h.diskOffset >= 0) {
            ffi::Bytes().swap(h.hashes);
            h.inRam = false;
            ++released;
        }
    if (released)
        logDebug([&]() -> QString { return QStringLiteral("%1 block hash list(s) left RAM").arg(released); });
}

void Ed3kShared::endHashRun()
{
    if (!m_runTimer.isValid())
        return;
    const qint64 ms = m_runTimer.elapsed();
    m_runTimer.invalidate();
    logInfo(QStringLiteral("hashing done: %1 file(s), %2 MiB in %3 s")
                .arg(m_runFiles).arg(m_runBytes >> 20).arg(static_cast<double>(ms) / 1000.0, 0, 'f', 1));
    releaseRamHashes();
}

void Ed3kShared::applyHashRamBudget()
{
    auto* e = m_session.engine();
    if (!m_started || !e)
        return;
    const std::uint64_t budget = hashRamBudget();
    if (m_hashRamSent == budget)
        return;
    if (!e->hashRamSet(m_session.now(), budget))
        return;
    m_hashRamSent = budget;
    const QString why = lowRam() ? QStringLiteral("low RAM") : m_hashRamMiB > 0 ? QStringLiteral("set") : QStringLiteral("auto");
    logInfo(QStringLiteral("block hash RAM budget %1 MiB (%2)").arg(budget >> 20).arg(why));
}

Ed3kShared::CacheIt Ed3kShared::cacheOfRoot(const ffi::Root& root)
{
    return std::ranges::find_if(m_cache, [&](const auto& c) { return c.second.root == root; });
}

void Ed3kShared::logHashProgress()
{
    if (!m_runTimer.isValid())
        return;
    const double secs = static_cast<double>(std::max<qint64>(1, m_runTimer.elapsed())) / 1000.0;
    logInfo(QStringLiteral("hashing: %1 file(s) done, %2 left, %3 MiB/s")
                .arg(m_runFiles).arg(hashQueueSize() + m_hashingVolumes.size())
                .arg(static_cast<double>(m_runBytes >> 20) / secs, 0, 'f', 1));
}

Ed3kShared::Counts Ed3kShared::counts() const
{
    Counts c{.total = m_files.size(), .hashing = m_hashingVolumes.size()};
    for (const auto& [md4, f] : m_files) {
        if (f.shared)
            ++c.published;
        else if (f.queued)
            ++c.hashing;
    }
    return c;
}

std::size_t Ed3kShared::hashQueueSize() const
{
    std::size_t n = 0;
    for (const auto& [volume, queue] : m_hashQueues)
        n += queue.size();
    return n;
}

QString Ed3kShared::volumeOf(const QString& path)
{
    const QString dir = QFileInfo(path).path();
    if (const auto it = m_volumeOfDir.find(dir); it != m_volumeOfDir.end())
        return it->second;
    // the limiter's key: eD2K and ed3k hashing of one disk share its budget
    QString volume = m_volumeKeyFn ? m_volumeKeyFn(dir) : DiskLoadLimiter::volumeKey(dir);
    if (volume.isEmpty())
        volume = QStringLiteral("/");
    m_volumeOfDir.emplace(dir, volume);
    return volume;
}

void Ed3kShared::primeHash(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed)
{
    if (!m_started || (m_neverShared && m_neverShared(path)))
        return;
    if (const auto c = m_cache.find(md4);
        c != m_cache.end() && c->second.path == path && c->second.size == hashed.size && c->second.mtimeMs == mtimeMs)
        return;   // known already
    if (!unchanged(path, hashed.size, mtimeMs))
        return;   // written since eD2K read it: hashed here when listed
    Hashed& h = remember(md4, path, mtimeMs, hashed);
    // listed before this came (a rescan): shared now, its queue entry is skipped
    if (const auto it = m_files.find(md4); it != m_files.end() && !it->second.shared && it->second.entry.path == path) {
        File& f = it->second;
        f.queued = false;
        f.mtimeMs = mtimeMs;
        share(f, h);
        pumpSession();
    }
}

Ed3kShared::Hashed& Ed3kShared::remember(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed)
{
    Hashed& h = m_cache[md4];
    h = Hashed{};
    h.path = path;
    h.size = hashed.size;
    h.mtimeMs = mtimeMs;
    h.root = hashed.root;
    h.hashes = hashed.hashes;
    h.inRam = true;
    // on disk before the index names it; a failed store keeps it in RAM (hashed again next start)
    if (!storeHashes(h, std::span<const std::byte>{h.hashes}))
        logWarning(QStringLiteral("cannot store block hashes of \"%1\" in %2").arg(QFileInfo(path).fileName(), hashesPath()));
    // was: scheduleSave(kOwner);
    // the index of 16k files is MBs: rewritten every 30 s while hashing, at stop() anyway
    scheduleSave(kOwner, kHashRunSaveDelayMs);
    return h;
}

} // namespace eMule::ed3k
