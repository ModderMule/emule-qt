/// @file tst_NzbDrop.cpp
/// @brief What the application takes from a drop, and what it leaves alone.
///
/// Both halves matter. Accepting too little means a drop does nothing; accepting
/// too much means a future `.emulecollection`, `server.met` or `ed2k:` drop is
/// swallowed here instead of reaching whatever should handle it.

#include "utils/NzbDrop.h"

#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace eMule::gui;

class tst_NzbDrop : public QObject {
    Q_OBJECT

private slots:
    void init();

    void aDropOfNzbFilesIsAccepted();
    void aDropOfSeveralNzbFilesKeepsEveryOne();
    void aDropOfSomethingElseIsRefused();
    void anHttpUrlEndingInNzbIsRouted();
    void anIndexerLinkWithAQueryStringIsStillAnNzb();
    void aNonExistentLocalFileIsNotAccepted();
    void aDropWithNoUrlsIsRefused();

private:
    QTemporaryDir m_dir;
    QString makeFile(const QString& name);
};

QString tst_NzbDrop::makeFile(const QString& name)
{
    const QString path = m_dir.filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
    return path;
}

void tst_NzbDrop::init()
{
    QVERIFY(m_dir.isValid());
}

void tst_NzbDrop::aDropOfNzbFilesIsAccepted()
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("release.nzb")))});

    const auto out = nzbDropCandidates(&mime);
    QCOMPARE(out.files.size(), 1);
    QVERIFY(out.urls.isEmpty());
    QVERIFY(out.files.first().endsWith(QStringLiteral("release.nzb")));

    // Case does not matter: a file manager hands back whatever is on disk.
    QMimeData upper;
    upper.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("SHOUTY.NZB")))});
    QCOMPARE(nzbDropCandidates(&upper).files.size(), 1);
}

void tst_NzbDrop::aDropOfSeveralNzbFilesKeepsEveryOne()
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("a.nzb"))),
                  QUrl::fromLocalFile(makeFile(QStringLiteral("b.nzb"))),
                  QUrl::fromLocalFile(makeFile(QStringLiteral("c.nzb")))});

    QCOMPARE(nzbDropCandidates(&mime).files.size(), 3);
}

void tst_NzbDrop::aDropOfSomethingElseIsRefused()
{
    // Each of these has somewhere else it should end up, and swallowing it here
    // is how that never gets built.
    QMimeData collection;
    collection.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("x.emulecollection")))});
    QVERIFY(nzbDropCandidates(&collection).isEmpty());

    QMimeData serverMet;
    serverMet.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("server.met")))});
    QVERIFY(nzbDropCandidates(&serverMet).isEmpty());

    QMimeData link;
    link.setUrls({QUrl(QStringLiteral("ed2k://|file|x|1|AABB|/"))});
    QVERIFY(nzbDropCandidates(&link).isEmpty());

    // And a remote URL that is not an .nzb at all.
    QMimeData page;
    page.setUrls({QUrl(QStringLiteral("https://example.org/index.html"))});
    QVERIFY(nzbDropCandidates(&page).isEmpty());

    // ftp is not something the daemon will fetch.
    QMimeData ftp;
    ftp.setUrls({QUrl(QStringLiteral("ftp://example.org/release.nzb"))});
    QVERIFY(nzbDropCandidates(&ftp).isEmpty());
}

void tst_NzbDrop::anHttpUrlEndingInNzbIsRouted()
{
    QMimeData mime;
    mime.setUrls({QUrl(QStringLiteral("https://indexer.example/get/release.nzb"))});

    const auto out = nzbDropCandidates(&mime);
    QVERIFY(out.files.isEmpty());
    QCOMPARE(out.urls.size(), 1);
}

void tst_NzbDrop::anIndexerLinkWithAQueryStringIsStillAnNzb()
{
    // The *path* has to end in .nzb, not the whole URL: an indexer's download
    // link carries its API key after it.
    QMimeData mime;
    mime.setUrls({QUrl(QStringLiteral("https://ix.example/get/r.nzb?apikey=abc&x=1"))});

    QCOMPARE(nzbDropCandidates(&mime).urls.size(), 1);
}

void tst_NzbDrop::aNonExistentLocalFileIsNotAccepted()
{
    // Accepting the drag would promise something the drop then has to refuse
    // with a dialog. A remote URL is different -- it cannot be checked in a
    // dragMoveEvent, and the daemon is the one that fetches it.
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("ghost.nzb")))});

    QVERIFY(nzbDropCandidates(&mime).isEmpty());
}

void tst_NzbDrop::aDropWithNoUrlsIsRefused()
{
    QMimeData text;
    text.setText(QStringLiteral("release.nzb"));
    QVERIFY(nzbDropCandidates(&text).isEmpty());

    QVERIFY(nzbDropCandidates(nullptr).isEmpty());
}

QTEST_MAIN(tst_NzbDrop)
#include "tst_NzbDrop.moc"
