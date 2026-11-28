/// @file Ed3kShared.cpp
/// @brief eMule's shared files served over ed3k — implementation.

#include "Ed3kShared.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "utils/Opcodes.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/FileDate.h"
#include "utils/OwnerOnly.h"
#include "utils/SystemMemory.h"
#include "utils/ArgStr.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

constexpr quint32 kIndexMagic = 0x45335349;   // "E3SI", shared-index.dat
// was: 2; 3 adds the eD2K hash of path-keyed entries
constexpr quint32 kIndexVersion = 3;
/// head of shared-hashes.dat; offsets count from the file start
constexpr char kHashesMagic[] = "E3SHASH1";
constexpr qint64 kHashesMagicLen = 8;
/// rewritten when the dead values are at least this much and more than the live ones
constexpr qint64 kCompactMinDead = 4 << 20;
constexpr qint64 kCopyChunk = 1 << 20;
// was: kNoteRepublishMs (20 h): packmule stores a kept note again by itself
/// Note.nick and the media tags at most (Protocol.md §10.7, §5.4)
constexpr qsizetype kNickMax = 64;
constexpr qsizetype kMediaTagMax = 128;
constexpr qsizetype kCodecMax = 32;
/// the index while hashing: rewritten at most this often (values are on disk at once)
constexpr int kHashRunSaveDelayMs = 30'000;
/// unlisted cache entries checked for their file at most this often (off the main thread)
constexpr qint64 kPruneEveryMs = 60 * 60 * 1000;
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

/// none for an ed3k-only file (its key is no eD2K hash) or a path-keyed one
std::optional<ffi::Ed2kHash> ed2kOf(const SharedEntry& e)
{
    if (e.md4.size() != 16 || e.ed3kOnly)
        return std::nullopt;
    ffi::Ed2kHash h{};
    std::memcpy(h.data(), e.md4.constData(), h.size());
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
    // own republish path for a list change (onDeclinedChanged): no FileInfo kept per file
    setKeepPublishInfo(false);
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
    m_aliasRetry.setSingleShot(true);
    m_aliasRetry.setInterval(5000);
    m_stoppedSave.setSingleShot(true);
    m_stoppedSave.setInterval(kHashRunSaveDelayMs);
    connect(&m_stoppedSave, &QTimer::timeout, this, &Ed3kShared::flushStoppedSave);
    connect(&m_aliasRetry, &QTimer::timeout, this, [this] {
        for (auto& [key, f] : m_files)
            if (f.aliasOnly && f.aliasWaits)
                if (const auto c = m_cache.find(key); c != m_cache.end())
                    publishAliasOnly(f, c->second);
        pumpSession();
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
    SharedEntry e{QByteArray(reinterpret_cast<const char*>(file.fileHash()), 16), file.filePath(), file.fileName(),
                  static_cast<uint64>(file.fileSize()), priority};
    // the comment getters load fileinfo.ini once (lazily), hence not const
    auto& f = const_cast<KnownFile&>(file);
    e.rating = std::min<std::uint32_t>(f.getFileRating(), 5);
    // a note's text is at most 512 bytes of UTF-8 (§10.3); eMule's comment limit is 128 characters
    // was: while (e.comment.toUtf8().size() > 512) e.comment.chop(1);
    e.comment = clipUtf8(f.getFileComment(), 512);
    e.duration = file.getIntTagValue(FT_MEDIA_LENGTH);
    e.bitrate = file.getIntTagValue(FT_MEDIA_BITRATE);
    e.artist = clipUtf8(file.getStrTagValue(FT_MEDIA_ARTIST), kMediaTagMax);
    e.album = clipUtf8(file.getStrTagValue(FT_MEDIA_ALBUM), kMediaTagMax);
    e.title = clipUtf8(file.getStrTagValue(FT_MEDIA_TITLE), kMediaTagMax);
    e.codec = clipUtf8(file.getStrTagValue(FT_MEDIA_CODEC), kCodecMax);
    e.ed3kOnly = file.isEd3kOnly();
    e.fileDate = file.utcFileDate();
    return e;
}

QString Ed3kShared::clipUtf8(const QString& s, qsizetype bytes)
{
    QString out = s;
    while (out.toUtf8().size() > bytes) {
        out.chop(1);
        // not half a surrogate pair
        if (!out.isEmpty() && out.back().isHighSurrogate())
            out.chop(1);
    }
    return out;
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
    flushStoppedSave();   // primed while stopped: on disk before the reload
    loadCache();
    for (const SharedEntry& e : m_source.files())
        add(e);
    for (auto it = m_aliasOnly.cbegin(); it != m_aliasOnly.cend(); ++it)
        add(SharedEntry{it.key(), it.value(), QFileInfo(it.value()).fileName(), std::uint64_t(QFileInfo(it.value()).size())}, true);
    // a disk read folder by folder: fewer seeks between small files
    for (auto& [volume, queue] : m_hashQueues)
        std::ranges::sort(queue, {}, [this](const QByteArray& md4) {
            const auto it = m_files.find(md4);
            return it == m_files.end() ? QString() : it->second.entry.path;
        });
    logInfo(QStringLiteral("%1 file(s) of the share list, %2 to hash on %3 disk(s)")
                .arg(argStr(m_files.size()), argStr(hashQueueSize()), argStr(m_hashQueues.size())));
    m_sideOn->store(true);
    hashNext();
    m_hashRamSent.reset();
    applyHashRamBudget();
    m_priorityTimer.start();
    pumpSession();
}

void Ed3kShared::stop()
{
    flushStoppedSave();
    if (!m_started)
        return;
    m_priorityTimer.stop();
    m_aliasRetry.stop();
    m_sideOn->store(false);
    saveCache();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [md4, f] : m_files) {
            if (!f.alias.empty())
                (void)e->unpublishAlias(t, std::span{&f.alias, 1});
            if (!f.shared)
                continue;
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
    flushBlobWrites();   // index on disk once stopped (see Ed3kSession::stop)
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

void Ed3kShared::forEachSharedRoot(const std::function<void(const QByteArray&, const ffi::Root&)>& fn) const
{
    for (const auto& [md4, f] : m_files) {
        if (!f.shared)
            continue;
        if (const auto c = m_cache.find(md4); c != m_cache.end())
            fn(md4, c->second.root);
    }
}

bool Ed3kShared::reopen(const ffi::Root& root)
{
    for (auto& [md4, f] : m_files) {
        if (!f.shared)
            continue;
        const auto c = m_cache.find(md4);
        if (c == m_cache.end() || c->second.root != root)
            continue;
        // shareMember() only updates a root it holds: forget it so fileOpen runs again
        forgetShare(root);
        f.shared = false;
        share(f, c->second);
        pumpSession();
        return f.shared;
    }
    return false;
}

bool Ed3kShared::holdsRoot(const ffi::Root& root) const
{
    return std::ranges::any_of(m_files, [&](const auto& kv) {
        if (kv.second.aliasOnly)
            return false;   // its join holds it
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

std::optional<ffi::KeyKind> Ed3kShared::keyOfRoot(const ffi::Root& root) const
{
    return holdsRoot(root) ? std::optional(keyOf(root)) : std::nullopt;
}

std::vector<ffi::Root> Ed3kShared::rootsOfListed(const DeclineList& list) const
{
    std::vector<ffi::Root> out;
    if (list.ed2k.empty())
        return out;
    for (const auto& [md4, f] : m_files)
        if (list.listsEd2k(md4))
            if (const auto c = m_cache.find(md4); c != m_cache.end())
                out.push_back(c->second.root);
    return out;
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
    // was: "... is under the anonymous key and on %2 too: sources there show this address (hops.md §1). Remove it
    // there or move it to the node key."
    logWarning(QStringLiteral("\"%1\" is shared on ed3k under the anonymous key but also on %2, where peers see "
                              "this computer's address next to it. Stop it there, or move it to the node key in "
                              "Shared Files.").arg(name, networks));
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

    QByteArray root() override
    {
        if (!done())
            return {};
        return QByteArray(reinterpret_cast<const char*>(m_done->root.bytes.data()), qsizetype(m_done->root.bytes.size()));
    }

    void finish(const QByteArray& md4, const QString& path, quint64 size, qint64 mtimeMs) override
    {
        if (!done() || m_done->size != size)
            return;
        FileHash hashed = std::move(*m_done);
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
    /// The hash once all bytes are in (root() and finish() both ask)
    bool done()
    {
        if (!m_done && m_ok) {
            m_ok = false;   // finished once
            if (auto d = m_hasher.finish()) {
                const auto bytes = d->hashes.bytes();
                m_done = FileHash{d->root, d->size, ffi::Bytes(bytes.begin(), bytes.end())};
            }
        }
        return m_done.has_value();
    }

    QPointer<Ed3kShared> m_owner;
    ffi::Hasher m_hasher;
    bool m_ok = true;
    std::optional<FileHash> m_done;
};

SideHasherFactory Ed3kShared::sideHasherFactory(quint64 pipeFrom)
{
    QPointer<Ed3kShared> self(this);
    return [self, on = m_sideOn, pipeFrom](const QString& path, quint64 size, bool rootWanted) -> std::unique_ptr<SideHash> {
        Q_UNUSED(path)
        // a hasher while serving, or for an ed3k-only file's key even with ed3k off
        // (size 0: never shared)
        if ((!on->load() && !rootWanted) || size == 0)
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
    // a rename keeps size and date: the root stays, reads follow the new path. The share
    // list's date (stat'ed off the main thread) answers it without a stat here (#200)
    const bool sameDate = entry.fileDate != 0 && f.mtimeMs != kMtimeUnknown && entry.size == f.entry.size
        && time_t(f.mtimeMs / 1000) == entry.fileDate;
    if (sameDate || unchanged(entry.path, f.entry.size, f.mtimeMs)) {
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
                .arg(argStr(m_files.size()), argStr(hashQueueSize() + m_hashingVolumes.size())));
    // before it most entries are unlisted: nothing worth a check
    m_prunedAtMs = 0;
    pruneCacheLater();
}

void Ed3kShared::onFileChanged(const QByteArray& md4)
{
    if (!m_started || m_files.contains(md4))
        return;
    if (auto e = m_source.find(md4))
        onFileAdded(*e);
}

QByteArray Ed3kShared::pathKeyOf(const QString& path)
{
    return QByteArray(1, '\x01') + SharedFileList::pathKey(path).toUtf8();
}

void Ed3kShared::setEd2kHashing(bool on)
{
    if (m_ed2kHashing == on)
        return;
    m_ed2kHashing = on;
    if (!on || !m_started)
        return;
    // alias-only files waiting for an eD2K hash: read now
    for (auto& [key, f] : m_files) {
        if (!f.aliasOnly || !f.alias.empty())
            continue;
        if (const auto c = m_cache.find(key); c == m_cache.end() || c->second.ed2k.size() != 16)
            enqueueHash(f);
    }
    hashNext();
}

QByteArray Ed3kShared::aliasOnlyEd2k(const QString& path) const
{
    const auto c = m_cache.find(pathKeyOf(path));
    return c == m_cache.end() ? QByteArray() : c->second.ed2k;
}

void Ed3kShared::setAliasOnlyFiles(const QStringList& paths)
{
    QHash<QByteArray, QString> next;
    for (const QString& p : paths)
        next.insert(pathKeyOf(p), p);
    if (m_started) {
        for (auto it = m_aliasOnly.cbegin(); it != m_aliasOnly.cend(); ++it)
            if (!next.contains(it.key()))
                remove(it.key());
        for (auto it = next.cbegin(); it != next.cend(); ++it)
            if (!m_files.contains(it.key()))
                add(SharedEntry{it.key(), it.value(), QFileInfo(it.value()).fileName(), std::uint64_t(QFileInfo(it.value()).size())}, true);
    }
    m_aliasOnly = std::move(next);
    if (m_started) {
        hashNext();
        pumpSession();
    }
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
    if (owner != kOwner)
        return;
    saveCache();
    pruneCacheLater();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kShared::add(const SharedEntry& entry, bool aliasOnly)
{
    const bool byPath = entry.md4.startsWith('\x01');
    if ((entry.md4.size() != 16 && !byPath) || entry.size == 0 || entry.path.isEmpty())
        return;
    // #12 MUST: an opened plain copy is not served (its sealed copy is). An alias-only file
    // is a landed one kept out on purpose (the caller leaves plain copies out)
    if (!aliasOnly && m_neverShared && m_neverShared(entry.path))
        return;
    if (const auto it = m_files.find(entry.md4); it != m_files.end()) {
        if (it->second.entry.path != entry.path)
            onFileRelocated(entry);
        return;
    }
    // was: a stat per file here: thousands on a NAS froze the core for seconds (#200).
    // The share list stat'ed it off the main thread: its date picks the cached one
    qint64 mtimeMs = kMtimeUnknown;
    if (const auto known = listedDateMs(entry, byPath, aliasOnly)) {
        mtimeMs = *known;
    } else if (entry.fileDate == 0 || hasCacheFor(entry, byPath)) {
        // unknown date, or a cached one off by a DST hour: stat'ed as before
        const QFileInfo fi(entry.path);
        if (!fi.isFile() || std::uint64_t(fi.size()) != entry.size) {
            logDebug([&]() -> QString { return QStringLiteral("\"%1\" is not what the share list says, left out").arg(entry.path); });
            return;
        }
        mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    }
    // else: nothing cached, hashed: hashNext stats it
    File& f = m_files[entry.md4];
    f.entry = entry;
    f.aliasOnly = aliasOnly;
    f.mtimeMs = mtimeMs;
    if (!aliasOnly) {
        f.member = m_nextMember++;
        m_members[f.member] = entry.md4;
    }
    // hashed before without an MD4 (eD2K was disabled): the same root, no second read
    if (!byPath && !m_cache.contains(entry.md4))
        if (const auto p = m_cache.find(pathKeyOf(entry.path));
            p != m_cache.end() && !m_aliasOnly.contains(p->first) && cacheMatches(p->second, entry.path, entry.size, f.mtimeMs)) {
            Hashed moved = std::move(p->second);
            moved.ed2k.clear();
            m_cache.erase(p);
            m_cache[entry.md4] = std::move(moved);
            scheduleSave(kOwner);
        }
    const auto c = m_cache.find(entry.md4);
    if (aliasOnly) {
        if (c != m_cache.end() && cacheMatches(c->second, entry.path, entry.size, f.mtimeMs) && c->second.ed2k.size() == 16)
            publishAliasOnly(f, c->second);
        else if (m_ed2kHashing)
            enqueueHash(f);   // read for its eD2K hash (and root)
        return;
    }
    // was: exact path + exact ms date: a moved file, other path case or a NAS/FAT date shift
    // hashed the whole share again at each start (#200)
    if (c != m_cache.end() && cacheMatches(c->second, entry.path, entry.size, f.mtimeMs)) {
        if (c->second.path != entry.path || c->second.mtimeMs != f.mtimeMs) {
            c->second.path = entry.path;
            c->second.mtimeMs = f.mtimeMs;
            scheduleSave(kOwner);
        }
        share(f, c->second);
    } else
        enqueueHash(f);
}

void Ed3kShared::remove(const QByteArray& md4)
{
    const auto it = m_files.find(md4);
    if (it == m_files.end())
        return;
    File& f = it->second;
    withdrawAlias(f);
    if (f.shared) {
        if (const auto c = m_cache.find(md4); c != m_cache.end()) {
            if (m_session.engine()) {
                // was: if (!f.alias.empty()) (void)e->unpublishAlias(t, std::span{&f.alias, 1}); (withdrawAlias)
                // was: (void)e->unpublish(t, c->second.root, ...) (now under its key)
                unpublishShared(c->second.root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
            }
            unshare(c->second.root);
        }
    }
    if (f.member >= 0)
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
    const auto ed2k = anon ? std::nullopt : ed2kOf(f.entry);
    // the MD4 is eMule's of the same file (the share list checks size and date)
    // was: if (!shareMember(ffi::SharedFile{h.root, h.size, name, ed2k, std::nullopt, ffi::Have::complete(), std::span{hashes}}, kOwner,
    // FileStatus.note: the uploader's rating and comment (§12.2)
    std::optional<ffi::Note> note;
    if (f.entry.hasNote())
        note = ffi::Note{f.entry.rating, utf8(f.entry.comment), noteNick(h.root)};
    // was: ffi::SharedFile file{h.root, h.size, name, ed2k, std::nullopt, ffi::Have::complete(), hashes};
    ffi::SharedFile file{h.root, h.size, name, ed2k, note, ffi::Have::complete(), hashes};
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
    publishFile(f, h.root, h.size);
    emit fileShared(f.entry.md4);
}

void Ed3kShared::publishFile(File& f, const ffi::Root& root, std::uint64_t size)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const std::string name = utf8(f.entry.name);
    const bool anon = keyOf(root) == ffi::KeyKind::Anon;
    const auto ed2k = anon ? std::nullopt : ed2kOf(f.entry);
    const auto t = m_session.now();
    // sources 1: this node, the file held complete (Protocol.md §10.7)
    // was: const ffi::FileInfo info{h.root, h.size, name, ed2k, 1, 0, 0, 0, 0, 0};
    ffi::FileInfo info{root, size, name, ed2k, 1, 0, f.entry.duration, f.entry.bitrate, 0, 0};
    info.artist = utf8(f.entry.artist);
    info.album = utf8(f.entry.album);
    info.title = utf8(f.entry.title);
    info.codec = utf8(f.entry.codec);
    // was: ToDo: keywords (Publish::Both) for files the user wants found over ed3k search
    // (done: every shared file, as eD2K's share list; only the source while holding back)
    // if (auto ok = e->publish(t, info, ffi::Publish::Source); !ok)
    // const auto mode = m_holdBack ? ffi::Publish::Source : ffi::Publish::Both;
    // if (auto ok = e->publish(t, info, mode); !ok)
    // declined-records.md §2: a root or eD2K hash of decline.txt is served, never published
    // (no source, keywords, note, ALIAS). was: ToDo: a list change takes back what is out
    // already (done: onDeclinedChanged withdraws, and publishes again once it is off the list)
    const bool declined = m_session.declined().listsFile(root, f.entry.md4);
    f.declined = declined;
    // a name without a keyword: its source record alone (publishShared)
    if (declined) {
        logInfo(QStringLiteral("\"%1\" shared, not published: declined (%2)").arg(f.entry.name, QString::fromLatin1(kDeclineFile)));
        // listed by its MD4 alone: its root goes to the engine too, which refuses its uploads
        if (!m_session.declined().listsRoot(root))
            m_session.refreshDeclined();
    } else if (auto ok = publishShared(info); !ok)
        logDebug([&]() -> QString { return QStringLiteral("publish of \"%1\" failed (error %2)").arg(f.entry.name, errorName(ok.error())); });
    else
        f.keywords = *ok == ffi::Publish::Both;
    publishNote(f, root);
    // hops.md §1 SHOULD: every file here is in eD2K's share list too, its sources there show
    // the address and tie the anonymous key to it: the user is told once
    // was: ToDo: the same for a file under the anonymous key downloaded over eD2K or a hoster
    // (done: DaemonApp, DownloadQueue::fileAdded → tellDownloadElsewhere; Ed3kDownloads::add
    // of an anonymous root → setAnonAddedHook → tellExposed)
    // was: ToDo: BitTorrent / HTTP: a file under the anonymous key downloaded or seeded over
    // BitTorrent (TorrentSession add, by its ed3k root/MD4) or a plain HTTP download is not
    // checked; DaemonApp's anon hooks (next to setAnonAddedHook) are the place
    // (done for BitTorrent: DaemonApp, Ed3kTorrentJoin::memberVerified of a root held here
    // under the anonymous key, and setAnonAddedHook asks Ed3kTorrentJoin::holdsRoot)
    // ToDo: a plain HTTP download (not a hoster item) of such a file is still not checked
    // eD2K off (Options › Extended, setEd2kHashing): nothing serves it there, nothing to tell (#232)
    if (anon && m_ed2kHashing)
        (void)tellExposed(root, f.entry.name, QStringLiteral("eD2K/Kad"));
    // alias.md §3: the root is shared here and both names come from these bytes; never for a
    // file of the anonymous key (ffi.md: eD2K is never anonymous)
    // was: if (!m_holdBack) {
    if (!m_holdBack && !anon && !declined && ed2kOf(f.entry)) {
        f.alias = ed2kAlias(f.entry.md4, size, root, f.entry.name);
        if (auto ok = e->publishAlias(t, std::span{&f.alias, 1}); !ok) {
            logDebug([&]() -> QString { return QStringLiteral("ed2k record of \"%1\" not published (error %2)").arg(f.entry.name, errorName(ok.error())); });
            f.alias.clear();
        }
    }
}

void Ed3kShared::onDeclinedChanged()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto& d = m_session.declined();
    const auto t = m_session.now();
    int out = 0;
    int again = 0;
    for (auto& [md4, f] : m_files) {
        const auto c = m_cache.find(md4);
        if (!f.shared || c == m_cache.end())
            continue;
        const ffi::Root root = c->second.root;
        const bool listed = d.listsFile(root, md4);
        if (listed == f.declined)
            continue;
        if (listed) {
            if (!f.alias.empty())
                (void)e->unpublishAlias(t, std::span{&f.alias, 1});
            f.alias.clear();
            unpublishShared(root, ffi::Publish::Both);
            f.keywords = false;
            f.declined = true;
            publishNote(f, root);   // listed: its note taken back
            ++out;
        } else {
            publishFile(f, root, c->second.size);
            ++again;
        }
    }
    if (out > 0 || again > 0)
        logInfo(QStringLiteral("decline list changed: %1 shared file(s) withdrawn, %2 published again").arg(out).arg(again));
    pumpSession();
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
            if (f.mtimeMs == kMtimeUnknown)
                f.mtimeMs = mtimeOf(f.entry.path);   // one stat per hashed file, not per added one
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
            const bool withEd2k = wantsEd2k(f) && m_ed2kHashing;
            hashInBackground(path, [this, md4, path, mtime, fileTimer, volume](std::optional<FileHash> hashed) {
                m_hashingVolumes.erase(volume);
                if (hashed) {
                    ++m_runFiles;
                    m_runBytes += static_cast<qint64>(hashed->size);
                    if (lcEmuleSharedVerbose().isDebugEnabled())
                        logInfo(QStringLiteral("Hashed \"%1\" (%2 bytes) in %3 ms")
                                    .arg(argStr(QDir::toNativeSeparators(path)), argStr(hashed->size), argStr(fileTimer.elapsed())));
                }
                onHashed(md4, path, mtime, hashed);
                hashNext();
                if (m_hashingVolumes.empty())
                    endHashRun();
                pumpSession();
            }, withEd2k);
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
    if (f.aliasOnly) {
        Hashed& h = remember(md4, path, mtimeMs, *hashed);
        h.ed2k = hashed->ed2k;
        publishAliasOnly(f, h);
        return;
    }
    share(f, remember(md4, path, mtimeMs, *hashed));
}

void Ed3kShared::publishAliasOnly(File& f, const Hashed& h)
{
    f.aliasWaits = false;
    auto* e = m_session.engine();
    if (!e || !f.alias.empty() || h.ed2k.size() != 16)
        return;
    // as publishFile: never under the anonymous key, nor declined, nor while holding back
    // the key its join holds the root under (keyOf is the share list's own choice)
    // was: if (m_holdBack || keyOf(h.root) == ffi::KeyKind::Anon || ...)
    if (m_holdBack || m_session.hide().keyOf(h.root) == ffi::KeyKind::Anon || m_session.declined().listsFile(h.root, h.ed2k))
        return;
    const ffi::Bytes alias = ed2kAlias(h.ed2k, h.size, h.root, f.entry.name);
    if (auto ok = e->publishAlias(m_session.now(), std::span{&alias, 1}); ok) {
        f.alias = alias;
        logDebug([&]() -> QString { return QStringLiteral("ed2k record of landed \"%1\" published").arg(f.entry.name); });
    } else if (ok.error() == ffi::Error::State) {
        f.aliasWaits = true;   // its join has not shared the root (yet)
        // once soon (the join shares it right after landing); later with the priority timer
        if (f.aliasTries++ == 0 && !m_aliasRetry.isActive())
            m_aliasRetry.start();
    } else {
        logDebug([&]() -> QString { return QStringLiteral("ed2k record of landed \"%1\" not published (error %2)").arg(f.entry.name, errorName(ok.error())); });
    }
}

void Ed3kShared::withdrawAlias(File& f)
{
    if (f.alias.empty())
        return;
    if (auto* e = m_session.engine())
        (void)e->unpublishAlias(m_session.now(), std::span{&f.alias, 1});
    f.alias.clear();
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
        // a path-keyed one is not in the share list by MD4: taken again from its entry
        std::optional<SharedEntry> again;
        bool aliasOnly = false;
        if (const auto it = m_files.find(md4); it != m_files.end() && md4.size() != 16) {
            again = it->second.entry;
            aliasOnly = it->second.aliasOnly;
        }
        remove(md4);
        m_cache.erase(md4);
        if (again) {
            again->size = std::uint64_t(std::max<qint64>(0, QFileInfo(again->path).size()));
            add(*again, aliasOnly);
        } else if (auto e = m_source.find(md4)) {
            add(*e);
        }
    }
    scheduleSave(kOwner);
    hashNext();
    pumpSession();
}

void Ed3kShared::refreshPriorities()
{
    for (auto& [md4, f] : m_files) {
        if (f.aliasOnly && f.aliasWaits)
            if (const auto c = m_cache.find(md4); c != m_cache.end())
                publishAliasOnly(f, c->second);
        if (!f.shared)
            continue;
        const auto e = m_source.find(md4);
        if (!e)
            continue;
        const auto c = m_cache.find(md4);
        if (e->priority != f.entry.priority) {
            f.entry.priority = e->priority;
            if (c != m_cache.end())
                updateUploadPriority(c->second.root);
        }
        // own note changed: at once (packmule keeps a held one stored)
        // was: also every kNoteRepublishMs
        const bool changed = e->rating != f.entry.rating || e->comment != f.entry.comment;
        f.entry.rating = e->rating;
        f.entry.comment = e->comment;
        if (c != m_cache.end() && changed)
            publishNote(f, c->second.root);
    }
}

void Ed3kShared::publishNote(File& f, const ffi::Root& root)
{
    auto* e = m_session.engine();
    if (!e || !f.shared)
        return;
    const auto t = m_session.now();
    const bool anon = keyOf(root) == ffi::KeyKind::Anon;
    // was: a cleared note was not taken back (no unpublish of one note)
    // was: if (m_holdBack || !f.entry.hasNote()) {
    // declined-records.md §2: no note for a declined root
    if (m_holdBack || !f.entry.hasNote() || m_session.declined().listsFile(root, f.entry.md4)) {
        if (f.notePublished)
            (void)(anon ? e->anonUnpublishNote(t, root) : e->unpublishNote(t, root));
        f.notePublished = false;
        return;
    }
    const ffi::Note note{f.entry.rating, utf8(f.entry.comment), noteNick(root)};
    const auto ok = anon ? e->anonPublishNote(t, root, note) : e->publishNote(t, root, note);
    if (!ok) {
        logDebug([&]() -> QString { return QStringLiteral("note of \"%1\" not published (error %2)").arg(f.entry.name, errorName(ok.error())); });
        f.notePublished = false;
        return;
    }
    f.notePublished = true;
}

std::string Ed3kShared::noteNick(const ffi::Root& root) const
{
    // hops.md §1: nothing under the anonymous key names its owner
    if (!m_noteNick || keyOf(root) == ffi::KeyKind::Anon)
        return {};
    return utf8(clipUtf8(m_noteNick().trimmed(), kNickMax));
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

std::optional<qint64> Ed3kShared::listedDateMs(const SharedEntry& entry, bool byPath, bool aliasOnly) const
{
    if (aliasOnly || entry.fileDate == 0)
        return std::nullopt;
    const auto matches = [&](const Hashed& h) -> std::optional<qint64> {
        if (h.size == entry.size && time_t(h.mtimeMs / 1000) == entry.fileDate)
            return h.mtimeMs;
        return std::nullopt;
    };
    if (const auto c = m_cache.find(entry.md4); c != m_cache.end())
        return matches(c->second);
    if (byPath)
        return std::nullopt;
    // hashed before without an MD4 (add() moves it under the MD4)
    if (const auto p = m_cache.find(pathKeyOf(entry.path)); p != m_cache.end() && !m_aliasOnly.contains(p->first))
        return matches(p->second);
    return std::nullopt;
}

bool Ed3kShared::hasCacheFor(const SharedEntry& entry, bool byPath) const
{
    if (m_cache.contains(entry.md4))
        return true;
    if (byPath)
        return false;
    const auto p = m_cache.find(pathKeyOf(entry.path));
    return p != m_cache.end() && !m_aliasOnly.contains(p->first);
}

qint64 Ed3kShared::mtimeOf(const QString& path)
{
    return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

bool Ed3kShared::cacheMatches(const Hashed& c, const QString& path, std::uint64_t size, qint64 mtimeMs)
{
    if (c.size != size)
        return false;
    if (c.mtimeMs == mtimeMs)
        return true;
    // the MD4 key already names the content: the date as tolerant as known.met's
    return sameFileDate(time_t(c.mtimeMs / 1000), time_t(mtimeMs / 1000), isLocalTimeVolume(QFileInfo(path).absolutePath()));
}

void Ed3kShared::loadCache()
{
    m_cache.clear();
    m_cacheLoaded = true;
    const QByteArray blob = readBlob(cachePath());
    if (blob.isEmpty())
        return;
    QDataStream in(blob);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0, count = 0;
    in >> magic >> version >> count;
    // was: version != kIndexVersion; 2 is read too (no eD2K hash of path-keyed entries)
    if (magic != kIndexMagic || (version != kIndexVersion && version != 2) || count > kMaxCacheEntries) {
        logWarning(QStringLiteral("%1 is not a hash cache, ignored").arg(cachePath()));
        return;
    }
    // values stay on disk: read when shared (share())
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        QByteArray md4, root;
        Hashed h;
        quint64 size = 0;
        in >> md4 >> h.path >> size >> h.mtimeMs >> root >> h.diskOffset >> h.diskLen >> h.diskCheck;
        if (version >= 3)
            in >> h.ed2k;
        // was: md4.size() != 16 (path-keyed entries: pathKeyOf)
        if (in.status() != QDataStream::Ok || (md4.size() != 16 && !md4.startsWith('\x01'))
            || root.size() != qsizetype(h.root.bytes.size()))
            break;
        h.size = size;
        std::memcpy(h.root.bytes.data(), root.constData(), h.root.bytes.size());
        m_cache[md4] = std::move(h);
    }
    if (in.status() != QDataStream::Ok)
        logWarning(QStringLiteral("%1 is cut short, %2 entries kept").arg(argStr(cachePath()), argStr(m_cache.size())));
    compactHashes();
}

void Ed3kShared::saveCache()
{
    // was: std::erase_if(m_cache, ... !QFileInfo::exists(c.second.path)) here: one stat per
    // unlisted entry on the main thread each save (8 s stalls on Windows, #200) → pruneCacheLater()
    const QByteArray blob = indexBlob();
    QDir().mkpath(stateDir(m_configDir));
    writeBlobLater(cachePath(), spanOf(blob));
    dropDirty(kOwner);
}

void Ed3kShared::pruneCacheLater()
{
    // entries of files not listed (yet) stay while their path exists: the list may load later
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_started || !m_scanLogged || m_pruning || (m_prunedAtMs && now - m_prunedAtMs < kPruneEveryMs))
        return;
    std::vector<std::pair<QByteArray, QString>> unlisted;
    for (const auto& [md4, h] : m_cache)
        if (!m_files.contains(md4))
            unlisted.emplace_back(md4, h.path);
    m_prunedAtMs = now;
    if (unlisted.empty())
        return;
    m_pruning = true;
    QPointer<Ed3kShared> self(this);
    QThreadPool::globalInstance()->start([self, unlisted = std::move(unlisted)]() mutable {
        std::erase_if(unlisted, [](const auto& u) { return QFileInfo::exists(u.second); });
        QMetaObject::invokeMethod(
            self.get(),
            [self, gone = std::move(unlisted)] {
                if (self)
                    self->onCachePruned(gone);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kShared::onCachePruned(const std::vector<std::pair<QByteArray, QString>>& gone)
{
    m_pruning = false;
    std::size_t dropped = 0;
    for (const auto& [md4, path] : gone) {
        // listed or hashed again meanwhile: kept
        const auto c = m_cache.find(md4);
        if (c == m_cache.end() || m_files.contains(md4) || c->second.path != path)
            continue;
        m_cache.erase(c);
        ++dropped;
    }
    if (dropped == 0)
        return;
    logDebug([&]() -> QString { return QStringLiteral("%1 cached hash(es) of vanished files dropped").arg(dropped); });
    scheduleSave(kOwner);
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
        out << md4 << h.path << quint64(h.size) << h.mtimeMs << root << h.diskOffset << h.diskLen << h.diskCheck << h.ed2k;
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
                .arg(argStr(m_runFiles), argStr(m_runBytes >> 20), argFmt(static_cast<double>(ms) / 1000.0, 0, 'f', 1)));
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
                .arg(argStr(m_runFiles), argStr(hashQueueSize() + m_hashingVolumes.size()),
                     argFmt(static_cast<double>(m_runBytes >> 20) / secs, 0, 'f', 1)));
}

Ed3kShared::Counts Ed3kShared::counts() const
{
    Counts c{.hashing = m_hashingVolumes.size()};
    for (const auto& [md4, f] : m_files) {
        if (f.aliasOnly)
            continue;   // not served here
        ++c.total;
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
    if (m_neverShared && m_neverShared(path))
        return;
    // was: if (!m_started) return; (an ed3k-only file hashed with ed3k off was read again at start)
    if (!m_started) {
        primeStopped(md4, path, mtimeMs, hashed);
        return;
    }
    if (const auto c = m_cache.find(md4); c != m_cache.end() && c->second.path == path && cacheMatches(c->second, path, hashed.size, mtimeMs))
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

void Ed3kShared::primeStopped(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed)
{
    if (md4.size() != 16)
        return;
    if (!m_cacheLoaded)
        loadCache();   // never started yet: a save must not drop the index on disk
    if (const auto c = m_cache.find(md4); c != m_cache.end() && c->second.path == path && cacheMatches(c->second, path, hashed.size, mtimeMs))
        return;
    if (!unchanged(path, hashed.size, mtimeMs))
        return;
    Hashed& h = remember(md4, path, mtimeMs, hashed);   // its scheduleSave is a no-op while stopped
    // no RAM budget while stopped: the values are read back from disk when shared
    if (h.diskOffset >= 0) {
        ffi::Bytes().swap(h.hashes);
        h.inRam = false;
    }
    m_stoppedDirty = true;
    if (!m_stoppedSave.isActive())
        m_stoppedSave.start();
}

void Ed3kShared::flushStoppedSave()
{
    m_stoppedSave.stop();
    if (!std::exchange(m_stoppedDirty, false))
        return;
    saveCache();
    flushBlobWrites();
}

Ed3kShared::Hashed& Ed3kShared::remember(const QByteArray& md4, const QString& path, qint64 mtimeMs, const FileHash& hashed)
{
    // hashed for eD2K now: an entry of the same file without MD4 is stale
    if (md4.size() == 16)
        if (const QByteArray byPath = pathKeyOf(path); !m_aliasOnly.contains(byPath) && !m_files.contains(byPath))
            m_cache.erase(byPath);
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
