/// @file tst_TorrentLinks.cpp
/// @brief Which magnets go to BitTorrent and which stay with eD2K, and .torrent drops.
///
/// The one rule that must not slip: an eD2K magnet — hybrid or not — keeps going
/// to Transfers. Only a magnet naming a BT info hash and no eD2K file goes to
/// the Torrents tab.

#include "utils/TorrentLinks.h"

#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace eMule::gui;

namespace {
const QString kHash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
const QString kEd2k = QStringLiteral("31D6CFE0D16AE931B73C59D7E0C089C0");
}

class tst_TorrentLinks : public QObject {
    Q_OBJECT

private slots:
    void btihMagnetIsBitTorrent();
    void btmhAndNumberedXtAreBitTorrent();
    void ed2kMagnetStaysEd2k();
    void hybridMagnetStaysEd2k();
    void notAMagnet();
    void magnetsInMixedText();
    void displayName();
    void magnetFromHash();
    void torrentDrop();
};

void tst_TorrentLinks::btihMagnetIsBitTorrent()
{
    QVERIFY(isBitTorrentMagnet(QStringLiteral("magnet:?xt=urn:btih:%1&dn=Ubuntu").arg(kHash)));
    QVERIFY(isBitTorrentMagnet(QStringLiteral("MAGNET:?XT=URN:BTIH:%1").arg(kHash.toUpper())));
    // base32 info hash, trackers after it
    QVERIFY(isBitTorrentMagnet(QStringLiteral(
        "magnet:?xt=urn:btih:AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&tr=udp%3A%2F%2Ftracker.example%3A80")));
    QVERIFY(isBitTorrentMagnet(QStringLiteral("  magnet:?xt=urn:btih:%1  ").arg(kHash)));
}

void tst_TorrentLinks::btmhAndNumberedXtAreBitTorrent()
{
    QVERIFY(isBitTorrentMagnet(QStringLiteral(
        "magnet:?xt=urn:btmh:1220caf1e1c30e81cb361b9ee167c4aa64228a7fa4fa9f6105232b28ad099f3a302e")));
    QVERIFY(isBitTorrentMagnet(QStringLiteral("magnet:?dn=x&xt.1=urn:btih:%1").arg(kHash)));
}

void tst_TorrentLinks::ed2kMagnetStaysEd2k()
{
    QVERIFY(!isBitTorrentMagnet(
        QStringLiteral("magnet:?xt=urn:ed2k:%1&dn=file.iso&xl=1000").arg(kEd2k)));
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("magnet:?xt=urn:ed2khash:%1").arg(kEd2k)));
}

void tst_TorrentLinks::hybridMagnetStaysEd2k()
{
    // either order, numbered or not
    QVERIFY(!isBitTorrentMagnet(
        QStringLiteral("magnet:?xt=urn:btih:%1&xt=urn:ed2k:%2").arg(kHash, kEd2k)));
    QVERIFY(!isBitTorrentMagnet(
        QStringLiteral("magnet:?xt.1=urn:ed2k:%2&xt.2=urn:btih:%1").arg(kHash, kEd2k)));
    // an embedded ed2k:// source is an eD2K file link too (ED2KLink.cpp)
    // concatenated, not arg(): the %2F escapes would read as placeholders
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("magnet:?xt=urn:btih:") + kHash
                                + QStringLiteral("&xs=ed2k%3A%2F%2F%7Cfile%7Ca%7C1%7C") + kEd2k
                                + QStringLiteral("%7C%2F")));
}

void tst_TorrentLinks::notAMagnet()
{
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("ed2k://|file|a.iso|1|%1|/").arg(kEd2k)));
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("magnet:")));
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("magnet:?dn=only-a-name")));
    QVERIFY(!isBitTorrentMagnet(QStringLiteral("https://example.com/?xt=urn:btih:%1").arg(kHash)));
    QVERIFY(!isBitTorrentMagnet(QString()));
}

void tst_TorrentLinks::magnetsInMixedText()
{
    const QString bt1 = QStringLiteral("magnet:?xt=urn:btih:%1&dn=One").arg(kHash);
    const QString bt2 = QStringLiteral("magnet:?xt=urn:btmh:1220aa");
    const QString text = QStringLiteral("ed2k://|file|a.iso|1|%1|/\n%2 %3\n"
                                        "magnet:?xt=urn:ed2k:%1\nhello")
                             .arg(kEd2k, bt1, bt2);
    QCOMPARE(bitTorrentMagnetsIn(text), (QStringList{bt1, bt2}));
    QVERIFY(bitTorrentMagnetsIn(QStringLiteral("nothing here")).isEmpty());
}

void tst_TorrentLinks::displayName()
{
    QCOMPARE(magnetDisplayName(QStringLiteral("magnet:?xt=urn:btih:") + kHash
                               + QStringLiteral("&dn=Ubuntu+24.04%20ISO")),
             QStringLiteral("Ubuntu 24.04 ISO"));
    QCOMPARE(magnetDisplayName(QStringLiteral("magnet:?xt=urn:btih:%1").arg(kHash)), kHash);
}

void tst_TorrentLinks::magnetFromHash()
{
    QCOMPARE(torrentMagnetLink(kHash.toUpper(), QStringLiteral("A B&C")),
             QStringLiteral("magnet:?xt=urn:btih:") + kHash + QStringLiteral("&dn=A%20B%26C"));
    QVERIFY(torrentMagnetLink({}, QStringLiteral("v2 only")).isEmpty());
    // round trip: what we copy, we route back to BitTorrent
    QVERIFY(isBitTorrentMagnet(torrentMagnetLink(kHash, QStringLiteral("x"))));
}

void tst_TorrentLinks::torrentDrop()
{
    QTemporaryDir dir;
    const QString torrent = dir.filePath(QStringLiteral("a.torrent"));
    const QString other = dir.filePath(QStringLiteral("b.nzb"));
    for (const QString& p : {torrent, other}) {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("d4:infod4:name1:aee");
    }
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(torrent), QUrl::fromLocalFile(other),
                  QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.torrent"))),
                  QUrl(QStringLiteral("https://example.com/c.torrent"))});
    QCOMPARE(torrentDropFiles(&mime), QStringList{torrent});
    QVERIFY(torrentDropFiles(nullptr).isEmpty());
    QVERIFY(isTorrentFilePath(QStringLiteral("/x/Y.TORRENT")));
    QVERIFY(!isTorrentFilePath(QStringLiteral("/x/y.torrent.part")));
}

QTEST_GUILESS_MAIN(tst_TorrentLinks)
#include "tst_TorrentLinks.moc"
