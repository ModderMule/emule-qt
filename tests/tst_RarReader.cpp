/// @file tst_RarReader.cpp
/// @brief The stored-RAR header parser behind phase 6b's offset map.
///
/// Fixtures come from RarFixtures.h, built byte by byte because nothing here can
/// write a RAR. Every offset asserted below was computed from the format, so a
/// passing case pins the parser to the spec and not to whatever some encoder
/// happened to emit.

#include "RarFixtures.h"

#include "stream/RarReader.h"

#include <QTest>
#include <QtEndian>

using namespace eMule::usenet;
using namespace eMule::testing::rar;

class tst_RarReader : public QObject {
    Q_OBJECT

private slots:
    void rar4StoredVolumeMapsItsPayload();
    void rar4CarriesSizesBeyondFourGigabytes();
    void rar4MiddleVolumeIsSplitOnBothSides();
    void rar5StoredVolumeMapsItsPayload();
    void rar5ReportsItsVolumeNumber();
    void aVolumeCarryingTwoFileHeadersListsBoth();
    void aTruncatedSecondHeaderStillReportsTheFirst();
    void resumingMidVolumeFindsTheHeaderPastTheProbeWindow();
    void aResumedBlockAtAWrongAddressIsRejectedByItsChecksum();
    void compressedIsReadButNotStored();
    void solidAndEncryptedAreRefusedWithAReason();
    void truncationAtEveryLengthAsksForMoreBytes();
    void nonRarBytesAreReportedAsSuch();
};

void tst_RarReader::rar4StoredVolumeMapsItsPayload()
{
    const QByteArray name = "Some.Release.mkv";
    QByteArray vol = rar4Marker();
    vol += rar4Main(kMainFirstVolume);
    const qint64 fileHeaderAt = vol.size();
    vol += rar4File(name, /*packed*/ 5000, /*unpacked*/ 12000, kSplitAfter);
    const qint64 dataAt = vol.size();
    vol += QByteArray(5000, 'x');

    const RarVolume v = parseRarVolume(vol);
    QCOMPARE(v.status, RarParse::Ok);
    QVERIFY(v.isFirstVolume);
    QCOMPARE(v.entries.size(), 1);

    const RarEntry& e = v.entries.first();
    QCOMPARE(e.name, QString::fromLatin1(name));
    QVERIFY(e.stored);
    QVERIFY(!e.encrypted);
    QVERIFY(!e.splitBefore);
    QVERIFY(e.splitAfter);
    QCOMPARE(e.packedSize, 5000);
    QCOMPARE(e.unpackedSize, 12000);

    // The whole point of the parser: where the payload starts. Header size is
    // 32 fixed bytes plus the name, measured from the file header's own start.
    QCOMPARE(e.dataOffset, dataAt);
    QCOMPARE(e.dataOffset, fileHeaderAt + 32 + name.size());
}

void tst_RarReader::rar4CarriesSizesBeyondFourGigabytes()
{
    // Every release this feature exists for is larger than a u32. Without
    // LHD_LARGE's high words a 12 GB file maps as its low 32 bits, and the
    // failure is silent — offsets simply land in the wrong volume.
    constexpr qint64 kUnpacked = 12LL * 1024 * 1024 * 1024 + 4321;
    constexpr qint64 kPacked = 5LL * 1024 * 1024 * 1024 + 99;

    QByteArray vol = rar4Marker();
    vol += rar4Main(kMainFirstVolume);
    vol += rar4File("Big.mkv", kPacked, kUnpacked, kSplitAfter | kLarge);

    const RarVolume v = parseRarVolume(vol);
    QCOMPARE(v.status, RarParse::Ok);
    QCOMPARE(v.entries.size(), 1);
    QCOMPARE(v.entries.first().packedSize, kPacked);
    QCOMPARE(v.entries.first().unpackedSize, kUnpacked);
    QVERIFY(v.entries.first().packedSize > 0xFFFFFFFFLL);
}

void tst_RarReader::rar4MiddleVolumeIsSplitOnBothSides()
{
    QByteArray vol = rar4Marker();
    vol += rar4Main(0);                       // not the first volume
    vol += rar4File("Some.Release.mkv", 5000, 12000, kSplitBefore | kSplitAfter);

    const RarVolume v = parseRarVolume(vol);
    QCOMPARE(v.status, RarParse::Ok);
    QVERIFY(!v.isFirstVolume);
    QVERIFY(v.entries.first().splitBefore);
    QVERIFY(v.entries.first().splitAfter);
}

void tst_RarReader::rar5StoredVolumeMapsItsPayload()
{
    const QByteArray name = "Some.Release.mkv";
    QByteArray vol = rar5Marker();
    vol += rar5Main(0);
    vol += rar5File(name, /*packed*/ 5000, /*unpacked*/ 12000,
                    /*splitBefore*/ false, /*splitAfter*/ true);
    const qint64 dataAt = vol.size();
    vol += QByteArray(5000, 'y');

    const RarVolume v = parseRarVolume(vol);
    QCOMPARE(v.status, RarParse::Ok);
    QCOMPARE(v.entries.size(), 1);

    const RarEntry& e = v.entries.first();
    QCOMPARE(e.name, QString::fromLatin1(name));
    QVERIFY(e.stored);
    QVERIFY(e.splitAfter);
    QCOMPARE(e.packedSize, 5000);
    QCOMPARE(e.unpackedSize, 12000);
    QCOMPARE(e.dataOffset, dataAt);
}

void tst_RarReader::rar5ReportsItsVolumeNumber()
{
    // RAR5 states the volume number in its main header, which is what makes an
    // obfuscated RAR5 set orderable when the filenames say nothing. RAR4 has no
    // such field and reports -1 — "the format did not say", not volume zero.
    QByteArray five = rar5Marker();
    five += rar5Main(41);
    five += rar5File("x.mkv", 10, 100, true, true);
    QCOMPARE(parseRarVolume(five).volumeNumber, 41);
    QVERIFY(!parseRarVolume(five).isFirstVolume);

    QByteArray four = rar4Marker();
    four += rar4Main(0);
    four += rar4File("x.mkv", 10, 100, kSplitBefore | kSplitAfter);
    QCOMPARE(parseRarVolume(four).volumeNumber, -1);
}

void tst_RarReader::aVolumeCarryingTwoFileHeadersListsBoth()
{
    // A release that packs a small .nfo ahead of the feature puts both headers
    // inside the first probe window. Stopping at the first one is how the whole
    // set came to be mapped as a 500-byte "movie".
    QByteArray four = rar4Marker();
    four += rar4Main(kMainFirstVolume);
    const qint64 nfoHeader4 = four.size();
    four += rar4File("intro.nfo", /*packed*/ 500, /*unpacked*/ 500, 0);
    const qint64 nfoData4 = four.size();
    four += QByteArray(500, 'n');
    const qint64 mkvHeader4 = four.size();
    four += rar4File("Some.Release.mkv", /*packed*/ 2500, /*unpacked*/ 20500, kSplitAfter);
    const qint64 mkvData4 = four.size();
    four += QByteArray(2500, 'm');

    const RarVolume v4 = parseRarVolume(four);
    QCOMPARE(v4.status, RarParse::Ok);
    QCOMPARE(v4.entries.size(), 2);
    QCOMPARE(v4.entries.at(0).name, QStringLiteral("intro.nfo"));
    QCOMPARE(v4.entries.at(0).headerOffset, nfoHeader4);
    QCOMPARE(v4.entries.at(0).dataOffset, nfoData4);
    QVERIFY(!v4.entries.at(0).splitAfter);
    QCOMPARE(v4.entries.at(1).name, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(v4.entries.at(1).headerOffset, mkvHeader4);
    QCOMPARE(v4.entries.at(1).dataOffset, mkvData4);
    QCOMPARE(v4.entries.at(1).unpackedSize, 20500);
    QVERIFY(v4.entries.at(1).splitAfter);

    QByteArray five = rar5Marker();
    five += rar5Main(0);
    const qint64 nfoHeader5 = five.size();
    five += rar5File("intro.nfo", 500, 500, false, false);
    const qint64 nfoData5 = five.size();
    five += QByteArray(500, 'n');
    const qint64 mkvHeader5 = five.size();
    five += rar5File("Some.Release.mkv", 2500, 20500, false, true);
    const qint64 mkvData5 = five.size();
    five += QByteArray(2500, 'm');

    const RarVolume v5 = parseRarVolume(five);
    QCOMPARE(v5.status, RarParse::Ok);
    QCOMPARE(v5.entries.size(), 2);
    QCOMPARE(v5.entries.at(0).headerOffset, nfoHeader5);
    QCOMPARE(v5.entries.at(0).dataOffset, nfoData5);
    QCOMPARE(v5.entries.at(1).name, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(v5.entries.at(1).headerOffset, mkvHeader5);
    QCOMPARE(v5.entries.at(1).dataOffset, mkvData5);
    QVERIFY(v5.entries.at(1).splitAfter);
}

void tst_RarReader::aTruncatedSecondHeaderStillReportsTheFirst()
{
    // The invariant is "nothing partial was appended", not "entries is empty":
    // a window holding one whole header and half of the next is a complete
    // answer about the first file.
    QByteArray four = rar4Marker();
    four += rar4Main(kMainFirstVolume);
    four += rar4File("intro.nfo", 500, 500, 0);
    four += QByteArray(500, 'n');
    const qint64 mkvHeader = four.size();
    four += rar4File("Some.Release.mkv", 2500, 20500, kSplitAfter);

    for (qint64 cut = mkvHeader; cut < four.size(); ++cut) {
        const RarVolume v = parseRarVolume(four.left(int(cut)));
        QVERIFY2(v.status == RarParse::Ok,
                 qPrintable(QStringLiteral("cut at %1 gave status %2")
                                .arg(cut).arg(int(v.status))));
        QCOMPARE(v.entries.size(), 1);
        QCOMPARE(v.entries.at(0).name, QStringLiteral("intro.nfo"));
    }
}

namespace {

/// A RAR4 volume holding a big first file and a second header far past any
/// probe window — the layout that forces a resumed parse.
QByteArray twoFileVolume(qint64 firstPayload, qint64* secondHeaderAt)
{
    QByteArray vol = rar4Marker();
    vol += rar4Main(kMainFirstVolume);
    vol += rar4File("big.bin", firstPayload, firstPayload, 0);
    vol += QByteArray(int(firstPayload), 'b');
    *secondHeaderAt = vol.size();
    vol += rar4File("Some.Release.mkv", 2500, 20500, kSplitAfter);
    vol += QByteArray(2500, 'm');
    return vol;
}

} // namespace

void tst_RarReader::resumingMidVolumeFindsTheHeaderPastTheProbeWindow()
{
    qint64 secondHeaderAt = 0;
    const QByteArray vol = twoFileVolume(20000, &secondHeaderAt);
    QVERIFY(secondHeaderAt > kRarHeaderProbeBytes);   // else this proves nothing

    // The probe window sees only the first file, and says where to look next.
    const RarVolume head = parseRarVolume(vol.left(kRarHeaderProbeBytes));
    QCOMPARE(head.status, RarParse::Ok);
    QCOMPARE(head.format, RarFormat::Rar4);
    QCOMPARE(head.entries.size(), 1);
    QCOMPARE(head.nextOffset, secondHeaderAt);

    // Resuming there finds the second file, at absolute offsets.
    const QByteArray window = vol.mid(int(head.nextOffset));
    const RarVolume more = parseRarBlocks(head.format, window, head.nextOffset);
    QCOMPARE(more.status, RarParse::Ok);
    QCOMPARE(more.entries.size(), 1);
    QCOMPARE(more.entries.at(0).name, QStringLiteral("Some.Release.mkv"));
    QCOMPARE(more.entries.at(0).headerOffset, secondHeaderAt);
    QCOMPARE(more.entries.at(0).dataOffset, secondHeaderAt + 32 + 16);
    QVERIFY(more.entries.at(0).splitAfter);
}

void tst_RarReader::aResumedBlockAtAWrongAddressIsRejectedByItsChecksum()
{
    // A resumed address is computed from a previously trusted packedSize, so an
    // error there would propagate into every later entry. The head of a volume
    // is anchored by its marker and needs no such check; a resumed block does.
    qint64 secondHeaderAt = 0;
    QByteArray vol = twoFileVolume(20000, &secondHeaderAt);

    const QByteArray good = vol.mid(int(secondHeaderAt));
    QCOMPARE(parseRarBlocks(RarFormat::Rar4, good, secondHeaderAt).status, RarParse::Ok);

    // Flip a byte inside the header body — the CRC field itself is untouched.
    vol[int(secondHeaderAt) + 9] = char(vol.at(int(secondHeaderAt) + 9) ^ 0x40);
    const RarVolume bad = parseRarBlocks(RarFormat::Rar4, vol.mid(int(secondHeaderAt)),
                                         secondHeaderAt);
    QCOMPARE(bad.status, RarParse::Unsupported);
    QVERIFY(!bad.reason.isEmpty());
    QVERIFY(bad.entries.isEmpty());

    // parseRarVolume() keeps its documented behaviour: no CRC check at a known
    // address, because a wrong map there fails the =ypart cross-check anyway.
    QByteArray headBad = twoFileVolume(20, &secondHeaderAt);
    headBad[0 + 7 + 13 + 9] = char(headBad.at(7 + 13 + 9) ^ 0x40);
    QCOMPARE(parseRarVolume(headBad).status, RarParse::Ok);
}

void tst_RarReader::compressedIsReadButNotStored()
{
    // A compressed archive parses fine — we just cannot map through it. The
    // distinction matters: the caller reports "not seekable" rather than
    // "unreadable", and only `stored` decides.
    QByteArray four = rar4Marker();
    four += rar4Main(kMainFirstVolume);
    four += rar4File("Some.Release.mkv", 5000, 12000, kSplitAfter, /*method*/ 0x33);
    const RarVolume v4 = parseRarVolume(four);
    QCOMPARE(v4.status, RarParse::Ok);
    QVERIFY(!v4.entries.first().stored);

    QByteArray five = rar5Marker();
    five += rar5Main(0);
    five += rar5File("Some.Release.mkv", 5000, 12000, false, true, /*method*/ 3);
    const RarVolume v5 = parseRarVolume(five);
    QCOMPARE(v5.status, RarParse::Ok);
    QVERIFY(!v5.entries.first().stored);
}

void tst_RarReader::solidAndEncryptedAreRefusedWithAReason()
{
    // Both refusals must carry text, because it is shown to the user in place of
    // a disabled Preview action.
    QByteArray solid = rar4Marker();
    solid += rar4Main(kMainFirstVolume | kMainSolid);
    solid += rar4File("Some.Release.mkv", 5000, 12000, kSplitAfter);
    const RarVolume vs = parseRarVolume(solid);
    QCOMPARE(vs.status, RarParse::Unsupported);
    QVERIFY(vs.solid);
    QVERIFY(!vs.reason.isEmpty());

    QByteArray enc = rar4Marker();
    enc += rar4Main(kMainFirstVolume | kMainPassword);
    const RarVolume ve = parseRarVolume(enc);
    QCOMPARE(ve.status, RarParse::Unsupported);
    QVERIFY(ve.headersEncrypted);
    QVERIFY(!ve.reason.isEmpty());

    // Per-entry encryption: the bytes are there but they are not the payload.
    QByteArray perFile = rar4Marker();
    perFile += rar4Main(kMainFirstVolume);
    perFile += rar4File("Some.Release.mkv", 5000, 12000, kSplitAfter | kFilePassword);
    QVERIFY(parseRarVolume(perFile).entries.first().encrypted);
}

void tst_RarReader::truncationAtEveryLengthAsksForMoreBytes()
{
    // The caller routinely holds only the first article of a volume, and an
    // article boundary lands wherever it lands. Every prefix must either parse
    // or ask for more — never read past the buffer, never invent an entry.
    QByteArray four = rar4Marker();
    four += rar4Main(kMainFirstVolume);
    four += rar4File("Some.Release.mkv", 5000, 12000, kSplitAfter);

    QByteArray five = rar5Marker();
    five += rar5Main(0);
    five += rar5File("Some.Release.mkv", 5000, 12000, false, true);

    for (const QByteArray& full : {four, five}) {
        for (int n = 0; n < full.size(); ++n) {
            const RarVolume v = parseRarVolume(full.left(n));
            QVERIFY2(v.status == RarParse::NeedMoreBytes || v.status == RarParse::Ok,
                     qPrintable(QStringLiteral("prefix of %1 bytes gave status %2")
                                    .arg(n).arg(int(v.status))));
            if (v.status == RarParse::NeedMoreBytes)
                QVERIFY(v.entries.isEmpty());
        }
        QCOMPARE(parseRarVolume(full).status, RarParse::Ok);
    }
}

void tst_RarReader::nonRarBytesAreReportedAsSuch()
{
    // A par2 file, a jpeg, an HTML error page — all reach this the same way, and
    // NotRar is what lets the caller fall back to treating the file as raw.
    QCOMPARE(parseRarVolume(QByteArray("PAR2\0PKT", 8)).status, RarParse::NotRar);
    QCOMPARE(parseRarVolume(QByteArray(64, 'q')).status, RarParse::NotRar);

    // Too short to tell a marker from a truncation: asking for more is the only
    // answer that cannot be wrong.
    QCOMPARE(parseRarVolume(QByteArray("Rar!", 4)).status, RarParse::NeedMoreBytes);
    QCOMPARE(parseRarVolume(QByteArray()).status, RarParse::NeedMoreBytes);
}

QTEST_MAIN(tst_RarReader)
#include "tst_RarReader.moc"
