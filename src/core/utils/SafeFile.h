#pragma once

/// @file SafeFile.h
/// @brief Portable file I/O classes replacing MFC CFileDataIO / CSafeFile / CSafeMemFile.
///
/// Provides:
///   FileDataIO  — abstract base with typed read/write (replaces CFileDataIO)
///   SafeFile    — QFile-backed implementation (replaces CSafeFile)
///   SafeMemFile — QBuffer-backed in-memory implementation (replaces CSafeMemFile)

#include "Types.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <cstdint>
#include <stdexcept>

namespace eMule {

/// UTF-8 encoding mode for string read/write.
enum class UTF8Mode : uint8 {
    None,       ///< Write as local codepage (Latin-1 fallback)
    OptBOM,     ///< Write BOM+UTF-8 if non-ASCII, else Latin-1
    Raw         ///< Write raw UTF-8 (no BOM)
};

/// Exception thrown on file I/O errors.
class FileException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// FileDataIO — abstract interface
// ---------------------------------------------------------------------------

/// Abstract base class for typed binary I/O, matching the original CFileDataIO.
class FileDataIO {
public:
    virtual ~FileDataIO() = default;

    virtual qint64 read(void* buf, qint64 count) = 0;
    virtual void write(const void* buf, qint64 count) = 0;
    virtual qint64 seek(qint64 offset, int from) = 0;
    virtual qint64 position() const = 0;
    virtual qint64 length() const = 0;

    // Typed readers
    uint8  readUInt8();
    uint16 readUInt16();
    uint32 readUInt32();
    uint64 readUInt64();
    void   readHash16(uint8* hash);
    QString readString(bool optUTF8);
    QString readString(bool optUTF8, uint32 rawSize);

    // Typed writers
    void writeUInt8(uint8 val);
    void writeUInt16(uint16 val);
    void writeUInt32(uint32 val);
    void writeUInt64(uint64 val);
    void writeHash16(const uint8* hash);
    void writeString(const QString& str, UTF8Mode encode);
    void writeLongString(const QString& str, UTF8Mode encode);
};

// ---------------------------------------------------------------------------
// SafeFile — QFile-backed
// ---------------------------------------------------------------------------

/// File-based implementation of FileDataIO using QFile.
class SafeFile : public FileDataIO {
public:
    SafeFile() = default;
    explicit SafeFile(const QString& filePath, QIODevice::OpenMode mode = QIODevice::ReadOnly);
    ~SafeFile() override;

    SafeFile(const SafeFile&) = delete;
    SafeFile& operator=(const SafeFile&) = delete;

    bool open(const QString& filePath, QIODevice::OpenMode mode);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString filePath() const;

    qint64 read(void* buf, qint64 count) override;
    void write(const void* buf, qint64 count) override;
    qint64 seek(qint64 offset, int from) override;
    qint64 position() const override;
    qint64 length() const override;

private:
    QFile m_file;
};

// ---------------------------------------------------------------------------
// SafeMemFile — QBuffer-backed in-memory
// ---------------------------------------------------------------------------

/// In-memory implementation of FileDataIO using QBuffer.
class SafeMemFile : public FileDataIO {
public:
    /// Create an empty growable mem file.
    SafeMemFile();

    /// Create from existing data (read-only view).
    explicit SafeMemFile(const QByteArray& data);

    /// Create from existing raw buffer (read-only view).
    SafeMemFile(const uint8* data, qint64 size);

    ~SafeMemFile() override;

    SafeMemFile(const SafeMemFile&) = delete;
    SafeMemFile& operator=(const SafeMemFile&) = delete;

    /// Access the underlying buffer.
    [[nodiscard]] const QByteArray& buffer() const;

    /// Detach and return the buffer contents.
    [[nodiscard]] QByteArray takeBuffer();

    qint64 read(void* buf, qint64 count) override;
    void write(const void* buf, qint64 count) override;
    qint64 seek(qint64 offset, int from) override;
    qint64 position() const override;
    qint64 length() const override;

private:
    QByteArray m_data;
    QBuffer m_buffer;
};

} // namespace eMule
