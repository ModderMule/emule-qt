#pragma once

/// @file RarFixtures.h
/// @brief Byte-crafted RAR volumes for tests.
///
/// There is no tool to make these with: `rar` is commercial and absent, `unrar`
/// and `7zz` only read, and libarchive cannot write RAR at all — so
/// tst_UsenetUnpack's `archive_write_*` trick does not transfer. Building them
/// by hand turns out to be the better fixture anyway: every offset a test
/// asserts was computed from the format, so a passing case pins the parser to
/// the spec rather than to whatever some encoder happened to emit.
///
/// Shared by tst_RarReader (which checks the parser) and tst_UsenetStream
/// (which posts these volumes through a fake news server and streams them).

#include <QByteArray>
#include <QList>
#include <QtEndian>

namespace eMule::testing::rar {

/// Standard CRC-32, the one both RAR formats use for their header checksums.
/// libarchive verifies them, so a fixture without them is rejected outright:
/// the format is recognised and the archive then lists no entries at all.
inline quint32 rarCrc32(const char* data, qsizetype len)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (qsizetype i = 0; i < len; ++i)
        crc = table[(crc ^ quint8(data[i])) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline quint32 rarCrc32(const QByteArray& b) { return rarCrc32(b.constData(), b.size()); }

void put8(QByteArray& b, quint8 v) { b.append(char(v)); }

void put16(QByteArray& b, quint16 v)
{
    char raw[2];
    qToLittleEndian(v, raw);
    b.append(raw, 2);
}

void put32(QByteArray& b, quint32 v)
{
    char raw[4];
    qToLittleEndian(v, raw);
    b.append(raw, 4);
}

/// Write the RAR4 block's HEAD_CRC: the low 16 bits of CRC-32 over the header
/// from just past the CRC field to its end.
inline void sealRar4Block(QByteArray& b)
{
    const quint16 crc = quint16(rarCrc32(b.constData() + 2, b.size() - 2) & 0xFFFF);
    char raw[2];
    qToLittleEndian(crc, raw);
    b[0] = raw[0];
    b[1] = raw[1];
}

/// RAR5's 7-bits-per-byte little-endian varint.
void putVint(QByteArray& b, quint64 v)
{
    do {
        quint8 byte = v & 0x7F;
        v >>= 7;
        if (v)
            byte |= 0x80;
        b.append(char(byte));
    } while (v);
}

// --- RAR4 builders ---------------------------------------------------------

constexpr quint16 kSplitBefore = 0x0001;
constexpr quint16 kSplitAfter  = 0x0002;
constexpr quint16 kFilePassword = 0x0004;
constexpr quint16 kLarge       = 0x0100;
constexpr quint16 kLongBlock   = 0x8000;

constexpr quint16 kMainSolid       = 0x0008;
constexpr quint16 kMainPassword    = 0x0080;
constexpr quint16 kMainFirstVolume = 0x0100;

QByteArray rar4Marker()
{
    return QByteArray::fromRawData("\x52\x61\x72\x21\x1A\x07\x00", 7);
}

QByteArray rar4Main(quint16 flags)
{
    QByteArray b;
    put16(b, 0);          // HEAD_CRC, filled in by sealRar4Block()
    put8(b, 0x73);
    put16(b, flags | 0x0001 /* MHD_VOLUME */);
    put16(b, 13);         // HEAD_SIZE
    put16(b, 0);          // HighPosAV
    put32(b, 0);          // PosAV
    sealRar4Block(b);
    return b;
}

/// @p method 0x30 is stored; anything else is compressed.
QByteArray rar4File(const QByteArray& name, qint64 packed, qint64 unpacked,
                    quint16 flags, quint8 method = 0x30, quint32 fileCrc = 0)
{
    const bool large = (flags & kLarge) != 0;
    const quint16 headSize = quint16(32 + (large ? 8 : 0) + name.size());

    QByteArray b;
    put16(b, 0);                       // HEAD_CRC, filled in by sealRar4Block()
    put8(b, 0x74);
    put16(b, flags | kLongBlock);
    put16(b, headSize);
    put32(b, quint32(packed & 0xFFFFFFFF));    // doubles as ADD_SIZE
    put32(b, quint32(unpacked & 0xFFFFFFFF));
    put8(b, 0x02);                     // HOST_OS
    put32(b, fileCrc);                 // FILE_CRC — of the whole unpacked file
    put32(b, 0);                       // FTIME
    put8(b, 20);                       // UNP_VER
    put8(b, method);
    put16(b, quint16(name.size()));
    put32(b, 0x20);                    // ATTR
    if (large) {
        put32(b, quint32(quint64(packed) >> 32));
        put32(b, quint32(quint64(unpacked) >> 32));
    }
    b.append(name);
    sealRar4Block(b);
    return b;
}

// --- RAR5 builders ---------------------------------------------------------

QByteArray rar5Marker()
{
    return QByteArray::fromRawData("\x52\x61\x72\x21\x1A\x07\x01\x00", 8);
}

/// Wraps a header body (from HeaderType onward) in its CRC and size fields.
QByteArray rar5Block(const QByteArray& body)
{
    QByteArray sized;
    putVint(sized, quint64(body.size()));
    sized.append(body);

    // The CRC covers the HeaderSize vint *and* the body — rar5.c reads
    // `raw_hdr_size + hdr_size_len` bytes back and sums them.
    QByteArray b;
    put32(b, rarCrc32(sized));
    b.append(sized);
    return b;
}

QByteArray rar5Main(int volumeNumber, bool solid = false)
{
    QByteArray body;
    putVint(body, 1);                       // HeaderType: main
    putVint(body, 0);                       // HeaderFlags
    quint64 arcFlags = 0x0001;              // volume
    if (volumeNumber > 0)
        arcFlags |= 0x0002;                 // volume number present
    if (solid)
        arcFlags |= 0x0004;
    putVint(body, arcFlags);
    if (volumeNumber > 0)
        putVint(body, quint64(volumeNumber));
    return rar5Block(body);
}

QByteArray rar5File(const QByteArray& name, qint64 packed, qint64 unpacked,
                    bool splitBefore, bool splitAfter, int method = 0)
{
    QByteArray body;
    putVint(body, 2);                       // HeaderType: file
    quint64 flags = 0x0002;                 // data area present
    if (splitBefore)
        flags |= 0x0008;
    if (splitAfter)
        flags |= 0x0010;
    putVint(body, flags);
    putVint(body, quint64(packed));         // DataSize
    putVint(body, 0);                       // FileFlags
    putVint(body, quint64(unpacked));
    putVint(body, 0);                       // Attributes
    putVint(body, quint64(method) << 7);    // CompressionInfo
    putVint(body, 0);                       // HostOS
    putVint(body, quint64(name.size()));
    body.append(name);
    return rar5Block(body);
}


/// A stored multi-volume set holding one file, exactly as `rar -m0 -v` lays it
/// out: volume 1 opens the file, the middle volumes are split on both sides,
/// and the last closes it. @p perVolume is the payload each volume carries.
///
/// @p rar5 picks the format. RAR4 has no volume number, so an obfuscated RAR4
/// set cannot be ordered from its headers — which is exactly the limitation the
/// streaming index documents.
inline QList<QByteArray> makeStoredRarSet(const QByteArray& innerName,
                                          const QByteArray& payload,
                                          qint64 perVolume,
                                          bool rar5 = false)
{
    QList<QByteArray> volumes;
    const qint64 total = payload.size();
    const quint32 wholeCrc = rarCrc32(payload);
    qint64 at = 0;
    int number = 0;

    while (at < total) {
        const qint64 take = qMin<qint64>(perVolume, total - at);
        const bool splitBefore = at > 0;
        const bool splitAfter = at + take < total;

        QByteArray vol;
        if (rar5) {
            vol = rar5Marker();
            vol += rar5Main(number);
            vol += rar5File(innerName, take, total, splitBefore, splitAfter);
        } else {
            vol = rar4Marker();
            vol += rar4Main(number == 0 ? kMainFirstVolume : quint16(0));
            quint16 flags = 0;
            if (splitBefore)
                flags |= kSplitBefore;
            if (splitAfter)
                flags |= kSplitAfter;
            vol += rar4File(innerName, take, total, flags, 0x30, wholeCrc);
        }
        vol += payload.mid(int(at), int(take));

        volumes.append(vol);
        at += take;
        ++number;
    }
    return volumes;
}

/// The same set with a compression method set, so nothing in it is mappable.
inline QList<QByteArray> makeCompressedRarSet(const QByteArray& innerName,
                                              const QByteArray& payload,
                                              qint64 perVolume)
{
    QList<QByteArray> volumes;
    const qint64 total = payload.size();
    qint64 at = 0;
    int number = 0;

    while (at < total) {
        const qint64 take = qMin<qint64>(perVolume, total - at);
        quint16 flags = 0;
        if (at > 0)
            flags |= kSplitBefore;
        if (at + take < total)
            flags |= kSplitAfter;

        QByteArray vol = rar4Marker();
        vol += rar4Main(number == 0 ? kMainFirstVolume : quint16(0));
        vol += rar4File(innerName, take, total, flags, /*method*/ 0x33);
        vol += payload.mid(int(at), int(take));

        volumes.append(vol);
        at += take;
        ++number;
    }
    return volumes;
}

} // namespace eMule::testing::rar
