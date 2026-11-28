#include "pch.h"
/// @file LocalCopyScan.cpp
/// @brief "Locate Files..." for eD2K downloads — implementation.

#include "transfer/LocalCopyScan.h"

#include "app/AppContext.h"
#include "crypto/FileIdentifier.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "transfer/ImportParts.h"
#include "utils/ArgStr.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#include <map>
#include <set>

namespace eMule {

LocalCopyScan::LocalCopyScan(QObject* parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    qRegisterMetaType<eMule::LocalScanResult>("eMule::LocalScanResult");
}

LocalCopyScan::~LocalCopyScan()
{
    m_cancel->store(true);
}

bool LocalCopyScan::start(const QStringList& folders)
{
    if (m_running)
        return false;
    std::vector<Wanted> wanted = wantedDownloads();
    if (wanted.empty() || folders.isEmpty()) {
        logInfo(QStringLiteral("Locate Files: no eD2K download to look for"));
        emit finished(LocalScanResult{});
        return false;
    }
    m_running = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    logInfo(QStringLiteral("Locate Files: %1 folder(s) for %2 eD2K download(s)").arg(argStr(folders.size()), argStr(wanted.size())));
    QPointer<LocalCopyScan> self(this);
    const auto cancel = m_cancel;
    QThreadPool::globalInstance()->start([self, cancel, folders, wanted = std::move(wanted)] {
        const auto found = [self](const QString& path, const Hashed& hashed) {
            QMetaObject::invokeMethod(self.data(), [path, hashed] { (void)takeCopy(path, hashed); }, Qt::QueuedConnection);
        };
        const LocalScanResult r = scan(folders, wanted, *cancel, found);
        QMetaObject::invokeMethod(self.data(), [self, r] {
            if (!self)
                return;
            self->m_running = false;
            logInfo(QStringLiteral("Locate Files: %1 file(s), %2 hashed, %3 eD2K download(s) found%4")
                        .arg(argStr(r.files), argStr(r.hashed), argStr(r.matched),
                             argStr(r.cancelled ? QStringLiteral(" (cancelled)") : QString())));
            emit self->finished(r);
        }, Qt::QueuedConnection);
    });
    return true;
}

void LocalCopyScan::cancel()
{
    m_cancel->store(true);
}

std::vector<LocalCopyScan::Wanted> LocalCopyScan::wantedDownloads()
{
    std::vector<Wanted> out;
    if (!theApp.downloadQueue)
        return out;
    for (const PartFile* file : theApp.downloadQueue->files()) {
        const PartFileStatus st = file->status(true);
        if (st == PartFileStatus::Complete || st == PartFileStatus::Completing || st == PartFileStatus::Error
            || st == PartFileStatus::Hashing || st == PartFileStatus::WaitingForHash)
            continue;
        if (file->fileOp() != PartFileOp::None || file->gapList().empty() || file->fileSize() == 0)
            continue;
        Wanted w;
        md4cpy(w.hash.data(), file->fileHash());
        w.size = uint64(file->fileSize());
        out.push_back(w);
    }
    return out;
}

LocalScanResult LocalCopyScan::scan(const QStringList& folders, const std::vector<Wanted>& wanted,
                                    const std::atomic<bool>& cancel,
                                    const std::function<void(const QString&, const Hashed&)>& found)
{
    LocalScanResult r;
    // size → hashes of that size still missing
    std::map<uint64, std::set<Md4>> bySize;
    for (const auto& w : wanted)
        if (w.size > 0)
            bySize[w.size].insert(w.hash);
    const auto candidates = walkLocalCopies(folders, [&](uint64 size) { return bySize.contains(size); }, cancel, r);
    if (r.cancelled)
        return r;

    for (const auto& c : candidates) {
        if (cancel.load()) {
            r.cancelled = true;
            break;
        }
        const auto left = bySize.find(c.size);
        if (left == bySize.end() || left->second.empty())
            continue;   // every download of this size found already
        const auto hashed = hashFile(c.path, c.size, cancel);
        if (!hashed)
            continue;
        ++r.hashed;
        if (!left->second.erase(hashed->fileHash))
            continue;
        ++r.matched;
        r.paths << c.path;
        if (found)
            found(c.path, *hashed);
    }
    return r;
}

std::optional<LocalCopyScan::Hashed> LocalCopyScan::hashFile(const QString& path, uint64 size, const std::atomic<bool>& cancel)
{
    QFile file(path);
    if (size == 0 || !file.open(QIODevice::ReadOnly) || uint64(file.size()) != size)
        return std::nullopt;
    const QDateTime before = QFileInfo(path).lastModified();
    Hashed h;
    for (uint64 off = 0; off < size; off += PARTSIZE) {
        Md4 part{};
        if (!KnownFile::createHash(file, std::min<uint64>(PARTSIZE, size - off), part.data(), nullptr, &cancel))
            return std::nullopt;
        h.partHashes.push_back(part);
    }
    // a file ending on a part boundary has one more hash, that of no data (MFC CreateFromFile)
    if (size % PARTSIZE == 0)
        h.partHashes.push_back(FileIdentifier::kEmptyMD4);
    // still being written: the hash names no file
    const QFileInfo after(path);
    if (uint64(after.size()) != size || after.lastModified() != before)
        return std::nullopt;
    if (h.partHashes.size() == 1) {
        h.fileHash = h.partHashes.front();
    } else {
        MD4Hasher md4;
        for (const Md4& p : h.partHashes)
            md4.add(p.data(), p.size());
        md4.finish();
        md4cpy(h.fileHash.data(), md4.getHash());
    }
    return h;
}

bool LocalCopyScan::takeCopy(const QString& path, const Hashed& hashed)
{
    PartFile* file = theApp.downloadQueue ? theApp.downloadQueue->fileByID(hashed.fileHash.data()) : nullptr;
    if (!file || file->status(true) == PartFileStatus::Complete || file->status(true) == PartFileStatus::Completing)
        return false;
    // no hash set yet: the copy's, proved by the file hash (setMD4HashSet checks it again)
    FileIdentifier& ident = file->fileIdentifier();
    if (ident.getTheoreticalMD4PartHashCount() > 0 && !ident.hasExpectedMD4HashCount()) {
        if (!ident.setMD4HashSet(hashed.partHashes)) {
            logWarning(QStringLiteral("Locate Files: hash set of %1 not taken from %2").arg(file->fileName(), path));
            return false;
        }
        file->setMD4HashsetNeeded(false);
        file->hashsetReceived();
    }
    if (!startPartImport(file, path)) {
        logWarning(QStringLiteral("Locate Files: %1 not imported from %2 (an import runs already)").arg(file->fileName(), path));
        return false;
    }
    logInfo(QStringLiteral("Locate Files: importing %1 from %2").arg(file->fileName(), path));
    return true;
}

} // namespace eMule
