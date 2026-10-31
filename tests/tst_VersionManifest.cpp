/// @file tst_VersionManifest.cpp
/// @brief core/app/VersionManifest: the download entries the auto-updater installs from.

#include "app/VersionManifest.h"

#include <QTest>

using namespace eMule;

namespace {

// Shape written by scripts/update-manifest.sh
// no raw string: it hides the Q_OBJECT below from AUTOMOC
constexpr const char* kManifest =
    "{\n"
    "  \"latest\": \"0.8.0\",\n"
    "  \"date\": \"2026-11-02T10:00:00Z\",\n"
    "  \"releaseNotes\": \"https://github.com/ModderMule/emule-qt/releases/tag/v0.8.0\",\n"
    "  \"minVersion\": \"0.0.1\",\n"
    "  \"downloads\": {\n"
    "    \"windows-x64\": {\"url\": \"https://github.com/ModderMule/emule-qt/releases/download/v0.8.0/eMuleQt-v0.8.0-win64.zip\",\n"
    "                    \"sha256\": \"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\", \"size\": 100},\n"
    "    \"windows-x64-installer\": {\"url\": \"https://github.com/ModderMule/emule-qt/releases/download/v0.8.0/eMuleQt-v0.8.0-win64-setup.exe\",\n"
    "                    \"sha256\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\", \"size\": 200},\n"
    "    \"mac-arm64\":   {\"url\": \"https://github.com/ModderMule/emule-qt/releases/download/v0.8.0/eMuleQt-v0.8.0-macOS-arm64.dmg\",\n"
    "                    \"sha256\": \"\", \"size\": 300},\n"
    "    \"mac-x64\":     {\"url\": \"not-ready://x/y.dmg\", \"sha256\": \"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\", \"size\": 300},\n"
    "    \"linux-x64\":   {\"url\": \"https://github.com/ModderMule/emule-qt/releases/download/v0.8.0/eMuleQt-v0.8.0-linux-x86_64.tar.gz\",\n"
    "                    \"sha256\": \"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\", \"size\": 0}\n"
    "  }\n"
    "}\n";

} // namespace

class TestVersionManifest : public QObject {
    Q_OBJECT

private slots:
    void parsesEveryEntry();
    void hashIsLowercased();
    void installableNeedsHashUrlAndSize();
    void missingEntryIsEmpty();
};

void TestVersionManifest::parsesEveryEntry()
{
    const auto info = VersionManifest::parse(kManifest);
    QVERIFY(info.isValid());
    QCOMPARE(info.latest, QStringLiteral("0.8.0"));
    QCOMPARE(info.downloads.size(), 5);
    QCOMPARE(info.downloadFor(QStringLiteral("windows-x64-installer")).size, 200);
}

void TestVersionManifest::hashIsLowercased()
{
    const auto info = VersionManifest::parse(kManifest);
    QCOMPARE(info.downloadFor(QStringLiteral("windows-x64")).sha256, QString(64, QLatin1Char('a')));
}

void TestVersionManifest::installableNeedsHashUrlAndSize()
{
    const auto info = VersionManifest::parse(kManifest);
    QVERIFY(info.downloadFor(QStringLiteral("windows-x64")).isInstallable());
    QVERIFY(info.downloadFor(QStringLiteral("windows-x64-installer")).isInstallable());
    // no hash: a link, never an install
    QVERIFY(info.downloadFor(QStringLiteral("mac-arm64")).isUsable());
    QVERIFY(!info.downloadFor(QStringLiteral("mac-arm64")).isInstallable());
    // placeholder scheme / size 0
    QVERIFY(!info.downloadFor(QStringLiteral("mac-x64")).isInstallable());
    QVERIFY(!info.downloadFor(QStringLiteral("linux-x64")).isInstallable());
}

void TestVersionManifest::missingEntryIsEmpty()
{
    const auto info = VersionManifest::parse(kManifest);
    const VersionDownload d = info.downloadFor(QStringLiteral("linux-arm64"));
    QVERIFY(d.url.isEmpty());
    QVERIFY(!d.isUsable());
}

QTEST_GUILESS_MAIN(TestVersionManifest)
#include "tst_VersionManifest.moc"
