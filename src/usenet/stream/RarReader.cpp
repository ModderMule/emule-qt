#include "stream/RarReader.h"

#include <QtEndian>

#include <cstring>

namespace eMule::usenet {

namespace {

// --- RAR4 -----------------------------------------------------------------

constexpr char kRar4Marker[] = "\x52\x61\x72\x21\x1A\x07\x00";
constexpr int  kRar4MarkerLen = 7;

constexpr quint8 kRar4TypeMain = 0x73;
constexpr quint8 kRar4TypeFile = 0x74;

constexpr quint16 kMhdSolid       = 0x0008;
constexpr quint16 kMhdPassword    = 0x0080;   // header encryption
constexpr quint16 kMhdFirstVolume = 0x0100;

constexpr quint16 kLhdSplitBefore = 0x0001;
constexpr quint16 kLhdSplitAfter  = 0x0002;
constexpr quint16 kLhdPassword    = 0x0004;
constexpr quint16 kLhdSolid       = 0x0010;
constexpr quint16 kLhdLarge       = 0x0100;   // 64-bit size extension
constexpr quint16 kLhdSalt        = 0x0400;
constexpr quint16 kLhdLongBlock   = 0x8000;

constexpr quint8 kRar4MethodStore = 0x30;

// --- RAR5 -----------------------------------------------------------------

constexpr char kRar5Marker[] = "\x52\x61\x72\x21\x1A\x07\x01\x00";
constexpr int  kRar5MarkerLen = 8;

constexpr quint64 kRar5TypeMain      = 1;
constexpr quint64 kRar5TypeFile      = 2;
constexpr quint64 kRar5TypeArcCrypt  = 4;
constexpr quint64 kRar5TypeEndOfArc  = 5;

constexpr quint64 kHdrExtraArea   = 0x0001;
constexpr quint64 kHdrDataArea    = 0x0002;
constexpr quint64 kHdrSplitBefore = 0x0008;
constexpr quint64 kHdrSplitAfter  = 0x0010;

constexpr quint64 kArcVolume      = 0x0001;
constexpr quint64 kArcVolNumber   = 0x0002;
constexpr quint64 kArcSolid       = 0x0004;

constexpr quint64 kFileTimePresent = 0x0002;
constexpr quint64 kFileCrcPresent  = 0x0004;

constexpr quint64 kExtraRecFileEncryption = 1;

/// A bounds-checked forward cursor. Every read either succeeds or trips `bad`,
/// after which further reads are no-ops returning zero — so a caller may read a
/// whole header and check once at the end instead of after every field.
class Cursor {
public:
    Cursor(const QByteArray& buf, qint64 at) : m_buf(buf), m_at(at) {}

    [[nodiscard]] bool bad() const { return m_bad; }
    [[nodiscard]] qint64 pos() const { return m_at; }
    void seek(qint64 to) { m_at = to; }

    void skip(qint64 n)
    {
        if (!want(n))
            return;
        m_at += n;
    }

    quint8 u8()
    {
        if (!want(1))
            return 0;
        return quint8(m_buf.at(int(m_at++)));
    }

    quint16 u16()
    {
        if (!want(2))
            return 0;
        const auto v = qFromLittleEndian<quint16>(m_buf.constData() + m_at);
        m_at += 2;
        return v;
    }

    quint32 u32()
    {
        if (!want(4))
            return 0;
        const auto v = qFromLittleEndian<quint32>(m_buf.constData() + m_at);
        m_at += 4;
        return v;
    }

    /// RAR5's variable-length integer: 7 bits per byte, little-endian, high bit
    /// set means "another byte follows". Capped at 10 bytes so a corrupt stream
    /// cannot spin.
    quint64 vint()
    {
        quint64 out = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (!want(1))
                return 0;
            const quint8 b = quint8(m_buf.at(int(m_at++)));
            out |= quint64(b & 0x7F) << shift;
            if (!(b & 0x80))
                return out;
        }
        m_bad = true;
        return 0;
    }

    QByteArray bytes(qint64 n)
    {
        if (n < 0) {
            m_bad = true;
            return {};
        }
        if (!want(n))
            return {};
        const QByteArray out = m_buf.mid(int(m_at), int(n));
        m_at += n;
        return out;
    }

private:
    bool want(qint64 n)
    {
        if (m_bad)
            return false;
        if (n < 0 || m_at < 0 || m_at + n > m_buf.size()) {
            m_bad = true;
            return false;
        }
        return true;
    }

    const QByteArray& m_buf;
    qint64 m_at = 0;
    bool m_bad = false;
};

/// RAR4 stores the name as ASCII, and when LHD_UNICODE is set follows it with a
/// NUL and a compressed Unicode form. The ASCII half is all we need — the name
/// only picks a MIME type and identifies the inner file.
QString rar4Name(const QByteArray& raw)
{
    const qsizetype nul = raw.indexOf('\0');
    return QString::fromLatin1(nul >= 0 ? raw.left(nul) : raw);
}

RarVolume parseRar4(const QByteArray& buf)
{
    RarVolume out;
    Cursor c(buf, kRar4MarkerLen);

    while (true) {
        const qint64 blockStart = c.pos();
        if (blockStart >= buf.size())
            break;

        c.u16();                              // HEAD_CRC, deliberately unverified
        const quint8  type  = c.u8();
        const quint16 flags = c.u16();
        const quint16 headSize = c.u16();

        if (c.bad()) {
            // Ran off the end mid-header. If we already have the file header we
            // came for, that is a complete answer, not a truncation.
            if (!out.entries.isEmpty())
                out.status = RarParse::Ok;
            return out;
        }
        if (headSize < 7)
            return out;   // nonsense; leaves status NeedMoreBytes

        // LONG_BLOCK means an ADD_SIZE field follows the common header. On a
        // *file* header that field is PACK_SIZE itself — read it twice and every
        // subsequent offset is four bytes wrong.
        qint64 addSize = 0;
        if ((flags & kLhdLongBlock) && type != kRar4TypeFile)
            addSize = c.u32();

        if (type == kRar4TypeMain) {
            if (flags & kMhdPassword) {
                out.status = RarParse::Unsupported;
                out.headersEncrypted = true;
                out.reason = QStringLiteral("Encrypted archive headers");
                return out;
            }
            out.isFirstVolume = (flags & kMhdFirstVolume) != 0;
            out.solid = (flags & kMhdSolid) != 0;
        } else if (type == kRar4TypeFile) {
            RarEntry e;
            quint64 packLow = c.u32();
            quint64 unpLow  = c.u32();
            c.u8();                            // HOST_OS
            c.u32();                           // FILE_CRC
            c.u32();                           // FTIME
            c.u8();                            // UNP_VER
            const quint8 method = c.u8();
            const quint16 nameSize = c.u16();
            c.u32();                           // ATTR

            quint64 packHigh = 0;
            quint64 unpHigh = 0;
            if (flags & kLhdLarge) {
                packHigh = c.u32();
                unpHigh = c.u32();
            }

            const QByteArray rawName = c.bytes(nameSize);
            if (c.bad())
                return out;                    // NeedMoreBytes

            e.name = rar4Name(rawName);
            e.stored = (method == kRar4MethodStore);
            e.encrypted = (flags & kLhdPassword) != 0;
            e.splitBefore = (flags & kLhdSplitBefore) != 0;
            e.splitAfter = (flags & kLhdSplitAfter) != 0;
            e.packedSize = qint64((packHigh << 32) | packLow);
            e.unpackedSize = qint64((unpHigh << 32) | unpLow);
            e.dataOffset = blockStart + headSize;

            if (flags & kLhdSolid)
                out.solid = true;
            if (flags & kLhdSalt)
                e.encrypted = true;

            out.entries.append(e);
            out.status = RarParse::Ok;

            // The next block sits past this entry's payload, which is far
            // outside any probe buffer. One file header per volume is what a
            // split set has, and it is what we came for.
            return out;
        }

        const qint64 next = blockStart + headSize + addSize;
        if (next <= blockStart)
            return out;
        c.seek(next);
        if (c.pos() >= buf.size()) {
            if (!out.entries.isEmpty())
                out.status = RarParse::Ok;
            return out;
        }
    }

    return out;
}

/// Walk a RAR5 extra area looking for the file-encryption record. Each record is
/// `Size vint, Type vint, payload`, where Size counts Type plus payload.
bool rar5ExtraSaysEncrypted(const QByteArray& buf, qint64 start, qint64 end)
{
    Cursor c(buf, start);
    while (c.pos() < end && !c.bad()) {
        const qint64 recStart = c.pos();
        const quint64 size = c.vint();
        if (c.bad() || size == 0)
            return false;
        const qint64 afterSize = c.pos();
        const quint64 type = c.vint();
        if (c.bad())
            return false;
        if (type == kExtraRecFileEncryption)
            return true;
        const qint64 next = afterSize + qint64(size);
        if (next <= recStart)
            return false;
        c.seek(next);
    }
    return false;
}

RarVolume parseRar5(const QByteArray& buf)
{
    RarVolume out;
    Cursor c(buf, kRar5MarkerLen);

    while (true) {
        const qint64 blockStart = c.pos();
        if (blockStart >= buf.size())
            break;

        c.u32();                               // header CRC32, deliberately unverified
        const qint64 sizeFieldAt = c.pos();
        const quint64 headerSize = c.vint();
        if (c.bad()) {
            if (!out.entries.isEmpty())
                out.status = RarParse::Ok;
            return out;
        }
        const qint64 bodyStart = c.pos();
        const qint64 headerEnd = bodyStart + qint64(headerSize);
        if (headerSize == 0 || headerEnd <= sizeFieldAt)
            return out;

        const quint64 type = c.vint();
        const quint64 flags = c.vint();

        quint64 extraSize = 0;
        if (flags & kHdrExtraArea)
            extraSize = c.vint();
        quint64 dataSize = 0;
        if (flags & kHdrDataArea)
            dataSize = c.vint();

        if (c.bad())
            return out;

        if (type == kRar5TypeArcCrypt) {
            out.status = RarParse::Unsupported;
            out.headersEncrypted = true;
            out.reason = QStringLiteral("Encrypted archive headers");
            return out;
        }

        if (type == kRar5TypeMain) {
            const quint64 arcFlags = c.vint();
            out.solid = (arcFlags & kArcSolid) != 0;
            if (arcFlags & kArcVolNumber) {
                const quint64 n = c.vint();
                out.volumeNumber = int(n);
            } else if (arcFlags & kArcVolume) {
                // A volume set whose first member omits the field: volume zero.
                out.volumeNumber = 0;
            }
            out.isFirstVolume = (out.volumeNumber <= 0);
            if (c.bad())
                return out;
        } else if (type == kRar5TypeFile) {
            RarEntry e;
            const quint64 fileFlags = c.vint();
            const quint64 unpackedSize = c.vint();
            c.vint();                          // attributes
            if (fileFlags & kFileTimePresent)
                c.u32();
            if (fileFlags & kFileCrcPresent)
                c.u32();
            const quint64 compInfo = c.vint();
            c.vint();                          // host OS
            const quint64 nameLength = c.vint();
            const QByteArray rawName = c.bytes(qint64(nameLength));
            if (c.bad())
                return out;                    // NeedMoreBytes

            e.name = QString::fromUtf8(rawName);
            e.stored = ((compInfo >> 7) & 0x07) == 0;
            e.splitBefore = (flags & kHdrSplitBefore) != 0;
            e.splitAfter = (flags & kHdrSplitAfter) != 0;
            e.packedSize = qint64(dataSize);
            e.unpackedSize = qint64(unpackedSize);
            e.dataOffset = headerEnd;

            if ((compInfo >> 6) & 0x01)
                out.solid = true;
            if (extraSize > 0) {
                const qint64 extraStart = headerEnd - qint64(extraSize);
                if (extraStart >= bodyStart && headerEnd <= buf.size())
                    e.encrypted = rar5ExtraSaysEncrypted(buf, extraStart, headerEnd);
            }

            out.entries.append(e);
            out.status = RarParse::Ok;
            return out;
        } else if (type == kRar5TypeEndOfArc) {
            if (!out.entries.isEmpty())
                out.status = RarParse::Ok;
            return out;
        }

        const qint64 next = headerEnd + qint64(dataSize);
        if (next <= blockStart)
            return out;
        c.seek(next);
        if (c.bad() || c.pos() >= buf.size()) {
            if (!out.entries.isEmpty())
                out.status = RarParse::Ok;
            return out;
        }
    }

    return out;
}

} // namespace

RarVolume parseRarVolume(const QByteArray& head)
{
    RarVolume out;

    if (head.size() >= kRar5MarkerLen
        && std::memcmp(head.constData(), kRar5Marker, kRar5MarkerLen) == 0) {
        out = parseRar5(head);
    } else if (head.size() >= kRar4MarkerLen
               && std::memcmp(head.constData(), kRar4Marker, kRar4MarkerLen) == 0) {
        out = parseRar4(head);
    } else if (head.size() < kRar5MarkerLen) {
        // Too short to tell a marker from a truncation. Asking for more bytes is
        // the only answer that cannot be wrong.
        out.status = RarParse::NeedMoreBytes;
        return out;
    } else {
        out.status = RarParse::NotRar;
        return out;
    }

    if (out.status == RarParse::Ok && out.solid) {
        out.status = RarParse::Unsupported;
        out.reason = QStringLiteral("Solid archive — cannot seek without decompressing");
        return out;
    }

    return out;
}

} // namespace eMule::usenet
