#include "pch.h"
/// @file SafeFile.cpp
/// @brief Portable file I/O class implementations.

#include "SafeFile.h"
#include "OtherFunctions.h"

#include <QIODevice>
#include <QStringDecoder>

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// FileDataIO — typed readers
// ---------------------------------------------------------------------------

uint8 FileDataIO::readUInt8()
{
    uint8 val{};
    read(&val, sizeof val);
    return val;
}

uint16 FileDataIO::readUInt16()
{
    uint16 val{};
    read(&val, sizeof val);
    return val;
}

uint32 FileDataIO::readUInt32()
{
    uint32 val{};
    read(&val, sizeof val);
    return val;
}

uint64 FileDataIO::readUInt64()
{
    uint64 val{};
    read(&val, sizeof val);
    return val;
}

void FileDataIO::readHash16(uint8* hash)
{
    read(hash, 16);
}

QString FileDataIO::readString(bool optUTF8)
{
    const uint16 rawSize = readUInt16();
    return readString(optUTF8, rawSize);
}

QString FileDataIO::readString(bool optUTF8, uint32 rawSize)
{
    if (rawSize == 0)
        return {};

    QByteArray raw(static_cast<qsizetype>(rawSize), Qt::Uninitialized);
    read(raw.data(), rawSize);

    // Check for UTF-8 BOM (0xEF 0xBB 0xBF)
    if (rawSize >= 3
        && static_cast<uint8>(raw[0]) == 0xEF
        && static_cast<uint8>(raw[1]) == 0xBB
        && static_cast<uint8>(raw[2]) == 0xBF) {
        return QString::fromUtf8(raw.constData() + 3, static_cast<qsizetype>(rawSize) - 3);
    }

    if (optUTF8) {
        // Try UTF-8 decoding
        auto codec = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString result = codec(raw);
        if (!codec.hasError())
            return result;
    }

    // Fallback to Latin-1
    return QString::fromLatin1(raw.constData(), static_cast<qsizetype>(rawSize));
}

// ---------------------------------------------------------------------------
// FileDataIO — typed writers
// ---------------------------------------------------------------------------

void FileDataIO::writeUInt8(uint8 val)
{
    write(&val, sizeof val);
}

void FileDataIO::writeUInt16(uint16 val)
{
    write(&val, sizeof val);
}

void FileDataIO::writeUInt32(uint32 val)
{
    write(&val, sizeof val);
}

void FileDataIO::writeUInt64(uint64 val)
{
    write(&val, sizeof val);
}

void FileDataIO::writeHash16(const uint8* hash)
{
    write(hash, 16);
}

void FileDataIO::writeString(const QString& str, UTF8Mode encode)
{
    QByteArray raw;
    switch (encode) {
    case UTF8Mode::Raw:
        raw = str.toUtf8();
        break;
    case UTF8Mode::OptBOM: {
        // Check if the string contains non-ASCII characters
        bool hasNonAscii = false;
        for (auto ch : str) {
            if (ch.unicode() > 0x7F) {
                hasNonAscii = true;
                break;
            }
        }
        if (hasNonAscii) {
            // Write BOM + UTF-8
            raw.reserve(3 + str.size() * 4);
            raw.append('\xEF');
            raw.append('\xBB');
            raw.append('\xBF');
            raw.append(str.toUtf8());
        } else {
            raw = str.toLatin1();
        }
        break;
    }
    case UTF8Mode::None:
    default:
        raw = str.toLatin1();
        break;
    }

    if (raw.size() > 0xFFFF)
        throw FileException("String too long for 16-bit length prefix");

    const auto len = static_cast<uint16>(raw.size());
    writeUInt16(len);
    if (len > 0)
        write(raw.constData(), len);
}

void FileDataIO::writeLongString(const QString& str, UTF8Mode encode)
{
    QByteArray raw;
    switch (encode) {
    case UTF8Mode::Raw:
        raw = str.toUtf8();
        break;
    case UTF8Mode::OptBOM: {
        bool hasNonAscii = false;
        for (auto ch : str) {
            if (ch.unicode() > 0x7F) {
                hasNonAscii = true;
                break;
            }
        }
        if (hasNonAscii) {
            raw.reserve(3 + str.size() * 4);
            raw.append('\xEF');
            raw.append('\xBB');
            raw.append('\xBF');
            raw.append(str.toUtf8());
        } else {
            raw = str.toLatin1();
        }
        break;
    }
    case UTF8Mode::None:
    default:
        raw = str.toLatin1();
        break;
    }

    const auto len = static_cast<uint32>(raw.size());
    writeUInt32(len);
    if (len > 0)
        write(raw.constData(), len);
}

// ---------------------------------------------------------------------------
// SafeFile
// ---------------------------------------------------------------------------

SafeFile::SafeFile(const QString& filePath, QIODevice::OpenMode mode)
{
    if (!open(filePath, mode))
        throw FileException("Failed to open file: " + filePath.toStdString());
}

SafeFile::~SafeFile()
{
    close();
}

bool SafeFile::open(const QString& filePath, QIODevice::OpenMode mode)
{
    m_file.setFileName(filePath);
    return m_file.open(mode);
}

void SafeFile::close()
{
    if (m_file.isOpen())
        m_file.close();
}

bool SafeFile::isOpen() const
{
    return m_file.isOpen();
}

QString SafeFile::filePath() const
{
    return m_file.fileName();
}

qint64 SafeFile::read(void* buf, qint64 count)
{
    const qint64 bytesRead = m_file.read(static_cast<char*>(buf), count);
    if (bytesRead < count)
        throw FileException("Failed to read requested bytes from file");
    return bytesRead;
}

void SafeFile::write(const void* buf, qint64 count)
{
    const qint64 bytesWritten = m_file.write(static_cast<const char*>(buf), count);
    if (bytesWritten < count)
        throw FileException("Failed to write requested bytes to file");
}

qint64 SafeFile::seek(qint64 offset, int from)
{
    qint64 newPos = 0;
    switch (from) {
    case 0: // SEEK_SET
        newPos = offset;
        break;
    case 1: // SEEK_CUR
        newPos = m_file.pos() + offset;
        break;
    case 2: // SEEK_END
        newPos = m_file.size() + offset;
        break;
    default:
        throw FileException("Invalid seek origin");
    }
    if (!m_file.seek(newPos))
        throw FileException("Seek failed");
    return m_file.pos();
}

qint64 SafeFile::position() const
{
    return m_file.pos();
}

qint64 SafeFile::length() const
{
    return m_file.size();
}

// ---------------------------------------------------------------------------
// SafeMemFile
// ---------------------------------------------------------------------------

SafeMemFile::SafeMemFile()
{
    m_buffer.setBuffer(&m_data);
    m_buffer.open(QIODevice::ReadWrite);
}

SafeMemFile::SafeMemFile(const QByteArray& data)
    : m_data(data)
{
    m_buffer.setBuffer(&m_data);
    m_buffer.open(QIODevice::ReadOnly);
}

SafeMemFile::SafeMemFile(const uint8* data, qint64 size)
    : m_data(reinterpret_cast<const char*>(data), size)
{
    m_buffer.setBuffer(&m_data);
    m_buffer.open(QIODevice::ReadOnly);
}

SafeMemFile::~SafeMemFile()
{
    m_buffer.close();
}

const QByteArray& SafeMemFile::buffer() const
{
    return m_data;
}

QByteArray SafeMemFile::takeBuffer()
{
    m_buffer.close();
    QByteArray result = std::move(m_data);
    m_data.clear();
    m_buffer.setBuffer(&m_data);
    m_buffer.open(QIODevice::ReadWrite);
    return result;
}

qint64 SafeMemFile::read(void* buf, qint64 count)
{
    const qint64 bytesRead = m_buffer.read(static_cast<char*>(buf), count);
    if (bytesRead < count)
        throw FileException("Attempt to read past end of memory file");
    return bytesRead;
}

void SafeMemFile::write(const void* buf, qint64 count)
{
    const qint64 bytesWritten = m_buffer.write(static_cast<const char*>(buf), count);
    if (bytesWritten < count)
        throw FileException("Failed to write to memory file");
}

qint64 SafeMemFile::seek(qint64 offset, int from)
{
    qint64 newPos = 0;
    switch (from) {
    case 0: // SEEK_SET
        newPos = offset;
        break;
    case 1: // SEEK_CUR
        newPos = m_buffer.pos() + offset;
        break;
    case 2: // SEEK_END
        newPos = m_buffer.size() + offset;
        break;
    default:
        throw FileException("Invalid seek origin");
    }
    if (!m_buffer.seek(newPos))
        throw FileException("Seek failed in memory file");
    return m_buffer.pos();
}

qint64 SafeMemFile::position() const
{
    return m_buffer.pos();
}

qint64 SafeMemFile::length() const
{
    return m_buffer.size();
}

} // namespace eMule
