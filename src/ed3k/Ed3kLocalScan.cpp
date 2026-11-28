#include "Ed3kLocalScan.h"

#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <map>
#include <set>

namespace eMule::ed3k {

namespace {

/// Unfinished downloads of any network, their state files: never a copy of a whole file.
bool skippedName(const QString& name)
{
    static const QStringList kSuffixes{QStringLiteral(".part"), QStringLiteral(".met"), QStringLiteral(".tmp"),
                                       QStringLiteral(".!qb"), QStringLiteral(".crdownload")};
    return std::ranges::any_of(kSuffixes, [&](const QString& s) { return name.endsWith(s, Qt::CaseInsensitive); });
}

} // namespace

Ed3kLocalScan::Ed3kLocalScan(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    qRegisterMetaType<eMule::ed3k::Ed3kLocalScan::Result>("eMule::ed3k::Ed3kLocalScan::Result");
}

Ed3kLocalScan::~Ed3kLocalScan()
{
    m_cancel->store(true);
}

bool Ed3kLocalScan::start(const QStringList& folders, const Wanted& extra)
{
    if (m_running)
        return false;
    // what is still missing: open downloads, and the extra roots, not on disk here already
    std::map<ffi::Root, std::uint64_t> wanted;
    for (const auto& [root, size] : Ed3kJoinBase::wantedFiles(m_session))
        wanted.emplace(root, size);
    for (const auto& [root, size] : extra)
        wanted.emplace(root, size);
    std::erase_if(wanted, [&](const auto& w) { return !Ed3kJoinBase::localCopyPath(m_session, w.first, w.second).isEmpty(); });
    if (wanted.empty() || folders.isEmpty()) {
        logEd3k(QStringLiteral("ed3k: folder scan: nothing to look for"));
        emit finished(Result{});
        return false;
    }
    m_running = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    logEd3k(QStringLiteral("ed3k: folder scan of %1 folder(s) for %2 file(s)").arg(folders.size()).arg(wanted.size()));
    QPointer<Ed3kLocalScan> self(this);
    const auto cancel = m_cancel;
    Wanted list(wanted.begin(), wanted.end());
    QThreadPool::globalInstance()->start([self, cancel, folders, list = std::move(list)] {
        const auto found = [self](const ffi::Root& root, const QString& path, std::uint64_t size, qint64 mtimeMs) {
            QMetaObject::invokeMethod(self.data(), [self, root, path, size, mtimeMs] {
                if (self)
                    Ed3kJoinBase::addFoundCopy(self->m_session, root, path, size, mtimeMs);
            }, Qt::QueuedConnection);
        };
        const Result r = scan(folders, list, *cancel, found);
        QMetaObject::invokeMethod(self.data(), [self, r] {
            if (!self)
                return;
            self->m_running = false;
            logEd3k(QStringLiteral("ed3k: folder scan done: %1 file(s), %2 hashed, %3 found%4")
                        .arg(argStr(r.files), argStr(r.hashed), argStr(r.matched),
                             argStr(r.cancelled ? QStringLiteral(" (cancelled)") : QString())));
            emit self->finished(r);
        }, Qt::QueuedConnection);
    });
    return true;
}

void Ed3kLocalScan::cancel()
{
    m_cancel->store(true);
}

Ed3kLocalScan::Result Ed3kLocalScan::scan(const QStringList& folders, const Wanted& wanted, const std::atomic<bool>& cancel,
                                          const std::function<void(const ffi::Root&, const QString&, std::uint64_t, qint64)>& found)
{
    Result r;
    // size → roots of that size still missing
    std::map<std::uint64_t, std::set<ffi::Root>> bySize;
    for (const auto& [root, size] : wanted)
        if (size > 0)
            bySize[size].insert(root);
    // walk: only sizes count here, nothing is read (BiglyBT's buildFileMap)
    struct Candidate {
        QString path;
        std::uint64_t size = 0;
        QString dir;
    };
    std::vector<Candidate> candidates;
    std::set<QString> seen;   // one folder given twice, or inside another
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancel.load()) {
                r.cancelled = true;
                return r;
            }
            const QFileInfo fi = it.nextFileInfo();
            if (fi.isSymLink() || skippedName(fi.fileName()))
                continue;
            const QString path = fi.canonicalFilePath();
            if (path.isEmpty() || !seen.insert(path).second)
                continue;
            ++r.files;
            if (bySize.contains(std::uint64_t(fi.size())))
                candidates.push_back({path, std::uint64_t(fi.size()), fi.absolutePath()});
        }
    }
    r.candidates = int(candidates.size());
    // the folder with most candidates first: one release folder wins over scattered copies
    QHash<QString, int> perDir;
    for (const auto& c : candidates)
        ++perDir[c.dir];
    std::ranges::stable_sort(candidates, [&](const Candidate& a, const Candidate& b) { return perDir.value(a.dir) > perDir.value(b.dir); });

    for (const auto& c : candidates) {
        if (cancel.load()) {
            r.cancelled = true;
            break;
        }
        const auto left = bySize.find(c.size);
        if (left == bySize.end() || left->second.empty())
            continue;   // every root of this size found already
        const qint64 mtime = QFileInfo(c.path).lastModified().toMSecsSinceEpoch();
        const auto hashed = Ed3kJoinBase::hashFileRoot(c.path, cancel);
        if (!hashed)
            continue;
        ++r.hashed;
        if (hashed->second != c.size || !left->second.erase(hashed->first))
            continue;
        ++r.matched;
        r.paths << c.path;
        if (found)
            found(hashed->first, c.path, c.size, mtime);
    }
    return r;
}

} // namespace eMule::ed3k
