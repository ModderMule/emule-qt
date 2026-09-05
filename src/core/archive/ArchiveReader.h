#pragma once

/// @file ArchiveReader.h
/// @brief Unified archive reader using libarchive — replaces ZIPFile, RARFile, GZipFile.
///
/// Supports ZIP, RAR (read), 7z, GZip, tar, ISO, CAB, and 30+ formats
/// via automatic format detection.
///
/// **Multi-volume sets need every volume, in order.** libarchive reads a set as
/// one continuous byte stream over the file list the *client* supplies; the RAR
/// readers never open a sibling volume by name. Handed volume 1 alone, a set
/// stops at the end of volume 1. So pass the whole list to open(QStringList).
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

/// libarchive's opaque handle. Forward-declared at global scope on purpose: an
/// elaborated `struct archive*` inside namespace eMule would declare a *new*
/// eMule::archive and every libarchive call would stop matching.
struct archive;

namespace eMule {

/// Supplies an archive's volumes to the reader, in order.
///
/// The only thing that differs between unpacking a finished download and
/// unpacking one still in flight is where the next volume comes from, so that
/// is the only thing abstracted.
class ArchiveVolumeSource {
public:
    virtual ~ArchiveVolumeSource() = default;

    /// Path of volume @p index, blocking until it exists. False ends the set —
    /// either genuinely, or because the caller cancelled.
    [[nodiscard]] virtual bool volumePath(int index, QString& out) = 0;
};

class ArchiveReader {
public:
    ArchiveReader();
    ~ArchiveReader();

    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    bool open(const QString& filePath);

    /// Open a multi-volume set. Volumes must be in volume order — the stream is
    /// their concatenation, so a wrong order is a corrupt archive.
    bool open(const QStringList& volumes);

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

    /// Extract every member of a set whose volumes arrive over time, in one
    /// pass, blocking inside libarchive whenever @p source has no next volume
    /// yet. That is what unpacks a download while it is still running.
    ///
    /// There is deliberately no open()/entry-index form for a live set: a scan
    /// would consume the stream, and a volume can only be read once as it lands.
    /// Safety and refusals work exactly as in extractAll().
    bool extractAllFrom(ArchiveVolumeSource& source, const QString& destDir);

    /// Paths the last extractAll()/extractAllFrom() actually wrote. The only way
    /// to know what a live set produced, since it has no entry index.
    [[nodiscard]] QStringList extractedFiles() const;

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
    /// Create, configure and open a libarchive handle over the volumes recorded
    /// by the last open(). Every read path needs one and they differ in nothing
    /// but what they do with it. Returns nullptr on failure, already logged.
    [[nodiscard]] ::archive* openArchive(const char* what) const;

    /// Walk the archive once, recording entry metadata. The open() forms only.
    [[nodiscard]] bool scanEntries();

    /// The extraction loop itself, over an already-opened handle. Shared by the
    /// on-disk and the live path, which differ only in how @p ar was opened.
    [[nodiscard]] bool extractAllInto(::archive* ar, const QString& destDir);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace eMule
