/// @file tst_UsenetUnpack.cpp
/// @brief Volume-set detection and extraction, plus the archive safety rules.
///
/// Volume detection carries almost all the risk here, and none of it is
/// obvious. libarchive follows a multi-volume set on its own, but *only* when
/// opened on volume one; handed any other member it reports a split-file error
/// that reads like a corrupt download. The old `.rNN` scheme makes that worse by
/// putting its first volume under a different extension entirely, so a set
/// sorted by filename starts in the middle.
///
/// The extraction cases guard ArchiveReader's member-name sanitising. Usenet
/// archives are written by strangers, and a `../` member that escapes the
/// destination directory fails silently and destructively.

#include "RarFixtures.h"
#include "TestHelpers.h"

#include "archive/ArchiveReader.h"
#include "post/UsenetUnpacker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

using namespace eMule;
using namespace eMule::usenet;

namespace {

bool touch(const QString& path, const QByteArray& data = "x")
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

/// Build a zip whose member names are given verbatim — including the hostile
/// ones, which is the entire point.
bool writeZip(const QString& path, const QList<QPair<QString, QByteArray>>& members)
{
    auto* a = archive_write_new();
    if (!a)
        return false;
    archive_write_set_format_zip(a);
    if (archive_write_open_filename(a, path.toUtf8().constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }

    bool ok = true;
    for (const auto& [name, data] : members) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.toUtf8().constData());
        archive_entry_set_size(entry, data.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        if (archive_write_header(a, entry) != ARCHIVE_OK)
            ok = false;
        else if (archive_write_data(a, data.constData(), size_t(data.size())) != data.size())
            ok = false;
        archive_entry_free(entry);
    }

    archive_write_close(a);
    archive_write_free(a);
    return ok;
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

class TestUsenetUnpack : public QObject {
    Q_OBJECT

private slots:
    void detectsThePartNumberedRarScheme();
    void detectsTheOldRarSchemeWhereRarIsVolumeOne();
    void detectsNumberedContainerVolumes();
    void ignoresNonArchives();
    void unpacksAZipAndListsWhatItProduced();
    void reportsNothingToDoForAPlainDirectory();
    void refusesMembersThatEscapeTheDestination();
    void rewritesWindowsReservedNamesRatherThanDroppingThem();
    void unpacksAMultiVolumeRarSet();
    void skipsASetAlreadyUnpackedElsewhere();
};

// ---------------------------------------------------------------------------

void TestUsenetUnpack::detectsThePartNumberedRarScheme()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir d(dir.path());

    // Deliberately out of order on disk, and with a part width that makes the
    // string comparison disagree with the numeric one.
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.part10.rar"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.part02.rar"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.part01.rar"))));

    const QList<ArchiveSet> sets = UsenetUnpacker::findArchiveSets(dir.path());
    QCOMPARE(sets.size(), 1);
    QCOMPARE(sets.first().volumes.size(), 3);
    QCOMPARE(QFileInfo(sets.first().firstVolume).fileName(),
             QStringLiteral("Rel.part01.rar"));
}

void TestUsenetUnpack::detectsTheOldRarSchemeWhereRarIsVolumeOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir d(dir.path());

    QVERIFY(touch(d.filePath(QStringLiteral("Rel.r00"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.r01"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.rar"))));

    const QList<ArchiveSet> sets = UsenetUnpacker::findArchiveSets(dir.path());
    QCOMPARE(sets.size(), 1);
    QCOMPARE(sets.first().volumes.size(), 3);

    // The whole reason this scheme gets its own case: sorted by name, "Rel.rar"
    // is last, and opening "Rel.r00" instead yields a split-file error.
    QCOMPARE(QFileInfo(sets.first().firstVolume).fileName(), QStringLiteral("Rel.rar"));
}

void TestUsenetUnpack::detectsNumberedContainerVolumes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir d(dir.path());

    QVERIFY(touch(d.filePath(QStringLiteral("Rel.7z.003"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.7z.001"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.7z.002"))));

    const QList<ArchiveSet> sets = UsenetUnpacker::findArchiveSets(dir.path());
    QCOMPARE(sets.size(), 1);
    QCOMPARE(QFileInfo(sets.first().firstVolume).fileName(), QStringLiteral("Rel.7z.001"));
}

void TestUsenetUnpack::ignoresNonArchives()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir d(dir.path());

    QVERIFY(touch(d.filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.par2"))));
    QVERIFY(touch(d.filePath(QStringLiteral("Rel.vol000+01.par2"))));
    QVERIFY(touch(d.filePath(QStringLiteral("notes.nfo"))));

    QVERIFY(UsenetUnpacker::findArchiveSets(dir.path()).isEmpty());

    // par2 must never read as an archive, or cleanup would delete the recovery
    // set as "consumed" before a repair ever got the chance to use it.
    QVERIFY(!UsenetUnpacker::isArchiveVolume(QStringLiteral("Rel.vol000+01.par2")));
    QVERIFY(!UsenetUnpacker::isArchiveVolume(QStringLiteral("Movie.mkv")));
    QVERIFY(UsenetUnpacker::isArchiveVolume(QStringLiteral("Rel.part01.rar")));
}

void TestUsenetUnpack::unpacksAZipAndListsWhatItProduced()
{
    QTemporaryDir src;
    QTemporaryDir dst;
    QVERIFY(src.isValid() && dst.isValid());

    const QByteArray payload = QByteArray("the actual content", 18);
    QVERIFY(writeZip(QDir(src.path()).filePath(QStringLiteral("Rel.zip")),
                     {{QStringLiteral("Movie.mkv"), payload},
                      {QStringLiteral("sub/Movie.srt"), QByteArray("subs")}}));

    UsenetUnpacker unpacker;
    const auto r = unpacker.unpack(src.path(), dst.path());

    QVERIFY(r.ok);
    QVERIFY(!r.nothingToDo);
    QCOMPARE(r.extractedFiles.size(), 2);
    QCOMPARE(r.consumedArchives.size(), 1);
    QCOMPARE(readFile(QDir(dst.path()).filePath(QStringLiteral("Movie.mkv"))), payload);
    QVERIFY(QFileInfo::exists(QDir(dst.path()).filePath(QStringLiteral("sub/Movie.srt"))));

    // consumedArchives is what cleanup deletes, so it must name the volume and
    // never anything that came out of it.
    QCOMPARE(QFileInfo(r.consumedArchives.first()).fileName(), QStringLiteral("Rel.zip"));
}

void TestUsenetUnpack::reportsNothingToDoForAPlainDirectory()
{
    QTemporaryDir src;
    QTemporaryDir dst;
    QVERIFY(src.isValid() && dst.isValid());
    QVERIFY(touch(QDir(src.path()).filePath(QStringLiteral("Movie.mkv"))));

    UsenetUnpacker unpacker;
    const auto r = unpacker.unpack(src.path(), dst.path());

    // A release posted as bare files is ordinary, not an error: the caller
    // publishes what it already has.
    QVERIFY(r.ok);
    QVERIFY(r.nothingToDo);
    QVERIFY(r.extractedFiles.isEmpty());
}

void TestUsenetUnpack::refusesMembersThatEscapeTheDestination()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir rootDir(root.path());
    QVERIFY(rootDir.mkpath(QStringLiteral("src")));
    QVERIFY(rootDir.mkpath(QStringLiteral("dest")));

    const QString srcDir  = rootDir.filePath(QStringLiteral("src"));
    const QString destDir = rootDir.filePath(QStringLiteral("dest"));

    QVERIFY(writeZip(QDir(srcDir).filePath(QStringLiteral("Evil.zip")),
                     {{QStringLiteral("../escaped.txt"), QByteArray("pwned")},
                      {QStringLiteral("a/../../also-escaped.txt"), QByteArray("pwned")},
                      {QStringLiteral("/absolute.txt"), QByteArray("pwned")},
                      {QStringLiteral("legit.txt"), QByteArray("fine")}}));

    UsenetUnpacker unpacker;
    const auto r = unpacker.unpack(srcDir, destDir);

    // The good member still lands: one hostile name must not cost the release.
    QVERIFY(QFileInfo::exists(QDir(destDir).filePath(QStringLiteral("legit.txt"))));

    // And nothing at all outside the destination.
    QVERIFY(!QFileInfo::exists(rootDir.filePath(QStringLiteral("escaped.txt"))));
    QVERIFY(!QFileInfo::exists(rootDir.filePath(QStringLiteral("also-escaped.txt"))));
    QVERIFY(!QFileInfo::exists(QStringLiteral("/absolute.txt")));

    QVERIFY(ArchiveReader::safeEntryPath(destDir, QStringLiteral("../x")).isEmpty());
    QVERIFY(ArchiveReader::safeEntryPath(destDir, QStringLiteral("/etc/passwd")).isEmpty());
    QVERIFY(ArchiveReader::safeEntryPath(destDir, QStringLiteral("C:\\win.ini")).isEmpty());
    // Backslashes are separators in RAR, so a backslash traversal must be caught
    // by the same rule rather than passing through as a filename character.
    QVERIFY(ArchiveReader::safeEntryPath(destDir, QStringLiteral("..\\..\\x")).isEmpty());
    QVERIFY(!ArchiveReader::safeEntryPath(destDir, QStringLiteral("a/b/c.txt")).isEmpty());
}

void TestUsenetUnpack::rewritesWindowsReservedNamesRatherThanDroppingThem()
{
    const QString dest = QStringLiteral("/tmp/dest");

    // Rejecting these would silently lose a legitimate file; Windows would
    // otherwise write to a device instead of creating one.
    const QString aux = ArchiveReader::safeEntryPath(dest, QStringLiteral("aux.mkv"));
    QVERIFY(!aux.isEmpty());
    QCOMPARE(QFileInfo(aux).fileName(), QStringLiteral("aux_.mkv"));

    QCOMPARE(QFileInfo(ArchiveReader::safeEntryPath(dest, QStringLiteral("con"))).fileName(),
             QStringLiteral("con_"));

    // Trailing dots and spaces are stripped by Windows itself, which makes
    // "name." and "name" the same file there.
    QCOMPARE(QFileInfo(ArchiveReader::safeEntryPath(dest, QStringLiteral("file.txt. "))).fileName(),
             QStringLiteral("file.txt"));

    QCOMPARE(QFileInfo(ArchiveReader::safeEntryPath(dest, QStringLiteral("normal.mkv"))).fileName(),
             QStringLiteral("normal.mkv"));
}

// The volume list, not just volume one. libarchive reads a set as one stream
// over the files it is handed and never opens a sibling volume by name, so
// getting this wrong truncates every multi-volume release at volume one — the
// symptom being a "split file" error that looks exactly like a bad download.
void TestUsenetUnpack::unpacksAMultiVolumeRarSet()
{
    eMule::testing::TempDir tmpDir;
    QByteArray payload(30000, '\0');
    for (qsizetype i = 0; i < payload.size(); ++i)
        payload[i] = char('a' + (i % 26));

    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 7000);
    QCOMPARE(vols.size(), 5);
    for (int i = 0; i < vols.size(); ++i) {
        const QString name = i == 0 ? QStringLiteral("Rel.rar")
                                    : QStringLiteral("Rel.r%1").arg(i - 1, 2, 10, QLatin1Char('0'));
        QVERIFY(touch(tmpDir.filePath(name), vols.at(i)));
    }

    const QString dest = tmpDir.filePath(QStringLiteral("out"));
    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(tmpDir.path(), dest);

    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.nothingToDo);
    QCOMPARE(result.extractedFiles.size(), 1);
    QCOMPARE(result.consumedArchives.size(), 5);

    QFile out(QDir(dest).filePath(QStringLiteral("movie.mkv")));
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
}

void TestUsenetUnpack::skipsASetAlreadyUnpackedElsewhere()
{
    eMule::testing::TempDir tmpDir;
    QByteArray payload(9000, 'z');
    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 4000);
    for (int i = 0; i < vols.size(); ++i) {
        const QString name = i == 0 ? QStringLiteral("Rel.rar")
                                    : QStringLiteral("Rel.r%1").arg(i - 1, 2, 10, QLatin1Char('0'));
        QVERIFY(touch(tmpDir.filePath(name), vols.at(i)));
    }

    const auto sets = UsenetUnpacker::findArchiveSets(tmpDir.path());
    QCOMPARE(sets.size(), 1);

    const QString dest = tmpDir.filePath(QStringLiteral("out"));
    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(tmpDir.path(), dest, {},
                                        QSet<QString>{sets.first().firstVolume});

    // Found, so not "nothing to do" — but deliberately not extracted again.
    QVERIFY(result.ok);
    QVERIFY(!result.nothingToDo);
    QVERIFY(result.extractedFiles.isEmpty());
    QVERIFY(!QFile::exists(QDir(dest).filePath(QStringLiteral("movie.mkv"))));
}

QTEST_MAIN(TestUsenetUnpack)
#include "tst_UsenetUnpack.moc"
