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

/// Lines as a BODY response puts them on the wire: dot-stuffed, CRLF, and the
/// terminating ".". @p tail is whatever the server sends next.
QByteArray toWire(const QByteArrayList& lines, const QByteArray& tail = {})
{
    QByteArray wire;
    for (const QByteArray& line : lines) {
        if (line.startsWith('.'))
            wire.append('.');
        wire.append(line);
        wire.append("\r\n");
    }
    wire.append(".\r\n");
    wire.append(tail);
    return wire;
}

struct RawResult {
    QByteArray out;
    qsizetype consumed = 0;
    bool ended = false;
};

/// Feed @p wire in the given chunk sizes (cycled), stopping at the end.
RawResult decodeRaw(YencDecoder& decoder, const QByteArray& wire, QList<qsizetype> chunks)
{
    RawResult r;
    decoder.setSink([&r](QByteArrayView chunk) { r.out.append(chunk); });
    decoder.reset();
    qsizetype pos = 0;
    int c = 0;
    while (pos < wire.size() && !r.ended) {
        const qsizetype n = std::min(chunks.at(c++ % chunks.size()), wire.size() - pos);
        const qsizetype used = decoder.feedRaw(QByteArrayView(wire).sliced(pos, n), r.ended);
        if (!r.ended && used != n)
            return r;   // contract broken: a test assertion catches consumed
        pos += used;
    }
    r.consumed = pos;
    return r;
}

QByteArrayList multipartArticle(const QByteArray& data, qint64 begin, qint64 total)
{
    QByteArrayList lines;
    lines << QByteArrayLiteral("=ybegin part=2 total=3 line=128 size=")
                 + QByteArray::number(total) + " name=raw test.bin";
    lines << QByteArrayLiteral("=ypart begin=") + QByteArray::number(begin)
                 + " end=" + QByteArray::number(begin + data.size() - 1);
    lines << encodeYenc(data);
    lines << QByteArrayLiteral("=yend size=") + QByteArray::number(data.size())
                 + " part=2 pcrc32=" + QByteArray::number(yencCrc32(0, data), 16);
    return lines;
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
    void singlePartCrcIsVerified_data();
    void singlePartCrcIsVerified();
    void wholeFileCrcOnAMultiPartIsNotThePartsCrc();
    void sizeMismatchIsReported();
    void articleWithoutYbeginIsRejected();
    void headersBeforeYbeginAreIgnored();
    void danglingEscapeDoesNotLeakIntoTheNextArticle();
    void decoderIsReusable();
    void decodesALineBeginningWithAnEncodedDot();

    void rawFeedMatchesAtEverySplit_data();
    void rawFeedMatchesAtEverySplit();
    void rawFeedUnstuffsEncodedDots_data();
    void rawFeedUnstuffsEncodedDots();
    void rawFeedHandlesSinglePartAndGluedHeaders_data();
    void rawFeedHandlesSinglePartAndGluedHeaders();
    void rawFeedReportsAMissingYend_data();
    void rawFeedReportsAMissingYend();
    void rawFeedSkipsHeadersBeforeYbegin();
    void cleanup();
};

void tst_UsenetYenc::crc32MatchesTheStandardVector()
{
    // yEnc's pcrc32 is the ordinary zip/gzip CRC-32. If this drifts, every
    // article verifies as corrupt and the cause looks like the network.
    QCOMPARE(yencCrc32(0, QByteArrayLiteral("123456789")), 0xCBF43926u);
    QCOMPARE(yencCrc32(0, QByteArray{}), 0u);
}

void tst_UsenetYenc::decodesALineBeginningWithAnEncodedDot()
{
    // Source byte 0x04 encodes to 0x2E, '.', so a line starting with one is an
    // ordinary occurrence in binary data — and it is the exact shape that broke
    // the SIMD decoder.
    //
    // NNTP stuffs such a line with a second dot on the wire and
    // NntpSocket::handleBodyLine strips it again, so what arrives here already
    // begins with a single, real '.'. rapidyenc was being called with is_raw=1,
    // which asks *it* to unstuff as well: the dot was eaten a second time and
    // every affected article came out one byte short per line. =yend's size
    // check turned that into a protocol error, so the article was retried
    // forever and the provider was backed off — a decoder bug wearing a network
    // failure's clothes.
    //
    // The scalar path never had the bug, so this only fails with
    // EMULE_USENET_RAPIDYENC on. Both must pass.
    QByteArray data;
    data.append(char(0x04));                 // encodes to '.', first on the line
    data.append(patternBytes(200));

    YencDecoder decoder;
    QByteArrayList lines{
        QByteArrayLiteral("=ybegin line=128 size=201 name=dot.bin")};
    const QByteArrayList encoded = encodeYenc(data);
    QVERIFY(!encoded.isEmpty());
    QVERIFY2(encoded.first().startsWith('.'),
             "fixture no longer produces a leading-dot line; the case is vacuous");
    lines += encoded;
    lines.append(QStringLiteral("=yend size=201 crc32=%1")
                     .arg(yencCrc32(0, data), 8, 16, QLatin1Char('0'))
                     .toLatin1());

    const QByteArray out = decodeAll(decoder, lines);
    QCOMPARE(out.size(), data.size());
    QCOMPARE(out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
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

void tst_UsenetYenc::singlePartCrcIsVerified_data()
{
    rawFeedMatchesAtEverySplit_data();
}

void tst_UsenetYenc::singlePartCrcIsVerified()
{
    QFETCH(bool, lineMode);
    YencDecoder::setLineModeForTests(lineMode);

    // A single-part post has no =ypart and no pcrc32: its checksum is crc32.
    // Reading only pcrc32 let every damaged nfo/sfv/small par2 through on the
    // size check alone (NZBGet: Decoder.cpp, " crc32=" when !m_part).
    const QByteArray data = patternBytes(300);
    const QByteArrayList head{QByteArrayLiteral("=ybegin line=128 size=300 name=one.nfo")};

    YencDecoder decoder;
    QByteArrayList bad = head + encodeYenc(data);
    bad << QByteArrayLiteral("=yend size=300 crc32=deadbeef");
    RawResult r = decodeRaw(decoder, toWire(bad), {64});
    QVERIFY(r.ended);
    QCOMPARE(decoder.status(), YencDecoder::Status::CrcMismatch);
    QCOMPARE(decoder.expectedCrc(), 0xdeadbeefu);

    const QByteArray crc = QByteArray::number(yencCrc32(0, data), 16);
    QByteArrayList good = head + encodeYenc(data);
    good << QByteArrayLiteral("=yend size=300 crc32=") + crc;
    r = decodeRaw(decoder, toWire(good), {64});
    QCOMPARE(r.out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);

    // Some posters write pcrc32 on a single part; it is the same bytes.
    QByteArrayList pcrc = head + encodeYenc(data);
    pcrc << QByteArrayLiteral("=yend size=300 pcrc32=deadbeef");
    decodeRaw(decoder, toWire(pcrc), {64});
    QCOMPARE(decoder.status(), YencDecoder::Status::CrcMismatch);

    // And no checksum at all is still not a failure.
    QByteArrayList none = head + encodeYenc(data);
    none << QByteArrayLiteral("=yend size=300");
    decodeRaw(decoder, toWire(none), {64});
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::wholeFileCrcOnAMultiPartIsNotThePartsCrc()
{
    // On a multi-part post crc32 is the whole file's. It must neither be taken
    // for this part's checksum nor be found inside the word "pcrc32".
    const QByteArray data = patternBytes(400);
    const QByteArray pcrc = QByteArray::number(yencCrc32(0, data), 16);
    const QByteArrayList head = QByteArrayList{
        QByteArrayLiteral("=ybegin part=1 total=2 line=128 size=800 name=two.bin"),
        QByteArrayLiteral("=ypart begin=1 end=400")} + encodeYenc(data);

    YencDecoder decoder;
    decodeAll(decoder, head + QByteArrayList{QByteArrayLiteral("=yend size=400 part=1 pcrc32=")
                                             + pcrc + " crc32=deadbeef"});
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);

    decodeAll(decoder, head + QByteArrayList{QByteArrayLiteral("=yend size=400 part=1 crc32=deadbeef")});
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);

    decodeAll(decoder, head + QByteArrayList{QByteArrayLiteral("=yend size=400 part=1 crc32=")
                                             + pcrc + " pcrc32=deadbeef"});
    QCOMPARE(decoder.status(), YencDecoder::Status::CrcMismatch);
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

void tst_UsenetYenc::cleanup()
{
    YencDecoder::setLineModeForTests(false);
}

void tst_UsenetYenc::rawFeedMatchesAtEverySplit_data()
{
    QTest::addColumn<bool>("lineMode");
    QTest::newRow("blocks") << false;
    QTest::newRow("lines") << true;
}

void tst_UsenetYenc::rawFeedMatchesAtEverySplit()
{
    QFETCH(bool, lineMode);
    YencDecoder::setLineModeForTests(lineMode);

    // Every byte value, so escapes land everywhere, including on read edges.
    QByteArray data = patternBytes(2000);
    for (int b = 0; b < 256; ++b)
        data.append(static_cast<char>(b));
    const QByteArray tail = QByteArrayLiteral("222 0 <next@x>\r\n");
    const QByteArray wire = toWire(multipartArticle(data, 3001, 9000), tail);

    YencDecoder decoder;
    for (qsizetype k = 1; k < wire.size(); ++k) {
        const RawResult r = decodeRaw(decoder, wire, {k, wire.size()});
        QVERIFY2(r.ended, qPrintable(QStringLiteral("split %1").arg(k)));
        QCOMPARE(r.consumed, wire.size() - tail.size());
        QCOMPARE(r.out, data);
        QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
        QCOMPARE(decoder.offset(), 3000);
        QCOMPARE(decoder.fileName(), QStringLiteral("raw test.bin"));
    }

    // And in small irregular reads.
    const RawResult r = decodeRaw(decoder, wire, {1, 7, 3, 64, 2, 129, 5});
    QVERIFY(r.ended);
    QCOMPARE(r.out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::rawFeedUnstuffsEncodedDots_data()
{
    rawFeedMatchesAtEverySplit_data();
}

void tst_UsenetYenc::rawFeedUnstuffsEncodedDots()
{
    QFETCH(bool, lineMode);
    YencDecoder::setLineModeForTests(lineMode);

    // 0x04 encodes to '.', so every line starts with one and the server
    // stuffs each: the decoder must remove exactly one dot per line.
    const QByteArray data(1000, '\x04');
    const QByteArray wire = toWire(multipartArticle(data, 1, 1000));
    QVERIFY(wire.contains("\r\n.."));

    YencDecoder decoder;
    for (qsizetype k : {qsizetype(1), qsizetype(2), qsizetype(3), qsizetype(130), wire.size()}) {
        const RawResult r = decodeRaw(decoder, wire, {k});
        QVERIFY(r.ended);
        QCOMPARE(r.out, data);
        QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
    }
}

void tst_UsenetYenc::rawFeedHandlesSinglePartAndGluedHeaders_data()
{
    rawFeedMatchesAtEverySplit_data();
}

void tst_UsenetYenc::rawFeedHandlesSinglePartAndGluedHeaders()
{
    QFETCH(bool, lineMode);
    YencDecoder::setLineModeForTests(lineMode);

    const QByteArray data = patternBytes(700);
    YencDecoder decoder;

    // No =ypart: the payload starts on the line after =ybegin, at offset 0.
    QByteArrayList single;
    single << QByteArrayLiteral("=ybegin line=128 size=700 name=single.bin");
    single << encodeYenc(data);
    single << QByteArrayLiteral("=yend size=700 crc32=") + QByteArray::number(yencCrc32(0, data), 16);
    RawResult r = decodeRaw(decoder, toWire(single), {50});
    QVERIFY(r.ended);
    QCOMPARE(r.out, data);
    QCOMPARE(decoder.offset(), 0);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);

    // =ypart glued onto the =ybegin line.
    QByteArrayList glued;
    glued << QByteArrayLiteral("=ybegin part=1 total=2 line=128 size=1400 name=g.bin=ypart begin=701 end=1400");
    glued << encodeYenc(data);
    glued << QByteArrayLiteral("=yend size=700 part=1 pcrc32=") + QByteArray::number(yencCrc32(0, data), 16);
    r = decodeRaw(decoder, toWire(glued), {33});
    QVERIFY(r.ended);
    QCOMPARE(r.out, data);
    QCOMPARE(decoder.offset(), 700);
    QCOMPARE(decoder.fileName(), QStringLiteral("g.bin"));
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

void tst_UsenetYenc::rawFeedReportsAMissingYend_data()
{
    rawFeedMatchesAtEverySplit_data();
}

void tst_UsenetYenc::rawFeedReportsAMissingYend()
{
    QFETCH(bool, lineMode);
    YencDecoder::setLineModeForTests(lineMode);

    const QByteArray data = patternBytes(500);
    QByteArrayList lines = multipartArticle(data, 1, 500);
    lines.removeLast();   // no =yend: the "." arrives straight after payload
    const QByteArray tail = QByteArrayLiteral("430 no such article\r\n");
    const QByteArray wire = toWire(lines, tail);

    YencDecoder decoder;
    for (qsizetype k : {qsizetype(1), qsizetype(2), qsizetype(4), qsizetype(100)}) {
        const RawResult r = decodeRaw(decoder, wire, {k});
        QVERIFY(r.ended);
        QCOMPARE(r.consumed, wire.size() - tail.size());
        QCOMPARE(r.out, data);
        QCOMPARE(decoder.status(), YencDecoder::Status::Incomplete);
    }
}

void tst_UsenetYenc::rawFeedSkipsHeadersBeforeYbegin()
{
    const QByteArray data = patternBytes(300);
    QByteArrayList lines;
    lines << QByteArrayLiteral("Subject: test") << QByteArrayLiteral("") ;
    lines << multipartArticle(data, 1, 300);
    YencDecoder decoder;
    const RawResult r = decodeRaw(decoder, toWire(lines), {17});
    QVERIFY(r.ended);
    QCOMPARE(r.out, data);
    QCOMPARE(decoder.status(), YencDecoder::Status::Ok);
}

QTEST_MAIN(tst_UsenetYenc)
#include "tst_UsenetYenc.moc"
