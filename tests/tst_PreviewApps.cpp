/// @file tst_PreviewApps.cpp
/// @brief Tests for media/PreviewApps — config parser + preview-ability checker.

#include "TestHelpers.h"
#include "media/PreviewApps.h"
#include "files/PartFile.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTextStream>

using namespace eMule;
using namespace Qt::StringLiterals;

// ---------------------------------------------------------------------------
// Helper: write a config file into the temp directory
// ---------------------------------------------------------------------------

static QString writeConfigFile(const eMule::testing::TempDir& dir,
                               const QString& content,
                               const QString& name = u"PreviewApps.dat"_s)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    f.write(content.toUtf8());
    f.close();
    return path;
}

// ---------------------------------------------------------------------------
// Helper: create a PartFile with given name, size, and filled range
// ---------------------------------------------------------------------------

static std::unique_ptr<PartFile> makePartFile(const QString& name,
                                               uint64 size,
                                               uint64 filledEnd = 0)
{
    auto pf = std::make_unique<PartFile>();
    pf->setFileName(name);
    pf->setFileSize(size);

    // PartFile starts with a gap spanning the whole file.
    // Fill from start to filledEnd (inclusive).
    if (filledEnd > 0 && filledEnd < size) {
        pf->fillGap(0, filledEnd);
        pf->updateCompletedInfos();
    } else if (filledEnd >= size && size > 0) {
        pf->fillGap(0, size - 1);
        pf->updateCompletedInfos();
    }

    return pf;
}

// ===========================================================================
// Test class
// ===========================================================================

class tst_PreviewApps : public QObject {
    Q_OBJECT

private slots:
    // parseLine tests
    void parseLine_validEntry();
    void parseLine_commentLine();
    void parseLine_emptyLine();
    void parseLine_tooFewFields();
    void parseLine_hexParsing();
    void parseLine_multipleExtensions();
    void parseLine_noArgs();

    // loadFromFile tests
    void loadFromFile_validFile();
    void loadFromFile_nonexistent();
    void loadFromFile_emptyFile();
    void loadFromFile_mixedValidInvalid();

    // canPreview tests
    void canPreview_matchingExtension();
    void canPreview_noMatchingExtension();
    void canPreview_insufficientStart();
    void canPreview_sufficientCompletion();
    void canPreview_nullFile();

    // menuEntries tests
    void menuEntries_multipleMatches();
    void menuEntries_noMatches();
};

// ===========================================================================
// parseLine tests
// ===========================================================================

void tst_PreviewApps::parseLine_validEntry()
{
    const QString line = u"VLC\tavi,mkv,mp4\t10000\t50000\t/usr/bin/vlc\t--play-and-exit %1"_s;
    auto result = PreviewApps::parseLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->title, u"VLC"_s);
    QCOMPARE(result->extensions.size(), 3);
    QCOMPARE(result->extensions[0], u"avi"_s);
    QCOMPARE(result->extensions[1], u"mkv"_s);
    QCOMPARE(result->extensions[2], u"mp4"_s);
    QCOMPARE(result->minStartOfFile, 0x10000ULL);
    QCOMPARE(result->minCompletedSize, 0x50000ULL);
    QCOMPARE(result->command, u"/usr/bin/vlc"_s);
    QCOMPARE(result->commandArgs, u"--play-and-exit %1"_s);
}

void tst_PreviewApps::parseLine_commentLine()
{
    QVERIFY(!PreviewApps::parseLine(u"# This is a comment"_s).has_value());
    QVERIFY(!PreviewApps::parseLine(u"  # Indented comment"_s).has_value());
}

void tst_PreviewApps::parseLine_emptyLine()
{
    QVERIFY(!PreviewApps::parseLine(u""_s).has_value());
    QVERIFY(!PreviewApps::parseLine(u"   "_s).has_value());
}

void tst_PreviewApps::parseLine_tooFewFields()
{
    QVERIFY(!PreviewApps::parseLine(u"VLC\tavi"_s).has_value());
    QVERIFY(!PreviewApps::parseLine(u"VLC\tavi\t1000"_s).has_value());
    QVERIFY(!PreviewApps::parseLine(u"VLC\tavi\t1000\t2000"_s).has_value());
}

void tst_PreviewApps::parseLine_hexParsing()
{
    const QString line = u"App\text\tFF\tFFFF\t/bin/app\t%1"_s;
    auto result = PreviewApps::parseLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->minStartOfFile, 0xFFULL);
    QCOMPARE(result->minCompletedSize, 0xFFFFULL);
}

void tst_PreviewApps::parseLine_multipleExtensions()
{
    const QString line = u"Player\twmv, avi , mpg\t0\t0\t/bin/player\t%1"_s;
    auto result = PreviewApps::parseLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->extensions.size(), 3);
    QCOMPARE(result->extensions[0], u"wmv"_s);
    QCOMPARE(result->extensions[1], u"avi"_s);
    QCOMPARE(result->extensions[2], u"mpg"_s);
}

void tst_PreviewApps::parseLine_noArgs()
{
    const QString line = u"Simple\tavi\t0\t0\t/bin/simple"_s;
    auto result = PreviewApps::parseLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->command, u"/bin/simple"_s);
    QVERIFY(result->commandArgs.isEmpty());
}

// ===========================================================================
// loadFromFile tests
// ===========================================================================

void tst_PreviewApps::loadFromFile_validFile()
{
    eMule::testing::TempDir tmpDir;

    const QString config =
        u"# Preview apps config\n"
        u"VLC\tavi,mkv\t10000\t50000\t/usr/bin/vlc\t--play-and-exit %1\n"
        u"MPlayer\tmpg,mpeg\t0\t0\t/usr/bin/mplayer\t%1\n"_s;

    const QString path = writeConfigFile(tmpDir, config);
    QVERIFY(!path.isEmpty());

    PreviewApps apps;
    QCOMPARE(apps.loadFromFile(path), 2);
    QCOMPARE(apps.count(), 2);
    QCOMPARE(apps.apps()[0].title, u"VLC"_s);
    QCOMPARE(apps.apps()[1].title, u"MPlayer"_s);
}

void tst_PreviewApps::loadFromFile_nonexistent()
{
    PreviewApps apps;
    QCOMPARE(apps.loadFromFile(u"/nonexistent/path/PreviewApps.dat"_s), 0);
    QCOMPARE(apps.count(), 0);
}

void tst_PreviewApps::loadFromFile_emptyFile()
{
    eMule::testing::TempDir tmpDir;
    const QString path = writeConfigFile(tmpDir, u""_s);

    PreviewApps apps;
    QCOMPARE(apps.loadFromFile(path), 0);
    QCOMPARE(apps.count(), 0);
}

void tst_PreviewApps::loadFromFile_mixedValidInvalid()
{
    eMule::testing::TempDir tmpDir;

    const QString config =
        u"# comment\n"
        u"ValidApp\tavi\t0\t0\t/bin/app\t%1\n"
        u"bad line no tabs\n"
        u"\n"
        u"AnotherApp\tmkv\tFF\tFF\t/bin/other\t%1\n"_s;

    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    QCOMPARE(apps.loadFromFile(path), 2);
    QCOMPARE(apps.apps()[0].title, u"ValidApp"_s);
    QCOMPARE(apps.apps()[1].title, u"AnotherApp"_s);
}

// ===========================================================================
// canPreview tests
// ===========================================================================

void tst_PreviewApps::canPreview_matchingExtension()
{
    eMule::testing::TempDir tmpDir;
    const QString config = u"VLC\tavi,mkv\t0\t0\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    // 1 MB file, fully downloaded
    auto pf = makePartFile(u"movie.avi"_s, 1'000'000, 999'999);
    QVERIFY(apps.canPreview(pf.get()));
}

void tst_PreviewApps::canPreview_noMatchingExtension()
{
    eMule::testing::TempDir tmpDir;
    const QString config = u"VLC\tavi,mkv\t0\t0\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    auto pf = makePartFile(u"document.pdf"_s, 1'000'000, 999'999);
    QVERIFY(!apps.canPreview(pf.get()));
}

void tst_PreviewApps::canPreview_insufficientStart()
{
    eMule::testing::TempDir tmpDir;
    // Requires 0x10000 = 65536 bytes from start
    const QString config = u"VLC\tavi\t10000\t0\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    // File is 1 MB, but only 100 bytes completed from start
    auto pf = makePartFile(u"movie.avi"_s, 1'000'000, 99);
    QVERIFY(!apps.canPreview(pf.get()));
}

void tst_PreviewApps::canPreview_sufficientCompletion()
{
    eMule::testing::TempDir tmpDir;
    // Requires 0x100 = 256 bytes completed, 0x100 start bytes
    const QString config = u"VLC\tavi\t100\t100\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    // 10 KB file with 512 bytes completed from start — exceeds both requirements (0x100 = 256)
    auto pf = makePartFile(u"movie.avi"_s, 10'000, 511);
    QVERIFY(apps.canPreview(pf.get()));
}

void tst_PreviewApps::canPreview_nullFile()
{
    eMule::testing::TempDir tmpDir;
    const QString config = u"VLC\tavi\t0\t0\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    QVERIFY(!apps.canPreview(nullptr));
}

// ===========================================================================
// menuEntries tests
// ===========================================================================

void tst_PreviewApps::menuEntries_multipleMatches()
{
    eMule::testing::TempDir tmpDir;
    const QString config =
        u"VLC\tavi,mkv\t0\t0\t/usr/bin/vlc\t%1\n"
        u"MPlayer\tavi\t0\t0\t/usr/bin/mplayer\t%1\n"
        u"MPV\tmpg\t0\t0\t/usr/bin/mpv\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    auto pf = makePartFile(u"movie.avi"_s, 1'000'000, 999'999);
    auto entries = apps.menuEntries(pf.get());

    // VLC and MPlayer match avi; MPV does not
    QCOMPARE(entries.size(), 2u);
    QCOMPARE(entries[0].first, u"VLC"_s);
    QCOMPARE(entries[0].second, 0);
    QCOMPARE(entries[1].first, u"MPlayer"_s);
    QCOMPARE(entries[1].second, 1);
}

void tst_PreviewApps::menuEntries_noMatches()
{
    eMule::testing::TempDir tmpDir;
    const QString config = u"VLC\tavi\t0\t0\t/usr/bin/vlc\t%1\n"_s;
    const QString path = writeConfigFile(tmpDir, config);

    PreviewApps apps;
    std::ignore = apps.loadFromFile(path);

    auto pf = makePartFile(u"music.mp3"_s, 1'000'000, 999'999);
    auto entries = apps.menuEntries(pf.get());

    QVERIFY(entries.empty());
}

QTEST_MAIN(tst_PreviewApps)
#include "tst_PreviewApps.moc"
