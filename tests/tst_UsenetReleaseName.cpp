/// @file tst_UsenetReleaseName.cpp
/// @brief Episode keys for the duplicate check, and the obfuscated-name rename.

#include "post/UsenetDeobfuscate.h"
#include "queue/UsenetReleaseName.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::usenet;

class tst_UsenetReleaseName : public QObject {
    Q_OBJECT

private slots:
    void seasonEpisodeForms();
    void releasesOfOneEpisodeShareTheKey();
    void datedEpisodes();
    void notAnEpisode();
    void fixTagsOnlyCountAfterTheEpisode();

    void obfuscatedNames();
    void theClearlyBiggestFileTakesTheJobName();
    void noRenameWithoutAClearWinner();
    void noRenameForRealNamesOrAnObfuscatedJobName();
};

void tst_UsenetReleaseName::seasonEpisodeForms()
{
    const auto a = parseReleaseEpisode(QStringLiteral("Show.Name.S01E02.720p.WEB.x264-GRP"));
    QVERIFY(a);
    QCOMPARE(a->title, QStringLiteral("show name"));
    QCOMPARE(a->season, 1);
    QCOMPARE(a->episode, 2);
    QCOMPARE(a->label(), QStringLiteral("S01E02"));

    const auto multi = parseReleaseEpisode(QStringLiteral("Show Name - S01E02E03 - Title"));
    QVERIFY(multi);
    QCOMPARE(multi->episode, 2);

    const auto cross = parseReleaseEpisode(QStringLiteral("Show_Name_1x02_HDTV"));
    QVERIFY(cross);
    QCOMPARE(cross->season, 1);
    QCOMPARE(cross->episode, 2);
}

void tst_UsenetReleaseName::releasesOfOneEpisodeShareTheKey()
{
    const auto a = parseReleaseEpisode(QStringLiteral("Show.Name.S01E02.720p.WEB-A"));
    const auto b = parseReleaseEpisode(QStringLiteral("Show Name S01E02 1080p HDTV-B"));
    const auto c = parseReleaseEpisode(QStringLiteral("Show.Name.S01E03.720p.WEB-A"));
    QVERIFY(a && b && c);
    QCOMPARE(a->key(), b->key());
    QVERIFY(a->key() != c->key());
}

void tst_UsenetReleaseName::datedEpisodes()
{
    const auto ymd = parseReleaseEpisode(QStringLiteral("Daily.Show.2024.03.05.720p-GRP"));
    QVERIFY(ymd);
    QCOMPARE(ymd->airDate, QDate(2024, 3, 5));
    const auto dmy = parseReleaseEpisode(QStringLiteral("Daily Show 05.03.2024 HDTV"));
    QVERIFY(dmy);
    QCOMPARE(dmy->key(), ymd->key());
}

void tst_UsenetReleaseName::notAnEpisode()
{
    QVERIFY(!parseReleaseEpisode(QStringLiteral("Some.Movie.2019.1080p.BluRay.x264-GRP")));
    QVERIFY(!parseReleaseEpisode(QStringLiteral("Show.Name.S01.COMPLETE.720p")));   // season pack
    QVERIFY(!parseReleaseEpisode(QStringLiteral("Video.1920x1080.mkv")));
    QVERIFY(!parseReleaseEpisode(QStringLiteral("S01E02.720p")));                    // no title
}

void tst_UsenetReleaseName::fixTagsOnlyCountAfterTheEpisode()
{
    QVERIFY(parseReleaseEpisode(QStringLiteral("Show.S01E02.PROPER.720p"))->proper);
    QVERIFY(parseReleaseEpisode(QStringLiteral("Show.S01E02.720p.REPACK-GRP"))->proper);
    QVERIFY(!parseReleaseEpisode(QStringLiteral("Real.Time.S01E02.720p"))->proper);
    QVERIFY(!parseReleaseEpisode(QStringLiteral("Show.S01E02.Realm.720p"))->proper);
}

void tst_UsenetReleaseName::obfuscatedNames()
{
    QVERIFY(looksObfuscatedName(QStringLiteral("0a1b2c3d4e5f60718293a4b5c6d7e8f9")));
    QVERIFY(looksObfuscatedName(QStringLiteral("abc.xyz.a4c567edbcbf27.BLAH")));
    QVERIFY(looksObfuscatedName(QStringLiteral("e0bf8e2f5d1a4e3c")));
    QVERIFY(looksObfuscatedName(QStringLiteral("KQZXWVPLMN")));

    QVERIFY(!looksObfuscatedName(QStringLiteral("Show.Name.S01E02.720p")));
    QVERIFY(!looksObfuscatedName(QStringLiteral("My Holiday Video")));
    QVERIFY(!looksObfuscatedName(QStringLiteral("Great_File_1969")));
    QVERIFY(!looksObfuscatedName(QStringLiteral("Holiday")));
}

namespace {

QString write(const QTemporaryDir& tmp, const QString& name, qint64 size)
{
    const QString path = QDir(tmp.path()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray(size, 'x'));
    return path;
}

} // namespace

void tst_UsenetReleaseName::theClearlyBiggestFileTakesTheJobName()
{
    QTemporaryDir tmp;
    const QString big = write(tmp, QStringLiteral("a3f9e1c2b4d5e6f7.mkv"), 40000);
    const QString subs = write(tmp, QStringLiteral("a3f9e1c2b4d5e6f7.eng.srt"), 100);
    const QString sample = write(tmp, QStringLiteral("a3f9e1c2b4d5e6f7-sample.mkv"), 2000);
    const QString other = write(tmp, QStringLiteral("readme.txt"), 50);

    const auto renames = deobfuscatedNames({big, subs, sample, other},
                                           QStringLiteral("Show.Name.S01E02.720p-GRP"));
    QCOMPARE(renames.value(big), QStringLiteral("Show.Name.S01E02.720p-GRP.mkv"));
    QCOMPARE(renames.value(subs), QStringLiteral("Show.Name.S01E02.720p-GRP.eng.srt"));
    QCOMPARE(renames.value(sample), QStringLiteral("Show.Name.S01E02.720p-GRP-sample.mkv"));
    QVERIFY(!renames.contains(other));

    // a job name that already carries the extension
    QCOMPARE(deobfuscatedNames({big}, QStringLiteral("Movie.mkv")).value(big),
             QStringLiteral("Movie.mkv"));
}

void tst_UsenetReleaseName::noRenameWithoutAClearWinner()
{
    QTemporaryDir tmp;
    const QString a = write(tmp, QStringLiteral("a3f9e1c2b4d5e6f7.bin"), 10000);
    const QString b = write(tmp, QStringLiteral("b4c5d6e7f8091a2b.bin"), 12000);
    QVERIFY(deobfuscatedNames({a, b}, QStringLiteral("Some Release")).isEmpty());

    // an archive volume keeps its name: cleanup and the set depend on it
    const QString rar = write(tmp, QStringLiteral("e0bf8e2f5d1a4e3c.part01.rar"), 90000);
    QVERIFY(deobfuscatedNames({rar}, QStringLiteral("Some Release")).isEmpty());
}

void tst_UsenetReleaseName::noRenameForRealNamesOrAnObfuscatedJobName()
{
    QTemporaryDir tmp;
    const QString real = write(tmp, QStringLiteral("Show.Name.S01E02.720p.mkv"), 40000);
    QVERIFY(deobfuscatedNames({real}, QStringLiteral("Other Name")).isEmpty());

    const QString noise = write(tmp, QStringLiteral("a3f9e1c2b4d5e6f7.mkv"), 40000);
    QVERIFY(deobfuscatedNames({noise}, QStringLiteral("0a1b2c3d4e5f60718293a4b5c6d7e8f9")).isEmpty());
}

QTEST_GUILESS_MAIN(tst_UsenetReleaseName)
#include "tst_UsenetReleaseName.moc"
