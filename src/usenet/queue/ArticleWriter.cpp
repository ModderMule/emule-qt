#include "queue/ArticleWriter.h"

#include "queue/ArticleFileCache.h"

namespace eMule::usenet {

ArticleWriter::~ArticleWriter()
{
    close();
}

bool ArticleWriter::open(const QString& path, QString& error)
{
    close();
    m_file = m_cache ? m_cache->acquire(path, error) : ArticleFileCache::openFile(path, error);
    if (!m_file)
        return false;
    m_pos = 0;
    m_bytesWritten = 0;
    return true;
}

bool ArticleWriter::reserve(qint64 size, QString& error)
{
    if (!isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (size <= 0 || m_file->size() >= size)
        return true;

    if (!m_file->resize(size)) {
        error = m_file->errorString();
        return false;
    }
    return true;
}

bool ArticleWriter::seekTo(qint64 offset, QString& error)
{
    if (!isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (offset < 0) {
        error = QStringLiteral("Negative offset %1").arg(offset);
        return false;
    }
    m_pos = offset;
    return true;
}

bool ArticleWriter::write(QByteArrayView data, QString& error)
{
    if (!isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (data.isEmpty())
        return true;

    // Another writer on the shared handle may have moved it.
    if (m_file->pos() != m_pos && !m_file->seek(m_pos)) {
        error = m_file->errorString();
        return false;
    }
    const qint64 written = m_file->write(data.data(), data.size());
    if (written != data.size()) {
        // A short write is almost always a full disk, and continuing would
        // leave a hole that only surfaces as a PAR2 failure much later.
        error = m_file->errorString().isEmpty()
                    ? QStringLiteral("Short write (%1 of %2 bytes)")
                          .arg(written).arg(data.size())
                    : m_file->errorString();
        return false;
    }
    m_pos += written;
    m_bytesWritten += written;
    return true;
}

bool ArticleWriter::flush(QString& error)
{
    if (!isOpen())
        return true;
    if (!m_file->flush()) {
        error = m_file->errorString();
        return false;
    }
    return true;
}

void ArticleWriter::close()
{
    // Drops this writer's share; the last one closes the file.
    m_file.reset();
}

} // namespace eMule::usenet
