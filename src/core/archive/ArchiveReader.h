#pragma once

/// @file ArchiveReader.h
/// @brief Unified archive reader using libarchive — replaces ZIPFile, RARFile, GZipFile.
///
/// Supports ZIP, RAR (read), 7z, GZip, tar, ISO, CAB, and 30+ formats
/// via automatic format detection. Multi-volume RAR works: libarchive's RAR4 and
/// RAR5 readers both follow the remaining volumes themselves once opened on the
/// first one, so callers pass volume 1 and nothing else.
///
/// **Member names are untrusted.** Since the Usenet module started feeding this
/// archives downloaded from strangers, every destination path goes through
/// safeEntryPath(), which refuses anything that would escape the destination
/// directory. Callers must never join a raw entryName() onto a path themselves.

#include "utils/Types.h"

#include <QDateTime>
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
    [[nodiscard]] QDateTime entryMtime(int index) const;
    [[nodiscard]] bool entryIsDir(int index) const;
    [[nodiscard]] uint16 entryMode(int index) const;
    [[nodiscard]] QStringList entryNames() const;

    /// Passphrase for encrypted members, applied at open(). libarchive can
    /// decrypt ZIP and 7z. It can only *detect* RAR encryption, never undo it —
    /// see hasEncryptedEntries().
    void setPassphrase(const QString& passphrase);

    /// True when the archive declared any member's data encrypted. For RAR this
    /// is as far as libarchive goes, so a caller must report it rather than let
    /// extraction fail with a meaningless read error.
    [[nodiscard]] bool hasEncryptedEntries() const;

    /// libarchive's name for the detected format, e.g. "RAR5", "ZIP", "7-Zip".
    [[nodiscard]] QString formatName() const;

    /// Extract one member. Re-scans the archive to reach @p index, so extracting
    /// everything this way is quadratic — use extractAll() for that. Kept for
    /// random access, which is what the archive preview panel does.
    ///
    /// @p destPath is used as given; the caller owns its safety.
    bool extractEntry(int index, const QString& destPath);

    /// Extract every member into @p destDir in a **single pass**.
    ///
    /// Both properties matter. The single pass is what makes a solid or
    /// multi-volume archive viable at all — the per-entry path re-reads every
    /// volume for every file. And each member is placed through safeEntryPath(),
    /// so a hostile name cannot write outside @p destDir; refusals are collected
    /// in rejectedEntries() rather than aborting the extraction.
    bool extractAll(const QString& destDir);

    /// Members the last extractAll() refused as unsafe. Empty on a clean archive.
    [[nodiscard]] QStringList rejectedEntries() const;

    /// Resolve an archive member name against a destination directory.
    ///
    /// Returns an empty string for a name that must not be extracted at all:
    /// absolute paths, drive letters, UNC roots and any `..` component. Names
    /// that are merely awkward are rewritten rather than refused — a Windows
    /// reserved device name (`aux.c`, `con`) gains an underscore, and trailing
    /// dots and spaces are trimmed, because Windows strips those itself and that
    /// is its own way out of a directory.
    [[nodiscard]] static QString safeEntryPath(const QString& destDir, const QString& entryName);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace eMule
