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

    /// How many articles the subject counter claims that this file does not
    /// list. Zero when `partsTotal` is 0 — **not** because nothing is missing,
    /// but because an obfuscated post carries no counter and the question cannot
    /// be answered. Treating that as "complete" is what hasAllSegments() does
    /// and is the right default; treating it as "all missing" would read every
    /// obfuscated release as unfetchable.
    ///
    /// Also zero when the counter is *lower* than the segment list, which real
    /// posts do — some posters put the file count in the subject rather than the
    /// part count.
    [[nodiscard]] int missingSegmentCount() const;

    /// Mean encoded article size, used to price missingSegmentCount() in bytes.
    /// Zero for a file with no segments, which the parser drops anyway.
    [[nodiscard]] qint64 meanSegmentBytes() const;

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

/// What an .nzb is short of *by its own account*.
///
/// **Not a statement about any server.** A file whose subject says `(1/42)`
/// while listing 40 segments means the indexer never saw two articles; those
/// articles may well still be on every provider, but this NZB cannot ask for
/// them. Whether the servers still hold what the NZB *does* list is a separate
/// question with a separate answer — see UsenetQueue's availability probe.
struct NzbShortfall {
    /// Articles listed, i.e. what the queue will actually try to fetch.
    int listedSegments = 0;

    /// Articles the subject counters claim exist and the NZB does not list.
    int missingSegments = 0;

    /// missingSegments priced at each file's own mean article size. Estimated:
    /// the NZB says nothing about the size of an article it does not list.
    qint64 missingBytes = 0;

    /// Encoded size of the PAR2 recovery volumes this release ships. A
    /// shortfall smaller than this is very likely repairable, which is why a
    /// bare percentage would be alarmism.
    qint64 recoveryBytes = 0;

    /// Files with no part counter, so with no opinion either way. A release
    /// that is entirely these is not "100% complete" — it is unknown.
    int unknownFiles = 0;

    /// Percentage of the claimed article count that the NZB actually lists.
    /// 100 when nothing is known to be missing, including when nothing is
    /// knowable.
    [[nodiscard]] int percent() const;

    /// Whether the shortfall is small enough for the shipped recovery data to
    /// plausibly cover. Byte-level and deliberately crude: the par2 block size
    /// is not knowable until the index file is downloaded.
    [[nodiscard]] bool likelyRecoverable() const
    { return missingBytes == 0 || recoveryBytes >= missingBytes; }
};

/// One .nzb.
struct NzbInfo {
    QString name;        ///< display name, normally the .nzb file's own name
    QString password;    ///< from <meta type="password">, or the filename
    QList<NzbFileInfo> files;

    [[nodiscard]] qint64 totalEncodedBytes() const;
    [[nodiscard]] int segmentCount() const;
    [[nodiscard]] bool isEmpty() const { return files.isEmpty(); }

    /// What this NZB is short of, summed over its files. Free — no network.
    [[nodiscard]] NzbShortfall shortfall() const;
};

} // namespace eMule::usenet
