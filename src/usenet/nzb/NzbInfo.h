#pragma once

/// @file NzbInfo.h
/// @brief The queue-item model an .nzb parses into.
///
/// Plain value types. The parser fills them, the queue schedules from them, and
/// the writer places bytes using them — nothing here knows about sockets.
///
/// One field is a trap worth stating up front: `NzbSegment::bytes` is the
/// **encoded** size, yEnc overhead and line breaks included. Decoded offsets
/// cannot be derived from an NZB at all; only the article's own `=ypart begin/
/// end` is authoritative. Treat `bytes` as a scheduling hint and a progress
/// denominator, never as a file offset.

#include <QList>
#include <QString>
#include <QStringList>

namespace eMule::usenet {

/// One article of one file.
struct NzbSegment {
    QString messageId;   ///< without angle brackets; the wire form adds them
    qint64 bytes = 0;    ///< ENCODED size, per the NZB. See the file comment.
    int number = 0;      ///< 1-based part number, as the NZB states it
};

/// One posted file, i.e. one `<file>` element.
struct NzbFileInfo {
    QString subject;
    QString poster;
    qint64 date = 0;             ///< seconds since epoch, 0 when absent
    QStringList groups;
    QList<NzbSegment> segments;

    /// Filename recovered from the subject. **Empty is a normal outcome**, not
    /// an error: obfuscated posts carry no readable name and the real one only
    /// appears in the first article's `=ybegin name=`.
    QString fileName;

    /// Total parts the subject claims, from its `(n/m)` counter. 0 when the
    /// subject has no counter — also normal, and not the same as "no parts".
    int partsTotal = 0;

    [[nodiscard]] qint64 encodedBytes() const;

    /// Whether the segment numbers form a complete 1..n run. Only meaningful
    /// when partsTotal is known; a false here means the NZB itself is short,
    /// which is different from an article being missing on a server.
    [[nodiscard]] bool hasAllSegments() const;

    /// Any part of a PAR2 set — the index file or a recovery volume.
    [[nodiscard]] bool isPar2() const;

    /// Recovery blocks this file carries, read out of a `.vol{start}+{count}.par2`
    /// name. Zero for the index `.par2`, which holds the file list and no
    /// recovery data at all, and zero for everything that is not par2.
    ///
    /// This is what makes on-demand fetching possible: the queue can total up
    /// exactly enough volumes to cover the damage par2 reported, instead of
    /// downloading a recovery set it will usually throw away.
    ///
    /// Both spellings occur — par2cmdline writes `rel.vol0+1.par2`, QuickPar and
    /// MultiPar pad to `rel.vol000+01.par2` — so the digits are parsed, never
    /// matched.
    [[nodiscard]] int par2RecoveryBlocks() const;

    /// A recovery volume as opposed to the index file. These are the files the
    /// initial download plan leaves out.
    [[nodiscard]] bool isPar2Volume() const { return par2RecoveryBlocks() > 0; }
};

/// One .nzb.
struct NzbInfo {
    QString name;        ///< display name, normally the .nzb file's own name
    QString password;    ///< from <meta type="password">, or the filename
    QList<NzbFileInfo> files;

    [[nodiscard]] qint64 totalEncodedBytes() const;
    [[nodiscard]] int segmentCount() const;
    [[nodiscard]] bool isEmpty() const { return files.isEmpty(); }
};

} // namespace eMule::usenet
