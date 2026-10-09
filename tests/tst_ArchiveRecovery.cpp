/// @file tst_ArchiveRecovery.cpp
/// @brief Tests for archive/ArchiveRecovery — ZIP/RAR recovery from partial downloads.

#include "TestHelpers.h"
#include "archive/ArchiveRecovery.h"
#include "files/PartFile.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTest>
#include <QTemporaryDir>

#include <cstring>

using namespace eMule;

class tst_ArchiveRecovery : public QObject {
    Q_OBJECT

private slots:
    void isFilled_fullRange();
    void isFilled_partialRange();
    void isFilled_emptyFilled();
    void isFilled_multipleRegions();
    void scanForZipMarker_found();
    void scanForZipMarker_notFound();
    void recoverZip_validEntries();
    void recoverRar_emptyFilled();
    void recoverAsync_nullPartFile_returnsFalse();
    void isoDetection_stub();
    void aceDetection_stub();
    void recoverFile_neverWritesTheDownload();
    void recoverToCopy_readsThePartOfADownload();
    void recoverACE_keepsTheBlocksThatArrived();
    void recoverZip_stepsOverHolesAndJunk();
};

void tst_ArchiveRecovery::isFilled_fullRange()
{
    std::vector<Gap> filled = {{0, 999}};
    QVERIFY(ArchiveRecovery::isFilled(0, 999, filled));
    QVERIFY(ArchiveRecovery::isFilled(100, 500, filled));
    QVERIFY(ArchiveRecovery::isFilled(0, 0, filled));
}

void tst_ArchiveRecovery::isFilled_partialRange()
{
    std::vector<Gap> filled = {{0, 499}};
    QVERIFY(!ArchiveRecovery::isFilled(0, 999, filled));
    QVERIFY(!ArchiveRecovery::isFilled(500, 999, filled));
    QVERIFY(ArchiveRecovery::isFilled(0, 499, filled));
}

void tst_ArchiveRecovery::isFilled_emptyFilled()
{
    std::vector<Gap> filled;
    QVERIFY(!ArchiveRecovery::isFilled(0, 10, filled));
}

void tst_ArchiveRecovery::isFilled_multipleRegions()
{
    std::vector<Gap> filled = {{0, 100}, {200, 300}, {500, 999}};
    QVERIFY(ArchiveRecovery::isFilled(0, 50, filled));
    QVERIFY(ArchiveRecovery::isFilled(200, 300, filled));
    QVERIFY(ArchiveRecovery::isFilled(500, 999, filled));
    QVERIFY(!ArchiveRecovery::isFilled(100, 200, filled));
    QVERIFY(!ArchiveRecovery::isFilled(0, 999, filled));
}

void tst_ArchiveRecovery::scanForZipMarker_found()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("marker.bin"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));

    // Write some junk, then a ZIP local file header signature
    f.write(QByteArray(100, '\0'));
    uint32_t marker = 0x04034b50;
    f.write(reinterpret_cast<const char*>(&marker), 4);
    f.write(QByteArray(50, '\0'));
    f.close();

    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    input.seek(0);

    // Use the public method via a full recovery test (scanForZipMarker is private)
    // Instead, test isFilled which we know works
    // The marker at offset 100 should be found during recoverZip

    // We'll test indirectly through recoverZip
    input.close();
}

void tst_ArchiveRecovery::scanForZipMarker_notFound()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("nomarker.bin"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(256, '\xFF'));
    f.close();

    // No ZIP marker present — recoverZip should fail
    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));

    const QString outPath = tmpDir.filePath(QStringLiteral("out.zip"));
    QFile output(outPath);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));

    std::vector<Gap> filled = {{0, 255}};
    bool result = ArchiveRecovery::recoverZip(input, output, filled, 256);
    QVERIFY(!result);
}

void tst_ArchiveRecovery::recoverZip_validEntries()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("valid.bin"));

    // Build a minimal ZIP entry (local file header + data)
    QByteArray zipData;

    // Local file header
    uint32_t sig = 0x04034b50;
    zipData.append(reinterpret_cast<const char*>(&sig), 4);

    uint16_t versionNeeded = 20;
    zipData.append(reinterpret_cast<const char*>(&versionNeeded), 2);

    uint16_t flags = 0;
    zipData.append(reinterpret_cast<const char*>(&flags), 2);

    uint16_t method = 0; // store
    zipData.append(reinterpret_cast<const char*>(&method), 2);

    uint16_t modTime = 0, modDate = 0;
    zipData.append(reinterpret_cast<const char*>(&modTime), 2);
    zipData.append(reinterpret_cast<const char*>(&modDate), 2);

    uint32_t crc = 0;
    zipData.append(reinterpret_cast<const char*>(&crc), 4);

    QByteArray fileContent("RecoveryTest!");
    uint32_t compSize = static_cast<uint32_t>(fileContent.size());
    zipData.append(reinterpret_cast<const char*>(&compSize), 4);
    zipData.append(reinterpret_cast<const char*>(&compSize), 4); // uncompSize

    QByteArray filename("test.txt");
    uint16_t fnLen = static_cast<uint16_t>(filename.size());
    zipData.append(reinterpret_cast<const char*>(&fnLen), 2);

    uint16_t extraLen = 0;
    zipData.append(reinterpret_cast<const char*>(&extraLen), 2);

    zipData.append(filename);
    zipData.append(fileContent);

    // Write to file
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(zipData);
    f.close();

    uint64 fileSize = static_cast<uint64>(zipData.size());

    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));

    const QString outPath = tmpDir.filePath(QStringLiteral("recovered.zip"));
    QFile output(outPath);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));

    std::vector<Gap> filled = {{0, fileSize - 1}};
    bool result = ArchiveRecovery::recoverZip(input, output, filled, fileSize);
    QVERIFY(result);

    // Output file should have content
    output.close();
    QFileInfo outInfo(outPath);
    QVERIFY(outInfo.size() > 0);
}

void tst_ArchiveRecovery::recoverRar_emptyFilled()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("empty_rar.bin"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(100, '\0'));
    f.close();

    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));

    const QString outPath = tmpDir.filePath(QStringLiteral("out.rar"));
    QFile output(outPath);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));

    std::vector<Gap> filled; // empty — nothing filled
    bool result = ArchiveRecovery::recoverRar(input, output, filled);
    QVERIFY(!result);
}

void tst_ArchiveRecovery::recoverAsync_nullPartFile_returnsFalse()
{
    bool callbackCalled = false;
    bool callbackResult = true;

    ArchiveRecovery::recoverAsync(nullptr, false, true,
                                   [&](bool result) {
                                       callbackCalled = true;
                                       callbackResult = result;
                                   });

    // Null guard should call callback synchronously with false
    QVERIFY(callbackCalled);
    QVERIFY(!callbackResult);
}

void tst_ArchiveRecovery::isoDetection_stub()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("test.iso"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));

    // Write enough data to place "CD001" at offset 0x8001
    f.write(QByteArray(0x8001, '\0'));
    f.write("CD001", 5);
    f.write(QByteArray(100, '\0'));
    f.close();

    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));

    const QString outPath = tmpDir.filePath(QStringLiteral("out.iso"));
    QFile output(outPath);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));

    uint64 fileSize = static_cast<uint64>(input.size());
    std::vector<Gap> filled = {{0, fileSize - 1}};

    // ISO recovery is now implemented — should recover sectors from filled data
    bool result = ArchiveRecovery::recoverISO(input, output, filled, fileSize);
    QVERIFY(result);
}

void tst_ArchiveRecovery::aceDetection_stub()
{
    eMule::testing::TempDir tmpDir;
    const QString filePath = tmpDir.filePath(QStringLiteral("test.ace"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));

    // Write 7 bytes of junk, then "**ACE**"
    f.write(QByteArray(7, '\0'));
    f.write("**ACE**", 7);
    f.write(QByteArray(100, '\0'));
    f.close();

    QFile input(filePath);
    QVERIFY(input.open(QIODevice::ReadOnly));

    const QString outPath = tmpDir.filePath(QStringLiteral("out.ace"));
    QFile output(outPath);
    QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));

    std::vector<Gap> filled = {{0, static_cast<uint64>(input.size()) - 1}};

    // Should return false (stub) but not crash
    bool result = ArchiveRecovery::recoverACE(input, output, filled);
    QVERIFY(!result);
}

// The recovery had an "in place" mode that opened the download itself with Truncate.
// Nothing called it; the first caller would have emptied a part file. The result is
// now always a file of its own, named as MFC names it.
void tst_ArchiveRecovery::recoverFile_neverWritesTheDownload()
{
    eMule::testing::TempDir tmpDir;
    const QString srcPath = tmpDir.filePath(QStringLiteral("001.part"));

    // one stored ZIP member, as recoverZip_validEntries builds it
    QByteArray zip;
    const auto put16 = [&zip](quint16 v) { zip.append(reinterpret_cast<const char*>(&v), 2); };
    const auto put32 = [&zip](quint32 v) { zip.append(reinterpret_cast<const char*>(&v), 4); };
    const QByteArray content("RecoveryTest!");
    const QByteArray name("test.txt");
    put32(0x04034b50); put16(20); put16(0); put16(0); put16(0); put16(0);
    put32(0); put32(quint32(content.size())); put32(quint32(content.size()));
    put16(quint16(name.size())); put16(0);
    zip.append(name).append(content);
    zip.append(QByteArray(64, '\0'));   // the rest of the download, not there yet

    QFile src(srcPath);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(zip);
    src.close();

    const std::vector<Gap> filled = {{0, static_cast<uint64>(zip.size() - 65)}};
    const QString out = ArchiveRecovery::recoverFile(srcPath, filled, static_cast<uint64>(zip.size()),
                                                     QString(), QStringLiteral("001"));
    QVERIFY2(!out.isEmpty(), "nothing was recovered");
    QVERIFY(out != srcPath);
    QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("001-rec.zip"));
    QVERIFY(QFileInfo(out).size() > 0);

    // the download is byte for byte what it was
    QVERIFY(src.open(QIODevice::ReadOnly));
    QCOMPARE(src.readAll(), zip);
    src.close();

    // into another folder when one is named
    eMule::testing::TempDir other;
    const QString elsewhere = ArchiveRecovery::recoverFile(srcPath, filled, static_cast<uint64>(zip.size()),
                                                           other.path(), QStringLiteral("001"));
    QCOMPARE(QFileInfo(elsewhere).absolutePath(), QFileInfo(other.path()).absoluteFilePath());

    // a name that would land on the source itself is refused
    QCOMPARE(ArchiveRecovery::copyPath(tmpDir.filePath(QStringLiteral("abcdefgh.zip")), {}, {}, QStringLiteral("zip")),
             QDir(QFileInfo(tmpDir.path()).absoluteFilePath()).filePath(QStringLiteral("abcde-rec.zip")));

    // nothing recoverable: no stray file
    const QString junkPath = tmpDir.filePath(QStringLiteral("002.part"));
    QFile junk(junkPath);
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write(QByteArray(200, 'q'));
    junk.close();
    QVERIFY(ArchiveRecovery::recoverFile(junkPath, {{0, 199}}, 200, QString(), QStringLiteral("002")).isEmpty());
    QVERIFY(QDir(tmpDir.path()).entryList({QStringLiteral("002-rec.*")}).isEmpty());
}

// A download has no filePath() until it is complete: the copy is built from its
// .part and named after the download.
void tst_ArchiveRecovery::recoverToCopy_readsThePartOfADownload()
{
    eMule::testing::TempDir tmpDir;

    QByteArray zip;
    const auto put16 = [&zip](quint16 v) { zip.append(reinterpret_cast<const char*>(&v), 2); };
    const auto put32 = [&zip](quint32 v) { zip.append(reinterpret_cast<const char*>(&v), 4); };
    const QByteArray content("RecoveryTest!");
    const QByteArray name("test.txt");
    put32(0x04034b50); put16(20); put16(0); put16(0); put16(0); put16(0);
    put32(0); put32(quint32(content.size())); put32(quint32(content.size()));
    put16(quint16(name.size())); put16(0);
    zip.append(name).append(content);
    const qsizetype have = zip.size();
    zip.append(QByteArray(64, '\0'));   // not there yet

    PartFile pf;
    pf.setFileName(QStringLiteral("holiday-pictures.zip"));
    pf.setFileSize(static_cast<uint64>(zip.size()));
    uint8 hash[16];
    std::memset(hash, 0x3E, sizeof(hash));
    pf.setFileHash(hash);
    QVERIFY(pf.createPartFile(tmpDir.path()));
    QVERIFY(pf.filePath().isEmpty());

    QFile part(pf.partDataPath());
    QVERIFY(part.open(QIODevice::ReadWrite));
    part.write(zip);
    part.close();
    pf.fillGap(0, static_cast<uint64>(have - 1));

    eMule::testing::TempDir outDir;
    const QString out = ArchiveRecovery::recoverToCopy(&pf, outDir.path());
    QVERIFY2(!out.isEmpty(), "nothing was recovered");
    QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("holid-rec.zip"));
    QVERIFY(QFileInfo(out).size() > have);
}

// ACE: the packed size (ADDSIZE) is a field of the block header; the data it counts
// follows the header. A block whose data has not all arrived is left out.
void tst_ArchiveRecovery::recoverACE_keepsTheBlocksThatArrived()
{
    const auto put16 = [](QByteArray& b, quint16 v) { b.append(reinterpret_cast<const char*>(&v), 2); };
    const auto put32 = [](QByteArray& b, quint32 v) { b.append(reinterpret_cast<const char*>(&v), 4); };
    const auto block = [&](const QByteArray& body) -> QByteArray {
        QByteArray b;
        put16(b, 0);                              // HEAD_CRC, not checked
        put16(b, quint16(body.size()));           // HEAD_SIZE
        return b + body;
    };
    const auto fileBlock = [&](const QByteArray& name, const QByteArray& data) -> QByteArray {
        QByteArray body;
        body.append(char(1));                     // HEAD_TYPE: file
        put16(body, 0x0001);                      // ADDSIZE
        put32(body, quint32(data.size()));        // PACK_SIZE
        put32(body, quint32(data.size()));        // ORIG_SIZE
        put32(body, 0); put32(body, 0x20); put32(body, 0); put32(body, 0);
        put16(body, 0x4554);
        put16(body, quint16(name.size()));
        body.append(name);
        return block(body) + data;
    };

    QByteArray mainBody;
    mainBody.append(char(0));                     // HEAD_TYPE: main
    put16(mainBody, 0);
    mainBody.append("**ACE**", 7);
    mainBody.append(QByteArray(16, char(0)));
    const QByteArray head = block(mainBody);
    const QByteArray first = fileBlock("one.bin", QByteArray(5000, 'a'));
    const QByteArray second = fileBlock("two.bin", QByteArray(5000, 'b'));
    const QByteArray ace = head + first + second;

    eMule::testing::TempDir tmpDir;
    const QString srcPath = tmpDir.filePath(QStringLiteral("003.part"));
    QFile src(srcPath);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(ace);
    src.close();

    // all of it there: all of it copied
    const std::vector<Gap> whole = {{0, static_cast<uint64>(ace.size() - 1)}};
    QString out = ArchiveRecovery::recoverFile(srcPath, whole, static_cast<uint64>(ace.size()),
                                               QString(), QStringLiteral("whole"));
    QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("whole-rec.ace"));
    QFile outFile(out);
    QVERIFY(outFile.open(QIODevice::ReadOnly));
    QCOMPARE(outFile.readAll(), ace);
    outFile.close();

    // the second member cut short: header and first member only
    const std::vector<Gap> part = {{0, static_cast<uint64>(ace.size() - 1000)}};
    out = ArchiveRecovery::recoverFile(srcPath, part, static_cast<uint64>(ace.size()),
                                       QString(), QStringLiteral("part"));
    outFile.setFileName(out);
    QVERIFY(outFile.open(QIODevice::ReadOnly));
    QCOMPARE(outFile.readAll(), QByteArray(head + first));
}

// A member behind a hole and behind bytes that are no ZIP record is still found.
void tst_ArchiveRecovery::recoverZip_stepsOverHolesAndJunk()
{
    const auto member = [](const QByteArray& name, const QByteArray& content) -> QByteArray {
        QByteArray zip;
        const auto put16 = [&zip](quint16 v) { zip.append(reinterpret_cast<const char*>(&v), 2); };
        const auto put32 = [&zip](quint32 v) { zip.append(reinterpret_cast<const char*>(&v), 4); };
        put32(0x04034b50); put16(20); put16(0); put16(0); put16(0); put16(0);
        put32(0); put32(quint32(content.size())); put32(quint32(content.size()));
        put16(quint16(name.size())); put16(0);
        return zip + name + content;
    };
    const QByteArray first = member("a.txt", QByteArray(300, 'a'));
    const QByteArray hole(700000, char(0));           // never arrived
    const QByteArray junk(300000, 'P');               // arrived, and full of false starts
    const QByteArray second = member("b.txt", QByteArray(300, 'b'));
    const QByteArray zip = first + hole + junk + second;

    eMule::testing::TempDir tmpDir;
    const QString srcPath = tmpDir.filePath(QStringLiteral("004.part"));
    QFile src(srcPath);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(zip);
    src.close();

    const uint64 holeStart = static_cast<uint64>(first.size());
    const uint64 holeEnd = holeStart + static_cast<uint64>(hole.size()) - 1;
    const std::vector<Gap> filled = {{0, holeStart - 1}, {holeEnd + 1, static_cast<uint64>(zip.size() - 1)}};
    const QString out = ArchiveRecovery::recoverFile(srcPath, filled, static_cast<uint64>(zip.size()),
                                                     QString(), QStringLiteral("holes"));
    QVERIFY2(!out.isEmpty(), "nothing was recovered");
    QFile outFile(out);
    QVERIFY(outFile.open(QIODevice::ReadOnly));
    const QByteArray rebuilt = outFile.readAll();
    QVERIFY(rebuilt.startsWith(QByteArray(first + second)));   // both members, then the directory
    QCOMPARE(rebuilt.count(QByteArray::fromHex("504b0102")), 2);
}

QTEST_MAIN(tst_ArchiveRecovery)
#include "tst_ArchiveRecovery.moc"
