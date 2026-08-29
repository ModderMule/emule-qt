#include "queue/ArticleWriter.h"

#include <QDir>
#include <QFileInfo>

namespace eMule::usenet {

ArticleWriter::~ArticleWriter()
{
    close();
}

bool ArticleWriter::open(const QString& path, QString& error)
{
    close();

    const QDir parent = QFileInfo(path).absoluteDir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        error = QStringLiteral("Could not create %1").arg(parent.absolutePath());
        return false;
    }

    m_file.setFileName(path);
    // ReadWrite rather than WriteOnly: WriteOnly implies Truncate for some
    // backends, and truncating is precisely wrong for a resumed download.
    if (!m_file.open(QIODevice::ReadWrite)) {
        error = m_file.errorString();
        return false;
    }
    m_bytesWritten = 0;
    return true;
}

bool ArticleWriter::reserve(qint64 size, QString& error)
{
    if (!m_file.isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (size <= 0 || m_file.size() >= size)
        return true;

    if (!m_file.resize(size)) {
        error = m_file.errorString();
        return false;
    }
    return true;
}

bool ArticleWriter::seekTo(qint64 offset, QString& error)
{
    if (!m_file.isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (offset < 0) {
        error = QStringLiteral("Negative offset %1").arg(offset);
        return false;
    }
    if (!m_file.seek(offset)) {
        error = m_file.errorString();
        return false;
    }
    return true;
}

bool ArticleWriter::write(QByteArrayView data, QString& error)
{
    if (!m_file.isOpen()) {
        error = QStringLiteral("File is not open");
        return false;
    }
    if (data.isEmpty())
        return true;

    const qint64 written = m_file.write(data.data(), data.size());
    if (written != data.size()) {
        // A short write is almost always a full disk, and continuing would
        // leave a hole that only surfaces as a PAR2 failure much later.
        error = m_file.errorString().isEmpty()
                    ? QStringLiteral("Short write (%1 of %2 bytes)")
                          .arg(written).arg(data.size())
                    : m_file.errorString();
        return false;
    }
    m_bytesWritten += written;
    return true;
}

bool ArticleWriter::flush(QString& error)
{
    if (!m_file.isOpen())
        return true;
    if (!m_file.flush()) {
        error = m_file.errorString();
        return false;
    }
    return true;
}

void ArticleWriter::close()
{
    if (m_file.isOpen())
        m_file.close();
}

} // namespace eMule::usenet
