/// @file tst_VersionChecker.cpp
/// @brief Parsing emuleqt-version.json.
///
/// The version check decides whether to interrupt the user with a dialog, so a
/// mis-parse is either a popup that should not exist or a release nobody hears about.
/// Everything checked here is pure — VersionChecker::parse/isNewer/platformKey touch
/// no network, no preferences and no widgets — so the whole decision is testable
/// without a reply to wait for.
///
/// kRealManifest is the live document from https://emule-qt.org/pub/emuleqt-version.json
/// verbatim (as written by scripts/update-manifest.sh), so the fixture also asserts
/// that this build's platform key is one the server actually publishes.

#include "app/VersionChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using eMule::VersionChecker;

class TestVersionChecker : public QObject {
    Q_OBJECT

private slots:
    void realManifestParsesCompletely();
    void realManifestOffersThisPlatformsDownload();
    void stubManifestOffersNoDownload();
    void platformKeyIsOneTheServerPublishes();

    void emptyBodyIsAnError();
    void nonJsonIsAnError();
    void jsonArrayIsAnError();
    void missingLatestIsAnError();

    void usableDownloadEntryIsPicked();
    void zeroSizeDownloadIsSkipped();
    void nonHttpDownloadSchemeIsSkipped();
    void missingPlatformEntryIsSkipped();

    void malformedDateDoesNotFailTheParse();
    void isNewerComparesNumerically();
    void manifestUrlHonoursTheEnvironmentOverride();
};

// The fixtures sit BELOW the class on purpose: CMake's AUTOMOC scanner does not
// understand raw string literals, and a JSON blob full of quotes ahead of the class
// leaves it convinced Q_OBJECT is inside a string — it then writes an empty .moc and
// the link fails on a missing vtable.
namespace {

/// The manifest as served on 2026-10-31.
constexpr auto kRealManifest = R"({
  "latest": "0.7.1",
  "date": "2026-10-31T03:10:47Z",
  "releaseNotes": "https://github.com/ModderMule/emule-qt/releases/tag/v0.7.1",
  "minVersion": "0.0.1",
  "downloads": {
    "windows-x64": {
      "url": "https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/eMuleQt-v0.7.1-win64.zip",
      "sha256": "8e601e46250d7fdd176457999b8952a09c25c1eb0520838be5bc0067c365b98e",
      "size": 111412704
    },
    "windows-x64-installer": {
      "url": "https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/eMuleQt-v0.7.1-win64-setup.exe",
      "sha256": "acf273b531a6d01a2efb3642bcef3faeccf6c055e4fdb50e3e3a8e4e77691ef2",
      "size": 96936458
    },
    "mac-arm64": {
      "url": "https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/eMuleQt-v0.7.1-macOS-arm64.dmg",
      "sha256": "0fcda240ea3bbc32b995921565630f9cdfcf3137c9fc28051827d12692151d46",
      "size": 123841467
    },
    "mac-x64": {
      "url": "https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/eMuleQt-v0.7.1-macOS-x86_64.dmg",
      "sha256": "9b436ac7394d934291265d493d1f2a770904e9fc151ca7c0fa82d17997fb5398",
      "size": 127311160
    },
    "linux-x64": {
      "url": "https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/eMuleQt-v0.7.1-linux-x86_64.tar.gz",
      "sha256": "74760979906f657169a30134b3ac74030028a48febca179a426dc163c78c9f23",
      "size": 480094905
    }
  },
  "specialdlp": {
    "libVersion": "1.0.0",
    "rulesVersion": 44006,
    "abi": 1,
    "downloads": {}
  },
  "hosterResolver": {
    "url": "https://emule-qt.org/pub/resolver.zip",
    "sha256": "94af025652b87917dc2edc9278abf704aab4ce99ba289b3cfdd3234b4e255800",
    "size": 39220986,
    "jdRevision": 53779
  },
  "geoip": {
    "database": "GeoLite2-Country",
    "buildDate": "2026-10-27T17:52:02Z",
    "sha256": "54e327d9fe9214e568c5b8f8ce97a7f7e7087b7de10b0560640417695d153797",
    "size": 3395840
  }
})";

/// The manifest as served on 2026-08-06: every download a not-ready:// stub.
constexpr auto kStubManifest = R"({
  "latest": "0.2.0",
  "date": "2026-07-30T13:37:02Z",
  "releaseNotes": "https://github.com/ModderMule/emule-qt/releases/tag/v0.2.0",
  "minVersion": "0.0.1",
  "downloads": {
    "windows-x64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-win-x64.exe",
      "sha256": "",
      "size": 0
    },
    "windows-arm64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-win-arm64.exe",
      "sha256": "",
      "size": 0
    },
    "mac-x64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-mac-x64.dmg",
      "sha256": "",
      "size": 0
    },
    "mac-arm64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-mac-arm64.dmg",
      "sha256": "",
      "size": 0
    },
    "linux-x64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-linux-x64.AppImage",
      "sha256": "",
      "size": 0
    },
    "linux-arm64": {
      "url": "not-ready://emule-qt.org/releases/1.2.0/emuleqt-1.2.0-linux-arm64.AppImage",
      "sha256": "",
      "size": 0
    }
  }
})";

/// Platforms release.yml builds; windows-arm64 / linux-arm64 have a key but no build.
bool isBuiltPlatform(const QString& key)
{
    return !key.isEmpty() && key != QLatin1StringView("windows-arm64")
           && key != QLatin1StringView("linux-arm64");
}

/// A manifest advertising exactly one download, for this build's platform.
QByteArray manifestWithDownload(const QString& url, qint64 size)
{
    return QStringLiteral(R"({"latest":"9.9.9","downloads":{"%1":{"url":"%2","size":%3}}})")
        .arg(VersionChecker::platformKey(), url)
        .arg(size)
        .toUtf8();
}

} // namespace

void TestVersionChecker::realManifestParsesCompletely()
{
    const auto info = VersionChecker::parse(QByteArray(kRealManifest));

    QVERIFY2(info.isValid(), qPrintable(info.error));
    QCOMPARE(info.latest, QStringLiteral("0.7.1"));
    QCOMPARE(info.date, QDate(2026, 10, 31));
    QCOMPARE(info.minVersion, QStringLiteral("0.0.1"));
    QVERIFY(info.releaseNotes.contains(QStringLiteral("releases/tag/v0.7.1")));
}

void TestVersionChecker::realManifestOffersThisPlatformsDownload()
{
    if (!isBuiltPlatform(VersionChecker::platformKey()))
        QSKIP("no release build for this OS/CPU combination");

    // The dialog still only opens the website; the URL is parsed for later use.
    const auto info = VersionChecker::parse(QByteArray(kRealManifest));
    QVERIFY(info.isValid());
    QVERIFY2(info.downloadUrl.startsWith(
                 QStringLiteral("https://github.com/ModderMule/emule-qt/releases/download/v0.7.1/")),
             qPrintable(info.downloadUrl));
}

void TestVersionChecker::stubManifestOffersNoDownload()
{
    // not-ready:// stubs with size 0 are not an error -- just no download
    const auto info = VersionChecker::parse(QByteArray(kStubManifest));
    QVERIFY(info.isValid());
    QVERIFY2(info.downloadUrl.isEmpty(), qPrintable(info.downloadUrl));
}

void TestVersionChecker::platformKeyIsOneTheServerPublishes()
{
    const QString key = VersionChecker::platformKey();
    if (!isBuiltPlatform(key))
        QSKIP("no release build for this OS/CPU combination");

    // Catches a mapping that drifts away from what the server actually ships: a key
    // of "macos-aarch64" would silently mean "never offer a download" forever.
    const QJsonObject downloads = QJsonDocument::fromJson(QByteArray(kRealManifest))
                                      .object()
                                      .value(QStringLiteral("downloads"))
                                      .toObject();
    QVERIFY2(downloads.contains(key), qPrintable(key));
}

void TestVersionChecker::emptyBodyIsAnError()
{
    const auto info = VersionChecker::parse({});
    QVERIFY(!info.isValid());
    QVERIFY(!info.error.isEmpty());
    QVERIFY(info.latest.isEmpty());
}

void TestVersionChecker::nonJsonIsAnError()
{
    // What a captive portal or a 404 page actually returns.
    const auto info = VersionChecker::parse("<html><body>404</body></html>");
    QVERIFY(!info.isValid());
    QVERIFY(info.latest.isEmpty());
}

void TestVersionChecker::jsonArrayIsAnError()
{
    const auto info = VersionChecker::parse(R"([{"latest":"9.9.9"}])");
    QVERIFY(!info.isValid());
    QVERIFY(info.latest.isEmpty());
}

void TestVersionChecker::missingLatestIsAnError()
{
    // Valid JSON, no version — must not be read as "you are up to date".
    const auto info = VersionChecker::parse(R"({"date":"2026-07-30T13:37:02Z"})");
    QVERIFY(!info.isValid());
    QVERIFY(info.latest.isEmpty());
}

void TestVersionChecker::usableDownloadEntryIsPicked()
{
    if (VersionChecker::platformKey().isEmpty())
        QSKIP("no manifest entry defined for this OS/CPU combination");

    const QString url = QStringLiteral("https://emule-qt.org/releases/9.9.9/emuleqt.bin");
    const auto info = VersionChecker::parse(manifestWithDownload(url, 12345));

    QVERIFY2(info.isValid(), qPrintable(info.error));
    QCOMPARE(info.downloadUrl, url);
}

void TestVersionChecker::zeroSizeDownloadIsSkipped()
{
    if (VersionChecker::platformKey().isEmpty())
        QSKIP("no manifest entry defined for this OS/CPU combination");

    const auto info = VersionChecker::parse(
        manifestWithDownload(QStringLiteral("https://emule-qt.org/releases/9.9.9/emuleqt.bin"), 0));

    QVERIFY(info.isValid());          // still a usable manifest
    QVERIFY(info.downloadUrl.isEmpty());
}

void TestVersionChecker::nonHttpDownloadSchemeIsSkipped()
{
    if (VersionChecker::platformKey().isEmpty())
        QSKIP("no manifest entry defined for this OS/CPU combination");

    for (const auto& url : {QStringLiteral("not-ready://emule-qt.org/x.dmg"),
                            QStringLiteral("file:///etc/passwd"),
                            QStringLiteral("ftp://emule-qt.org/x.dmg")}) {
        const auto info = VersionChecker::parse(manifestWithDownload(url, 999));
        QVERIFY(info.isValid());
        QVERIFY2(info.downloadUrl.isEmpty(), qPrintable(url));
    }
}

void TestVersionChecker::missingPlatformEntryIsSkipped()
{
    const auto info = VersionChecker::parse(
        R"({"latest":"9.9.9","downloads":{"solaris-sparc":{"url":"https://x/y","size":9}}})");

    QVERIFY(info.isValid());
    QVERIFY(info.downloadUrl.isEmpty());
}

void TestVersionChecker::malformedDateDoesNotFailTheParse()
{
    const auto info = VersionChecker::parse(R"({"latest":"9.9.9","date":"soon"})");

    QVERIFY2(info.isValid(), qPrintable(info.error));
    QCOMPARE(info.latest, QStringLiteral("9.9.9"));
    QVERIFY(!info.date.isValid());   // the dialog drops the "released on" clause
}

void TestVersionChecker::isNewerComparesNumerically()
{
    // The case a lexicographic compare gets wrong.
    QVERIFY(VersionChecker::isNewer(QStringLiteral("0.10.0"), QStringLiteral("0.9.0")));
    QVERIFY(!VersionChecker::isNewer(QStringLiteral("0.9.0"), QStringLiteral("0.10.0")));

    QVERIFY(VersionChecker::isNewer(QStringLiteral("0.3"), QStringLiteral("0.2.0")));
    QVERIFY(!VersionChecker::isNewer(QStringLiteral("0.2.0"), QStringLiteral("0.2.0")));

    // Garbage on either side must never be read as "a new version exists".
    QVERIFY(!VersionChecker::isNewer(QStringLiteral("banana"), QStringLiteral("0.2.0")));
    QVERIFY(!VersionChecker::isNewer(QStringLiteral("9.9.9"), QString{}));
}

void TestVersionChecker::manifestUrlHonoursTheEnvironmentOverride()
{
    const QByteArray saved = qgetenv("EMULEQT_VERSION_MANIFEST_URL");
    auto restore = qScopeGuard([&saved] {
        if (saved.isEmpty())
            qunsetenv("EMULEQT_VERSION_MANIFEST_URL");
        else
            qputenv("EMULEQT_VERSION_MANIFEST_URL", saved);
    });

    qunsetenv("EMULEQT_VERSION_MANIFEST_URL");
    QCOMPARE(VersionChecker::manifestUrl(),
             QStringLiteral("https://emule-qt.org/pub/emuleqt-version.json"));

    qputenv("EMULEQT_VERSION_MANIFEST_URL", "file:///tmp/fake-version.json");
    QCOMPARE(VersionChecker::manifestUrl(), QStringLiteral("file:///tmp/fake-version.json"));
}

QTEST_GUILESS_MAIN(TestVersionChecker)
#include "tst_VersionChecker.moc"
