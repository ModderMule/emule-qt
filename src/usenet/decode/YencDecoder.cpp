#include "decode/YencDecoder.h"

#include <QCoreApplication>

#include <array>

#if EMULE_HAVE_RAPIDYENC
#  include <rapidyenc.h>
#  include <mutex>
static_assert(sizeof(RapidYencDecoderState) <= sizeof(int),
              "YencDecoder::m_rapidState is too small for RapidYencDecoderState");
#endif

namespace eMule::usenet {

namespace {

/// Reflected CRC-32 (poly 0xEDB88320) — the zip/gzip one, which is what yEnc's
/// pcrc32 is. Built once at first use.
const std::array<quint32, 256>& crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

/// Value of `key=` in a yEnc header line, up to the next space. Returns a view
/// into @p line, so it must not outlive it.
QByteArrayView headerValue(QByteArrayView line, const char* key)
{
    const QByteArrayView needle(key, qstrlen(key));
    const qsizetype at = line.indexOf(needle);
    if (at < 0)
        return {};
    const qsizetype from = at + needle.size();
    qsizetype to = line.indexOf(' ', from);
    if (to < 0)
        to = line.size();
    return line.sliced(from, to - from);
}

qint64 headerNumber(QByteArrayView line, const char* key, qint64 fallback = 0)
{
    const auto value = headerValue(line, key);
    if (value.isEmpty())
        return fallback;
    bool ok = false;
    const qint64 n = QByteArray(value.data(), value.size()).toLongLong(&ok);
    return ok ? n : fallback;
}

} // namespace

quint32 yencCrc32(quint32 crc, QByteArrayView data)
{
#if EMULE_HAVE_RAPIDYENC
    // Same polynomial as the table below -- tst_UsenetYenc pins both against
    // the standard "123456789" vector, so a divergence fails loudly.
    static std::once_flag init;
    std::call_once(init, [] { rapidyenc_crc_init(); });
    return rapidyenc_crc(data.data(), size_t(data.size()), crc);
#else
    const auto& table = crcTable();
    quint32 c = crc ^ 0xFFFFFFFFu;
    for (const char ch : data)
        c = table[(c ^ static_cast<quint8>(ch)) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
#endif
}

YencDecoder::YencDecoder(Sink sink)
    : m_sink(std::move(sink))
{
}

void YencDecoder::reset()
{
    m_status = Status::NoBinaryData;
    m_sawBegin = false;
    m_sawPart = false;
    m_escape = false;
    m_fileName.clear();
    m_fileSize = 0;
    m_offset = 0;
    m_endOffset = 0;
    m_part = 0;
    m_total = 0;
    m_decodedBytes = 0;
    m_expectedSize = -1;
    m_crc = 0;
    m_expectedCrc = 0;
    m_haveExpectedCrc = false;
    m_out.clear();
    // Must be cleared with m_escape, which it mirrors on the SIMD path: a
    // leftover escape state would silently corrupt the *next* article.
    m_rapidState = 0;
}

void YencDecoder::feedLine(QByteArrayView line)
{
    if (line.startsWith("=ybegin ")) {
        parseBegin(line);
        return;
    }
    if (line.startsWith("=ypart ")) {
        parsePart(line);
        return;
    }
    if (line.startsWith("=yend")) {
        parseEnd(line);
        return;
    }

    // Anything before =ybegin is the article's own headers (Subject:, From:,
    // ...), which BODY does not send but ARTICLE would. Ignore rather than
    // decode: treating them as payload corrupts the file silently.
    if (!m_sawBegin)
        return;

    decodeInto(line);
}

void YencDecoder::parseBegin(QByteArrayView line)
{
    m_sawBegin = true;
    m_status = Status::Incomplete;

    m_part = int(headerNumber(line, "part=", 0));
    m_total = int(headerNumber(line, "total=", 0));
    m_fileSize = headerNumber(line, "size=", 0);

    // name= runs to the end of the line, not to the next space -- filenames
    // contain spaces. But some posters emit the =ypart header glued onto the
    // same line, in which case the name stops there and the rest is a real
    // =ypart to parse (NZBGet daemon/nntp/Decoder.h::ParseName documents this).
    const qsizetype nameAt = line.indexOf(QByteArrayView("name=", 5));
    if (nameAt >= 0) {
        QByteArrayView tail = line.sliced(nameAt + 5);
        const qsizetype glued = tail.indexOf(QByteArrayView("=ypart ", 7));
        if (glued >= 0) {
            const QByteArrayView partHeader = tail.sliced(glued);
            tail = tail.first(glued);
            m_fileName = QString::fromUtf8(tail.data(), tail.size()).trimmed();
            parsePart(partHeader);
            return;
        }
        m_fileName = QString::fromUtf8(tail.data(), tail.size()).trimmed();
    }
}

void YencDecoder::parsePart(QByteArrayView line)
{
    m_sawPart = true;

    // begin is 1-BASED. The file offset is begin - 1, and getting this wrong
    // shifts every part by a byte while every CRC still passes -- they verify
    // the payload, not where it lands.
    const qint64 begin = headerNumber(line, "begin=", 1);
    m_offset = begin > 0 ? begin - 1 : 0;
    m_endOffset = headerNumber(line, "end=", 0);
}

void YencDecoder::parseEnd(QByteArrayView line)
{
    m_expectedSize = headerNumber(line, "size=", -1);

    const auto crcText = headerValue(line, "pcrc32=");
    if (!crcText.isEmpty()) {
        bool ok = false;
        m_expectedCrc = QByteArray(crcText.data(), crcText.size()).toUInt(&ok, 16);
        m_haveExpectedCrc = ok;
    }

    if (!m_sawBegin) {
        m_status = Status::NoBinaryData;
        return;
    }
    if (m_expectedSize >= 0 && m_expectedSize != m_decodedBytes) {
        m_status = Status::SizeMismatch;
        return;
    }
    // A missing pcrc32 is common on single-part posts; absence is not failure.
    if (m_haveExpectedCrc && m_expectedCrc != m_crc) {
        m_status = Status::CrcMismatch;
        return;
    }
    m_status = Status::Ok;
}

void YencDecoder::decodeInto(QByteArrayView line)
{
    m_out.resize(line.size());
    char* out = m_out.data();
    qsizetype produced = 0;

#if EMULE_HAVE_RAPIDYENC
    {
        static std::once_flag init;
        std::call_once(init, [] { rapidyenc_decode_init(); });

        // is_raw = 1: dot-unstuffing and line endings were handled by
        // NntpSocket before we ever saw this line, so rapidyenc must not try to
        // do them again. m_rapidState carries the escape across lines -- the
        // same job m_escape does on the scalar path.
        auto* state = reinterpret_cast<RapidYencDecoderState*>(&m_rapidState);
        produced = qsizetype(rapidyenc_decode_ex(1, line.data(), out,
                                                 size_t(line.size()), state));
        if (produced > 0) {
            const QByteArrayView decoded(m_out.constData(), produced);
            m_crc = yencCrc32(m_crc, decoded);
            m_decodedBytes += produced;
            if (m_sink)
                m_sink(decoded);
        }
        return;
    }
#endif

    for (qsizetype i = 0; i < line.size(); ++i) {
        auto ch = static_cast<quint8>(line[i]);

        if (m_escape) {
            // An '=' that ended the previous line escapes the first byte of
            // this one. This is the only state that crosses a line boundary.
            m_escape = false;
            out[produced++] = static_cast<char>(static_cast<quint8>(ch - 64 - 42));
            continue;
        }
        if (ch == '=') {
            if (i + 1 >= line.size()) {
                m_escape = true;
                continue;
            }
            ch = static_cast<quint8>(line[++i]);
            out[produced++] = static_cast<char>(static_cast<quint8>(ch - 64 - 42));
            continue;
        }
        out[produced++] = static_cast<char>(static_cast<quint8>(ch - 42));
    }

    if (produced == 0)
        return;

    const QByteArrayView decoded(m_out.constData(), produced);
    m_crc = yencCrc32(m_crc, decoded);
    m_decodedBytes += produced;
    if (m_sink)
        m_sink(decoded);
}

QString YencDecoder::statusText() const
{
    switch (m_status) {
    case Status::Incomplete:
        return QCoreApplication::translate("Usenet", "Article ended without =yend");
    case Status::Ok:
        return QCoreApplication::translate("Usenet", "OK");
    case Status::NoBinaryData:
        return QCoreApplication::translate("Usenet", "Article contains no yEnc data");
    case Status::CrcMismatch:
        return QCoreApplication::translate("Usenet", "CRC mismatch (article is corrupt)");
    case Status::SizeMismatch:
        return QCoreApplication::translate("Usenet", "Decoded size does not match =yend");
    case Status::Malformed:
        return QCoreApplication::translate("Usenet", "Malformed yEnc header");
    }
    return QCoreApplication::translate("Usenet", "Unknown");
}

} // namespace eMule::usenet
