#include "LandedFiles.h"

#include "files/SharedFileList.h"
#include "utils/BackgroundFileWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace eMule {

LandedFiles::LandedFiles(QString listPath)
    : m_listPath(std::move(listPath))
{
}

void LandedFiles::load()
{
    m_paths.clear();
    m_keys.clear();
    if (m_listPath.isEmpty())
        return;
    const auto raw = BackgroundFileWriter::readFile(m_listPath);   // a queued save is newer
    if (!raw)
        return;
    for (const QByteArray& line : raw->split('\n')) {
        const QString path = QString::fromUtf8(line).trimmed();
        if (path.isEmpty())
            continue;
        const QString key = SharedFileList::pathKey(path);
        if (m_keys.contains(key))
            continue;
        m_keys.insert(key);
        m_paths.append(path);
    }
}

void LandedFiles::add(const QStringList& paths)
{
    bool added = false;
    for (const QString& path : paths) {
        if (path.isEmpty())
            continue;
        const QString key = SharedFileList::pathKey(path);
        if (m_keys.contains(key))
            continue;
        m_keys.insert(key);
        m_paths.append(path);
        added = true;
    }
    if (!added)
        return;
    while (m_paths.size() > kMaxEntries)
        m_keys.remove(SharedFileList::pathKey(m_paths.takeFirst()));
    save();
}

bool LandedFiles::contains(const QString& filePath) const
{
    return !m_keys.isEmpty() && !filePath.isEmpty() && m_keys.contains(SharedFileList::pathKey(filePath));
}

bool LandedFiles::setBlocking(bool on)
{
    if (on == m_blocking)
        return false;
    m_blocking = on;
    return true;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void LandedFiles::save() const
{
    if (m_listPath.isEmpty())
        return;
    QByteArray out;
    for (const QString& p : m_paths)
        out += p.toUtf8() + '\n';
    BackgroundFileWriter::instance().write(m_listPath, std::move(out), {});   // off the main thread (report 43)
}

} // namespace eMule
