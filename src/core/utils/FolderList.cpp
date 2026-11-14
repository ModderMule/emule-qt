#include "utils/FolderList.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

namespace eMule {

namespace {

QFileInfoList visibleDirs(const QString& path)
{
    QFileInfoList out;
    const auto entries = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries)
        if (!fi.fileName().startsWith(u'.') && fi.isReadable())
            out.append(fi);
    return out;
}

void walk(const QString& dir, QStringList& out, QSet<QString>& walked, int cap)
{
    if (out.size() >= cap)
        return;
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (canonical.isEmpty() || walked.contains(canonical))
        return;   // dangling link or a loop
    walked.insert(canonical);
    out.append(dir);
    for (const QFileInfo& fi : visibleDirs(dir))
        walk(fi.absoluteFilePath(), out, walked, cap);
}

} // namespace

QList<FolderEntry> folderRoots()
{
    QList<FolderEntry> out;
#ifdef Q_OS_MACOS
    out.append({QStringLiteral("/"), QStringLiteral("/"), true});
    const auto vols = QDir(QStringLiteral("/Volumes"))
                          .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                         QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : vols)
        if (fi.isReadable())
            out.append({fi.fileName(), fi.absoluteFilePath(), hasSubfolders(fi.absoluteFilePath())});
#else
    for (const QFileInfo& fi : QDir::drives()) {
        const QString path = fi.absoluteFilePath();
        QString label = path;
        // "Data (E:)" like the shell; "/" stays "/"
        if (label.size() > 1 && label.endsWith(u'/')) {
            label.chop(1);
            if (const QString volume = QStorageInfo(path).name(); !volume.isEmpty())
                label = QStringLiteral("%1 (%2)").arg(volume, label);
        }
        out.append({label, path, true});
    }
#endif
    return out;
}

QList<FolderEntry> subfolders(const QString& path)
{
    QList<FolderEntry> out;
    for (const QFileInfo& fi : visibleDirs(path))
        out.append({fi.fileName(), fi.absoluteFilePath(), hasSubfolders(fi.absoluteFilePath())});
    return out;
}

bool hasSubfolders(const QString& path)
{
    const auto entries = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    return std::ranges::any_of(entries, [](const QFileInfo& fi) {
        return !fi.fileName().startsWith(u'.') && fi.isReadable();
    });
}

QStringList folderTree(const QString& root, int cap)
{
    QStringList out;
    QSet<QString> walked;
    walk(QDir::cleanPath(root), out, walked, cap);
    return out;
}

} // namespace eMule
