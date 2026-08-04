/// @file tst_ArchiveUnpack.cpp
/// @brief Tests for archive/ArchiveUnpack — transparent unwrapping of downloaded lists.

#include "TestHelpers.h"
#include "archive/ArchiveUnpack.h"

#include <QTest>

#include <zlib.h>

using namespace eMule;
using eMule::testing::buildMinimalZip;
using eMule::testing::ZipMember;

/// gzip-compress @p data — the shape a `.gz` list mirror serves.
/// windowBits 16+MAX_WBITS selects a gzip wrapper rather than zlib's own.
static QByteArray gzipCompress(const QByteArray& data)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }

    QByteArray out(static_cast<qsizetype>(deflateBound(&zs, static_cast<uLong>(data.size()))) + 32,
                   Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    const int rc = deflate(&zs, Z_FINISH);
    const auto written = static_cast<qsizetype>(zs.total_out);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};

    out.truncate(written);
    return out;
}

class tst_ArchiveUnpack : public QObject {
    Q_OBJECT

private slots:
    void gzip_unwrapsToOriginal();
    void gzip_roundTripsBinary();
    void zip_prefersNamedMember();
    void zip_fallsBackToLargestMember();
    void plainText_passesThroughUnchanged();
    void binaryServerMet_passesThroughUnchanged();
    void garbage_passesThroughUnchanged();
    void truncatedGzip_doesNotYieldPartialData();
    void empty_staysEmpty();
    void sizeCap_rejectsOversizedExpansion();
};

// ---------------------------------------------------------------------------
// Compressed payloads
// ---------------------------------------------------------------------------

void tst_ArchiveUnpack::gzip_unwrapsToOriginal()
{
    // The case the whole feature exists for: a public list served as ipfilter.dat.gz.
    //
    // The leading "# " comment is load-bearing, not decoration. libarchive's mtree bidder
    // accepts any text file whose first line starts with '#', so under
    // archive_read_support_format_all() this exact payload was detected as an mtree
    // archive and unpacked to nothing — a download that "succeeded" and installed an
    // empty filter. Every real list opens with a comment header.
    const QByteArray plain =
        "# fullbogons-ipv6\n2001:db8::/32\n2a01:4f8::/32\n3fff::/20\n";
    const QByteArray gz = gzipCompress(plain);
    QVERIFY(!gz.isEmpty());
    QVERIFY(gz != plain);

    const UnwrapResult r = unwrapDownload(gz, {QStringLiteral("ipfilter.dat")});

    QVERIFY(r.error.isEmpty());
    QVERIFY(r.wasArchive);
    QCOMPARE(r.data, plain);
}

void tst_ArchiveUnpack::gzip_roundTripsBinary()
{
    // nodes.dat and server.met are binary; unwrapping must be byte-exact, not text-safe.
    QByteArray binary;
    for (int i = 0; i < 4096; ++i)
        binary.append(static_cast<char>(i * 7 % 256));

    const UnwrapResult r = unwrapDownload(gzipCompress(binary), {QStringLiteral("nodes.dat")});

    QVERIFY(r.wasArchive);
    QCOMPARE(r.data, binary);
}

void tst_ArchiveUnpack::zip_prefersNamedMember()
{
    // A real ipfilter .zip carries a readme alongside the list. The preferred-name list
    // is what stops us handing the readme to the parser — and note the readme is larger,
    // so the largest-member fallback would pick wrong here.
    const QByteArray list = "1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
    const QByteArray readme(4096, 'R');

    const QByteArray zip = buildMinimalZip({
        ZipMember{"readme.txt", readme},
        ZipMember{"ipfilter.dat", list},
    });

    const UnwrapResult r = unwrapDownload(zip, {QStringLiteral("ipfilter.dat"),
                                                QStringLiteral("guarding.p2p")});

    QVERIFY(r.error.isEmpty());
    QVERIFY(r.wasArchive);
    QCOMPARE(r.entryName, QStringLiteral("ipfilter.dat"));
    QCOMPARE(r.data, list);
}

void tst_ArchiveUnpack::zip_fallsBackToLargestMember()
{
    // No preferred name matches — take the biggest regular file rather than giving up.
    const QByteArray small = "tiny";
    const QByteArray big(2048, 'B');

    const QByteArray zip = buildMinimalZip({
        ZipMember{"notes.txt", small},
        ZipMember{"list.p2p", big},
    });

    const UnwrapResult r = unwrapDownload(zip, {QStringLiteral("ipfilter.dat")});

    QVERIFY(r.wasArchive);
    QCOMPARE(r.entryName, QStringLiteral("list.p2p"));
    QCOMPARE(r.data, big);
}

// ---------------------------------------------------------------------------
// Passthrough — the safety property that lets callers apply this unconditionally
// ---------------------------------------------------------------------------

void tst_ArchiveUnpack::plainText_passesThroughUnchanged()
{
    // libarchive's `raw` format bids on anything, so an uncompressed list would come
    // back as a one-entry "archive" if the passthrough check were missing.
    const QByteArray plain = "1.2.3.0 - 1.2.3.255 , 0 , blocked\n2a01:4f8::/32 , 0 , v6\n";

    const UnwrapResult r = unwrapDownload(plain, {QStringLiteral("ipfilter.dat")});

    QCOMPARE(r.data, plain);
    QVERIFY(!r.wasArchive);
    QVERIFY(r.entryName.isEmpty());
    QVERIFY(r.error.isEmpty());
}

void tst_ArchiveUnpack::binaryServerMet_passesThroughUnchanged()
{
    // A server.met opens with header 0xE0 then a LE server count. Nothing here may be
    // mistaken for a container signature.
    QByteArray met;
    met.append(static_cast<char>(0xE0));
    met.append(static_cast<char>(0x02));
    met.append(3, '\0');
    for (int i = 0; i < 512; ++i)
        met.append(static_cast<char>(i % 251));

    const UnwrapResult r = unwrapDownload(met, {QStringLiteral("server.met")});

    QCOMPARE(r.data, met);
    QVERIFY(!r.wasArchive);
}

void tst_ArchiveUnpack::garbage_passesThroughUnchanged()
{
    // An HTML error page served with 200 OK is the common real-world case. Hand it back
    // so the caller's own parser reports the problem in its own terms.
    const QByteArray html = "<!DOCTYPE html><html><body>404 Not Found</body></html>";

    const UnwrapResult r = unwrapDownload(html);

    QCOMPARE(r.data, html);
    QVERIFY(!r.wasArchive);
}

void tst_ArchiveUnpack::truncatedGzip_doesNotYieldPartialData()
{
    // A cut-off download must never reach a parser as if it were complete: either it is
    // reported as an error, or it is passed through still compressed (and fails to
    // parse). What it must not be is a silently half-decompressed list.
    const QByteArray plain(64 * 1024, 'x');
    QByteArray gz = gzipCompress(plain);
    QVERIFY(gz.size() > 32);
    gz.truncate(gz.size() / 2);

    const UnwrapResult r = unwrapDownload(gz);

    if (r.error.isEmpty())
        QVERIFY(r.data != plain);
    else
        QVERIFY(r.data.isEmpty());
}

void tst_ArchiveUnpack::empty_staysEmpty()
{
    const UnwrapResult r = unwrapDownload(QByteArray{});

    QVERIFY(r.data.isEmpty());
    QVERIFY(!r.wasArchive);
    QVERIFY(r.error.isEmpty());
}

// ---------------------------------------------------------------------------
// Bomb guard
// ---------------------------------------------------------------------------

void tst_ArchiveUnpack::sizeCap_rejectsOversizedExpansion()
{
    // Highly compressible but list-shaped, so this exercises the cap rather than some
    // degenerate detection path (a megabyte of zeros, for instance, bids as an empty tar).
    QByteArray huge;
    while (huge.size() < 1024 * 1024)
        huge.append("10.0.0.0 - 10.255.255.255 , 0 , filler\n");

    const QByteArray gz = gzipCompress(huge);
    QVERIFY(!gz.isEmpty());
    QVERIFY(gz.size() < 64 * 1024);   // small download, large expansion

    const UnwrapResult r = unwrapDownload(gz, {}, /*maxBytes*/ 4096);

    QVERIFY(!r.error.isEmpty());
    QVERIFY(r.data.isEmpty());

    // Same payload under a sufficient cap still works — the cap is the only thing
    // rejecting it above.
    const UnwrapResult ok = unwrapDownload(gz, {}, /*maxBytes*/ 4 * 1024 * 1024);
    QVERIFY(ok.error.isEmpty());
    QCOMPARE(ok.data.size(), huge.size());
}

QTEST_GUILESS_MAIN(tst_ArchiveUnpack)
#include "tst_ArchiveUnpack.moc"
