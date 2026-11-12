#include "pch.h"
/// @file AtomicFile.cpp
/// @brief QSaveFile without the forced disk flush.

#include "utils/AtomicFile.h"

#include <QDir>
#include <QRandomGenerator>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cstdio>
#endif

namespace eMule {

AtomicFile::AtomicFile(const QString& path)
    : m_target(path)
{
}

AtomicFile::~AtomicFile()
{
    cancelWriting();
}

bool AtomicFile::open(OpenMode mode)
{
    if (isOpen() || !(mode & WriteOnly))
        return false;
    // a name nobody else uses: NewOnly fails on a leftover, the next one is tried
    for (int attempt = 0; attempt < 16; ++attempt) {
        setFileName(QStringLiteral("%1.%2.tmp")
                        .arg(m_target)
                        .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));
        if (QFile::open((mode & ~(Append | ExistingOnly)) | WriteOnly | Truncate | NewOnly)) {
            m_tempMade = true;
            m_writeError = false;
            if (QFile::exists(m_target))
                setPermissions(QFile::permissions(m_target));   // as QSaveFile
            return true;
        }
        if (error() != OpenError || !QFile::exists(fileName()))
            return false;   // not a name clash
    }
    return false;
}

bool AtomicFile::commit()
{
    if (!isOpen())
        return false;
    bool ok = !m_writeError && flush();
    close();
    ok = ok && replaceTarget();
    if (!ok) {
        QFile::remove(fileName());
        m_tempMade = false;
        return false;
    }
    m_tempMade = false;
    return true;
}

void AtomicFile::cancelWriting()
{
    if (isOpen())
        close();
    if (m_tempMade) {
        QFile::remove(fileName());
        m_tempMade = false;
    }
}

qint64 AtomicFile::writeData(const char* data, qint64 len)
{
    const qint64 written = QFile::writeData(data, len);
    if (written != len)
        m_writeError = true;
    return written;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool AtomicFile::replaceTarget()
{
#ifdef Q_OS_WIN
    // no MOVEFILE_WRITE_THROUGH: that would wait for the disk again
    const QString from = QDir::toNativeSeparators(fileName());
    const QString to = QDir::toNativeSeparators(m_target);
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(to.utf16()),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(QFile::encodeName(fileName()).constData(), QFile::encodeName(m_target).constData()) == 0;
#endif
}

} // namespace eMule
