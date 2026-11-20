#include "utils/FolderList.h"

#include <algorithm>

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
        if (fi.isReadable() && !fi.fileName().startsWith(u'.'))   // not .timemachine
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

QString uncShareRoot(const QString& path)
{
    QString p = path.trimmed();
    p.replace(u'\\', u'/');
    if (!p.startsWith(QLatin1String("//")))
        return {};
    const QStringList parts = p.mid(2).split(u'/');
    if (parts.size() < 2)
        return {};
    const QString& server = parts.at(0);
    const QString& share = parts.at(1);
    // "\\?\C:\" and "\\.\pipe\" are device paths, not shares
    if (server.isEmpty() || share.isEmpty() || server == u'?' || server == u'.')
        return {};
    return QStringLiteral("//%1/%2").arg(server, share);
}

QStringList uncShareRoots(const QStringList& dirs)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString& d : dirs) {
        const QString root = uncShareRoot(d);
        if (!root.isEmpty() && !seen.contains(root.toCaseFolded())) {
            seen.insert(root.toCaseFolded());
            out.append(root);
        }
    }
    std::ranges::sort(out, [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString uncLabel(const QString& root)
{
    return QString(root).replace(u'/', u'\\');
}

bool isDriveRoot(const QString& path)
{
    return path.size() >= 2 && path.size() <= 3 && path.at(0).isLetter() && path.at(1) == u':'
        && (path.size() == 2 || path.at(2) == u'/' || path.at(2) == u'\\');
}

} // namespace eMule
