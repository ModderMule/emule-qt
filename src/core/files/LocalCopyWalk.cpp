#include "pch.h"
/// @file LocalCopyWalk.cpp
/// @brief "Locate Files..." folder walk — implementation.

#include "files/LocalCopyWalk.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <set>

namespace eMule {

namespace {

/// Unfinished downloads of any network, their state files: never a copy of a whole file.
bool skippedName(const QString& name)
{
    static const QStringList kSuffixes{QStringLiteral(".part"), QStringLiteral(".met"), QStringLiteral(".tmp"),
                                       QStringLiteral(".!qb"), QStringLiteral(".crdownload")};
    return std::ranges::any_of(kSuffixes, [&](const QString& s) { return name.endsWith(s, Qt::CaseInsensitive); });
}

} // namespace

std::vector<LocalCopyCandidate> walkLocalCopies(const QStringList& folders, const std::function<bool(uint64)>& wantedSize,
                                                const std::atomic<bool>& cancel, LocalScanResult& r)
{
    std::vector<LocalCopyCandidate> candidates;
    std::set<QString> seen;   // one folder given twice, or inside another
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancel.load()) {
                r.cancelled = true;
                return {};
            }
            const QFileInfo fi = it.nextFileInfo();
            if (fi.isSymLink() || skippedName(fi.fileName()))
                continue;
            const QString path = fi.canonicalFilePath();
            if (path.isEmpty() || !seen.insert(path).second)
                continue;
            ++r.files;
            if (fi.size() > 0 && wantedSize(uint64(fi.size())))
                candidates.push_back({path, uint64(fi.size()), fi.absolutePath()});
        }
    }
    r.candidates = int(candidates.size());
    QHash<QString, int> perDir;
    for (const auto& c : candidates)
        ++perDir[c.dir];
    std::ranges::stable_sort(candidates, [&](const LocalCopyCandidate& a, const LocalCopyCandidate& b) {
        return perDir.value(a.dir) > perDir.value(b.dir);
    });
    return candidates;
}

} // namespace eMule
