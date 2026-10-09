#pragma once

/// @file ArchiveScan.h
/// @brief List a ZIP or RAR archive from its headers alone, knowing which bytes of
///        the file are there — MFC's archive preview scan (ArchivePreviewDlg.cpp,
///        ArchiveRecovery.cpp recoverZip / recoverRar used as scanners).
///
/// libarchive lists a complete archive but says nothing about CRCs, entry comments
/// or where an entry's data sits, and it cannot be told that parts of the file are
/// missing. A part file needs exactly that: which entries are already whole.
/// No dependency beyond Qt Core; nothing is decompressed.

#include <QDateTime>
#include <QList>
#include <QString>

#include <utility>

class QIODevice;

namespace eMule {

enum class ArchiveScanType { Unknown, Zip, Rar };

struct ArchiveScanEntry {
    QString name;
    quint64 size = 0;          ///< uncompressed
    quint64 packedSize = 0;
    quint32 crc = 0;
    bool hasCrc = false;
    QString comment;
    QDateTime modified;
    bool directory = false;
    bool encrypted = false;
    bool fromPrevVolume = false;   ///< RAR: continued from the previous volume
    bool toNextVolume = false;     ///< RAR: continues in the next volume
    bool hasComment = false;       ///< RAR 4 file comment flag
    int level = -1;                ///< RAR compression level 0..5, -1 unknown
    /// Every byte of the entry (its header and its packed data) is in the file.
    bool complete = true;

    /// MFC's attribute letters: P password, D directory, <, > volume continuation,
    /// C comment, M data missing, Lx compression level
    /// (ArchivePreviewDlg.cpp:695-737, 867-883).
    [[nodiscard]] QString attributes() const;
};

struct ArchiveScanResult {
    enum class Status {
        Ok,                  ///< table of contents read
        ListIncomplete,      ///< read as far as the data goes
        NoTableOfContents,   ///< nothing usable found
        InsufficientData,    ///< the start of the file is missing
        HeadersEncrypted     ///< RAR with encrypted headers
    };

    ArchiveScanType type = ArchiveScanType::Unknown;
    Status status = Status::NoTableOfContents;
    QList<ArchiveScanEntry> entries;
    // Archive-wide marks (RAR main header; "password" from any entry)
    bool passwordProtected = false;
    bool solid = false;
    bool locked = false;
    bool recoveryRecord = false;
    bool hasComment = false;

    /// Files, without directories (MFC uArchiveFileEntries).
    [[nodiscard]] int fileCount() const;
    /// "Password protection,Solid,Locked,RecoveryRec,Comment" as MFC's info line
    /// (ArchivePreviewDlg.cpp:764-789); the two words that MFC translates are passed in.
    [[nodiscard]] QString infoLine(const QString& passwordText, const QString& commentText) const;
};

/// Missing byte ranges of a part file, inclusive ends, as the gap list has them.
using ArchiveGaps = QList<std::pair<qint64, qint64>>;

/// What the first bytes say the file is; needs the first 8 bytes to be present.
[[nodiscard]] ArchiveScanType detectArchiveType(QIODevice& device, const ArchiveGaps& gaps);

/// List @p device (random access, @p fileSize bytes long). Ranges in @p gaps are
/// treated as absent whatever the device returns for them.
[[nodiscard]] ArchiveScanResult scanArchive(QIODevice& device, qint64 fileSize, const ArchiveGaps& gaps = {});

} // namespace eMule
