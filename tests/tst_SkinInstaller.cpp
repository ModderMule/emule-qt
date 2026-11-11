/// @file tst_SkinInstaller.cpp
/// @brief Manifest skins: download, sha256 check, unpack into skins/<id>/, auto-update.

#include "app/SkinEngine.h"
#include "app/SkinInstaller.h"
#include "app/VersionChecker.h"
#include "dialogs/SkinBrowserDialog.h"
#include "prefs/Preferences.h"

#include "FakeHttpServer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

using namespace eMule;

namespace {

QByteArray zipOf(const QList<QPair<QString, QByteArray>>& members)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("s.zip"));
    auto* a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_open_filename(a, path.toUtf8().constData());
    for (const auto& [name, data] : members) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.toUtf8().constData());
        archive_entry_set_size(entry, data.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);
        archive_write_data(a, data.constData(), size_t(data.size()));
        archive_entry_free(entry);
    }
    archive_write_close(a);
    archive_write_free(a);
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}

QByteArray png()
{
    QImage img(8, 4, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

/// Neo skin in one top folder, as zipped by Finder; @p marker tells versions apart
QByteArray neoZip(const QByteArray& marker)
{
    return zipOf({{QStringLiteral("Pack/Neo.eMuleSkin.ini"),
                   "[Skin]\nName=Neo\nPreview=img\\p.png\n[Colors]\nDefLvBk=#0b1324\n; " + marker + "\n"},
                  {QStringLiteral("Pack/img/p.png"), png()},
                  {QStringLiteral("Pack/toolbar/Neo.eMuleToolbar.kad02.png"), png()}});
}

SkinRelease releaseOf(const QString& id, const QByteArray& zip, const QUrl& url)
{
    SkinRelease r;
    r.id = id;
    r.name = id;
    r.date = QDateTime::currentDateTimeUtc();
    r.file.url = url.toString();
    r.file.size = zip.size();
    r.file.sha256 = QString::fromLatin1(QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex());
    return r;
}

} // namespace

class tst_SkinInstaller : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void installsTopFolderBundle();
    void checksumMismatchKeepsInstalledCopy();
    void updateInstalledFollowsSha();
    void zipWithoutProfileFails();
    void browserFetchesListPreviewAndApplies();

private:
    /// install() and wait for finished(); @return its ok flag
    bool installAndWait(SkinInstaller& inst, const SkinRelease& r, QString* error = nullptr);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_savedConfig;
};

void tst_SkinInstaller::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_savedConfig = thePrefs.configDir();
    thePrefs.setConfigDir(m_dir->path());
}

void tst_SkinInstaller::cleanup()
{
    thePrefs.setConfigDir(m_savedConfig);
    m_dir.reset();
}

void tst_SkinInstaller::installsTopFolderBundle()
{
    const QByteArray zip = neoZip("v1");
    FakeHttpServer http;
    QVERIFY(http.isListening());
    http.response.body = zip;

    SkinInstaller inst;
    QVERIFY(installAndWait(inst, releaseOf(QStringLiteral("Neo"), zip, http.url(QStringLiteral("/skins/Neo.zip")))));

    const QString profile = SkinInstaller::profilePath(QStringLiteral("Neo"));
    QCOMPARE(profile, SkinInstaller::skinDir(QStringLiteral("Neo")) + QStringLiteral("/Neo.eMuleSkin.ini"));
    const auto installed = SkinInstaller::installed(QStringLiteral("Neo"));
    QVERIFY(installed);
    QCOMPARE(installed->sha256, releaseOf({}, zip, {}).file.sha256);
    QVERIFY(SkinInstaller::toolbarStripPath(SkinInstaller::skinDir(QStringLiteral("Neo")))
                .endsWith(QStringLiteral("/toolbar/Neo.eMuleToolbar.kad02.png")));
    // no preview/overview.png: the ini's Preview= (backslash, as written on Windows)
    QVERIFY(SkinEngine::previewImage(profile).endsWith(QStringLiteral("/img/p.png")));
    QCOMPARE(SkinInstaller::bundleIdOf(profile), QStringLiteral("Neo"));
    QVERIFY(SkinInstaller::bundleIdOf(m_dir->filePath(QStringLiteral("x.ini"))).isEmpty());
    // nothing left over beside the bundle
    QCOMPARE(QDir(SkinInstaller::skinsRoot()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
             QStringList{QStringLiteral("Neo")});
}

void tst_SkinInstaller::checksumMismatchKeepsInstalledCopy()
{
    const QByteArray v1 = neoZip("v1");
    FakeHttpServer http;
    http.response.body = v1;
    SkinInstaller inst;
    const QUrl url = http.url(QStringLiteral("/skins/Neo.zip"));
    QVERIFY(installAndWait(inst, releaseOf(QStringLiteral("Neo"), v1, url)));

    // served bytes differ from what the manifest promises
    const QByteArray v2 = neoZip("v2");
    SkinRelease bad = releaseOf(QStringLiteral("Neo"), v2, url);
    http.response.body = neoZip("v3");
    QString error;
    QVERIFY(!installAndWait(inst, bad, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(SkinInstaller::installed(QStringLiteral("Neo"))->sha256, releaseOf({}, v1, {}).file.sha256);
}

void tst_SkinInstaller::updateInstalledFollowsSha()
{
    const QByteArray v1 = neoZip("v1");
    const QByteArray v2 = neoZip("v2");
    FakeHttpServer http;
    http.response.body = v1;
    SkinInstaller inst;
    const QUrl url = http.url(QStringLiteral("/skins/Neo.zip"));
    QVERIFY(installAndWait(inst, releaseOf(QStringLiteral("Neo"), v1, url)));
    const int requests = http.requestCount();

    // same sha, and a skin that is not installed: nothing fetched
    inst.updateInstalled({releaseOf(QStringLiteral("Neo"), v1, url), releaseOf(QStringLiteral("Other"), v2, url)});
    QTest::qWait(50);
    QCOMPARE(http.requestCount(), requests);

    // republished zip: re-installed in place
    http.response.body = v2;
    QSignalSpy done(&inst, &SkinInstaller::finished);
    inst.updateInstalled({releaseOf(QStringLiteral("Neo"), v2, url)});
    QVERIFY(done.wait(5000));
    QVERIFY(done.first().at(1).toBool());
    QCOMPARE(SkinInstaller::installed(QStringLiteral("Neo"))->sha256, releaseOf({}, v2, {}).file.sha256);
    QFile ini(SkinInstaller::profilePath(QStringLiteral("Neo")));
    QVERIFY(ini.open(QIODevice::ReadOnly));
    QVERIFY(ini.readAll().contains("; v2"));
}

void tst_SkinInstaller::zipWithoutProfileFails()
{
    const QByteArray zip = zipOf({{QStringLiteral("readme.txt"), "hi"}});
    FakeHttpServer http;
    http.response.body = zip;
    SkinInstaller inst;
    QVERIFY(!installAndWait(inst, releaseOf(QStringLiteral("Bare"), zip, http.url(QStringLiteral("/skins/Bare.zip")))));
    QVERIFY(!QFileInfo::exists(SkinInstaller::skinDir(QStringLiteral("Bare"))));
    QCOMPARE(QDir(SkinInstaller::skinsRoot()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
             QStringList{});
}

void tst_SkinInstaller::browserFetchesListPreviewAndApplies()
{
    // EMULE_SKIN_SHOT_ZIP: a real bundle instead of the stand-in (screenshot runs)
    QByteArray zip = neoZip("v1");
    QString id = QStringLiteral("Neo");
    QByteArray preview = png();
    if (const QString real = qEnvironmentVariable("EMULE_SKIN_SHOT_ZIP"); !real.isEmpty()) {
        QFile f(real);
        QVERIFY(f.open(QIODevice::ReadOnly));
        zip = f.readAll();
        id = qEnvironmentVariable("EMULE_SKIN_SHOT_ID");
        QFile p(qEnvironmentVariable("EMULE_SKIN_SHOT_PREVIEW"));
        QVERIFY(p.open(QIODevice::ReadOnly));
        preview = p.readAll();
    }
    FakeHttpServer http;
    const auto sha = [](const QByteArray& b) {
        return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
    };
    const QJsonObject entry{
        {QStringLiteral("name"), QString(id + QStringLiteral(" Skin"))},
        {QStringLiteral("url"), http.url(QStringLiteral("/skins/%1.zip").arg(id)).toString()},
        {QStringLiteral("sha256"), sha(zip)},
        {QStringLiteral("size"), qint64(zip.size())},
        {QStringLiteral("date"), QStringLiteral("2026-11-06T13:41:40Z")},
        {QStringLiteral("author"), QStringLiteral("someone")},
        {QStringLiteral("preview"), QJsonObject{
            {QStringLiteral("url"), http.url(QStringLiteral("/skins/%1.preview.png").arg(id)).toString()},
            {QStringLiteral("sha256"), sha(preview)},
            {QStringLiteral("size"), qint64(preview.size())}}},
    };
    const QByteArray manifest = QJsonDocument(QJsonObject{
        {QStringLiteral("latest"), QStringLiteral("0.0.1")},
        {QStringLiteral("skins"), QJsonObject{{id, entry}}}}).toJson();
    http.handler = [&](const QByteArray& path, const QByteArray&) {
        FakeHttpServer::Response r;
        if (path == "/emuleqt-version.json")
            r.body = manifest;
        else if (path.endsWith(".zip"))
            r.body = zip;
        else if (path.endsWith(".png"))
            r.body = preview;
        else
            r.status = 404;
        return r;
    };
    qputenv("EMULEQT_VERSION_MANIFEST_URL", http.url(QStringLiteral("/emuleqt-version.json")).toEncoded());

    VersionChecker checker;
    SkinInstaller inst;
    SkinBrowserDialog dlg(&checker, &inst);
    dlg.show();
    auto* list = dlg.findChild<QListWidget*>();
    QVERIFY(list);
    // no manifest yet: the dialog fetched one
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->currentRow(), 0);
    QVERIFY(list->item(0)->text().startsWith(id + QStringLiteral(" Skin")));
    // not installed: the manifest's preview image
    QLabel* previewLabel = nullptr;
    for (auto* label : dlg.findChildren<QLabel*>()) {
        if (label->minimumWidth() == 320)
            previewLabel = label;
    }
    QVERIFY(previewLabel);
    QTRY_VERIFY(!previewLabel->pixmap().isNull());

    QPushButton* apply = nullptr;
    QPushButton* restore = nullptr;
    for (auto* b : dlg.findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("Apply"))
            apply = b;
        else if (b->text() == QStringLiteral("Restore Default Skin"))
            restore = b;
    }
    QVERIFY(apply && restore);
    QSignalSpy applied(&dlg, &SkinBrowserDialog::applySkin);
    apply->click();
    QTRY_COMPARE(applied.count(), 1);
    QCOMPARE(applied.first().first().toString(), id);
    QVERIFY(SkinInstaller::installed(id));
    QVERIFY(list->item(0)->text().contains(QStringLiteral("installed")));

    if (const QString shot = qEnvironmentVariable("EMULE_SKIN_SHOT"); !shot.isEmpty()) {
        dlg.resize(900, 560);
        QTest::qWait(100);
        dlg.grab().save(shot);
    }

    QSignalSpy restored(&dlg, &SkinBrowserDialog::restoreDefaultSkin);
    restore->setEnabled(true);   // nothing active in this test's UiState
    restore->click();
    QCOMPARE(restored.count(), 1);
    qunsetenv("EMULEQT_VERSION_MANIFEST_URL");
}

// ---------------------------------------------------------------------------

bool tst_SkinInstaller::installAndWait(SkinInstaller& inst, const SkinRelease& r, QString* error)
{
    QSignalSpy done(&inst, &SkinInstaller::finished);
    inst.install(r);
    if (done.isEmpty() && !done.wait(5000))
        return false;
    if (error)
        *error = done.first().at(2).toString();
    return done.first().at(1).toBool();
}

QTEST_MAIN(tst_SkinInstaller)
#include "tst_SkinInstaller.moc"
