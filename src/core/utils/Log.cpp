/// @file Log.cpp
/// @brief Logging system implementation.

#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

namespace eMule {

// ---------------------------------------------------------------------------
// LogFile — public API
// ---------------------------------------------------------------------------

LogFile::LogFile() = default;

LogFile::~LogFile()
{
    close();
}

bool LogFile::create(const QString& filePath, std::size_t maxSize)
{
    QMutexLocker lock(&m_mutex);
    closeLocked();
    m_filePath = filePath;
    m_maxFileSize = maxSize;
    return openLocked();
}

bool LogFile::open()
{
    QMutexLocker lock(&m_mutex);
    return openLocked();
}

bool LogFile::close()
{
    QMutexLocker lock(&m_mutex);
    closeLocked();
    return true;
}

bool LogFile::isOpen() const
{
    QMutexLocker lock(&m_mutex);
    return m_file.isOpen();
}

const QString& LogFile::filePath() const
{
    return m_filePath;
}

void LogFile::setMaxFileSize(std::size_t maxSize)
{
    QMutexLocker lock(&m_mutex);
    if (maxSize < 0x10000)
        m_maxFileSize = (maxSize == 0) ? std::numeric_limits<std::size_t>::max() : 0x10000;
    else
        m_maxFileSize = maxSize;
}

bool LogFile::log(const QString& message)
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen())
        return false;

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString line = QStringLiteral("%1: %2\n").arg(timestamp, message);

    const QByteArray utf8 = line.toUtf8();
    m_stream << line;
    m_stream.flush();
    m_bytesWritten += static_cast<std::size_t>(utf8.size());

    if (m_bytesWritten >= m_maxFileSize)
        rotateLocked();

    return true;
}

void LogFile::startNewLogFile()
{
    QMutexLocker lock(&m_mutex);
    rotateLocked();
}

// ---------------------------------------------------------------------------
// LogFile — private (caller must hold m_mutex)
// ---------------------------------------------------------------------------

bool LogFile::openLocked()
{
    if (m_file.isOpen())
        return true;

    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    m_stream.setDevice(&m_file);
    m_startTime = QDateTime::currentSecsSinceEpoch();
    m_bytesWritten = static_cast<std::size_t>(m_file.size());
    return true;
}

void LogFile::closeLocked()
{
    if (!m_file.isOpen())
        return;

    m_stream.flush();
    m_stream.setDevice(nullptr);
    m_file.close();
    m_bytesWritten = 0;
    m_startTime = 0;
}

void LogFile::rotateLocked()
{
    const qint64 startTime = m_startTime;
    closeLocked();

    const QDateTime started = QDateTime::fromSecsSinceEpoch(startTime);
    const QString dateStr = started.toString(QStringLiteral("yyyy.MM.dd HH.mm.ss"));

    const QFileInfo fi(m_filePath);
    const QString bakPath = fi.dir().filePath(
        QStringLiteral("%1 - %2.%3")
            .arg(fi.completeBaseName(), dateStr, fi.suffix()));

    if (!QFile::rename(m_filePath, bakPath))
        QFile::remove(m_filePath);

    openLocked();
}

// ---------------------------------------------------------------------------
// Convenience logging functions
// ---------------------------------------------------------------------------

void logInfo(const QString& msg)
{
    qCInfo(lcEmuleGeneral).noquote() << msg;
}

void logWarning(const QString& msg)
{
    qCWarning(lcEmuleGeneral).noquote() << msg;
}

void logError(const QString& msg)
{
    qCCritical(lcEmuleGeneral).noquote() << msg;
}

void logDebug(const QString& msg)
{
    qCDebug(lcEmuleGeneral).noquote() << msg;
}

} // namespace eMule
