#pragma once

/// @file ArchiveReader.h
/// @brief Unified archive reader using libarchive — replaces ZIPFile, RARFile, GZipFile.
///
/// Supports ZIP, RAR (read), 7z, GZip, tar, ISO, CAB, and 30+ formats
/// via automatic format detection.

#include "utils/Types.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace eMule {

class ArchiveReader {
public:
    ArchiveReader();
    ~ArchiveReader();

    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    bool open(const QString& filePath);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] int entryCount() const;
    [[nodiscard]] QString entryName(int index) const;
    [[nodiscard]] uint64 entrySize(int index) const;
    [[nodiscard]] QStringList entryNames() const;

    bool extractEntry(int index, const QString& destPath);
    bool extractAll(const QString& destDir);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace eMule
