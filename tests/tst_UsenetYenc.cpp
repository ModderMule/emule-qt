/// @file tst_UsenetYenc.cpp
/// @brief yEnc decoding, per-part CRC, and the header shapes real posters emit.
///
/// Three of these exist because of specific, silent failure modes:
///
///   - **`=ypart begin` is 1-based.** The file offset is `begin - 1`. Get it
///     wrong and every part lands one byte late — while every CRC still passes,
///     because pcrc32 verifies the payload, not where it goes. Nothing else in
///     the pipeline would catch it.
///   - **The glued header.** Real posters emit
///     `=ybegin ... name=file.dat=ypart begin=1 end=100000` on one line. A
///     decoder that takes the whole tail as the filename gets a garbage name
///     *and* no offsets, so the part is written at 0 and overwrites part 1.
///   - **The escape can straddle a line.** A trailing `=` escapes the first
///     byte of the *next* line. That is the only decoder state that crosses a
///     line boundary, and it is exactly the state a per-line API is likely to
///     drop.

#include "decode/YencDecoder.h"

#include <QTest>

using namespace eMule::usenet;

namespace {

/// Reference yEnc encoder, written from the spec rather than from our decoder
/// so a round trip proves something. Emits `lineLength`-column output with the
/// escape rules applied.
QByteArrayList encodeYenc(const QByteArray& data, int lineLength = 128)
{
    QByteArrayList lines;
    QByteArray line;
    for (const char raw : data) {
        const auto enc = static_cast<quint8>(static_cast<quint8>(raw) + 42);
        // NUL, LF, CR and '=' must be escaped. TAB and space are escaped only
        // at line edges; escaping them always is legal and simpler.
        const bool needsEscape = enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=';
        if (needsEscape) {
            line.append('=');
            line.append(static_cast<char>(static_cast<quint8>(enc + 64)));
        } else {
            line.append(static_cast<char>(enc));
        }
        if (line.size() >= lineLength) {
            lines.append(line);
            line.clear();
        }
    }
    if (!line.isEmpty())
        lines.append(line);
    return lines;
}

QByteArray decodeAll(YencDecoder& decoder, const QByteArrayList& lines)
{
    QByteArray out;
    decoder.setSink([&out](QByteArrayView chunk) { out.append(chunk); });
    decoder.reset();
    for (const QByteArray& line : lines)
        decoder.feedLine(line);
    return out;
}

QByteArray patternBytes(int n)
{
    QByteArray data;
    data.resize(n);
    for (int i = 0; i < n; ++i)
        data[i] = static_cast<char>((i * 7 + (i >> 3)) & 0xFF);
    return data;
}

} // namespace

class tst_UsenetYenc : public QObject {
    Q_OBJECT

private slots:
    void crc32MatchesTheStandardVector();
    void roundTripsEveryByteValue();
    void singlePartArticleHasNoOffset();
    void partOffsetIsOneBased();
    void gluedYpartHeaderIsSplit();
    void escapeStraddlingALineIsHonoured();
    void crcMismatchIsReported();
    void sizeMismatchIsReported();
    void articleWithoutYbeginIsRejected();
    void headersBeforeYbeginAreIgnored();
    void danglingEscapeDoesNotLeakIntoTheNextArticle();
    void decoderIsReusable();
};

void tst_UsenetYenc::crc32MatchesTheStandardVector()
{
    // yEnc's pcrc32 is the ordinary zip/gzip CRC-32. If this drifts, every
    // article verifies as corrupt and the cause looks like the network.
    QCOMPARE(yencCrc32(0, QByteArrayLiteral("123456789")), 0xCBF43926u);
    QCOMPARE(yencCrc32(0, QByteArray{}), 0u);
}

void tst_UsenetYenc::roundTripsEveryByteValue()
{
    QByteArray all;
    all.resize(256);
    for (int i = 0; i < 256; ++i)
        all[i] = static_cast<char>(i);

    YencDecoder decoder;
    QByteArrayList lines{QByteArrayLiteral("=ybegin line=128 size=256 name=all.bin")};
    lines += encodeYenc(all);
    lines.append(QStringLiteral("=yend size=256 crc32=%1")
                     .arg(yencCrc32(0, all), 8, 16, QLatin1Char('0'))
                     .toLatin1());

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out, all);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    QCOMPARE(decoder.fileName(), QStringLiteral("all.bin"));
    QCOMPARE(decoder.fileSize(), 256);
}

void tst_UsenetYenc::singlePartArticleHasNoOffset()
{
    const QByteArray data = patternBytes(1000);
    YencDecoder decoder;

    QByteArrayList lines{QByteArrayLiteral("=ybegin line=128 size=1000 name=whole.bin")};
    lines += encodeYenc(data);
    lines.append(QByteArrayLiteral("=yend size=1000"));

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    // A single-part post legitimately carries no =ypart; it starts at 0.
    QVERIFY(!decoder.hasPartHeader());
    QCOMPARE(decoder.offset(), 0);
}

void tst_UsenetYenc::partOffsetIsOneBased()
{
    const QByteArray data = patternBytes(500);
    YencDecoder decoder;

    // begin=805306369 is the 1-based first byte of part 12; the file offset is
    // one less. This is the assertion that catches an off-by-one no CRC can.
    QByteArrayList lines{
        QByteArrayLiteral("=ybegin part=12 total=97 line=128 size=1073741824 name=foo.r00"),
        QByteArrayLiteral("=ypart begin=805306369 end=805306868")};
    lines += encodeYenc(data);
    lines.append(QStringLiteral("=yend size=500 part=12 pcrc32=%1")
                     .arg(yencCrc32(0, data), 8, 16, QLatin1Char('0'))
                     .toLatin1());

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    QVERIFY(decoder.hasPartHeader());
    QCOMPARE(decoder.offset(), Q_INT64_C(805306368));
    QCOMPARE(decoder.partNumber(), 12);
    QCOMPARE(decoder.partTotal(), 97);
    QCOMPARE(decoder.fileName(), QStringLiteral("foo.r00"));
    QCOMPARE(decoder.fileSize(), Q_INT64_C(1073741824));
    QCOMPARE(decoder.partSize(), 500);
}

void tst_UsenetYenc::gluedYpartHeaderIsSplit()
{
    const QByteArray data = patternBytes(300);
    YencDecoder decoder;

    // The malformed form real posters emit: =ypart glued to the end of the
    // name. Taking the tail verbatim yields the name "mybinary.dat=ypart
    // begin=1 end=300" and offset 0 -- which silently overwrites part 1.
    QByteArrayList lines{
        QByteArrayLiteral("=ybegin part=1 total=10 line=128 size=500000 "
                          "name=mybinary.dat=ypart begin=1 end=300")};
    lines += encodeYenc(data);
    lines.append(QStringLiteral("=yend size=300 part=1 pcrc32=%1")
                     .arg(yencCrc32(0, data), 8, 16, QLatin1Char('0'))
                     .toLatin1());

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    QCOMPARE(decoder.fileName(), QStringLiteral("mybinary.dat"));
    QVERIFY(decoder.hasPartHeader());
    QCOMPARE(decoder.offset(), 0);   // begin=1 -> offset 0
}

void tst_UsenetYenc::escapeStraddlingALineIsHonoured()
{
    // Craft a line ending in a bare '=' so the escape applies to the first byte
    // of the following line. encodeYenc() cannot produce this on demand, so the
    // article is written by hand.
    YencDecoder decoder;
    QByteArray out;
    decoder.setSink([&out](QByteArrayView chunk) { out.append(chunk); });
    decoder.reset();

    // 0xD6 encodes as 0xD6+42 = 0x00, which must be escaped, so it goes out as
    // "=@" -- and here the '=' is the last byte of one line and the '@' the
    // first of the next.
    decoder.feedLine(QByteArrayLiteral("=ybegin line=128 size=1 name=split.bin"));
    decoder.feedLine(QByteArrayLiteral("="));       // escape, payload continues
    decoder.feedLine(QByteArrayLiteral("@"));       // 0x40 - 64 - 42 = 0xD6
    decoder.feedLine(QByteArrayLiteral("=yend size=1"));

    QCOMPARE(out.size(), 1);
    QCOMPARE(static_cast<quint8>(out.at(0)), quint8(0xD6));
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::crcMismatchIsReported()
{
    const QByteArray data = patternBytes(400);
    YencDecoder decoder;

    QByteArrayList lines{QByteArrayLiteral("=ybegin part=1 line=128 size=400 name=bad.bin"),
                         QByteArrayLiteral("=ypart begin=1 end=400")};
    lines += encodeYenc(data);
    lines.append(QByteArrayLiteral("=yend size=400 part=1 pcrc32=deadbeef"));

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out, data);   // the bytes still arrived; only the check failed
    QCOMPARE(decoder.status(), YencDecoder::Status::CrcMismatch);
    QCOMPARE(decoder.expectedCrc(), 0xdeadbeefu);
    QVERIFY(decoder.calculatedCrc() != decoder.expectedCrc());
}

void tst_UsenetYenc::sizeMismatchIsReported()
{
    const QByteArray data = patternBytes(400);
    YencDecoder decoder;

    QByteArrayList lines{QByteArrayLiteral("=ybegin part=1 line=128 size=400 name=short.bin")};
    lines += encodeYenc(data);
    lines.append(QByteArrayLiteral("=yend size=999 part=1"));

    decodeAll(decoder, lines);
    // A truncated article is the commonest real corruption, and it is caught by
    // size even when the poster omitted pcrc32.
    QCOMPARE(decoder.status(), YencDecoder::Status::SizeMismatch);
}

void tst_UsenetYenc::articleWithoutYbeginIsRejected()
{
    YencDecoder decoder;
    const QByteArray out = decodeAll(decoder, {QByteArrayLiteral("just some text"),
                                               QByteArrayLiteral("and more")});
    QVERIFY(out.isEmpty());
    QCOMPARE(decoder.status(), YencDecoder::Status::NoBinaryData);
}

void tst_UsenetYenc::headersBeforeYbeginAreIgnored()
{
    const QByteArray data = patternBytes(100);
    YencDecoder decoder;

    // ARTICLE (as opposed to BODY) sends RFC 5322 headers first. Decoding them
    // as payload would corrupt the file with no error anywhere.
    QByteArrayList lines{QByteArrayLiteral("Subject: [1/2] - \"x.bin\" yEnc (1/2)"),
                         QByteArrayLiteral("From: poster@example"),
                         QByteArrayLiteral(""),
                         QByteArrayLiteral("=ybegin line=128 size=100 name=x.bin")};
    lines += encodeYenc(data);
    lines.append(QByteArrayLiteral("=yend size=100"));

    QCOMPARE(decodeAll(decoder, lines), data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::danglingEscapeDoesNotLeakIntoTheNextArticle()
{
    // An article that ends while an escape is pending -- truncated mid-sequence,
    // which a dropped connection produces -- must not shift the first byte of
    // whatever the same decoder handles next. Both the scalar flag and
    // rapidyenc's state carry this across lines, so both have to be reset.
    YencDecoder decoder;
    QByteArray out;
    decoder.setSink([&out](QByteArrayView chunk) { out.append(chunk); });

    decoder.reset();
    decoder.feedLine(QByteArrayLiteral("=ybegin line=128 size=1 name=truncated.bin"));
    decoder.feedLine(QByteArrayLiteral("="));      // escape opened, never closed
    // ...connection dies here; no =yend.

    const QByteArray data = patternBytes(64);
    QByteArrayList lines{QByteArrayLiteral("=ybegin line=128 size=64 name=next.bin")};
    lines += encodeYenc(data);
    lines.append(QStringLiteral("=yend size=64 crc32=%1")
                     .arg(yencCrc32(0, data), 8, 16, QLatin1Char('0')).toLatin1());

    QCOMPARE(decodeAll(decoder, lines), data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::decoderIsReusable()
{
    // One decoder serves a pooled connection for article after article, so
    // reset() has to clear everything -- a leftover CRC or escape flag would
    // corrupt the *next* article, which is a miserable bug to track down.
    YencDecoder decoder;

    const QByteArray first = patternBytes(200);
    QByteArrayList lines1{QByteArrayLiteral("=ybegin part=1 line=128 size=200 name=a.bin"),
                          QByteArrayLiteral("=ypart begin=1 end=200")};
    lines1 += encodeYenc(first);
    lines1.append(QStringLiteral("=yend size=200 part=1 pcrc32=%1")
                      .arg(yencCrc32(0, first), 8, 16, QLatin1Char('0')).toLatin1());
    QCOMPARE(decodeAll(decoder, lines1), first);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);

    const QByteArray second = patternBytes(150);
    QByteArrayList lines2{QByteArrayLiteral("=ybegin part=2 line=128 size=350 name=b.bin"),
                          QByteArrayLiteral("=ypart begin=201 end=350")};
    lines2 += encodeYenc(second);
    lines2.append(QStringLiteral("=yend size=150 part=2 pcrc32=%1")
                      .arg(yencCrc32(0, second), 8, 16, QLatin1Char('0')).toLatin1());
    QCOMPARE(decodeAll(decoder, lines2), second);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    QCOMPARE(decoder.fileName(), QStringLiteral("b.bin"));
    QCOMPARE(decoder.offset(), 200);
    QCOMPARE(decoder.partSize(), 150);
}

QTEST_MAIN(tst_UsenetYenc)
#include "tst_UsenetYenc.moc"
