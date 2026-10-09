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

QTEST_GUILESS_MAIN(tst_ArchiveScan)
#include "tst_ArchiveScan.moc"
