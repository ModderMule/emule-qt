/// @file tst_UsenetLivePreviewImages.cpp
/// @brief Search Details › Images against a real provider: every .nzb in
///        EMULE_NZB_DIR that posts an image or holds a video gives pictures.
///
/// Environment: the provider variables from UsenetLiveEnv.h and EMULE_NZB_DIR.
/// EMULE_PREVIEW_OUT (optional): a folder the pictures are written to, to look at.
/// Labelled "live" and built only under EMULE_LIVE_TESTS.

#include "nzb/NzbFile.h"
#include "stream/UsenetImagePreview.h"

#include "UsenetLiveEnv.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace eMule::usenet;
using eMule::testing::loadProjectEnv;
using namespace eMule::testing::usenet;

class tst_UsenetLivePreviewImages : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void picturesOfARelease_data();
    void picturesOfARelease();
};

void tst_UsenetLivePreviewImages::initTestCase()
{
    loadProjectEnv();
}

void tst_UsenetLivePreviewImages::picturesOfARelease_data()
{
    QTest::addColumn<QString>("path");
    if (addNzbRows() == 0)
        QTest::newRow("none") << QString();
}

void tst_UsenetLivePreviewImages::picturesOfARelease()
{
    QFETCH(QString, path);
    if (!haveLiveProvider() || path.isEmpty())
        QSKIP("Set EMULE_NNTP_HOST and EMULE_NZB_DIR to run the live preview test");

    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parseFile(path, nzb, error), qPrintable(error));
    const auto plan = UsenetImagePreview::plan(nzb);

    UsenetImagePreview job({providerFromEnv()}, QNetworkProxy(QNetworkProxy::NoProxy));
    QSignalSpy done(&job, &UsenetImagePreview::finished);
    QElapsedTimer timer;
    timer.start();
    job.start(nzb);
    QVERIFY2(done.wait(300'000), "preview timed out");

    const auto images = done.first().at(0).value<QList<UsenetPreviewImage>>();
    int posted = 0;
    int frames = 0;
    for (const auto& img : images) {
        if (img.kind == UsenetPreviewImage::Kind::Posted)
            ++posted;
        else if (img.kind == UsenetPreviewImage::Kind::Frame)
            ++frames;
    }
    qInfo().noquote() << QFileInfo(path).fileName() << "posted" << posted << "frames" << frames
                      << "video" << job.videoName() << "in" << timer.elapsed() << "ms"
                      << "note" << done.first().at(1).toString();

    if (const QString out = liveEnv("EMULE_PREVIEW_OUT"); !out.isEmpty()) {
        const QString dir = QDir(out).filePath(QFileInfo(path).completeBaseName());
        QDir().mkpath(dir);
        int n = 0;
        for (const auto& img : images) {
            QString name = QStringLiteral("%1-%2").arg(++n).arg(img.name);
            if (img.kind == UsenetPreviewImage::Kind::Frame)
                name += QStringLiteral(".jpg");
            QFile f(QDir(dir).filePath(name));
            if (f.open(QIODevice::WriteOnly))
                f.write(img.data);
        }
    }

    if (!plan.images.isEmpty())
        QVERIFY2(posted > 0, "the release posts images, none came back");
    static const QStringList videoExts{QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("m4v"),
                                       QStringLiteral("avi"), QStringLiteral("mov")};
    if (videoExts.contains(QFileInfo(job.videoName()).suffix().toLower()))
        QVERIFY2(frames > 0, "the release holds a video, no frame came back");
    if (plan.images.isEmpty() && job.videoName().isEmpty())
        QSKIP("nothing to take pictures of");
}

// guiless: the daemon is a QCoreApplication too
QTEST_GUILESS_MAIN(tst_UsenetLivePreviewImages)
#include "tst_UsenetLivePreviewImages.moc"
