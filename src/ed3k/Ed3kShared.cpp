/// @file Ed3kShared.cpp
/// @brief eMule's shared files served over ed3k — implementation.

#include "Ed3kShared.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "files/KnownFile.h"
#include "files/SharedFileList.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstring>
#include <utility>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

constexpr quint32 kCacheMagic = 0x45335348;   // "E3SH"
constexpr quint32 kCacheVersion = 1;
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
    connect(&m_priorityTimer, &QTimer::timeout, this, &Ed3kShared::refreshPriorities);
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
    connect(&list, &SharedFileList::fileChanged, this, &Ed3kShared::onFileChanged);
}

void Ed3kShared::start()
{
    if (m_started || !m_enabled || !m_session.isRunning())
        return;
    beginStart();
    m_hashing = false;
    loadCache();
    for (const SharedEntry& e : m_source.files())
        add(e);
    logInfo(QStringLiteral("%1 file(s) of the share list, %2 to hash").arg(m_files.size()).arg(m_hashQueue.size()));
    hashNext();
    m_priorityTimer.start();
    pumpSession();
}

void Ed3kShared::stop()
{
    if (!m_started)
        return;
    m_priorityTimer.stop();
    saveCache();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [md4, f] : m_files) {
            if (!f.shared)
                continue;
            if (!f.alias.empty())
                (void)e->unpublishAlias(t, std::span{&f.alias, 1});
            if (const auto c = m_cache.find(md4); c != m_cache.end())
                (void)e->unpublish(t, c->second.root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
        }
    }
    closeAll();
    m_files.clear();
    m_members.clear();
    m_hashQueue.clear();
    m_hashing = false;
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

QString Ed3kShared::cachePath() const
{
    return joinDir(QStringLiteral("shared.dat"));
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
    m_cache.erase(md4);
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
    if (const auto it = m_files.find(entry.md4); it != m_files.end()) {
        if (it->second.entry.path != entry.path)
            onFileRelocated(entry);
        return;
    }
    const QFileInfo fi(entry.path);
    if (!fi.isFile() || std::uint64_t(fi.size()) != entry.size) {
        logDebug(QStringLiteral("\"%1\" is not what the share list says, left out").arg(entry.path));
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
                (void)e->unpublish(t, c->second.root, f.keywords ? ffi::Publish::Both : ffi::Publish::Source);
            }
            unshare(c->second.root);
        }
    }
    m_members.erase(f.member);
    m_files.erase(it);
    // a queued hash of it finds no file and is skipped
}

void Ed3kShared::share(File& f, const Hashed& h)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const std::string name = utf8(f.entry.name);
    const auto ed2k = ed2kOf(f.entry.md4);
    // the MD4 is eMule's of the same file (the share list checks size and date)
    if (!shareMember(ffi::SharedFile{h.root, h.size, name, ed2k, std::nullopt, ffi::Have::complete(), std::span{h.hashes}}, kOwner,
                     f.member)) {
        logWarning(QStringLiteral("cannot share \"%1\"").arg(f.entry.name));
        return;
    }
    f.shared = true;
    const auto t = m_session.now();
    const ffi::FileInfo info{h.root, h.size, name, ed2k, 0, 0, 0, 0, 0, 0};
    // was: ToDo: keywords (Publish::Both) for files the user wants found over ed3k search
    // (done: every shared file, as eD2K's share list; only the source while holding back)
    // if (auto ok = e->publish(t, info, ffi::Publish::Source); !ok)
    const auto mode = m_holdBack ? ffi::Publish::Source : ffi::Publish::Both;
    if (auto ok = e->publish(t, info, mode); !ok)
        logDebug(QStringLiteral("publish of \"%1\" failed (error %2)").arg(f.entry.name, errorName(ok.error())));
    else
        f.keywords = mode == ffi::Publish::Both;
    // alias.md §3: the root is shared here and both names come from these bytes
    if (!m_holdBack) {
        f.alias = ed2kAlias(f.entry.md4, h.size, h.root, f.entry.name);
        if (auto ok = e->publishAlias(t, std::span{&f.alias, 1}); !ok) {
            logDebug(QStringLiteral("ed2k record of \"%1\" not published (error %2)").arg(f.entry.name, errorName(ok.error())));
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
    m_hashQueue.push_back(f.entry.md4);
}

void Ed3kShared::hashNext()
{
    while (!m_hashing && m_started && !m_hashQueue.empty()) {
        const QByteArray md4 = m_hashQueue.front();
        m_hashQueue.pop_front();
        const auto it = m_files.find(md4);
        if (it == m_files.end() || !it->second.queued)
            continue;
        File& f = it->second;
        f.queued = false;
        m_hashing = true;
        ++m_hashCount;
        const QString path = f.entry.path;
        const qint64 mtime = f.mtimeMs;
        hashInBackground(path, [this, md4, path, mtime](std::optional<FileHash> hashed) {
            m_hashing = false;
            onHashed(md4, path, mtime, hashed);
            hashNext();
            pumpSession();
        });
    }
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
        logDebug(QStringLiteral("\"%1\" changed while hashed, left out").arg(path));
        return;
    }
    Hashed& h = m_cache[md4];
    h = Hashed{path, hashed->size, mtimeMs, hashed->root, hashed->hashes};
    scheduleSave(kOwner);
    share(f, h);
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
    if (blob.isEmpty())
        return;
    QDataStream in(blob);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0, count = 0;
    in >> magic >> version >> count;
    if (magic != kCacheMagic || version != kCacheVersion || count > kMaxCacheEntries) {
        logWarning(QStringLiteral("%1 is not a hash cache, ignored").arg(cachePath()));
        return;
    }
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        QByteArray md4, root, hashes;
        Hashed h;
        quint64 size = 0;
        in >> md4 >> h.path >> size >> h.mtimeMs >> root >> hashes;
        if (in.status() != QDataStream::Ok || md4.size() != 16 || root.size() != qsizetype(h.root.bytes.size()))
            break;
        h.size = size;
        std::memcpy(h.root.bytes.data(), root.constData(), h.root.bytes.size());
        const auto* b = reinterpret_cast<const std::byte*>(hashes.constData());
        h.hashes.assign(b, b + hashes.size());
        m_cache[md4] = std::move(h);
    }
    if (in.status() != QDataStream::Ok)
        logWarning(QStringLiteral("%1 is cut short, %2 entries kept").arg(cachePath()).arg(m_cache.size()));
}

void Ed3kShared::saveCache()
{
    // entries of files not listed (yet) stay while their path exists: the list may load later
    std::erase_if(m_cache, [&](const auto& c) { return !m_files.contains(c.first) && !QFileInfo::exists(c.second.path); });
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kCacheMagic << kCacheVersion << quint32(m_cache.size());
    for (const auto& [md4, h] : m_cache) {
        const QByteArray root(reinterpret_cast<const char*>(h.root.bytes.data()), qsizetype(h.root.bytes.size()));
        const QByteArray hashes(reinterpret_cast<const char*>(h.hashes.data()), qsizetype(h.hashes.size()));
        out << md4 << h.path << quint64(h.size) << h.mtimeMs << root << hashes;
    }
    QDir().mkpath(stateDir(m_configDir));
    if (!writeBlob(cachePath(), spanOf(blob)))
        logWarning(QStringLiteral("cannot write %1").arg(cachePath()));
    dropDirty(kOwner);
}

} // namespace eMule::ed3k
