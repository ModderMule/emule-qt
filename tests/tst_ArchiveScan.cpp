/// @file tst_ArchiveScan.cpp
/// @brief The header-only ZIP / RAR listing behind the archive preview: CRC, comments,
///        attributes, and which entries of a part file are already whole.

#include "TestHelpers.h"
#include "RarFixtures.h"

#include "archive/ArchiveScan.h"

#include <QBuffer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTest>

using namespace eMule;
using namespace eMule::testing::rar;

namespace {

void le16(QByteArray& b, quint16 v) { b.append(char(v & 0xFF)).append(char(v >> 8)); }
void le32(QByteArray& b, quint32 v) { le16(b, quint16(v & 0xFFFF)); le16(b, quint16(v >> 16)); }

struct ZipMember {
    QByteArray name;
    QByteArray data;
    QByteArray comment;
    quint16 flags = 0;
    quint32 crc = 0;
};

struct ZipImage {
    QByteArray bytes;
    QList<qint64> dataStart;    ///< per member: where its data begins
    qint64 directoryStart = 0;
};

/// A stored ZIP, byte by byte: local headers, central directory, end record.
ZipImage makeZip(const QList<ZipMember>& members)
{
    ZipImage zip;
    QList<qint64> localOffsets;
    for (const ZipMember& m : members) {
        localOffsets << zip.bytes.size();
        le32(zip.bytes, 0x04034B50);
        le16(zip.bytes, 20);
        le16(zip.bytes, m.flags);
        le16(zip.bytes, 0);                       // stored
        le16(zip.bytes, 0x6000);                  // 12:00:00
        le16(zip.bytes, 0x5B49);                  // 2025-10-09
        le32(zip.bytes, m.crc);
        le32(zip.bytes, quint32(m.data.size()));
        le32(zip.bytes, quint32(m.data.size()));
        le16(zip.bytes, quint16(m.name.size()));
        le16(zip.bytes, 0);
        zip.bytes.append(m.name);
        zip.dataStart << zip.bytes.size();
        zip.bytes.append(m.data);
    }
    zip.directoryStart = zip.bytes.size();
    for (qsizetype i = 0; i < members.size(); ++i) {
        const ZipMember& m = members.at(i);
        le32(zip.bytes, 0x02014B50);
        le16(zip.bytes, 20);
        le16(zip.bytes, 20);
        le16(zip.bytes, m.flags);
        le16(zip.bytes, 0);
        le16(zip.bytes, 0x6000);
        le16(zip.bytes, 0x5B49);
        le32(zip.bytes, m.crc);
        le32(zip.bytes, quint32(m.data.size()));
        le32(zip.bytes, quint32(m.data.size()));
        le16(zip.bytes, quint16(m.name.size()));
        le16(zip.bytes, 0);
        le16(zip.bytes, quint16(m.comment.size()));
        le16(zip.bytes, 0);
        le16(zip.bytes, 0);
        le32(zip.bytes, m.name.endsWith('/') ? 0x10 : 0);
        le32(zip.bytes, quint32(localOffsets.at(i)));
        zip.bytes.append(m.name);
        zip.bytes.append(m.comment);
    }
    const qint64 directorySize = zip.bytes.size() - zip.directoryStart;
    le32(zip.bytes, 0x06054B50);
    le16(zip.bytes, 0);
    le16(zip.bytes, 0);
    le16(zip.bytes, quint16(members.size()));
    le16(zip.bytes, quint16(members.size()));
    le32(zip.bytes, quint32(directorySize));
    le32(zip.bytes, quint32(zip.directoryStart));
    le16(zip.bytes, 0);
    return zip;
}

ArchiveScanResult scan(QByteArray bytes, const ArchiveGaps& gaps = {})
{
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    return scanArchive(buffer, bytes.size(), gaps);
}

const QList<ZipMember> kThree = {
    {"docs/", {}, {}, 0, 0},
    {"docs/readme.txt", QByteArray(100, 'r'), "the readme", 0, 0xCAFEBABE},
    {"secret.bin", QByteArray(400, 's'), {}, 0x0001, 0x11223344},
};

} // namespace

class tst_ArchiveScan : public QObject {
    Q_OBJECT

private slots:
    void zip_listsCrcCommentAndAttributes();
    void zip_marksEntriesWithMissingData();
    void zip_withoutItsDirectoryIsWalkedFromTheStart();
    void zip_madeByTheSystemTool();
    void rar4_listsFlagsAndArchiveMarks();
    void rar4_encryptedHeadersAreSaidSo();
    void rar5_listsAndFindsMissingData();
    void start_missingIsInsufficientData();
    void unknown_isNotAnArchive();
    void ace_listsBlocksAndMarksMissingData();
    void iso_listsTheTreeAndMarksMissingData();
    void iso_withoutJolietUsesThePlainNames();
};

// MFC ArchivePreviewDlg.cpp:853-900: CRC as %08X, the entry comment, P and D.
// libarchive gave "--", nothing and an octal mode.
void tst_ArchiveScan::zip_listsCrcCommentAndAttributes()
{
    const ArchiveScanResult r = scan(makeZip(kThree).bytes);
    QCOMPARE(r.type, ArchiveScanType::Zip);
    QCOMPARE(r.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(r.entries.size(), 3);
    QCOMPARE(r.fileCount(), 2);                       // the directory is not a file (G44)

    QVERIFY(r.entries.at(0).directory);
    QCOMPARE(r.entries.at(0).attributes(), QStringLiteral("D"));

    const ArchiveScanEntry& readme = r.entries.at(1);
    QCOMPARE(readme.name, QStringLiteral("docs/readme.txt"));
    QCOMPARE(readme.size, quint64(100));
    QVERIFY(readme.hasCrc);
    QCOMPARE(readme.crc, quint32(0xCAFEBABE));
    QCOMPARE(readme.comment, QStringLiteral("the readme"));
    QCOMPARE(readme.attributes(), QString());
    QCOMPARE(readme.modified, QDateTime(QDate(2025, 10, 9), QTime(12, 0, 0)));
    QVERIFY(readme.complete);

    QCOMPARE(r.entries.at(2).attributes(), QStringLiteral("P"));
    QVERIFY(r.passwordProtected);
    QCOMPARE(r.infoLine(QStringLiteral("Password protection"), QStringLiteral("Comment")),
             QStringLiteral("Password protection"));
}

// MFC marks an entry whose packed data is not there yet with M and greys the row
// (ArchivePreviewDlg.cpp:297-311, 876-883). The port could not tell.
void tst_ArchiveScan::zip_marksEntriesWithMissingData()
{
    const ZipImage zip = makeZip(kThree);
    // a hole in the middle of the last member's data
    const qint64 hole = zip.dataStart.at(2) + 100;
    const ArchiveScanResult r = scan(zip.bytes, {{hole, hole + 49}});

    QCOMPARE(r.status, ArchiveScanResult::Status::Ok);   // the directory is whole
    QCOMPARE(r.entries.size(), 3);
    QVERIFY(r.entries.at(1).complete);
    QVERIFY(!r.entries.at(2).complete);
    QCOMPARE(r.entries.at(2).attributes(), QStringLiteral("P,M"));

    // a hole over a local header: its data cannot be placed, so it is not whole either
    const ArchiveScanResult headerGone = scan(zip.bytes, {{zip.dataStart.at(1) - 10, zip.dataStart.at(1) - 5}});
    QVERIFY(!headerGone.entries.at(1).complete);
    QVERIFY(headerGone.entries.at(2).complete);
}

// The end of a download arrives last. Until then the local headers are all there is
// (MFC: "File list may be incomplete.").
void tst_ArchiveScan::zip_withoutItsDirectoryIsWalkedFromTheStart()
{
    const ZipImage zip = makeZip(kThree);
    const ArchiveScanResult r = scan(zip.bytes, {{zip.directoryStart, zip.bytes.size() - 1}});
    QCOMPARE(r.type, ArchiveScanType::Zip);
    QCOMPARE(r.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(r.entries.size(), 3);
    QCOMPARE(r.entries.at(1).name, QStringLiteral("docs/readme.txt"));
    QCOMPARE(r.entries.at(1).crc, quint32(0xCAFEBABE));
    QVERIFY(r.entries.at(2).encrypted);

    // cut inside the second member: the first two are listed, the walk stops there
    const qint64 cut = zip.dataStart.at(2) - 20;
    const ArchiveScanResult shortList = scan(zip.bytes, {{cut, zip.bytes.size() - 1}});
    QCOMPARE(shortList.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(shortList.entries.size(), 2);
}

// Not only the bytes this test wrote itself: an archive from the system's own zip.
void tst_ArchiveScan::zip_madeByTheSystemTool()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("zip"));
    if (tool.isEmpty())
        QSKIP("no zip tool on this machine");

    eMule::testing::TempDir tmp;
    const QByteArray payload = QByteArray(5000, 'x') + "tail";
    QFile file(tmp.filePath(QStringLiteral("payload.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(payload);
    file.close();

    QProcess zip;
    zip.setWorkingDirectory(tmp.path());
    zip.start(tool, {QStringLiteral("-q"), QStringLiteral("real.zip"), QStringLiteral("payload.txt")});
    QVERIFY(zip.waitForFinished(20000));
    QCOMPARE(zip.exitCode(), 0);

    QFile archive(tmp.filePath(QStringLiteral("real.zip")));
    QVERIFY(archive.open(QIODevice::ReadOnly));
    const ArchiveScanResult r = scanArchive(archive, archive.size());
    QCOMPARE(r.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(r.entries.size(), 1);
    QCOMPARE(r.entries.first().name, QStringLiteral("payload.txt"));
    QCOMPARE(r.entries.first().size, quint64(payload.size()));
    QCOMPARE(r.entries.first().crc, rarCrc32(payload));   // the standard CRC-32
    QVERIFY(r.entries.first().complete);
}

// MFC ArchivePreviewDlg.cpp:683-737 (P, D, <, >, C, M, Lx) and :764-789 (the info line)
void tst_ArchiveScan::rar4_listsFlagsAndArchiveMarks()
{
    QByteArray rar = rar4Marker();
    rar += rar4Main(0x0008 | 0x0004 | 0x0040 | 0x0002);     // solid, locked, recovery record, comment
    rar += rar4File("first.bin", 200, 900, 0x0004 | 0x0002, 0x33, 0xDEADBEEF);   // password, continues
    const qint64 firstData = rar.size();
    rar += QByteArray(200, 'a');
    rar += rar4File("folder", 0, 0, 0x00E0);
    rar += rar4File("second.bin", 300, 300, 0x0001);         // continued from the previous volume
    const qint64 secondData = rar.size();
    rar += QByteArray(300, 'b');

    const ArchiveScanResult whole = scan(rar);
    QCOMPARE(whole.type, ArchiveScanType::Rar);
    QCOMPARE(whole.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(whole.entries.size(), 3);
    QCOMPARE(whole.fileCount(), 2);
    QCOMPARE(whole.entries.at(0).name, QStringLiteral("first.bin"));
    QCOMPARE(whole.entries.at(0).size, quint64(900));
    QCOMPARE(whole.entries.at(0).crc, quint32(0xDEADBEEF));
    QCOMPARE(whole.entries.at(0).attributes(), QStringLiteral("P,>, L3"));
    QCOMPARE(whole.entries.at(1).attributes(), QStringLiteral("D, L0"));
    QCOMPARE(whole.entries.at(2).attributes(), QStringLiteral("<, L0"));
    QCOMPARE(whole.infoLine(QStringLiteral("Password protection"), QStringLiteral("Comment")),
             QStringLiteral("Password protection,Solid,Locked,RecoveryRec,Comment"));

    // The headers are all there, one member's data is not
    const ArchiveScanResult holed = scan(rar, {{secondData + 10, secondData + 19}});
    QCOMPARE(holed.entries.size(), 3);
    QVERIFY(holed.entries.at(0).complete);
    QVERIFY(!holed.entries.at(2).complete);
    QCOMPARE(holed.entries.at(2).attributes(), QStringLiteral("<,M, L0"));

    // A missing header ends the list there
    const ArchiveScanResult cut = scan(rar, {{firstData + 200, rar.size() - 1}});
    QCOMPARE(cut.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(cut.entries.size(), 1);
}

// MFC: "Headers encrypted - unable to read archive."
void tst_ArchiveScan::rar4_encryptedHeadersAreSaidSo()
{
    QByteArray rar = rar4Marker();
    rar += rar4Main(0x0080);
    rar += QByteArray(500, '\x5A');   // ciphertext
    const ArchiveScanResult r = scan(rar);
    QCOMPARE(r.type, ArchiveScanType::Rar);
    QCOMPARE(r.status, ArchiveScanResult::Status::HeadersEncrypted);
    QVERIFY(r.passwordProtected);
    QVERIFY(r.entries.isEmpty());
}

void tst_ArchiveScan::rar5_listsAndFindsMissingData()
{
    QByteArray rar = rar5Marker();
    rar += rar5Main(0, /*solid*/ true);
    rar += rar5File("alpha.bin", 120, 480, false, true, 3);
    rar += QByteArray(120, 'a');
    rar += rar5File("beta.bin", 80, 80, true, false);
    const qint64 betaData = rar.size();
    rar += QByteArray(80, 'b');

    const ArchiveScanResult whole = scan(rar);
    QCOMPARE(whole.type, ArchiveScanType::Rar);
    QCOMPARE(whole.status, ArchiveScanResult::Status::Ok);
    QVERIFY(whole.solid);
    QCOMPARE(whole.entries.size(), 2);
    QCOMPARE(whole.entries.at(0).name, QStringLiteral("alpha.bin"));
    QCOMPARE(whole.entries.at(0).size, quint64(480));
    QCOMPARE(whole.entries.at(0).packedSize, quint64(120));
    QCOMPARE(whole.entries.at(0).attributes(), QStringLiteral(">, L3"));
    QCOMPARE(whole.entries.at(1).attributes(), QStringLiteral("<, L0"));

    const ArchiveScanResult holed = scan(rar, {{betaData, betaData}});
    QVERIFY(holed.entries.at(0).complete);
    QVERIFY(!holed.entries.at(1).complete);
}

// MFC: "Insufficient data available." — the type is in the first bytes.
void tst_ArchiveScan::start_missingIsInsufficientData()
{
    const ZipImage zip = makeZip(kThree);
    const ArchiveScanResult r = scan(zip.bytes, {{0, 3}});
    QCOMPARE(r.status, ArchiveScanResult::Status::InsufficientData);
    QCOMPARE(r.type, ArchiveScanType::Unknown);

    QByteArray bytes = zip.bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QCOMPARE(detectArchiveType(buffer, {{0, 3}}), ArchiveScanType::Unknown);
    QCOMPARE(detectArchiveType(buffer, {}), ArchiveScanType::Zip);
}

void tst_ArchiveScan::unknown_isNotAnArchive()
{
    const ArchiveScanResult r = scan(QByteArray(4096, 'q'));
    QCOMPARE(r.type, ArchiveScanType::Unknown);
    QCOMPARE(r.status, ArchiveScanResult::Status::NoTableOfContents);
    QVERIFY(r.entries.isEmpty());
}

// ACE has no reader in libarchive: listed from its block headers, as MFC does.
void tst_ArchiveScan::ace_listsBlocksAndMarksMissingData()
{
    const auto block = [](const QByteArray& body) -> QByteArray {
        QByteArray b;
        le16(b, 0);                              // HEAD_CRC, not checked
        le16(b, quint16(body.size()));           // HEAD_SIZE
        return b + body;
    };
    const auto fileBlock = [&](const QByteArray& name, const QByteArray& data, quint16 flags,
                               quint32 attributes) -> QByteArray {
        QByteArray body;
        body.append(char(1));                    // HEAD_TYPE: file
        le16(body, quint16(flags | 0x0001));     // ADDSIZE
        le32(body, quint32(data.size()));        // PACK_SIZE
        le32(body, quint32(data.size()));        // ORIG_SIZE
        le32(body, (quint32((2026 - 1980) << 9 | 10 << 5 | 9) << 16) | (7 << 11));   // 2026-10-09 07:00
        le32(body, attributes);
        le32(body, 0xCAFEF00D);                  // CRC32
        le32(body, 0);                           // compression
        le16(body, 0x4554);
        le16(body, quint16(name.size()));
        body.append(name);
        return block(body) + data;
    };

    QByteArray mainBody;
    mainBody.append(char(0));                    // HEAD_TYPE: main
    le16(mainBody, 0x8000);                      // solid
    mainBody.append("**ACE**", 7);
    mainBody.append(QByteArray(16, char(0)));
    const QByteArray head = block(mainBody);
    const QByteArray one = fileBlock("docs\\one.bin", QByteArray(4000, 'a'), 0, 0x20);
    const QByteArray dir = fileBlock("docs", {}, 0, 0x10);
    const QByteArray two = fileBlock("two.bin", QByteArray(4000, 'b'), 0x4000, 0x20);   // password
    const QByteArray ace = head + one + dir + two;

    const ArchiveScanResult whole = scan(ace);
    QCOMPARE(whole.type, ArchiveScanType::Ace);
    QCOMPARE(whole.status, ArchiveScanResult::Status::Ok);
    QVERIFY(whole.solid);
    QVERIFY(whole.passwordProtected);
    QCOMPARE(whole.entries.size(), 3);
    QCOMPARE(whole.fileCount(), 2);
    QCOMPARE(whole.entries.at(0).name, QStringLiteral("docs/one.bin"));
    QCOMPARE(whole.entries.at(0).size, quint64{4000});
    QCOMPARE(whole.entries.at(0).crc, quint32{0xCAFEF00D});
    QCOMPARE(whole.entries.at(0).modified, QDateTime(QDate(2026, 10, 9), QTime(7, 0)));
    QVERIFY(whole.entries.at(0).complete);
    QVERIFY(whole.entries.at(1).directory);
    QVERIFY(whole.entries.at(2).encrypted);

    // the data of the last member is not all there: listed, marked
    const ArchiveScanResult holed = scan(ace, {{ace.size() - 100, ace.size() - 1}});
    QCOMPARE(holed.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(holed.entries.size(), 3);
    QVERIFY(holed.entries.at(0).complete);
    QVERIFY(!holed.entries.at(2).complete);
    QCOMPARE(holed.entries.at(2).attributes(), QStringLiteral("P,M"));

    // the download ends inside the second member's header: as far as it goes
    const qint64 cut = head.size() + one.size() + 10;
    const ArchiveScanResult shortOne = scan(ace, {{cut, ace.size() - 1}});
    QCOMPARE(shortOne.type, ArchiveScanType::Ace);
    QCOMPARE(shortOne.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(shortOne.entries.size(), 1);

    // a .part is only as long as what was written: the chain points past its end
    const ArchiveScanResult truncated = scan(ace.left(ace.size() - 100));
    QCOMPARE(truncated.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(truncated.entries.size(), 3);
    QVERIFY(!truncated.entries.at(2).complete);

    QBuffer buffer(const_cast<QByteArray*>(&ace));
    buffer.open(QIODevice::ReadOnly);
    QCOMPARE(detectArchiveType(buffer, {}), ArchiveScanType::Ace);
}

namespace {

constexpr qsizetype kSector = 2048;

/// One ISO 9660 directory record; only the little-endian halves are filled in.
QByteArray isoRecord(const QByteArray& name, quint32 extent, quint32 length, quint8 flags)
{
    QByteArray r;
    r.append(char(0)).append(char(0));
    le32(r, extent);
    le32(r, 0);
    le32(r, length);
    le32(r, 0);
    for (const int v : {125, 10, 9, 12, 0, 0, 0})   // 2025-10-09 12:00:00 GMT
        r.append(char(v));
    r.append(char(flags)).append(char(0)).append(char(0));
    le32(r, 1);
    r.append(char(name.size())).append(name);
    if (r.size() & 1)
        r.append(char(0));
    r[0] = char(r.size());
    return r;
}

QByteArray utf16be(const QString& text)
{
    QByteArray out;
    for (const QChar c : text)
        out.append(char(c.unicode() >> 8)).append(char(c.unicode() & 0xFF));
    return out;
}

QByteArray isoSector(const QByteArray& content)
{
    return content.leftJustified(kSector, char(0));
}

QByteArray isoDescriptor(quint8 type, quint32 rootExtent = 0, const QByteArray& escape = {})
{
    QByteArray d(kSector, char(0));
    d[0] = char(type);
    d.replace(1, 5, "CD001");
    d[6] = char(1);
    d.replace(88, escape.size(), escape);
    d[128] = char(kSector & 0xFF);
    d[129] = char(kSector >> 8);
    d.replace(156, 34, isoRecord(QByteArray(1, char(0)), rootExtent, kSector, 0x02));
    return d;
}

const QByteArray kSelf(1, char(0));
const QByteArray kParent(1, char(1));

/// Sectors: 16 primary, 17 boot, 18 Joliet (optional), 19 terminator, 20 plain root,
/// 21 Joliet root, 22 Joliet "docs", 24-26 big.bin (5000 bytes), 27 readme (100).
QByteArray makeIso(bool joliet)
{
    QByteArray boot(kSector, char(0));
    boot.replace(1, 5, "CD001");
    boot.replace(7, 23, "EL TORITO SPECIFICATION");

    QByteArray iso(16 * kSector, char(0));
    iso += isoDescriptor(1, 20);
    iso += boot;
    iso += joliet ? isoDescriptor(2, 21, QByteArray::fromHex("252F45")) : isoSector({});
    iso += isoDescriptor(0xFF);
    iso += isoSector(isoRecord(kSelf, 20, kSector, 0x02) + isoRecord(kParent, 20, kSector, 0x02)
                     + isoRecord("BIG.BIN;1", 24, 5000, 0x01) + isoRecord("NOEXT.;1", 27, 100, 0));
    iso += isoSector(isoRecord(kSelf, 21, kSector, 0x02) + isoRecord(kParent, 21, kSector, 0x02)
                     + isoRecord(utf16be(QStringLiteral("docs")), 22, kSector, 0x02)
                     + isoRecord(utf16be(QStringLiteral("big.bin")), 24, 5000, 0x01));
    iso += isoSector(isoRecord(kSelf, 22, kSector, 0x02) + isoRecord(kParent, 21, kSector, 0x02)
                     + isoRecord(utf16be(QStringLiteral("readme.txt")), 27, 100, 0));
    iso += QByteArray(kSector, char(0));
    iso += QByteArray(3 * kSector, 'b');
    iso += isoSector(QByteArray(100, 'r'));
    return iso;
}

} // namespace

// MFC ArchivePreviewDlg.cpp:498-617: an image has no signature at its start, the tree
// hangs off the volume descriptor in sector 16, and an entry is whole when its extent is.
void tst_ArchiveScan::iso_listsTheTreeAndMarksMissingData()
{
    const QByteArray iso = makeIso(true);

    const ArchiveScanResult whole = scan(iso);
    QCOMPARE(whole.type, ArchiveScanType::Iso);
    QCOMPARE(whole.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(whole.entries.size(), 3);
    QCOMPARE(whole.entries.at(0).name, QStringLiteral("docs/"));
    QCOMPARE(whole.entries.at(0).attributes(), QStringLiteral("D"));
    QCOMPARE(whole.entries.at(1).name, QStringLiteral("docs/readme.txt"));
    QCOMPARE(whole.entries.at(1).size, quint64(100));
    QCOMPARE(whole.entries.at(1).attributes(), QString());
    QVERIFY(whole.entries.at(1).modified.isValid());
    QCOMPARE(whole.entries.at(1).modified.toUTC().time(), QTime(12, 0));
    QCOMPARE(whole.entries.at(2).name, QStringLiteral("big.bin"));
    QCOMPARE(whole.entries.at(2).size, quint64(5000));
    QCOMPARE(whole.entries.at(2).attributes(), QStringLiteral("H"));
    QCOMPARE(whole.fileCount(), 2);
    QCOMPARE(whole.imageInfoLine(QStringLiteral("bootable")), QStringLiteral("bootable,ISO9660,Joliet"));

    // a hole in the middle of big.bin: listed, but marked
    const ArchiveScanResult holed = scan(iso, {{25 * kSector, 25 * kSector + 99}});
    QCOMPARE(holed.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(holed.entries.at(2).attributes(), QStringLiteral("H,M"));
    QVERIFY(holed.entries.at(1).complete);

    // the "docs" directory itself is missing: what is in it cannot be listed
    const ArchiveScanResult noDocs = scan(iso, {{22 * kSector, 23 * kSector - 1}});
    QCOMPARE(noDocs.status, ArchiveScanResult::Status::ListIncomplete);
    QCOMPARE(noDocs.entries.size(), 2);
    QCOMPARE(noDocs.entries.at(0).attributes(), QStringLiteral("D,M"));

    // a .part that ends before the last file
    const ArchiveScanResult shortOne = scan(iso.left(27 * kSector));
    QCOMPARE(shortOne.entries.size(), 3);
    QVERIFY(!shortOne.entries.at(1).complete);
    QVERIFY(shortOne.entries.at(2).complete);

    // no root directory at all
    const ArchiveScanResult noRoot = scan(iso, {{21 * kSector, 22 * kSector - 1}});
    QCOMPARE(noRoot.type, ArchiveScanType::Iso);
    QCOMPARE(noRoot.status, ArchiveScanResult::Status::InsufficientData);

    QByteArray copy = iso;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    QCOMPARE(detectArchiveType(buffer, {}), ArchiveScanType::Iso);
}

void tst_ArchiveScan::iso_withoutJolietUsesThePlainNames()
{
    const ArchiveScanResult r = scan(makeIso(false));
    QCOMPARE(r.type, ArchiveScanType::Iso);
    QCOMPARE(r.status, ArchiveScanResult::Status::Ok);
    QCOMPARE(r.entries.size(), 2);
    // the version suffix goes, and so does the dot of a name without extension
    QCOMPARE(r.entries.at(0).name, QStringLiteral("BIG.BIN"));
    QCOMPARE(r.entries.at(1).name, QStringLiteral("NOEXT"));
    QCOMPARE(r.imageInfoLine(QStringLiteral("bootable")), QStringLiteral("bootable,ISO9660"));
}

QTEST_GUILESS_MAIN(tst_ArchiveScan)
#include "tst_ArchiveScan.moc"
