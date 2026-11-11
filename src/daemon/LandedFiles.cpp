#include "LandedFiles.h"

#include "files/SharedFileList.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace eMule {

LandedFiles::LandedFiles(QString listPath)
    : m_listPath(std::move(listPath))
{
}

void LandedFiles::load()
{
    m_paths.clear();
    m_keys.clear();
    QFile f(m_listPath);
    if (m_listPath.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!f.atEnd()) {
        const QString path = QString::fromUtf8(f.readLine()).trimmed();
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
    QDir().mkpath(QFileInfo(m_listPath).absolutePath());
    QSaveFile f(m_listPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    for (const QString& p : m_paths)
        f.write(p.toUtf8() + '\n');
    f.commit();
}

} // namespace eMule
