/// @file tst_VersionManifest.cpp
/// @brief core/app/VersionManifest: the download entries the auto-updater installs from.

#include "app/VersionManifest.h"

#include <QTest>
#include <QTimeZone>

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
    void notesDefaultEmpty();
    void parsesDescriptionAndChangelog();
    void parsesHosterResolver();
    void parsesSkins();
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

void TestVersionManifest::notesDefaultEmpty()
{
    const auto info = VersionManifest::parse(kManifest);
    QVERIFY(info.description.isEmpty());
    QVERIFY(info.changelog.isEmpty());
}

void TestVersionManifest::parsesDescriptionAndChangelog()
{
    const auto info = VersionManifest::parse(
        "{\"latest\": \"0.8.1\", \"description\": \"  Bug fix release.\\n\","
        " \"changelog\": [\"Faster search\", \"\", 7, \"  Fix <b>crash</b> \"]}");
    QVERIFY(info.isValid());
    QCOMPARE(info.description, QStringLiteral("Bug fix release."));
    // empty and non-string entries dropped, rest trimmed
    QCOMPARE(info.changelog, (QStringList{QStringLiteral("Faster search"), QStringLiteral("Fix <b>crash</b>")}));
}

void TestVersionManifest::parsesHosterResolver()
{
    // written by scripts/update-manifest.sh; no api in an older manifest
    auto info = VersionManifest::parse(
        "{\"latest\": \"0.8.1\", \"hosterResolver\": {\"url\": \"https://emule-qt.org/pub/resolver.zip\","
        " \"sha256\": \"EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE\", \"size\": 4000,"
        " \"jdRevision\": 53800, \"api\": 2}}");
    QVERIFY(info.isValid());
    QVERIFY(info.hosterResolver.file.isInstallable());
    QCOMPARE(info.hosterResolver.file.sha256, QString(64, QLatin1Char('e')));
    QCOMPARE(info.hosterResolver.jdRevision, 53800);
    QCOMPARE(info.hosterResolver.api, 2);
    info = VersionManifest::parse(kManifest);
    QVERIFY(info.hosterResolver.file.url.isEmpty());
    QCOMPARE(info.hosterResolver.api, 0);
}

void TestVersionManifest::parsesSkins()
{
    // written by scripts/publish-skin.py; bad id / no hash / bad preview are dropped or emptied
    const auto info = VersionManifest::parse(
        "{\"latest\": \"0.8.1\", \"skins\": {"
        " \"Zeta\": {\"name\": \"Zeta Skin\", \"url\": \"https://emule-qt.org/pub/skins/Zeta.zip\","
        "  \"sha256\": \"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\", \"size\": 10,"
        "  \"date\": \"2026-11-06T13:41:40Z\", \"author\": \"me\","
        "  \"preview\": {\"url\": \"https://emule-qt.org/pub/skins/Zeta.preview.png\","
        "   \"sha256\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\", \"size\": 5}},"
        " \"Alpha\": {\"url\": \"https://emule-qt.org/pub/skins/Alpha.zip\","
        "  \"sha256\": \"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\", \"size\": 20,"
        "  \"preview\": {\"url\": \"https://x/p.png\", \"sha256\": \"\", \"size\": 5}},"
        " \"../evil\": {\"url\": \"https://emule-qt.org/pub/skins/e.zip\","
        "  \"sha256\": \"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\", \"size\": 20},"
        " \"NoHash\": {\"url\": \"https://emule-qt.org/pub/skins/n.zip\", \"size\": 20}}}");
    QVERIFY(info.isValid());
    QCOMPARE(info.skins.size(), 2);
    QCOMPARE(info.skins[0].id, QStringLiteral("Alpha"));   // sorted by name, id when unnamed
    QCOMPARE(info.skins[0].name, QStringLiteral("Alpha"));
    QVERIFY(info.skins[0].preview.url.isEmpty());
    QCOMPARE(info.skins[1].name, QStringLiteral("Zeta Skin"));
    QCOMPARE(info.skins[1].file.sha256, QString(64, QLatin1Char('a')));
    QCOMPARE(info.skins[1].date, QDateTime(QDate(2026, 11, 6), QTime(13, 41, 40), QTimeZone::UTC));
    QVERIFY(info.skins[1].preview.isInstallable());
    QVERIFY(VersionManifest::parse(kManifest).skins.isEmpty());
}

QTEST_GUILESS_MAIN(TestVersionManifest)
#include "tst_VersionManifest.moc"
