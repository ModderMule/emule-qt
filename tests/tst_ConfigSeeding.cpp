/// @file tst_ConfigSeeding.cpp
/// @brief AppConfig bundled-data seeding: freshness, provenance and pruning.
///
/// The behaviour under test is a three-way compare between the bundled file, the
/// live file and the manifest of what the last pass wrote. Deliberately not
/// mtime: git does not preserve it across a checkout, and it cannot tell "the
/// bundle moved on" from "the user edited their copy".

#include "app/AppConfig.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

constexpr auto kManifest = "bundled.yml";

void writeFile(const QString& path, const QByteArray& body)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("cannot write %1").arg(path)));
    f.write(body);
}

[[nodiscard]] QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestConfigSeeding : public QObject {
    Q_OBJECT

private slots:
    void init();

    void seedsAFreshConfigDirAndSkipsDotFiles();
    void aSecondPassWithNothingChangedWritesNothing();
    void aChangedBundledAssetIsRefreshed();
    void aHandEditedAssetSurvivesAnUnchangedBundle();
    void aHandEditedAssetGetsANewSiblingNotAnOverwrite();
    void liveDataFilesAreSeededOnceAndNeverRefreshed();
    void aDeletedFileIsSeededAgain();
    void aPreManifestInstallIsAdoptedWithABackup();
    void aFileDroppedFromTheBundleIsPruned();
    void aTruncatedBundleDoesNotTriggerPruning();
    void aBundleThatIsTheConfigDirIsANoOp();
    void theShippedBundleLayoutsAllResolve();

private:
    QTemporaryDir m_bundleDir;
    QTemporaryDir m_configDir;

    [[nodiscard]] QString bundle(const QString& rel) const
    { return m_bundleDir.path() + QLatin1Char('/') + rel; }
    [[nodiscard]] QString live(const QString& rel) const
    { return m_configDir.path() + QLatin1Char('/') + rel; }

    /// A bundle shaped like the real one: a template, a webserver asset, and a
    /// data file the app owns.
    void makeBundle()
    {
        writeFile(bundle(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("template v1"));
        writeFile(bundle(QStringLiteral("webserver/sprites.css")), QByteArrayLiteral("css v1"));
        writeFile(bundle(QStringLiteral("server.met")), QByteArrayLiteral("servers v1"));
    }
};

void TestConfigSeeding::init()
{
    // Fresh pair per test: seeding is stateful through its manifest.
    QVERIFY(m_bundleDir.isValid());
    QVERIFY(m_configDir.isValid());
    QDir(m_bundleDir.path()).removeRecursively();
    QDir(m_configDir.path()).removeRecursively();
    QDir().mkpath(m_bundleDir.path());
    QDir().mkpath(m_configDir.path());
}

void TestConfigSeeding::seedsAFreshConfigDirAndSkipsDotFiles()
{
    makeBundle();
    writeFile(bundle(QStringLiteral(".DS_Store")), QByteArrayLiteral("finder litter"));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.seeded, 3);
    QCOMPARE(report.refreshed, 0);
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl"))), QByteArrayLiteral("template v1"));
    QCOMPARE(readFile(live(QStringLiteral("webserver/sprites.css"))), QByteArrayLiteral("css v1"));
    QVERIFY(QFile::exists(live(QLatin1StringView(kManifest))));

    // Build-machine litter must not be shipped into a user's config dir.
    QVERIFY(!QFile::exists(live(QStringLiteral(".DS_Store"))));
}

void TestConfigSeeding::aSecondPassWithNothingChangedWritesNothing()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());
    const QByteArray manifestAfterFirst = readFile(live(QLatin1StringView(kManifest)));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.seeded, 0);
    QCOMPARE(report.refreshed, 0);
    QCOMPARE(report.conflicts, 0);
    QCOMPARE(report.pruned, 0);
    QCOMPARE(readFile(live(QLatin1StringView(kManifest))), manifestAfterFirst);
}

void TestConfigSeeding::aChangedBundledAssetIsRefreshed()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    writeFile(bundle(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("template v2"));
    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.refreshed, 1);
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl"))), QByteArrayLiteral("template v2"));
    // Only the first, pre-manifest adoption keeps a .bak.
    QVERIFY(!QFile::exists(live(QStringLiteral("eMule.tmpl.bak"))));
}

void TestConfigSeeding::aHandEditedAssetSurvivesAnUnchangedBundle()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    writeFile(live(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("my own template"));
    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.preserved, 1);
    QCOMPARE(report.refreshed, 0);
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl"))), QByteArrayLiteral("my own template"));
    QVERIFY(!QFile::exists(live(QStringLiteral("eMule.tmpl.new"))));
}

void TestConfigSeeding::aHandEditedAssetGetsANewSiblingNotAnOverwrite()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    writeFile(live(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("my own template"));
    writeFile(bundle(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("template v2"));
    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.conflicts, 1);
    QCOMPARE(report.refreshed, 0);
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl"))), QByteArrayLiteral("my own template"));
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl.new"))), QByteArrayLiteral("template v2"));
}

void TestConfigSeeding::liveDataFilesAreSeededOnceAndNeverRefreshed()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    // The app rewrites server.met constantly; refreshing it would wipe the
    // user's server list.
    writeFile(live(QStringLiteral("server.met")), QByteArrayLiteral("the user's servers"));
    writeFile(bundle(QStringLiteral("server.met")), QByteArrayLiteral("servers v2"));
    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.refreshed, 0);
    QCOMPARE(report.conflicts, 0);
    QCOMPARE(readFile(live(QStringLiteral("server.met"))), QByteArrayLiteral("the user's servers"));
    QVERIFY(!QFile::exists(live(QStringLiteral("server.met.new"))));
}

void TestConfigSeeding::aDeletedFileIsSeededAgain()
{
    makeBundle();
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());
    QVERIFY(QFile::remove(live(QStringLiteral("webserver/sprites.css"))));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.seeded, 1);
    QCOMPARE(readFile(live(QStringLiteral("webserver/sprites.css"))), QByteArrayLiteral("css v1"));
}

void TestConfigSeeding::aPreManifestInstallIsAdoptedWithABackup()
{
    // An install that predates the manifest: stale content, no provenance. We
    // cannot prove it is untouched, so we keep a copy before adopting.
    makeBundle();
    writeFile(live(QStringLiteral("eMule.tmpl")), QByteArrayLiteral("ancient template"));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.refreshed, 1);
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl"))), QByteArrayLiteral("template v1"));
    QCOMPARE(readFile(live(QStringLiteral("eMule.tmpl.bak"))), QByteArrayLiteral("ancient template"));
}

void TestConfigSeeding::aFileDroppedFromTheBundleIsPruned()
{
    makeBundle();
    writeFile(bundle(QStringLiteral("webserver/old.gif")), QByteArrayLiteral("gif"));
    writeFile(bundle(QStringLiteral("webserver/edited.gif")), QByteArrayLiteral("gif2"));
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    // One drops out untouched, one the user changed.
    QVERIFY(QFile::remove(bundle(QStringLiteral("webserver/old.gif"))));
    QVERIFY(QFile::remove(bundle(QStringLiteral("webserver/edited.gif"))));
    writeFile(live(QStringLiteral("webserver/edited.gif")), QByteArrayLiteral("mine"));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.pruned, 1);
    QVERIFY(!QFile::exists(live(QStringLiteral("webserver/old.gif"))));
    QCOMPARE(readFile(live(QStringLiteral("webserver/edited.gif"))), QByteArrayLiteral("mine"));
}

void TestConfigSeeding::aTruncatedBundleDoesNotTriggerPruning()
{
    // A packaging accident must not become "delete the user's working assets".
    makeBundle();
    for (int i = 0; i < 8; ++i)
        writeFile(bundle(QStringLiteral("webserver/a%1.gif").arg(i)), QByteArrayLiteral("gif"));
    AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    for (int i = 0; i < 8; ++i)
        QVERIFY(QFile::remove(bundle(QStringLiteral("webserver/a%1.gif").arg(i))));
    QVERIFY(QFile::remove(bundle(QStringLiteral("webserver/sprites.css"))));

    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_configDir.path());

    QCOMPARE(report.pruned, 0);
    QVERIFY(QFile::exists(live(QStringLiteral("webserver/a0.gif"))));
    QVERIFY(QFile::exists(live(QStringLiteral("webserver/sprites.css"))));
}

void TestConfigSeeding::aBundleThatIsTheConfigDirIsANoOp()
{
    // The Windows portable shape: the shipped config/ IS the live config dir.
    makeBundle();
    const auto report = AppConfig::seedFrom(m_bundleDir.path(), m_bundleDir.path());

    QCOMPARE(report.seeded, 0);
    QCOMPARE(report.refreshed, 0);
    QVERIFY(!QFile::exists(bundle(QLatin1StringView(kManifest))));
}

void TestConfigSeeding::theShippedBundleLayoutsAllResolve()
{
    // Pure path arithmetic, so the Linux and Windows shapes can be asserted
    // from any host. These are the exact directories the bundlers create.
    const QStringList mac =
        AppConfig::bundleCandidates(QStringLiteral("/A/emuleqt.app/Contents/MacOS"));
    QVERIFY(mac.contains(QStringLiteral("/A/emuleqt.app/Contents/MacOS/../Resources/config")));

    // bundle-linux.sh stages the binaries and config/ side by side at the
    // tarball root; bundle-win.ps1 does the same inside the zip.
    for (const QString& stageRoot : {QStringLiteral("/opt/eMuleQt"), QStringLiteral("C:/eMule")}) {
        const QStringList c = AppConfig::bundleCandidates(stageRoot);
        QVERIFY2(c.contains(stageRoot + QStringLiteral("/config")), qPrintable(stageRoot));
    }

    // The source tree is a fallback, never a winner over a real bundle.
    const QStringList any = AppConfig::bundleCandidates(QStringLiteral("/A"));
    if (any.size() > 2)
        QCOMPARE(any.indexOf(QStringLiteral("/A/config")) < any.size() - 1, true);
}

QTEST_MAIN(TestConfigSeeding)
#include "tst_ConfigSeeding.moc"
