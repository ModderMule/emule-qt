#pragma once

/// @file YencDecoder.h
/// @brief Streaming yEnc decoder with per-part CRC32 verification.
///
/// Fed one article line at a time, straight off the socket, and writes decoded
/// bytes out through a sink. Nothing buffers a whole article: they run to
/// roughly 750 KB encoded and the point of streaming is not to hold one twice.
///
/// What a yEnc article looks like:
///
///     =ybegin part=12 total=97 line=128 size=1073741824 name=foo.r00
///     =ypart begin=805306369 end=879001600
///     <encoded bytes>
///     =yend size=73695232 part=12 pcrc32=a1b2c3d4
///
/// Three properties this relies on, and one trap:
///
///   - `=ypart begin/end` are absolute offsets **into the final file**, so a
///     part places itself with no knowledge of any other part. That is why the
///     writer can write sparsely and why articles need no ordering.
///   - `pcrc32` verifies that part alone. The whole-file `crc32` on the last
///     part is optional and often absent.
///   - Decoder state is per-article. Nothing carries across articles except an
///     escape `=` that straddles a socket read, which is why the escape flag
///     lives here and not in the caller.
///   - **`begin` is 1-based.** The file offset is `begin - 1`. Getting this
///     wrong shifts every part by one byte and the CRCs still pass, because
///     they check the payload, not its placement.
///
/// Also handles the malformed single-line header real posters emit:
///
///     =ybegin part=1 total=10 line=128 size=500000 name=mybinary.dat=ypart begin=1 end=100000
///
/// documented in NZBGet's `daemon/nntp/Decoder.h::ParseName`. A decoder that
/// treats the whole tail as the filename produces garbage names and no offsets.

#include <QByteArray>
#include <QString>

#include <functional>

namespace eMule::usenet {

class YencDecoder {
public:
    enum class Status : quint8 {
        Incomplete,    ///< no =yend seen yet
        Ok,
        NoBinaryData,  ///< never saw =ybegin
        CrcMismatch,
        SizeMismatch,
        Malformed,
    };

    /// Receives decoded bytes in order. Called many times per article.
    using Sink = std::function<void(QByteArrayView)>;

    explicit YencDecoder(Sink sink = {});

    void setSink(Sink sink) { m_sink = std::move(sink); }

    /// Discard all state. Call between articles — the decoder is reusable, and
    /// reusing it is the point.
    void reset();

    /// Feed one line, without its CRLF, already dot-unstuffed.
    void feedLine(QByteArrayView line);

    /// Final verdict. Valid once =yend has been seen; Incomplete before that.
    [[nodiscard]] Status status() const { return m_status; }
    [[nodiscard]] QString statusText() const;

    // -- Header fields, valid once =ybegin has been seen ---------------------

    /// Filename from `=ybegin name=`. For an obfuscated post this is the only
    /// place the real name appears.
    [[nodiscard]] const QString& fileName() const { return m_fileName; }

    /// Total size of the *whole file*, from `=ybegin size=`.
    [[nodiscard]] qint64 fileSize() const { return m_fileSize; }

    /// Zero-based offset of this part in the file — already `begin - 1`.
    [[nodiscard]] qint64 offset() const { return m_offset; }

    /// Bytes this part contributes.
    [[nodiscard]] qint64 partSize() const { return m_decodedBytes; }

    [[nodiscard]] int partNumber() const { return m_part; }
    [[nodiscard]] int partTotal() const { return m_total; }

    /// Whether the article carried `=ypart`. Single-part posts legitimately do
    /// not, and then the part simply starts at offset 0.
    [[nodiscard]] bool hasPartHeader() const { return m_sawPart; }

    [[nodiscard]] quint32 calculatedCrc() const { return m_crc; }
    [[nodiscard]] quint32 expectedCrc() const { return m_expectedCrc; }

private:
    void parseBegin(QByteArrayView line);
    void parsePart(QByteArrayView line);
    void parseEnd(QByteArrayView line);
    void decodeInto(QByteArrayView line);

    Sink m_sink;
    Status m_status = Status::NoBinaryData;

    bool m_sawBegin = false;
    bool m_sawPart = false;
    bool m_escape = false;          ///< an '=' ended the previous line

    QString m_fileName;
    qint64 m_fileSize = 0;
    qint64 m_offset = 0;
    qint64 m_endOffset = 0;
    int m_part = 0;
    int m_total = 0;

    qint64 m_decodedBytes = 0;
    qint64 m_expectedSize = -1;
    quint32 m_crc = 0;
    quint32 m_expectedCrc = 0;
    bool m_haveExpectedCrc = false;

    QByteArray m_out;               ///< reused per line; never holds an article

    /// rapidyenc's RapidYencDecoderState, held opaquely so this header does not
    /// drag <rapidyenc.h> into every consumer. It is a small enum-sized value;
    /// the static_assert in the .cpp pins the size.
    int m_rapidState = 0;
};

/// yEnc uses the same CRC32 as zip/gzip (reflected, poly 0xEDB88320).
[[nodiscard]] quint32 yencCrc32(quint32 crc, QByteArrayView data);

} // namespace eMule::usenet
