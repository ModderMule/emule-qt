/// @file Ed3kDownloads.cpp
/// @brief Plain ed3k file downloads — implementation.

#include "Ed3kDownloads.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "files/KnownFile.h"
#include "utils/Log.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace {

constexpr quint32 kStateMagic = 0x4533444c;   // "E3DL"
/// 1: every row; 2: the complete rows (the others are in the .met files)
constexpr quint32 kStateVersion = 2;
constexpr quint32 kMetMagic = 0x4533504d;     // "E3PM"
constexpr quint32 kMetVersion = 1;
const QString kMetSuffix = QStringLiteral(".ed3k.part.met");
/// .met target: none (incoming + name), relative to the incoming dir, absolute
enum class TargetKind : quint8 { None = 0, Incoming = 1, Absolute = 2 };
/// a piece of the Have bitmap (Protocol.md §12.5): 4 MiB = 16 blocks
constexpr std::size_t kPieceBlocks = 16;
constexpr int kTickMs = 1000;
const QString kStateFile = QStringLiteral("downloads.dat");

QByteArray rootBytes(const ffi::Root& r)
{
    return QByteArray(reinterpret_cast<const char*>(r.bytes.data()), qsizetype(r.bytes.size()));
}

std::optional<ffi::Root> rootFrom(const QByteArray& b)
{
    ffi::Root r;
    if (b.size() != qsizetype(r.bytes.size()))
        return std::nullopt;
    std::memcpy(r.bytes.data(), b.constData(), r.bytes.size());
    return r;
}

QByteArray packBits(const std::vector<bool>& bits)
{
    QByteArray out(qsizetype((bits.size() + 7) / 8), '\0');
    for (std::size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            out[qsizetype(i / 8)] = char(std::uint8_t(out[qsizetype(i / 8)]) | (1u << (i % 8)));
    return out;
}

std::vector<bool> unpackBits(const QByteArray& packed, std::size_t count)
{
    std::vector<bool> bits(count, false);
    for (std::size_t i = 0; i < count && qsizetype(i / 8) < packed.size(); ++i)
        bits[i] = (std::uint8_t(packed[qsizetype(i / 8)]) >> (i % 8)) & 1u;
    return bits;
}

/// A file name of a link or a search row as a plain name in one directory.
QString safeName(QString name)
{
    name = QFileInfo(name.replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    static const QString bad = QStringLiteral("<>:\"|?*");
    for (QChar& c : name)
        if (c.unicode() < 0x20 || bad.contains(c))
            c = QLatin1Char('_');
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        name.clear();
    return name.trimmed();
}

} // namespace

Ed3kDownloads::Ed3kDownloads(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("dl"), parent)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &Ed3kDownloads::onTick);
    m_tick.start();
    load();
}

Ed3kDownloads::~Ed3kDownloads()
{
    if (m_started)
        stop();
}

void Ed3kDownloads::start()
{
    if (m_started || !m_session.isRunning())
        return;
    beginStart();
    scanTempDirs();   // .met files copied in meanwhile
    for (auto& [root, e] : m_entries) {
        if (e.info.state == Ed3kDownloadInfo::State::Downloading)
            openEntry(e);
        else if (e.info.state == Ed3kDownloadInfo::State::Paused)
            sharePieces(e);
    }
    // the complete ones: hashed again, then shared (keptMemberPath)
    shareKept();
    logInfo(QStringLiteral("%1 download(s)").arg(m_entries.size()));
    markChanged();
    pumpSession();
}

void Ed3kDownloads::stop()
{
    if (!m_started)
        return;
    saveNow();
    closeAll();
    for (auto& [root, e] : m_entries) {
        e.info.rate = 0;
        if (e.info.state == Ed3kDownloadInfo::State::Verifying)
            e.info.state = Ed3kDownloadInfo::State::Downloading;   // hashed again at the next start
    }
    markChanged();
}

void Ed3kDownloads::setDirs(const QStringList& tempDirs, const QString& incomingDir)
{
    m_tempDirs.clear();
    for (const QString& d : tempDirs)
        if (!d.isEmpty() && !m_tempDirs.contains(d))
            m_tempDirs.append(d);
    m_incomingDir = incomingDir;
    scanTempDirs();
    // rows of a version 1 downloads.dat: their .met files now
    if (std::ranges::any_of(m_entries, [](const auto& kv) { return kv.second.metDirty; }))
        saveNow();
}

std::expected<void, QString> Ed3kDownloads::add(const Ed3kDownloadSpec& spec)
{
    if (spec.size == 0)
        return std::unexpected(tr("The ed3k file has no size."));
    if (m_entries.contains(spec.root))
        return std::unexpected(tr("This ed3k file is in the download list already."));
    QString name = safeName(spec.name);
    if (name.isEmpty())
        name = hexOf(spec.root.bytes);
    if (spec.targetPath.isEmpty() && m_incomingDir.isEmpty())
        return std::unexpected(tr("No incoming directory for ed3k downloads."));
    if (m_tempDirs.isEmpty())
        return std::unexpected(tr("No temporary directory for ed3k downloads."));

    Entry e;
    e.id = m_nextId++;
    e.info.root = spec.root;
    e.info.size = spec.size;
    e.info.name = name;
    e.info.owner = spec.owner.isEmpty() ? kOwner : spec.owner;
    e.info.category = spec.category;
    e.info.priority = std::clamp(spec.priority, 0, 4);
    e.info.state = spec.paused ? Ed3kDownloadInfo::State::Paused : Ed3kDownloadInfo::State::Downloading;
    e.info.partPath = partPathOf(spec.root);
    e.info.targetPath = spec.targetPath;
    e.info.path = e.info.partPath;
    e.info.addedOn = QDateTime::currentSecsSinceEpoch();
    e.have.assign(std::size_t(blockCount(spec.size)), false);
    e.metDirty = true;
    // an earlier part file of this root (cancelled, list lost) is not trusted
    QFile::remove(e.info.partPath);
    QFile::remove(metPathOf(e));
    auto [it, inserted] = m_entries.emplace(spec.root, std::move(e));
    logInfo(QStringLiteral("download of \"%1\" (%2 B) added as %3").arg(name).arg(spec.size).arg(hexOf(spec.root.bytes)));
    if (it->second.info.state == Ed3kDownloadInfo::State::Downloading)
        openEntry(it->second);
    saveNow();
    emit added(spec.root);
    markChanged();
    pumpSession();
    return {};
}

std::expected<ffi::Root, QString> Ed3kDownloads::addLink(const QString& link, int category, bool paused)
{
    const QByteArray utf8 = link.trimmed().toUtf8();
    auto parsed = ffi::parseFileLink(std::string_view(utf8.constData(), std::size_t(utf8.size())));
    if (!parsed)
        return std::unexpected(tr("This is not a valid ed3k file link."));
    // torrent.md §4.1: a torrent set goes to the torrent join (the Torrents tab)
    if (std::ranges::any_of(parsed->other, [](const auto& kv) { return kv.first == "bt"; }))
        return std::unexpected(tr("This ed3k link names a torrent; open it in the Torrents tab."));
    Ed3kDownloadSpec spec;
    spec.root = parsed->root;
    spec.size = parsed->size;
    if (parsed->name)
        spec.name = QString::fromUtf8(parsed->name->data(), qsizetype(parsed->name->size()));
    spec.category = category;
    spec.paused = paused;
    if (auto ok = add(spec); !ok)
        return std::unexpected(ok.error());
    return parsed->root;
}

bool Ed3kDownloads::pause(const ffi::Root& root)
{
    Entry* e = entryOf(root);
    if (!e || e->info.state != Ed3kDownloadInfo::State::Downloading)
        return false;
    stopDownload(root);
    e->info.state = Ed3kDownloadInfo::State::Paused;
    e->info.rate = 0;
    touch(*e);
    markChanged();
    return true;
}

bool Ed3kDownloads::resume(const ffi::Root& root)
{
    Entry* e = entryOf(root);
    if (!e || (e->info.state != Ed3kDownloadInfo::State::Paused && e->info.state != Ed3kDownloadInfo::State::Failed))
        return false;
    e->info.state = Ed3kDownloadInfo::State::Downloading;
    e->info.error.clear();
    openEntry(*e);
    touch(*e);
    markChanged();
    pumpSession();
    return true;
}

bool Ed3kDownloads::cancel(const ffi::Root& root)
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end())
        return false;
    Entry& e = it->second;
    if (e.info.state != Ed3kDownloadInfo::State::Complete) {
        stopDownload(root);
        if (isShared(root))
            unshare(root);
        QFile::remove(e.info.partPath);
        QFile::remove(metPathOf(e));
        logInfo(QStringLiteral("download of \"%1\" cancelled").arg(e.info.name));
    } else if (isShared(root)) {
        // a complete file leaves the own share (its reads name the row); the share list
        // (Ed3kShared) keeps serving it when it took it
        unshare(root);
    }
    m_entries.erase(it);
    saveNow();
    emit removed(root);
    markChanged();
    pumpSession();
    return true;
}

bool Ed3kDownloads::setPriority(const ffi::Root& root, int priority)
{
    Entry* e = entryOf(root);
    if (!e)
        return false;
    e->info.priority = std::clamp(priority, 0, 4);
    // the turns at shared sources, the depth and the stream cap follow it
    priorityChanged(root);
    touch(*e);
    markChanged();
    return true;
}

bool Ed3kDownloads::setCategory(const ffi::Root& root, int category)
{
    Entry* e = entryOf(root);
    if (!e)
        return false;
    e->info.category = std::max(0, category);
    touch(*e);
    markChanged();
    return true;
}

bool Ed3kDownloads::setOwner(const ffi::Root& root, const QString& owner)
{
    Entry* e = entryOf(root);
    if (!e || owner.isEmpty() || e->info.state == Ed3kDownloadInfo::State::Complete)
        return false;
    if (e->info.owner == owner)
        return true;
    // the base keys streams and shares by owner: closed, opened again under the new one
    const bool running = downloading(root);
    if (running)
        stopDownload(root);
    if (isShared(root))
        unshare(root);
    e->info.owner = owner;
    if (running)
        openEntry(*e);
    else
        sharePieces(*e);
    touch(*e);
    markChanged();
    pumpSession();
    return true;
}

void Ed3kDownloads::clearCompleted()
{
    std::vector<ffi::Root> done;
    for (const auto& [root, e] : m_entries)
        if (e.info.state == Ed3kDownloadInfo::State::Complete)
            done.push_back(root);
    for (const auto& root : done)
        (void)cancel(root);
}

std::vector<Ed3kDownloadInfo> Ed3kDownloads::list() const
{
    std::vector<Ed3kDownloadInfo> out;
    out.reserve(m_entries.size());
    for (const auto& [root, e] : m_entries)
        if (auto i = info(root))
            out.push_back(std::move(*i));
    return out;
}

std::optional<Ed3kDownloadInfo> Ed3kDownloads::info(const ffi::Root& root) const
{
    const auto it = m_entries.find(root);
    if (it == m_entries.end())
        return std::nullopt;
    Ed3kDownloadInfo i = it->second.info;
    i.blocks = it->second.have;
    if (const Download* d = downloadOf(root)) {
        i.sources = int(d->streams.size());
        i.transferring = int(std::ranges::count_if(d->streams, [](const auto& s) { return s.second; }));
        i.pending.assign(i.blocks.size(), false);
        for (const auto& [offset, stream] : d->outstanding)
            if (const auto b = std::size_t(offset / kBlock); b < i.pending.size())
                i.pending[b] = true;
        i.availability = pieceAvailability(*d);
    }
    return i;
}

void Ed3kDownloads::releaseShare(const ffi::Root& root)
{
    Entry* e = entryOf(root);
    if (!e || e->info.state != Ed3kDownloadInfo::State::Complete)
        return;
    if (isShared(root))
        forgetShare(root);
    if (!e->sharedByList) {
        e->sharedByList = true;
        logDebug(QStringLiteral("\"%1\" is served by the share list now").arg(e->info.name));
        touch(*e);
    }
}

QString Ed3kDownloads::statePath() const
{
    return QDir(stateDir(m_configDir)).filePath(kStateFile);
}

void Ed3kDownloads::saveNow()
{
    QByteArray raw;
    {
        QDataStream out(&raw, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        const auto complete = std::ranges::count_if(
            m_entries, [](const auto& kv) { return kv.second.info.state == Ed3kDownloadInfo::State::Complete; });
        out << kStateMagic << kStateVersion << qint32(m_nextId) << quint32(complete);
        for (const auto& [root, e] : m_entries) {
            if (e.info.state != Ed3kDownloadInfo::State::Complete)
                continue;
            const auto& i = e.info;
            out << rootBytes(root) << quint64(i.size) << i.name << i.owner << qint32(i.category) << qint32(i.priority)
                << qint32(int(i.state)) << i.partPath << i.targetPath << i.path << packBits(e.have) << qint64(i.addedOn)
                << qint64(i.completedOn) << e.sharedByList << i.error << qint32(e.id);
        }
    }
    QDir().mkpath(stateDir(m_configDir));
    if (!writeBlob(statePath(), spanOf(raw)))
        logWarning(QStringLiteral("cannot write %1").arg(statePath()));
    for (auto& [root, e] : m_entries)
        if (e.metDirty && e.info.state != Ed3kDownloadInfo::State::Complete)
            writeMet(e);
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

std::vector<std::uint64_t> Ed3kDownloads::wantedBlocks(const Download& d) const
{
    const Entry* e = entryOfId(d.member);
    if (!e || e->info.state != Ed3kDownloadInfo::State::Downloading)
        return {};
    std::vector<std::uint64_t> wanted;
    for (std::size_t i = 0; i < e->have.size(); ++i)
        if (!e->have[i])
            wanted.push_back(std::uint64_t(i) * kBlock);
    return wanted;
}

void Ed3kDownloads::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Entry* e = entryOfId(d.member);
    if (!e || e->info.state != Ed3kDownloadInfo::State::Downloading || b.offset % kBlock != 0)
        return;
    const std::uint64_t index = b.offset / kBlock;
    if (index >= e->have.size() || e->have[std::size_t(index)])
        return;
    const QString path = e->info.partPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite) || !f.seek(qint64(b.offset))
        || f.write(reinterpret_cast<const char*>(b.data.data()), qint64(b.data.size())) != qint64(b.data.size())) {
        fail(*e, QStringLiteral("cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    f.close();
    e->have[std::size_t(index)] = true;
    e->info.done = std::min(e->info.size, e->info.done + std::uint64_t(b.data.size()));
    e->received += std::uint64_t(b.data.size());
    emit progress(e->info.root, e->info.done);
    markChanged();
    touch(*e);
    if (std::ranges::all_of(e->have, [](bool h) { return h; })) {
        verify(*e);
        return;
    }
    // a piece just completed: tell the engine (served from the part file)
    if (index / kPieceBlocks != (index + 1) / kPieceBlocks || index + 1 == e->have.size())
        sharePieces(*e);
}

void Ed3kDownloads::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    // no blobs are fetched here
    Q_UNUSED(owner) Q_UNUSED(member) Q_UNUSED(root) Q_UNUSED(data)
}

void Ed3kDownloads::onDownloadHashes(const Download& d, const ffi::Bytes& blob)
{
    Entry* e = entryOfId(d.member);
    if (!e || !e->hashes.empty())
        return;
    e->hashes = blob;
    sharePieces(*e);
}

std::optional<Ed3kJoinBase::FileRead> Ed3kDownloads::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    const Entry* e = entryOfId(s.member);
    if (!e || len == 0)
        return std::nullopt;
    if (e->info.state == Ed3kDownloadInfo::State::Complete)
        return FileRead{e->info.path, offset, len, qint64(e->info.size), -1};
    // only verified blocks of the part file
    const std::uint64_t first = offset / kBlock;
    const std::uint64_t last = (offset + len - 1) / kBlock;
    for (std::uint64_t i = first; i <= last; ++i)
        if (i >= e->have.size() || !e->have[std::size_t(i)])
            return std::nullopt;
    return FileRead{e->info.partPath, offset, len};
}

bool Ed3kDownloads::keepsOwner(const QString& owner) const
{
    return std::ranges::any_of(m_entries, [&](const auto& kv) {
        return kv.second.info.owner == owner && kv.second.info.state == Ed3kDownloadInfo::State::Complete;
    });
}

QString Ed3kDownloads::keptMemberPath(const QString& owner, int member) const
{
    const Entry* e = entryOfId(member);
    if (!e || e->info.owner != owner || e->info.state != Ed3kDownloadInfo::State::Complete || e->sharedByList
        || (m_sharedElsewhere && m_sharedElsewhere(e->info.root)))
        return {};
    return e->info.path;
}

bool Ed3kDownloads::hasOwner(const QString& owner) const
{
    return std::ranges::any_of(m_entries, [&](const auto& kv) { return kv.second.info.owner == owner; });
}

void Ed3kDownloads::saveOwner(const QString& owner)
{
    // one file for every owner
    Q_UNUSED(owner)
    saveNow();
}

int Ed3kDownloads::downloadPriority(const Download& d) const
{
    // kPr* to an order: very low, low, normal, high, very high (eD2K's realPriority - 2)
    const Entry* e = entryOfId(d.member);
    // was: switch (priority) { case 4: return -2; case 0: return -1; case 2: return 1; case 3: return 2; default: return 0; }
    return realPriority(std::uint8_t(e ? e->info.priority : kPrNormal)) - 2;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

Ed3kDownloads::Entry* Ed3kDownloads::entryOf(const ffi::Root& root)
{
    const auto it = m_entries.find(root);
    return it == m_entries.end() ? nullptr : &it->second;
}

const Ed3kDownloads::Entry* Ed3kDownloads::entryOfId(int id) const
{
    for (const auto& [root, e] : m_entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

Ed3kDownloads::Entry* Ed3kDownloads::entryOfId(int id)
{
    return const_cast<Entry*>(std::as_const(*this).entryOfId(id));
}

std::uint64_t Ed3kDownloads::blockCount(std::uint64_t size) const
{
    return size / kBlock + (size % kBlock != 0 ? 1 : 0);
}

void Ed3kDownloads::openEntry(Entry& e)
{
    if (!m_started || !m_session.engine() || downloading(e.info.root))
        return;
    if (std::ranges::all_of(e.have, [](bool h) { return h; })) {
        verify(e);   // every block came before the last stop
        return;
    }
    // a part file gone since: what the bitmap says is not there any more
    if (e.info.done > 0 && !QFile::exists(e.info.partPath)) {
        logWarning(QStringLiteral("part file of \"%1\" is gone: downloaded again").arg(e.info.name));
        std::fill(e.have.begin(), e.have.end(), false);  // not ranges: GCC 12 rejects vector<bool>
        e.info.done = 0;
    }
    QDir().mkpath(QFileInfo(e.info.partPath).absolutePath());
    if (!openDownload(e.info.root, e.info.size, e.info.owner, e.id))
        logDebug(QStringLiteral("download of \"%1\" not opened").arg(e.info.name));
    sharePieces(e);
}

void Ed3kDownloads::verify(Entry& e)
{
    e.info.state = Ed3kDownloadInfo::State::Verifying;
    e.info.rate = 0;
    stopDownload(e.info.root);
    markChanged();
    // stale bytes past the end (an older part file) would change the hash
    if (QFile f(e.info.partPath); f.exists() && f.size() != qint64(e.info.size))
        (void)f.resize(qint64(e.info.size));
    const ffi::Root root = e.info.root;
    hashInBackground(e.info.partPath, [this, root](std::optional<FileHash> hashed) { verified(root, hashed); });
}

void Ed3kDownloads::verified(const ffi::Root& root, const std::optional<FileHash>& hashed)
{
    Entry* e = entryOf(root);
    if (!e || e->info.state != Ed3kDownloadInfo::State::Verifying)
        return;   // cancelled meanwhile
    if (!hashed || hashed->root != root || hashed->size != e->info.size) {
        // every block was verified on arrival: the part file changed on disk since
        logWarning(QStringLiteral("\"%1\" does not hash to its root: downloaded again").arg(e->info.name));
        if (isShared(root))
            unshare(root);
        std::fill(e->have.begin(), e->have.end(), false);
        e->info.done = 0;
        QFile::remove(e->info.partPath);
        e->info.state = Ed3kDownloadInfo::State::Downloading;
        openEntry(*e);
        touch(*e);
        markChanged();
        pumpSession();
        return;
    }
    QString error;
    const QString path = moveToTarget(*e, &error);
    if (path.isEmpty()) {
        fail(*e, error);
        return;
    }
    e->info.path = path;
    e->info.state = Ed3kDownloadInfo::State::Complete;
    e->info.completedOn = QDateTime::currentSecsSinceEpoch();
    QFile::remove(metPathOf(*e));
    shareComplete(*e, std::span{hashed->hashes});
    saveNow();
    logInfo(QStringLiteral("\"%1\" complete: %2").arg(e->info.name, path));
    markChanged();
    const Ed3kDownloadInfo done = e->info;
    emit completed(root, path);
    if (m_completedHook)
        m_completedHook(done);
    pumpSession();
}

QString Ed3kDownloads::moveToTarget(const Entry& e, QString* error)
{
    QString target = e.info.targetPath;
    if (target.isEmpty()) {
        const QString dir = e.info.category > 0 && m_categoryDir ? m_categoryDir(e.info.category) : QString();
        target = QDir(dir.isEmpty() ? m_incomingDir : dir).filePath(e.info.name);
    }
    // a target of another machine (an imported .met) that cannot be made here: the incoming dir
    if (!QDir().mkpath(QFileInfo(target).absolutePath()) && !m_incomingDir.isEmpty()) {
        logWarning(QStringLiteral("cannot create the folder of %1: \"%2\" goes to the incoming dir").arg(target, e.info.name));
        target = QDir(m_incomingDir).filePath(e.info.name);
        QDir().mkpath(m_incomingDir);
    }
    target = freePath(target);
    if (QFile::rename(e.info.partPath, target))
        return target;
    // another file system
    if (QFile::copy(e.info.partPath, target)) {
        QFile::remove(e.info.partPath);
        return target;
    }
    if (error)
        *error = QStringLiteral("cannot move %1 to %2").arg(e.info.partPath, target);
    return {};
}

void Ed3kDownloads::shareComplete(Entry& e, std::span<const std::byte> hashes)
{
    auto* eng = m_session.engine();
    if (isShared(e.info.root))
        unshare(e.info.root);   // the part file's pieces; shared again whole below
    if (!eng || (m_sharedElsewhere && m_sharedElsewhere(e.info.root))) {
        e.sharedByList = m_sharedElsewhere && m_sharedElsewhere(e.info.root);
        return;
    }
    const QByteArray n = e.info.name.toUtf8();
    const std::string name(n.constData(), std::size_t(n.size()));
    if (!shareMember(ffi::SharedFile{e.info.root, e.info.size, name, std::nullopt, std::nullopt, ffi::Have::complete(), hashes},
                     e.info.owner, e.id))
        return;
    const ffi::FileInfo fi{e.info.root, e.info.size, name, std::nullopt, 0, 0, 0, 0, 0, 0};
    // was: eng->publish(m_session.now(), fi, m_holdBack ? ffi::Publish::Source : ffi::Publish::Both)
    if (auto ok = publishShared(fi); !ok)
        logDebug(QStringLiteral("publish of \"%1\" failed (error %2)").arg(e.info.name, errorName(ok.error())));
    keepMember(e.info.owner, e.id, e.info.root, e.info.size, e.info.name);
}

void Ed3kDownloads::sharePieces(Entry& e)
{
    // values needed to serve more than one block (§7.2)
    if (!m_started || (e.hashes.empty() && e.have.size() > 1))
        return;
    const std::size_t pieces = (e.have.size() + kPieceBlocks - 1) / kPieceBlocks;
    std::vector<std::uint8_t> bits((pieces + 7) / 8, 0);
    bool any = false;
    for (std::size_t p = 0; p < pieces; ++p) {
        const auto from = e.have.begin() + std::ptrdiff_t(p * kPieceBlocks);
        const auto to = e.have.begin() + std::ptrdiff_t(std::min(e.have.size(), (p + 1) * kPieceBlocks));
        if (std::all_of(from, to, [](bool h) { return h; })) {
            bits[p / 8] = std::uint8_t(bits[p / 8] | (1u << (p % 8)));
            any = true;
        }
    }
    if (!any)
        return;
    const QByteArray n = e.info.name.toUtf8();
    (void)shareMember(ffi::SharedFile{e.info.root, e.info.size, std::string(n.constData(), std::size_t(n.size())), std::nullopt,
                                      std::nullopt, ffi::Have::pieces(std::move(bits)), std::span{e.hashes}},
                      e.info.owner, e.id);
}

void Ed3kDownloads::fail(Entry& e, const QString& error)
{
    logWarning(QStringLiteral("download of \"%1\" failed: %2").arg(e.info.name, error));
    stopDownload(e.info.root);
    e.info.state = Ed3kDownloadInfo::State::Failed;
    e.info.error = error;
    e.info.rate = 0;
    touch(e);
    markChanged();
    emit failed(e.info.root, error);
}

void Ed3kDownloads::markChanged()
{
    m_changed = true;
}

void Ed3kDownloads::touch(Entry& e)
{
    e.metDirty = true;
    scheduleSave(e.info.owner);
}

void Ed3kDownloads::onTick()
{
    for (auto& [root, e] : m_entries) {
        const std::uint64_t rate = e.received - e.lastReceived;
        e.lastReceived = e.received;
        if (rate != e.info.rate) {
            e.info.rate = rate;
            m_changed = true;
        }
    }
    if (!m_changed)
        return;
    m_changed = false;
    emit changed();
}

void Ed3kDownloads::load()
{
    const QByteArray raw = readBlob(statePath());
    if (raw.isEmpty())
        return;
    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0, count = 0;
    qint32 nextId = 0;
    in >> magic >> version >> nextId >> count;
    if (in.status() != QDataStream::Ok || magic != kStateMagic || version < 1 || version > kStateVersion) {
        logWarning(QStringLiteral("%1 does not parse: dropped").arg(statePath()));
        return;
    }
    m_nextId = std::max(0, int(nextId));
    bool migrated = false;
    for (quint32 n = 0; n < count && in.status() == QDataStream::Ok; ++n) {
        QByteArray root, have;
        quint64 size = 0;
        qint32 category = 0, priority = 1, state = 0, id = -1;
        qint64 addedOn = 0, completedOn = 0;
        bool sharedByList = false;
        Entry e;
        in >> root >> size >> e.info.name >> e.info.owner >> category >> priority >> state >> e.info.partPath >> e.info.targetPath
            >> e.info.path >> have >> addedOn >> completedOn >> sharedByList >> e.info.error >> id;
        const auto r = rootFrom(root);
        if (in.status() != QDataStream::Ok || !r || size == 0 || state < 0 || state > int(Ed3kDownloadInfo::State::Failed))
            break;
        e.id = id >= 0 ? int(id) : m_nextId++;
        m_nextId = std::max(m_nextId, e.id + 1);
        e.info.root = *r;
        e.info.size = size;
        e.info.category = int(category);
        e.info.priority = std::clamp(int(priority), 0, 4);
        e.info.state = Ed3kDownloadInfo::State(state);
        if (e.info.state == Ed3kDownloadInfo::State::Verifying)
            e.info.state = Ed3kDownloadInfo::State::Downloading;
        e.info.addedOn = addedOn;
        e.info.completedOn = completedOn;
        e.sharedByList = sharedByList;
        e.have = unpackBits(have, std::size_t(blockCount(size)));
        if (e.info.state == Ed3kDownloadInfo::State::Complete) {
            std::fill(e.have.begin(), e.have.end(), true);
            e.info.done = size;
        } else {
            for (std::size_t i = 0; i < e.have.size(); ++i)
                if (e.have[i])
                    e.info.done += std::min<std::uint64_t>(kBlock, size - std::uint64_t(i) * kBlock);
        }
        // version 1 kept every row here: an unfinished one gets its .met beside the part file
        if (e.info.state != Ed3kDownloadInfo::State::Complete) {
            e.metDirty = true;
            migrated = true;
        }
        m_entries.emplace(*r, std::move(e));
    }
    // written by setDirs (targets relative to the incoming dir)
    if (migrated)
        logInfo(QStringLiteral("%1: unfinished downloads move to .ed3k.part.met files").arg(statePath()));
}

void Ed3kDownloads::scanTempDirs()
{
    for (const QString& dir : std::as_const(m_tempDirs)) {
        const QDir d(dir);
        for (const QString& name : d.entryList({QStringLiteral("*") + kMetSuffix}, QDir::Files)) {
            const QString metPath = d.filePath(name);
            auto e = readMet(metPath);
            if (!e) {
                logWarning(QStringLiteral("%1 does not parse: left alone").arg(metPath));
                continue;
            }
            if (const Entry* known = entryOf(e->info.root)) {
                if (QFileInfo(metPathOf(*known)) != QFileInfo(metPath))
                    logWarning(QStringLiteral("%1: \"%2\" is in the list already (%3)").arg(metPath, known->info.name, known->info.partPath));
                continue;
            }
            e->id = m_nextId++;
            const ffi::Root root = e->info.root;
            logInfo(QStringLiteral("download of \"%1\" taken up from %2 (%3 of %4 B)")
                        .arg(e->info.name, metPath).arg(e->info.done).arg(e->info.size));
            auto [it, inserted] = m_entries.emplace(root, std::move(*e));
            emit added(root);
            markChanged();
            if (m_started) {
                if (it->second.info.state == Ed3kDownloadInfo::State::Downloading)
                    openEntry(it->second);
                else if (it->second.info.state == Ed3kDownloadInfo::State::Paused)
                    sharePieces(it->second);
                pumpSession();
            }
        }
    }
}

std::optional<Ed3kDownloads::Entry> Ed3kDownloads::readMet(const QString& metPath) const
{
    const QByteArray raw = readBlob(metPath);
    if (raw.isEmpty())
        return std::nullopt;
    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0;
    in >> magic >> version;
    if (in.status() != QDataStream::Ok || magic != kMetMagic || version != kMetVersion)
        return std::nullopt;
    QByteArray root, have;
    quint64 size = 0;
    qint32 category = 0, priority = 1, state = 0;
    quint8 targetKind = 0;
    QString target;
    qint64 addedOn = 0;
    Entry e;
    in >> root >> size >> e.info.name >> e.info.owner >> category >> priority >> state >> targetKind >> target >> have
        >> addedOn >> e.info.error;
    const auto r = rootFrom(root);
    if (in.status() != QDataStream::Ok || !r || size == 0 || state < 0 || state > int(Ed3kDownloadInfo::State::Failed)
        || Ed3kDownloadInfo::State(state) == Ed3kDownloadInfo::State::Complete)
        return std::nullopt;
    e.info.root = *r;
    e.info.size = size;
    e.info.name = safeName(e.info.name);
    if (e.info.name.isEmpty())
        e.info.name = hexOf(r->bytes);
    if (e.info.owner.isEmpty())
        e.info.owner = kOwner;
    e.info.category = std::max(0, int(category));
    e.info.priority = std::clamp(int(priority), 0, 4);
    e.info.state = Ed3kDownloadInfo::State(state);
    if (e.info.state == Ed3kDownloadInfo::State::Verifying)
        e.info.state = Ed3kDownloadInfo::State::Downloading;
    if (TargetKind(targetKind) == TargetKind::Incoming && !target.isEmpty() && !m_incomingDir.isEmpty())
        e.info.targetPath = QDir::cleanPath(QDir(m_incomingDir).filePath(target));
    else if (TargetKind(targetKind) == TargetKind::Absolute)
        e.info.targetPath = target;
    // the part file is beside the .met, wherever it was copied to
    e.info.partPath = metPath.chopped(4);
    e.info.path = e.info.partPath;
    e.info.addedOn = addedOn;
    e.have = unpackBits(have, std::size_t(blockCount(size)));
    for (std::size_t i = 0; i < e.have.size(); ++i)
        if (e.have[i])
            e.info.done += std::min<std::uint64_t>(kBlock, size - std::uint64_t(i) * kBlock);
    return e;
}

void Ed3kDownloads::writeMet(Entry& e)
{
    // a target under the incoming dir stays relative (another machine has another one)
    TargetKind kind = TargetKind::None;
    QString target;
    if (!e.info.targetPath.isEmpty()) {
        const QString rel = m_incomingDir.isEmpty() ? QString() : QDir(m_incomingDir).relativeFilePath(e.info.targetPath);
        if (!rel.isEmpty() && !QDir::isAbsolutePath(rel) && !rel.startsWith(QLatin1String(".."))) {
            kind = TargetKind::Incoming;
            target = rel;
        } else {
            kind = TargetKind::Absolute;
            target = e.info.targetPath;
        }
    }
    const auto state = e.info.state == Ed3kDownloadInfo::State::Verifying ? Ed3kDownloadInfo::State::Downloading : e.info.state;
    QByteArray raw;
    {
        QDataStream out(&raw, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << kMetMagic << kMetVersion << rootBytes(e.info.root) << quint64(e.info.size) << e.info.name << e.info.owner
            << qint32(e.info.category) << qint32(e.info.priority) << qint32(int(state)) << quint8(kind) << target
            << packBits(e.have) << qint64(e.info.addedOn) << e.info.error;
    }
    const QString path = metPathOf(e);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (writeBlob(path, spanOf(raw)))
        e.metDirty = false;
    else
        logWarning(QStringLiteral("cannot write %1").arg(path));
}

QString Ed3kDownloads::partPathOf(const ffi::Root& root) const
{
    return QDir(m_tempDirs.value(0)).filePath(hexOf(root.bytes) + QStringLiteral(".ed3k.part"));
}

QString Ed3kDownloads::freePath(const QString& path)
{
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo fi(path);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = fi.dir().filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return path;
}

} // namespace eMule::ed3k
