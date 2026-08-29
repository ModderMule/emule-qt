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

    /// PAR2 recovery volumes are scheduled last: they are only fetched when
    /// something else came up short.
    [[nodiscard]] bool isPar2() const;
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
