#include "pch.h"
/// @file ArchiveScan.cpp
/// @brief ZIP, RAR, ACE and ISO header listing for (partial) files — see ArchiveScan.h.

#include "archive/ArchiveScan.h"

#include <QByteArray>
#include <QIODevice>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <optional>

namespace eMule {

namespace {

// ---------------------------------------------------------------------------
// Reading with the gap list in mind
// ---------------------------------------------------------------------------

class Source {
public:
    Source(QIODevice& device, qint64 size, const ArchiveGaps& gaps)
        : m_device(device), m_size(size), m_gaps(gaps)
    {
    }

    [[nodiscard]] qint64 size() const { return m_size; }

    /// Are the bytes [offset, offset + length) all in the file?
    [[nodiscard]] bool present(qint64 offset, qint64 length) const
    {
        if (offset < 0 || length < 0 || offset + length > m_size)
            return false;
        if (length == 0)
            return true;
        const qint64 last = offset + length - 1;
        return std::none_of(m_gaps.cbegin(), m_gaps.cend(), [&](const std::pair<qint64, qint64>& gap) {
            return gap.first <= last && gap.second >= offset;
        });
    }

    /// Where the gap-free stretch that reaches the end of the file begins, not
    /// before @p from.
    [[nodiscard]] qint64 presentTailStart(qint64 from) const
    {
        qint64 start = from;
        for (const auto& gap : m_gaps) {
            if (gap.second >= start)
                start = std::max(start, std::min(gap.second + 1, m_size));
        }
        return start;
    }

    /// The bytes, or nothing when any of them is missing or unreadable.
    [[nodiscard]] std::optional<QByteArray> read(qint64 offset, qint64 length) const
    {
        if (!present(offset, length) || !m_device.seek(offset))
            return std::nullopt;
        QByteArray data = m_device.read(length);
        if (data.size() != length)
            return std::nullopt;
        return data;
    }

private:
    QIODevice& m_device;
    qint64 m_size;
    const ArchiveGaps& m_gaps;
};

[[nodiscard]] quint16 le16(const QByteArray& d, qsizetype at)
{
    return static_cast<quint16>(static_cast<uchar>(d[at]) | (static_cast<uchar>(d[at + 1]) << 8));
}

[[nodiscard]] quint32 le32(const QByteArray& d, qsizetype at)
{
    return static_cast<quint32>(le16(d, at)) | (static_cast<quint32>(le16(d, at + 2)) << 16);
}

[[nodiscard]] quint64 le64(const QByteArray& d, qsizetype at)
{
    return static_cast<quint64>(le32(d, at)) | (static_cast<quint64>(le32(d, at + 4)) << 32);
}

/// MS-DOS date and time, as ZIP and RAR 4 store it.
[[nodiscard]] QDateTime dosDateTime(quint16 date, quint16 time)
{
    const QDate d(((date >> 9) & 0x7F) + 1980, (date >> 5) & 0x0F, date & 0x1F);
    const QTime t((time >> 11) & 0x1F, (time >> 5) & 0x3F, (time & 0x1F) * 2);
    return d.isValid() && t.isValid() ? QDateTime(d, t) : QDateTime();
}

// ---------------------------------------------------------------------------
// ZIP
// ---------------------------------------------------------------------------

constexpr quint32 kZipLocal = 0x04034B50;
constexpr quint32 kZipCentral = 0x02014B50;
constexpr quint32 kZipEnd = 0x06054B50;
constexpr quint32 kZip64End = 0x06064B50;
constexpr quint32 kZip64Locator = 0x07064B50;
constexpr qint64 kZipLocalFixed = 30;
constexpr qint64 kZipCentralFixed = 46;
constexpr qint64 kZipEndFixed = 22;

[[nodiscard]] QString zipName(const QByteArray& raw, quint16 flags)
{
    // Bit 11: UTF-8. Otherwise the DOS code page; Latin-1 keeps every byte visible.
    return (flags & 0x0800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);
}

/// ZIP64 sizes from the extra field (header id 1), for the values that read 0xFFFFFFFF.
void applyZip64(const QByteArray& extra, quint64& size, quint64& packed, quint64* localOffset)
{
    for (qsizetype at = 0; at + 4 <= extra.size();) {
        const quint16 id = le16(extra, at);
        const quint16 length = le16(extra, at + 2);
        if (at + 4 + length > extra.size())
            return;
        if (id == 0x0001) {
            qsizetype p = at + 4;
            const qsizetype end = p + length;
            if (size == 0xFFFFFFFFu && p + 8 <= end) { size = le64(extra, p); p += 8; }
            if (packed == 0xFFFFFFFFu && p + 8 <= end) { packed = le64(extra, p); p += 8; }
            if (localOffset && *localOffset == 0xFFFFFFFFu && p + 8 <= end) *localOffset = le64(extra, p);
            return;
        }
        at += 4 + length;
    }
}

/// The entries of the central directory at @p offset; nothing when it cannot be read.
[[nodiscard]] bool readZipCentralDirectory(const Source& src, qint64 offset, quint64 count,
                                           ArchiveScanResult& result)
{
    qint64 pos = offset;
    for (quint64 i = 0; i < count; ++i) {
        const auto fixed = src.read(pos, kZipCentralFixed);
        if (!fixed || le32(*fixed, 0) != kZipCentral)
            return i > 0;   // what was read so far stands
        const quint16 flags = le16(*fixed, 8);
        const quint16 nameLen = le16(*fixed, 28);
        const quint16 extraLen = le16(*fixed, 30);
        const quint16 commentLen = le16(*fixed, 32);
        const auto tail = src.read(pos + kZipCentralFixed, nameLen + extraLen + commentLen);
        if (!tail)
            return i > 0;

        ArchiveScanEntry entry;
        entry.name = zipName(tail->left(nameLen), flags);
        entry.comment = zipName(tail->mid(nameLen + extraLen, commentLen), flags);
        entry.crc = le32(*fixed, 16);
        entry.hasCrc = true;
        entry.packedSize = le32(*fixed, 20);
        entry.size = le32(*fixed, 24);
        quint64 localOffset = le32(*fixed, 42);
        applyZip64(tail->mid(nameLen, extraLen), entry.size, entry.packedSize, &localOffset);
        entry.modified = dosDateTime(le16(*fixed, 14), le16(*fixed, 12));
        entry.encrypted = (flags & 0x01) != 0 || (flags & 0x40) != 0;
        entry.directory = entry.name.endsWith(u'/') || (le32(*fixed, 38) & 0x10) != 0;

        // The entry is whole when its local header and its data are there. The local
        // header carries its own extra field, so its length has to be read from it.
        const auto local = src.read(static_cast<qint64>(localOffset), kZipLocalFixed);
        if (!local || le32(*local, 0) != kZipLocal) {
            entry.complete = false;
        } else {
            // from the first byte of the local header to the last byte of the data
            const qint64 headerSize = kZipLocalFixed + le16(*local, 26) + le16(*local, 28);
            entry.complete = src.present(static_cast<qint64>(localOffset),
                                         headerSize + static_cast<qint64>(entry.packedSize));
        }

        result.passwordProtected = result.passwordProtected || entry.encrypted;
        result.entries.append(entry);
        pos += kZipCentralFixed + nameLen + extraLen + commentLen;
    }
    return true;
}

/// Without a central directory: walk the local headers from the start as far as
/// they go (MFC recoverZip's scan).
void readZipLocalHeaders(const Source& src, ArchiveScanResult& result)
{
    qint64 pos = 0;
    while (true) {
        const auto fixed = src.read(pos, kZipLocalFixed);
        if (!fixed || le32(*fixed, 0) != kZipLocal)
            return;
        const quint16 flags = le16(*fixed, 6);
        const quint16 nameLen = le16(*fixed, 26);
        const quint16 extraLen = le16(*fixed, 28);
        const auto tail = src.read(pos + kZipLocalFixed, nameLen + extraLen);
        if (!tail)
            return;

        ArchiveScanEntry entry;
        entry.name = zipName(tail->left(nameLen), flags);
        entry.crc = le32(*fixed, 14);
        entry.hasCrc = (flags & 0x08) == 0;   // with a data descriptor it comes after the data
        entry.packedSize = le32(*fixed, 18);
        entry.size = le32(*fixed, 22);
        applyZip64(tail->mid(nameLen), entry.size, entry.packedSize, nullptr);
        entry.modified = dosDateTime(le16(*fixed, 12), le16(*fixed, 10));
        entry.encrypted = (flags & 0x01) != 0 || (flags & 0x40) != 0;
        entry.directory = entry.name.endsWith(u'/');

        const qint64 dataStart = pos + kZipLocalFixed + nameLen + extraLen;
        entry.complete = src.present(dataStart, static_cast<qint64>(entry.packedSize));
        result.passwordProtected = result.passwordProtected || entry.encrypted;
        result.entries.append(entry);

        // Sizes that only follow the data: there is no telling where the next header is.
        if ((flags & 0x08) != 0 && entry.packedSize == 0 && !entry.directory)
            return;
        pos = dataStart + static_cast<qint64>(entry.packedSize);
    }
}

void scanZip(const Source& src, ArchiveScanResult& result)
{
    result.type = ArchiveScanType::Zip;

    // The end record is in the last 22 bytes plus at most a 64 KiB comment. Only the
    // part of that window that is there in one piece up to the end is searched: a
    // hole further up must not hide a directory that has arrived.
    const qint64 windowStart = src.presentTailStart(std::max<qint64>(0, src.size() - kZipEndFixed - 0xFFFF));
    const qint64 window = src.size() - windowStart;
    if (const auto tail = window >= kZipEndFixed ? src.read(windowStart, window) : std::nullopt) {
        for (qint64 at = window - kZipEndFixed; at >= 0; --at) {
            if (le32(*tail, at) != kZipEnd)
                continue;
            quint64 count = le16(*tail, at + 10);
            quint64 cdOffset = le32(*tail, at + 16);
            // ZIP64: the real numbers are in the record the locator points at
            if (count == 0xFFFF || cdOffset == 0xFFFFFFFFu) {
                const auto locator = src.read(windowStart + at - 20, 20);
                if (locator && le32(*locator, 0) == kZip64Locator) {
                    const auto end64 = src.read(static_cast<qint64>(le64(*locator, 8)), 56);
                    if (end64 && le32(*end64, 0) == kZip64End) {
                        count = le64(*end64, 32);
                        cdOffset = le64(*end64, 48);
                    }
                }
            }
            if (readZipCentralDirectory(src, static_cast<qint64>(cdOffset), count, result)) {
                result.status = static_cast<quint64>(result.entries.size()) == count
                                    ? ArchiveScanResult::Status::Ok
                                    : ArchiveScanResult::Status::ListIncomplete;
                return;
            }
            break;
        }
    }

    // No directory (the end of the file is not there yet): the local headers
    result.entries.clear();
    result.passwordProtected = false;
    readZipLocalHeaders(src, result);
    result.status = result.entries.isEmpty() ? ArchiveScanResult::Status::NoTableOfContents
                                             : ArchiveScanResult::Status::ListIncomplete;
}

// ---------------------------------------------------------------------------
// RAR 4 (1.5 - 4.x)
// ---------------------------------------------------------------------------

constexpr qint64 kRar4MarkSize = 7;
constexpr qint64 kRar5MarkSize = 8;

void scanRar4(const Source& src, ArchiveScanResult& result)
{
    qint64 pos = kRar4MarkSize;
    bool reachedEnd = false;
    while (pos + 7 <= src.size()) {
        const auto base = src.read(pos, 7);
        if (!base)
            break;
        const quint8 type = static_cast<quint8>((*base)[2]);
        const quint16 flags = le16(*base, 3);
        const quint16 headSize = le16(*base, 5);
        if (headSize < 7)
            break;

        if (type == 0x73) {   // main header
            result.hasComment = (flags & 0x0002) != 0;
            result.locked = (flags & 0x0004) != 0;
            result.solid = (flags & 0x0008) != 0;
            result.recoveryRecord = (flags & 0x0040) != 0;
            if (flags & 0x0080) {   // everything after it is encrypted
                result.passwordProtected = true;
                result.status = ArchiveScanResult::Status::HeadersEncrypted;
                return;
            }
            pos += headSize;
            continue;
        }
        if (type == 0x7B) {   // end of archive
            reachedEnd = true;
            break;
        }

        const auto head = src.read(pos, headSize);
        if (!head)
            break;
        quint64 addSize = (flags & 0x8000) && headSize >= 11 ? le32(*head, 7) : 0;

        if (type == 0x74 && headSize >= 32) {   // file header
            ArchiveScanEntry entry;
            quint64 unpacked = le32(*head, 11);
            const quint16 nameSize = le16(*head, 26);
            qsizetype at = 32;
            if (flags & 0x0100) {   // sizes beyond 4 GiB
                if (headSize < 40)
                    break;
                addSize |= static_cast<quint64>(le32(*head, 32)) << 32;
                unpacked |= static_cast<quint64>(le32(*head, 36)) << 32;
                at = 40;
            }
            if (at + nameSize > head->size())
                break;
            QByteArray rawName = head->mid(at, nameSize);
            // With a Unicode name the plain one comes first, up to a zero byte.
            if (flags & 0x0200) {
                if (const qsizetype zero = rawName.indexOf('\0'); zero >= 0)
                    rawName.truncate(zero);
            }
            entry.name = QString::fromUtf8(rawName).contains(QChar::ReplacementCharacter)
                             ? QString::fromLatin1(rawName) : QString::fromUtf8(rawName);
            entry.name.replace(u'\\', u'/');
            entry.size = unpacked;
            entry.packedSize = addSize;
            entry.crc = le32(*head, 16);
            entry.hasCrc = true;
            const quint32 ftime = le32(*head, 20);
            entry.modified = dosDateTime(static_cast<quint16>(ftime >> 16), static_cast<quint16>(ftime));
            entry.level = static_cast<quint8>((*head)[25]) - 0x30;
            entry.fromPrevVolume = (flags & 0x01) != 0;
            entry.toNextVolume = (flags & 0x02) != 0;
            entry.encrypted = (flags & 0x04) != 0;
            entry.hasComment = (flags & 0x08) != 0;
            entry.directory = (flags & 0xE0) == 0xE0;
            entry.complete = src.present(pos + headSize, static_cast<qint64>(addSize));
            result.passwordProtected = result.passwordProtected || entry.encrypted;
            result.entries.append(entry);
        }
        pos += headSize + static_cast<qint64>(addSize);
    }
    result.status = reachedEnd || pos >= src.size()
                        ? ArchiveScanResult::Status::Ok
                        : result.entries.isEmpty() ? ArchiveScanResult::Status::NoTableOfContents
                                                   : ArchiveScanResult::Status::ListIncomplete;
}

// ---------------------------------------------------------------------------
// RAR 5
// ---------------------------------------------------------------------------

/// A RAR 5 variable-length integer; false when it runs past the data.
[[nodiscard]] bool vint(const QByteArray& d, qsizetype& at, quint64& value)
{
    value = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        if (at >= d.size())
            return false;
        const quint8 byte = static_cast<quint8>(d[at++]);
        value |= static_cast<quint64>(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

void scanRar5(const Source& src, ArchiveScanResult& result)
{
    qint64 pos = kRar5MarkSize;
    bool reachedEnd = false;
    while (pos + 7 <= src.size()) {
        // CRC32, then the header size as a vint of at most three bytes
        const auto lead = src.read(pos, std::min<qint64>(7, src.size() - pos));
        if (!lead)
            break;
        qsizetype at = 4;
        quint64 headSize = 0;
        if (!vint(*lead, at, headSize) || headSize == 0 || headSize > 0x200000)
            break;
        const qint64 headStart = pos + at;
        const auto head = src.read(headStart, static_cast<qint64>(headSize));
        if (!head)
            break;

        qsizetype p = 0;
        quint64 type = 0, flags = 0, extraSize = 0, dataSize = 0;
        if (!vint(*head, p, type) || !vint(*head, p, flags))
            break;
        if ((flags & 0x01) && !vint(*head, p, extraSize))
            break;
        if ((flags & 0x02) && !vint(*head, p, dataSize))
            break;
        const qint64 dataStart = headStart + static_cast<qint64>(headSize);

        if (type == 4) {   // archive encryption header: the rest cannot be read
            result.passwordProtected = true;
            result.status = ArchiveScanResult::Status::HeadersEncrypted;
            return;
        }
        if (type == 5) {
            reachedEnd = true;
            break;
        }
        if (type == 1) {
            quint64 archiveFlags = 0;
            if (vint(*head, p, archiveFlags)) {
                result.solid = (archiveFlags & 0x04) != 0;
                result.recoveryRecord = (archiveFlags & 0x08) != 0;
                result.locked = (archiveFlags & 0x10) != 0;
            }
        } else if (type == 2 || type == 3) {
            quint64 fileFlags = 0, unpacked = 0, attributes = 0, compression = 0, hostOs = 0, nameLen = 0;
            if (!vint(*head, p, fileFlags) || !vint(*head, p, unpacked) || !vint(*head, p, attributes))
                break;
            ArchiveScanEntry entry;
            if (fileFlags & 0x02) {
                if (p + 4 > head->size())
                    break;
                entry.modified = QDateTime::fromSecsSinceEpoch(le32(*head, p), QTimeZone::UTC).toLocalTime();
                p += 4;
            }
            if (fileFlags & 0x04) {
                if (p + 4 > head->size())
                    break;
                entry.crc = le32(*head, p);
                entry.hasCrc = true;
                p += 4;
            }
            if (!vint(*head, p, compression) || !vint(*head, p, hostOs) || !vint(*head, p, nameLen)
                || p + static_cast<qsizetype>(nameLen) > head->size())
                break;
            entry.name = QString::fromUtf8(head->mid(p, static_cast<qsizetype>(nameLen)));
            p += static_cast<qsizetype>(nameLen);

            if (type == 3) {   // service header: "CMT" is the archive comment
                if (entry.name == QLatin1StringView("CMT"))
                    result.hasComment = true;
            } else {
                entry.size = unpacked;
                entry.packedSize = dataSize;
                entry.directory = (fileFlags & 0x01) != 0;
                entry.level = static_cast<int>((compression >> 7) & 0x07);
                entry.fromPrevVolume = (flags & 0x08) != 0;
                entry.toNextVolume = (flags & 0x10) != 0;
                // The extra area: a record of type 1 is the file's encryption
                const qsizetype extraStart = head->size() - static_cast<qsizetype>(extraSize);
                for (qsizetype e = std::max(extraStart, p); extraSize > 0 && e < head->size();) {
                    quint64 recordSize = 0, recordType = 0;
                    if (!vint(*head, e, recordSize))
                        break;
                    const qsizetype recordEnd = e + static_cast<qsizetype>(recordSize);
                    if (!vint(*head, e, recordType) || recordEnd > head->size())
                        break;
                    if (recordType == 1)
                        entry.encrypted = true;
                    e = recordEnd;
                }
                entry.complete = src.present(dataStart, static_cast<qint64>(dataSize));
                result.passwordProtected = result.passwordProtected || entry.encrypted;
                result.entries.append(entry);
            }
        }
        pos = dataStart + static_cast<qint64>(dataSize);
    }
    result.status = reachedEnd || pos >= src.size()
                        ? ArchiveScanResult::Status::Ok
                        : result.entries.isEmpty() ? ArchiveScanResult::Status::NoTableOfContents
                                                   : ArchiveScanResult::Status::ListIncomplete;
}

const QByteArray kRar4Mark = QByteArray::fromHex("526172211A0700");
const QByteArray kRar5Mark = QByteArray::fromHex("526172211A070100");

// ---------------------------------------------------------------------------
// ACE — libarchive has no reader for it. A chain of blocks:
// HEAD_CRC(2) HEAD_SIZE(2) HEAD_TYPE(1) HEAD_FLAGS(2) ..., HEAD_SIZE counted from
// HEAD_TYPE; a block flagged ADDSIZE is followed by that many bytes of data
// (MFC ArchiveRecovery.cpp recoverAce, ArchivePreviewDlg.cpp).
// ---------------------------------------------------------------------------

const QByteArray kAceMark = QByteArrayLiteral("**ACE**");
constexpr qint64 kAceMarkOffset = 7;
constexpr qint64 kAceBlockFixed = 7;     // up to and including HEAD_FLAGS
constexpr qint64 kAceFileFixed = 35;     // a file block up to its name

void scanAce(const Source& src, ArchiveScanResult& result)
{
    using Status = ArchiveScanResult::Status;
    result.type = ArchiveScanType::Ace;

    qint64 pos = 0;
    bool first = true;
    while (pos < src.size()) {
        const auto fixed = src.read(pos, kAceBlockFixed);
        if (!fixed) {
            result.status = Status::ListIncomplete;
            return;
        }
        const qint64 headSize = qint64{le16(*fixed, 2)} + 4;
        const quint8 type = static_cast<quint8>((*fixed)[4]);
        const quint16 flags = le16(*fixed, 5);
        std::optional<QByteArray> head;
        if (headSize >= kAceBlockFixed)
            head = src.read(pos, headSize);
        if (!head) {
            // not there yet, or not a block at all
            result.status = first ? Status::NoTableOfContents : Status::ListIncomplete;
            return;
        }

        quint64 addSize = 0;
        if ((flags & 0x0001) && headSize >= 11)
            addSize = le32(*head, 7);

        if (first) {
            if (type != 0 || head->mid(kAceMarkOffset, kAceMark.size()) != kAceMark) {
                result.status = Status::NoTableOfContents;
                return;
            }
            result.hasComment = (flags & 0x0002) != 0;
            result.recoveryRecord = (flags & 0x2000) != 0;
            result.locked = (flags & 0x4000) != 0;
            result.solid = (flags & 0x8000) != 0;
            first = false;
        } else if (type == 1 && headSize >= kAceFileFixed) {
            const quint16 nameSize = le16(*head, 33);
            if (kAceFileFixed + nameSize > headSize) {
                result.status = Status::ListIncomplete;
                return;
            }
            ArchiveScanEntry entry;
            const QByteArray rawName = head->mid(kAceFileFixed, nameSize);
            entry.name = QString::fromUtf8(rawName).contains(QChar::ReplacementCharacter)
                             ? QString::fromLatin1(rawName) : QString::fromUtf8(rawName);
            entry.name.replace(u'\\', u'/');
            entry.packedSize = addSize;
            entry.size = le32(*head, 11);
            const quint32 ftime = le32(*head, 15);
            entry.modified = dosDateTime(static_cast<quint16>(ftime >> 16), static_cast<quint16>(ftime));
            entry.directory = (le32(*head, 19) & 0x10) != 0;
            entry.crc = le32(*head, 23);
            entry.hasCrc = true;
            entry.hasComment = (flags & 0x0002) != 0;
            entry.fromPrevVolume = (flags & 0x1000) != 0;
            entry.toNextVolume = (flags & 0x2000) != 0;
            entry.encrypted = (flags & 0x4000) != 0;
            entry.complete = src.present(pos + headSize, static_cast<qint64>(addSize));
            result.passwordProtected = result.passwordProtected || entry.encrypted;
            result.entries.append(entry);
        }
        pos += headSize + static_cast<qint64>(addSize);
    }
    // A .part is only as long as what was written: a chain that points past its
    // end has blocks still to come.
    result.status = first ? Status::NoTableOfContents
                  : pos == src.size() ? Status::Ok : Status::ListIncomplete;
}

// ---------------------------------------------------------------------------
// ISO 9660 / Joliet — the directory tree, so that each file's extent can be held
// against the gap list (MFC ArchiveRecovery.cpp recoverISO / ISOReadDirectory).
// ---------------------------------------------------------------------------

const QByteArray kIsoMark = QByteArrayLiteral("CD001");
constexpr qint64 kIsoSector = 2048;
constexpr qint64 kIsoFirstDescriptor = 16;
constexpr qsizetype kIsoRecordFixed = 33;   // a directory record up to its name
constexpr qsizetype kIsoRootRecord = 156;   // in a volume descriptor
constexpr int kIsoMaxDepth = 64;
constexpr qsizetype kIsoMaxEntries = 200'000;

struct IsoWalk {
    const Source& src;
    ArchiveScanResult& result;
    qint64 block = kIsoSector;
    bool joliet = false;
    bool allRead = true;        ///< every directory sector was there
    QList<quint32> seen;        ///< directory extents, against loops
};

/// The Rock Ridge name ("NM" entries) of a record's system use area, following a
/// continuation area; empty when there is none.
[[nodiscard]] QString rockRidgeName(const IsoWalk& w, QByteArray area)
{
    QByteArray name;
    for (int hops = 0; hops < 4; ++hops) {
        std::optional<QByteArray> next;
        for (qsizetype at = 0; at + 4 <= area.size();) {
            const auto sigOk = [&](qsizetype i) { return area[i] >= 'A' && area[i] <= 'Z'; };
            const int len = static_cast<uchar>(area[at + 2]);
            if (!sigOk(at) || !sigOk(at + 1) || len < 4 || at + len > area.size())
                break;
            if (area[at] == 'N' && area[at + 1] == 'M' && len >= 5) {
                if ((static_cast<uchar>(area[at + 4]) & 0x06) == 0)   // not "." / ".."
                    name += area.mid(at + 5, len - 5);
            } else if (area[at] == 'C' && area[at + 1] == 'E' && len >= 28) {
                next = w.src.read(qint64{le32(area, at + 4)} * w.block + le32(area, at + 12),
                                  le32(area, at + 20));
            }
            at += len;
        }
        if (!next)
            break;
        area = *next;
    }
    if (name.isEmpty())
        return {};
    const QString utf8 = QString::fromUtf8(name);
    return utf8.contains(QChar::ReplacementCharacter) ? QString::fromLatin1(name) : utf8;
}

[[nodiscard]] QString isoName(const IsoWalk& w, const QByteArray& record, qsizetype nameLen)
{
    const QByteArray raw = record.mid(kIsoRecordFixed, nameLen);
    QString name;
    if (w.joliet) {   // UTF-16BE
        for (qsizetype i = 0; i + 1 < raw.size(); i += 2)
            name += QChar(static_cast<char16_t>((static_cast<uchar>(raw[i]) << 8) | static_cast<uchar>(raw[i + 1])));
    } else {
        // the system use area starts on an even offset after the name
        name = rockRidgeName(w, record.mid(kIsoRecordFixed + nameLen + ((nameLen & 1) ? 0 : 1)));
        if (!name.isEmpty())
            return name;
        name = QString::fromLatin1(raw);
    }
    // ";1" is the file version, and a name without extension still ends in a dot
    if (const qsizetype version = name.lastIndexOf(u';'); version >= 0)
        name.truncate(version);
    if (!w.joliet && name.endsWith(u'.'))
        name.chop(1);
    return name;
}

void readIsoDirectory(IsoWalk& w, quint32 extent, quint32 length, const QString& path, int depth)
{
    if (depth > kIsoMaxDepth || w.seen.contains(extent))
        return;
    w.seen.append(extent);

    const qint64 start = qint64{extent} * w.block;
    bool continued = false;   // the record before had "more extents follow"
    for (qint64 offset = 0; offset < length; offset += w.block) {
        const auto sector = w.src.read(start + offset, w.block);
        if (!sector) {
            w.allRead = false;
            continue;
        }
        for (qsizetype at = 0; at + kIsoRecordFixed <= sector->size();) {
            const qsizetype len = static_cast<uchar>((*sector)[at]);
            if (len == 0)
                break;   // the rest of the sector is padding
            const qsizetype nameLen = static_cast<uchar>((*sector)[at + 32]);
            if (len < kIsoRecordFixed + nameLen || at + len > sector->size()) {
                w.allRead = false;
                break;
            }
            const QByteArray record = sector->mid(at, len);
            at += len;

            const quint8 flags = static_cast<quint8>(record[25]);
            const bool directory = (flags & 0x02) != 0;
            if (directory && nameLen == 1 && static_cast<uchar>(record[kIsoRecordFixed]) <= 1)
                continue;   // "." and ".."
            if (w.result.entries.size() >= kIsoMaxEntries)
                return;

            const quint32 dataExtent = le32(record, 2);
            const quint32 dataLength = le32(record, 10);
            const bool present = w.src.present(qint64{dataExtent} * w.block, dataLength);
            if (continued && !w.result.entries.isEmpty()) {
                // a file beyond 4 GiB is a run of records under one name
                ArchiveScanEntry& whole = w.result.entries.last();
                whole.size += dataLength;
                whole.complete = whole.complete && present;
                continued = (flags & 0x80) != 0;
                continue;
            }
            continued = (flags & 0x80) != 0;

            ArchiveScanEntry entry;
            entry.name = path + isoName(w, record, nameLen);
            entry.directory = directory;
            entry.hidden = (flags & 0x01) != 0;
            entry.readOnly = (flags & 0x10) != 0;
            entry.complete = present;
            if (directory)
                entry.name += u'/';
            else
                entry.size = entry.packedSize = dataLength;
            // years since 1900, then the offset from GMT in quarter hours
            const auto byte = [&](qsizetype i) { return static_cast<uchar>(record[i]); };
            const QDate date(1900 + byte(18), byte(19), byte(20));
            const QTime time(byte(21), byte(22), byte(23));
            if (date.isValid() && time.isValid()) {
                entry.modified = QDateTime(date, time,
                                           QTimeZone::fromSecondsAheadOfUtc(static_cast<qint8>(record[24]) * 900))
                                     .toLocalTime();
            }
            const QString name = entry.name;
            w.result.entries.append(entry);
            if (directory)
                readIsoDirectory(w, dataExtent, dataLength, name, depth + 1);
        }
    }
}

void scanIso(const Source& src, ArchiveScanResult& result)
{
    using Status = ArchiveScanResult::Status;
    result.type = ArchiveScanType::Iso;

    // The volume descriptors, up to the set terminator
    std::optional<QByteArray> primary, jolietVolume;
    qint64 sector = kIsoFirstDescriptor;
    for (; sector < kIsoFirstDescriptor + 64; ++sector) {
        const auto descriptor = src.read(sector * kIsoSector, kIsoSector);
        if (!descriptor || descriptor->mid(1, kIsoMark.size()) != kIsoMark)
            break;
        const quint8 type = static_cast<quint8>((*descriptor)[0]);
        if (type == 0xFF) {
            ++sector;
            break;
        }
        if (type == 0x01 && !primary) {
            primary = descriptor;
            result.iso9660 = true;
        } else if (type == 0x02 && (*descriptor)[88] == 0x25 && (*descriptor)[89] == 0x2F
                   && ((*descriptor)[90] == 0x40 || (*descriptor)[90] == 0x43 || (*descriptor)[90] == 0x45)) {
            jolietVolume = descriptor;
            result.joliet = true;
        } else if (type == 0x00 && descriptor->mid(7).startsWith("EL TORITO SPECIFICATION")) {
            result.bootable = true;
        }
    }
    // UDF: an extended area with an NSR descriptor follows the set
    for (qint64 end = sector + 4; sector < end; ++sector) {
        const auto magic = src.read(sector * kIsoSector + 1, 5);
        if (magic && (*magic == "NSR02" || *magic == "NSR03"))
            result.udf = true;
    }

    const std::optional<QByteArray>& volume = jolietVolume ? jolietVolume : primary;
    if (!volume) {
        result.type = ArchiveScanType::Unknown;   // UDF only: not ours to list
        return;
    }
    IsoWalk walk{.src = src, .result = result, .joliet = jolietVolume.has_value()};
    if (const quint16 block = le16(*volume, 128); block != 0)
        walk.block = block;
    readIsoDirectory(walk, le32(*volume, kIsoRootRecord + 2), le32(*volume, kIsoRootRecord + 10), {}, 0);

    result.status = walk.allRead ? Status::Ok
                  : result.entries.isEmpty() ? Status::InsufficientData : Status::ListIncomplete;
}

} // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

QString ArchiveScanEntry::attributes() const
{
    QStringList parts;
    if (encrypted)
        parts << QStringLiteral("P");
    if (directory)
        parts << QStringLiteral("D");
    if (hidden)
        parts << QStringLiteral("H");
    if (readOnly)
        parts << QStringLiteral("R");
    if (fromPrevVolume)
        parts << QStringLiteral("<");
    if (toNextVolume)
        parts << QStringLiteral(">");
    if (hasComment)
        parts << QStringLiteral("C");
    if (!complete)
        parts << QStringLiteral("M");
    QString text = parts.join(u',');
    if (level >= 0) {
        if (!text.isEmpty())
            text += QStringLiteral(", ");
        text += QStringLiteral("L%1").arg(level);
    }
    return text;
}

int ArchiveScanResult::fileCount() const
{
    return static_cast<int>(std::count_if(entries.cbegin(), entries.cend(),
                                          [](const ArchiveScanEntry& e) { return !e.directory; }));
}

QString ArchiveScanResult::infoLine(const QString& passwordText, const QString& commentText) const
{
    QStringList parts;
    if (passwordProtected)
        parts << passwordText;
    if (solid)
        parts << QStringLiteral("Solid");
    if (locked)
        parts << QStringLiteral("Locked");
    if (recoveryRecord)
        parts << QStringLiteral("RecoveryRec");
    if (hasComment)
        parts << commentText;
    return parts.join(u',');
}

QString ArchiveScanResult::imageInfoLine(const QString& bootableText) const
{
    QStringList parts;
    if (bootable)
        parts << bootableText;
    if (iso9660)
        parts << QStringLiteral("ISO9660");
    if (joliet)
        parts << QStringLiteral("Joliet");
    if (udf)
        parts << QStringLiteral("UDF");
    return parts.join(u',');
}

ArchiveScanType detectArchiveType(QIODevice& device, const ArchiveGaps& gaps)
{
    const Source src(device, device.size(), gaps);
    const auto head = src.read(0, std::min<qint64>(8, device.size()));
    if (!head || head->size() < 4)
        return ArchiveScanType::Unknown;
    if (head->startsWith(kRar4Mark) || head->startsWith(kRar5Mark))
        return ArchiveScanType::Rar;
    const quint32 sig = le32(*head, 0);
    // a local header, or the end record of an archive without entries
    if (sig == kZipLocal || sig == kZipEnd)
        return ArchiveScanType::Zip;
    if (const auto mark = src.read(kAceMarkOffset, kAceMark.size()); mark && *mark == kAceMark)
        return ArchiveScanType::Ace;
    if (const auto mark = src.read(kIsoFirstDescriptor * kIsoSector + 1, kIsoMark.size()); mark && *mark == kIsoMark)
        return ArchiveScanType::Iso;
    return ArchiveScanType::Unknown;
}

ArchiveScanResult scanArchive(QIODevice& device, qint64 fileSize, const ArchiveGaps& gaps)
{
    ArchiveScanResult result;
    const Source src(device, fileSize, gaps);

    // What it is can only be told from the start of the file (MFC: "Insufficient
    // data available." until the first bytes are there).
    const auto head = src.read(0, std::min<qint64>(8, fileSize));
    if (!head || head->size() < 8) {
        result.status = ArchiveScanResult::Status::InsufficientData;
        return result;
    }

    if (head->startsWith(kRar5Mark)) {
        result.type = ArchiveScanType::Rar;
        scanRar5(src, result);
    } else if (head->startsWith(kRar4Mark)) {
        result.type = ArchiveScanType::Rar;
        scanRar4(src, result);
    } else if (const quint32 sig = le32(*head, 0); sig == kZipLocal || sig == kZipEnd) {
        scanZip(src, result);
    } else if (const auto mark = src.read(kAceMarkOffset, kAceMark.size()); mark && *mark == kAceMark) {
        scanAce(src, result);
    } else if (const auto iso = src.read(kIsoFirstDescriptor * kIsoSector + 1, kIsoMark.size()); iso && *iso == kIsoMark) {
        scanIso(src, result);
    }
    return result;
}

} // namespace eMule
