/// @file tst_ToolbarSkin.cpp
/// @brief Toolbar strip slicing (MFC 14-tile layout + our optional tiles), skin
///        profile parsing of MFC-written *.eMuleSkin.ini files and [Colors] binding.

#include "app/SkinColors.h"
#include "app/SkinEngine.h"
#include "app/SkinIcons.h"
#include "app/ToolbarSkin.h"
#include "controls/KnownTypeStyle.h"
#include "utils/ClientIcons.h"
#include "utils/Smileys.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QScrollBar>
#include <QStringListModel>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace Qt::StringLiterals;

namespace {

/// Strip of @p count tiles, tile i filled with grey level i * 10 (no alpha).
QImage makeStrip(int count, int size = 32)
{
    QImage img(count * size, size, QImage::Format_RGB32);
    QPainter p(&img);
    for (int i = 0; i < count; ++i)
        p.fillRect(i * size, 0, size, size, QColor(i * 10, i * 10, i * 10));
    return img;
}

int tileGrey(const QIcon& icon)
{
    return qRed(icon.pixmap(32, 32).toImage().pixel(16, 16)) / 10;
}

QString solidPng(const QTemporaryDir& dir, const QString& name, const QColor& color)
{
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(color);
    const QString path = dir.filePath(name);
    img.save(path);
    return path;
}

QColor centre(const QIcon& icon)
{
    return icon.pixmap(16, 16).toImage().pixelColor(8, 8);
}

void writeRaw(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

} // namespace

class tst_ToolbarSkin : public QObject {
    Q_OBJECT

private slots:
    void slice_officialStrip();
    void slice_withOurTiles();
    void slice_rejectsWrongShapes();
    void slice_magentaKey();
    void slice_zeroAlphaIsOpaque();
    void icon_tileOrderMatchesMfc();
    void icon_extraTilesFallBackToProfile();
    void profile_mfcIni();
    void profile_utf16Bom();
    void profile_theme();
    void colors_listFallback();
    void colors_followProfile();
    void colors_keepOwnPalette();
    void watermark_profilePaths();
    void watermark_listFallback();
    void watermark_pinnedBottomRight();
    void colors_downloadStopped();
    void icons_resourceNames();
    void icons_profileFiles();
    void icons_followProfile();
    void icons_compositesRebuilt();
    void icons_labelAndSmiley();
    void cleanup();
};

void tst_ToolbarSkin::cleanup()
{
    SkinColors::instance().setColors({});
    SkinIcons::instance().setFiles({});
}

void tst_ToolbarSkin::slice_officialStrip()
{
    QCOMPARE(ToolbarSkin::sliceStrip(makeStrip(14)).size(), 14);
    // hi-dpi strip: tile size follows the height
    const auto big = ToolbarSkin::sliceStrip(makeStrip(14, 64));
    QCOMPARE(big.size(), 14);
    QCOMPARE(big.first().size(), QSize(64, 64));
}

void tst_ToolbarSkin::slice_withOurTiles()
{
    QCOMPARE(ToolbarSkin::sliceStrip(makeStrip(16)).size(), 16);
    QCOMPARE(ToolbarSkin::sliceStrip(makeStrip(kToolbarTileCount)).size(), kToolbarTileCount);
    // tiles of a newer layout are dropped, not refused
    QCOMPARE(ToolbarSkin::sliceStrip(makeStrip(kToolbarTileCount + 3)).size(), kToolbarTileCount);
}

void tst_ToolbarSkin::slice_rejectsWrongShapes()
{
    QVERIFY(ToolbarSkin::sliceStrip(makeStrip(12)).isEmpty());   // old 12-tile guess
    QVERIFY(ToolbarSkin::sliceStrip(QImage(14 * 32 + 5, 32, QImage::Format_RGB32)).isEmpty());
    QVERIFY(ToolbarSkin::sliceStrip(QImage()).isEmpty());
}

void tst_ToolbarSkin::slice_magentaKey()
{
    QImage strip = makeStrip(14);
    strip.setPixel(0, 0, qRgb(255, 0, 255));
    const auto tiles = ToolbarSkin::sliceStrip(strip);
    QCOMPARE(qAlpha(tiles[0].pixel(0, 0)), 0);
    QCOMPARE(qAlpha(tiles[0].pixel(1, 1)), 255);
}

void tst_ToolbarSkin::slice_zeroAlphaIsOpaque()
{
    QImage strip(14 * 32, 32, QImage::Format_ARGB32);
    strip.fill(qRgba(10, 20, 30, 0));
    const auto tiles = ToolbarSkin::sliceStrip(strip);
    QCOMPARE(tiles.size(), 14);
    QCOMPARE(qAlpha(tiles[3].pixel(5, 5)), 255);

    // a real alpha channel is kept, magenta included
    strip.fill(qRgba(255, 0, 255, 128));
    QCOMPARE(qAlpha(ToolbarSkin::sliceStrip(strip)[0].pixel(5, 5)), 128);
}

void tst_ToolbarSkin::icon_tileOrderMatchesMfc()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.eMuleToolbar.kad02.png"));
    QVERIFY(makeStrip(14).save(path));

    ToolbarSkin skin;
    const SkinEngine none;
    QVERIFY(skin.loadStrip(path));

    // MuleToolBarCtrl.cpp:548: Connect, Disconnect, StopConnecting, Kad, ...
    QCOMPARE(tileGrey(skin.icon(ToolbarTile::Connect, none)), 0);
    QCOMPARE(tileGrey(skin.icon(ToolbarTile::Disconnect, none)), 1);
    QCOMPARE(tileGrey(skin.icon(*tileForButton(ToolbarButtonId::Kad), none)), 3);
    QCOMPARE(tileGrey(skin.icon(*tileForButton(ToolbarButtonId::Options), none)), 11);
    QCOMPARE(tileGrey(skin.icon(*tileForButton(ToolbarButtonId::Help), none)), 13);
    QCOMPARE(tileForButton(ToolbarButtonId::Connect), ToolbarTile::Connect);
}

void tst_ToolbarSkin::icon_extraTilesFallBackToProfile()
{
    QTemporaryDir dir;
    const QString strip14 = dir.filePath(QStringLiteral("a.png"));
    const QString strip15 = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(makeStrip(14).save(strip14));
    QVERIFY(makeStrip(15).save(strip15));

    QImage usenet(32, 32, QImage::Format_RGB32);
    usenet.fill(QColor(200, 200, 200));
    QVERIFY(usenet.save(dir.filePath(QStringLiteral("usenet.png"))));
    const QString ini = dir.filePath(QStringLiteral("s.eMuleSkin.ini"));
    writeRaw(ini, "[Icons]\r\nUSENET=usenet.png\r\nHoster=missing.ico\r\n");

    SkinEngine profile;
    profile.loadProfile(ini);
    const SkinEngine none;

    ToolbarSkin skin;
    QVERIFY(skin.loadStrip(strip14));
    QCOMPARE(tileGrey(skin.icon(ToolbarTile::Usenet, profile)), 20);   // profile
    QVERIFY(skin.icon(ToolbarTile::Usenet, none).isNull());              // built-in
    QVERIFY(skin.icon(ToolbarTile::Hoster, profile).isNull());           // missing file
    QVERIFY(skin.icon(ToolbarTile::Torrents, profile).isNull());

    QVERIFY(skin.loadStrip(strip15));
    QCOMPARE(tileGrey(skin.icon(ToolbarTile::Usenet, profile)), 14);   // strip wins

    // no strip: the profile covers the official tiles too
    skin.clearStrip();
    QVERIFY(skin.icon(ToolbarTile::Kad, profile).isNull());
}

void tst_ToolbarSkin::profile_mfcIni()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("icons")));
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(dir.filePath(QStringLiteral("icons/kad.png"))));
    QVERIFY(img.save(dir.filePath(QStringLiteral("back.png"))));

    // as Windows writes it: CRLF, backslashes, commas, odd case, comments, quotes
    const QString ini = dir.filePath(QStringLiteral("x.eMuleSkin.ini"));
    writeRaw(ini,
             "; skin\r\n"
             "[ICONS]\r\n"
             "Kademlia = icons\\kad.png\r\n"
             "Help=icons\\kad.png,3\r\n"
             "Tools=eMule.dll,7\r\n"
             "kademlia=ignored.png\r\n"
             "[Bitmaps]\r\n"
             "MainToolBarBk=\"back.png\"\r\n"
             "[Colors]\r\n"
             "DownloadsLvFg=255, 0 ,128\r\n"
             "DownloadsLvBk=0x10,08,#\r\n"
             "LogBk=#336699\r\n");

    SkinEngine skin;
    skin.loadProfile(ini);
    QVERIFY(skin.isActive());
    QVERIFY(!skin.icon(QStringLiteral("KADEMLIA")).isNull());
    QVERIFY(!skin.icon(QStringLiteral("help")).isNull());       // icon index stripped
    QVERIFY(skin.icon(QStringLiteral("TOOLS")).isNull());       // not an image
    QVERIFY(skin.icon(QStringLiteral("SERVER")).isNull());
    QVERIFY(!skin.pixmap(QStringLiteral("MainToolBarBk")).isNull());
    QCOMPARE(skin.bitmapPath(QStringLiteral("maintoolbarbk")), dir.filePath(QStringLiteral("back.png")));
    QCOMPARE(skin.color(QStringLiteral("DownloadsLvFg")), QColor(255, 0, 128));
    QVERIFY(!skin.color(QStringLiteral("DownloadsLvBk")));
    QCOMPARE(skin.color(QStringLiteral("logbk")), QColor(0x33, 0x66, 0x99));

    skin.clear();
    QVERIFY(!skin.isActive());
    QVERIFY(skin.icon(QStringLiteral("KADEMLIA")).isNull());
}

void tst_ToolbarSkin::profile_utf16Bom()
{
    QTemporaryDir dir;
    const QString ini = dir.filePath(QStringLiteral("u.eMuleSkin.ini"));
    const QString text = QStringLiteral("[Colors]\r\nChatFg=1,2,3\r\n");
    QByteArray bytes("\xFF\xFE", 2);
    bytes.append(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
    writeRaw(ini, bytes);

    SkinEngine skin;
    skin.loadProfile(ini);
    QCOMPARE(skin.color(QStringLiteral("ChatFg")), QColor(1, 2, 3));
}

void tst_ToolbarSkin::profile_theme()
{
    QTemporaryDir dir;
    const auto themeOf = [&](const QByteArray& body) {
        const QString ini = dir.filePath(QStringLiteral("t.eMuleSkin.ini"));
        writeRaw(ini, body);
        SkinEngine skin;
        skin.loadProfile(ini);
        return skin.theme();
    };
    QCOMPARE(themeOf("[Skin]\r\nTheme = Dark\r\n"), ThemeChoice::Dark);
    QCOMPARE(themeOf("[skin]\nTHEME=light\n"), ThemeChoice::Light);
    QCOMPARE(themeOf("[Skin]\nTheme=system\n"), ThemeChoice::System);
    QCOMPARE(themeOf("[Skin]\nTheme=neon\n"), ThemeChoice::System);     // unknown
    QCOMPARE(themeOf("[Colors]\nLogBk=#336699\n"), ThemeChoice::System); // absent
    QCOMPARE(themeOf("[Colors]\nTheme=dark\n"), ThemeChoice::System);   // only [Skin]

    SkinEngine skin;
    writeRaw(dir.filePath(QStringLiteral("d.eMuleSkin.ini")), "[Skin]\nTheme=dark\n");
    skin.loadProfile(dir.filePath(QStringLiteral("d.eMuleSkin.ini")));
    skin.clear();
    QCOMPARE(skin.theme(), ThemeChoice::System);
}

void tst_ToolbarSkin::colors_listFallback()
{
    // MFC CMuleListCtrl::SetColors: own key, else DefLv; ours add a middle step
    QCOMPARE(SkinColors::listPrefixes(QStringLiteral("downloads")),
             (QStringList{QStringLiteral("DownloadsLv"), QStringLiteral("DefLv")}));
    QCOMPARE(SkinColors::listPrefixes(QStringLiteral("torrentList")).value(1), QStringLiteral("DownloadsLv"));
    QCOMPARE(SkinColors::listPrefixes(QStringLiteral("optionsFeeds")), QStringList{QStringLiteral("DefLv")});

    SkinColors::instance().setColors({{QStringLiteral("deflvbk"), QColor(1, 2, 3)},
                                      {QStringLiteral("downloadslvbk"), QColor(4, 5, 6)},
                                      {QStringLiteral("deflvfg"), QColor(7, 8, 9)}});
    QCOMPARE(SkinColors::instance().listColor(QStringLiteral("torrentList"), u"Bk"), QColor(4, 5, 6));
    QCOMPARE(SkinColors::instance().listColor(QStringLiteral("serverList"), u"Bk"), QColor(1, 2, 3));

    QListView view;
    SkinColors::bindList(&view, QStringLiteral("downloads"));
    QCOMPARE(view.palette().color(QPalette::Base), QColor(4, 5, 6));
    QCOMPARE(view.palette().color(QPalette::Active, QPalette::Text), QColor(7, 8, 9));
    QCOMPARE(view.palette().color(QPalette::Active, QPalette::Highlight),
             QApplication::palette(&view).color(QPalette::Active, QPalette::Highlight));
}

void tst_ToolbarSkin::colors_followProfile()
{
    QListView view;
    SkinColors::bind(&view, {QStringLiteral("Chat")});
    QVERIFY(!view.testAttribute(Qt::WA_SetPalette));

    SkinColors::instance().setColors({{QStringLiteral("chatbk"), QColor(Qt::black)},
                                      {QStringLiteral("chatfg"), QColor(Qt::green)}});
    QCOMPARE(view.palette().color(QPalette::Base), QColor(Qt::black));
    QCOMPARE(view.palette().color(QPalette::Active, QPalette::Text), QColor(Qt::green));

    // profile cleared: back to the theme
    SkinColors::instance().setColors({});
    QVERIFY(!view.testAttribute(Qt::WA_SetPalette));
    QCOMPARE(view.palette().color(QPalette::Base), QApplication::palette().color(QPalette::Base));
}

void tst_ToolbarSkin::colors_keepOwnPalette()
{
    // a widget's own palette is not ours to reset when the skin has nothing for it
    QListView view;
    QPalette own;
    own.setColor(QPalette::Base, Qt::yellow);
    view.setPalette(own);
    SkinColors::bind(&view, {QStringLiteral("Log")});
    SkinColors::instance().setColors({{QStringLiteral("chatbk"), QColor(Qt::black)}});
    QCOMPARE(view.palette().color(QPalette::Base), QColor(Qt::yellow));
}

void tst_ToolbarSkin::watermark_profilePaths()
{
    // MFC keeps <key>BkImg in [Colors], relative to the ini; a missing file is dropped
    QTemporaryDir dir;
    QImage(8, 8, QImage::Format_ARGB32).save(dir.filePath(QStringLiteral("wm.png")));
    const QString ini = dir.filePath(QStringLiteral("t.eMuleSkin.ini"));
    writeRaw(ini, "[Colors]\r\nDownloadsLvBkImg=wm.png\r\nDefLvBkImg=gone.png\r\nDefLvBk=1,2,3\r\n");
    SkinEngine skin;
    skin.loadProfile(ini);
    QCOMPARE(skin.listImages().size(), 1);
    QCOMPARE(skin.listImages().value(QStringLiteral("downloadslvbkimg")), dir.filePath(QStringLiteral("wm.png")));
    QCOMPARE(skin.colors().size(), 1);   // the image keys are no colours
}

void tst_ToolbarSkin::watermark_listFallback()
{
    QTemporaryDir dir;
    QImage own(10, 10, QImage::Format_RGB32);
    own.fill(Qt::red);
    own.save(dir.filePath(QStringLiteral("own.png")));
    QImage def(12, 12, QImage::Format_RGB32);
    def.fill(Qt::blue);
    def.save(dir.filePath(QStringLiteral("def.png")));
    SkinColors::instance().setColors({}, {{QStringLiteral("downloadslvbkimg"), dir.filePath(QStringLiteral("own.png"))},
                                          {QStringLiteral("deflvbkimg"), dir.filePath(QStringLiteral("def.png"))}});
    auto& skin = SkinColors::instance();
    QCOMPARE(skin.watermark(SkinColors::listPrefixes(QStringLiteral("downloads"))).size(), QSize(10, 10));
    QCOMPARE(skin.watermark(SkinColors::listPrefixes(QStringLiteral("torrentList"))).size(), QSize(10, 10));
    QCOMPARE(skin.watermark(SkinColors::listPrefixes(QStringLiteral("serverList"))).size(), QSize(12, 12));
    // trees and logs have none in MFC
    QVERIFY(skin.watermark({QStringLiteral("Log")}).isNull());

    // a watermark alone skins the list: see-through stripes
    QListView view;
    SkinColors::bindList(&view, QStringLiteral("serverList"));
    QVERIFY(view.testAttribute(Qt::WA_SetPalette));
    QVERIFY(view.palette().color(QPalette::AlternateBase).alpha() < 255);

    SkinColors::instance().setColors({});
    QVERIFY(skin.watermark(SkinColors::listPrefixes(QStringLiteral("downloads"))).isNull());
    QVERIFY(!view.testAttribute(Qt::WA_SetPalette));
}

void tst_ToolbarSkin::watermark_pinnedBottomRight()
{
    QTemporaryDir dir;
    QImage img(20, 20, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(dir.filePath(QStringLiteral("wm.png")));
    SkinColors::instance().setColors({{QStringLiteral("deflvbk"), QColor(Qt::white)}},
                                     {{QStringLiteral("deflvbkimg"), dir.filePath(QStringLiteral("wm.png"))}});

    QStringList rows;
    for (int i = 0; i < 200; ++i)
        rows.append(QString::number(i));
    QStringListModel model(rows);
    QListView view;
    view.setModel(&model);
    view.resize(200, 150);
    SkinColors::bindList(&view, QStringLiteral("serverList"));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto check = [&] {
        const QImage shot = view.viewport()->grab().toImage();
        const qreal dpr = shot.devicePixelRatio();
        const QPoint corner(int((view.viewport()->width() - 5) * dpr), int((view.viewport()->height() - 5) * dpr));
        QCOMPARE(QColor(shot.pixel(corner)), QColor(Qt::red));
        QCOMPARE(QColor(shot.pixel(int(2 * dpr), int(2 * dpr))) == QColor(Qt::red), false);
    };
    check();
    // stays in its corner while the rows scroll
    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() / 2);
    QCoreApplication::processEvents();
    check();
}

void tst_ToolbarSkin::colors_downloadStopped()
{
    // MFC SearchListCtrl: Fg_DownloadStopped (no list prefix), else the Downloading colour
    auto& skin = SkinColors::instance();
    skin.setColors({{QStringLiteral("searchresultslvfg_downloading"), QColor(1, 1, 1)}});
    QCOMPARE(knownTypeColor(2, true), QColor(1, 1, 1));
    skin.setColors({{QStringLiteral("searchresultslvfg_downloading"), QColor(1, 1, 1)},
                    {QStringLiteral("searchresultslvfg_downloadstopped"), QColor(2, 2, 2)}});
    QCOMPARE(knownTypeColor(2, true), QColor(2, 2, 2));
    skin.setColors({{QStringLiteral("searchresultslvfg_downloadstopped"), QColor(2, 2, 2)},
                    {QStringLiteral("fg_downloadstopped"), QColor(3, 3, 3)}});
    QCOMPARE(knownTypeColor(2, true), QColor(3, 3, 3));
    QCOMPARE(knownTypeColor(2, false) == QColor(3, 3, 3), false);
    // only a download can be stopped
    QCOMPARE(knownTypeColor(1, true) == QColor(3, 3, 3), false);
}

void tst_ToolbarSkin::icons_resourceNames()
{
    // MFC [Icons] keys are emule.rc names, not file names; ours use the file name
    QCOMPARE(SkinIcons::resourceNames(QStringLiteral(":/icons/Kad.ico")), QStringList{u"KADEMLIA"_s});
    QCOMPARE(SkinIcons::resourceNames(QStringLiteral(":/icons/FileRename.ico")),
             (QStringList{u"FILERENAME"_s, u"RENAME"_s}));
    QCOMPARE(SkinIcons::resourceNames(QStringLiteral(":/icons/Rename.ico")), QStringList{u"RENAMEITEM"_s});
    QCOMPARE(SkinIcons::resourceNames(QStringLiteral(":/smileys/Smiley_Smile.ico")),
             QStringList{u"SMILEY_SMILE"_s});
    QCOMPARE(SkinIcons::resourceNames(QStringLiteral(":/icons/Usenet.ico")), QStringList{u"Usenet"_s});
}

void tst_ToolbarSkin::icons_profileFiles()
{
    QTemporaryDir dir;
    solidPng(dir, u"kad.png"_s, Qt::red);
    const QString ini = dir.filePath(u"t.eMuleSkin.ini"_s);
    writeRaw(ini, "[Icons]\r\nKademlia=kad.png\r\nServer=gone.ico\r\nUsenet=%NO_SUCH_VAR%\\x.png\r\n");
    SkinEngine skin;
    skin.loadProfile(ini);
    const auto files = skin.iconFiles();
    QCOMPARE(files.size(), 1);   // unreadable entries keep the built-in icon
    QCOMPARE(files.value(u"kademlia"_s), dir.filePath(u"kad.png"_s));
}

void tst_ToolbarSkin::icons_followProfile()
{
    QTemporaryDir dir;
    const QString red = solidPng(dir, u"red.png"_s, Qt::red);
    const QString blue = solidPng(dir, u"blue.png"_s, Qt::blue);
    auto& skin = SkinIcons::instance();

    // made before the profile: resolves when painted (no :/icons in the test binary)
    const QIcon kad = skinIcon(u":/icons/Kad.ico"_s);
    const QIcon usenet = skinIcon(u":/icons/Usenet.ico"_s);
    QVERIFY(kad.isNull());

    QVERIFY(skin.setFiles({{u"kademlia"_s, red}, {u"usenet"_s, blue}}));
    QVERIFY(!skin.setFiles({{u"kademlia"_s, red}, {u"usenet"_s, blue}}));   // no change
    QCOMPARE(centre(kad), QColor(Qt::red));
    QCOMPARE(centre(usenet), QColor(Qt::blue));
    QCOMPARE(skin.file(u":/icons/Kad.ico"_s), red);

    skin.setFiles({{u"kademlia"_s, blue}});
    QCOMPARE(centre(kad), QColor(Qt::blue));
    QVERIFY(usenet.isNull());

    skin.setFiles({});
    QVERIFY(kad.isNull());
    QVERIFY(skin.file(u":/icons/Kad.ico"_s).isEmpty());
}

void tst_ToolbarSkin::icons_compositesRebuilt()
{
    // the friend badge is baked into a cached pixmap: a profile switch rebuilds it
    QTemporaryDir dir;
    const QString red = solidPng(dir, u"red.png"_s, Qt::red);
    const QString blue = solidPng(dir, u"blue.png"_s, Qt::blue);
    const QString green = solidPng(dir, u"green.png"_s, Qt::green);
    auto& skin = SkinIcons::instance();
    constexpr int kEmule = 0;

    skin.setFiles({{u"clientcompatible"_s, red}, {u"friend"_s, green}});
    QImage img = clientSoftwareIcon(kEmule, false, true).pixmap(16, 16).toImage();
    QCOMPARE(img.pixelColor(2, 2), QColor(Qt::red));
    QCOMPARE(img.pixelColor(12, 12), QColor(Qt::green));

    skin.setFiles({{u"clientcompatible"_s, blue}, {u"friend"_s, green}});
    img = clientSoftwareIcon(kEmule, false, true).pixmap(16, 16).toImage();
    QCOMPARE(img.pixelColor(2, 2), QColor(Qt::blue));
}

void tst_ToolbarSkin::icons_labelAndSmiley()
{
    QTemporaryDir dir;
    const QString red = solidPng(dir, u"red.png"_s, Qt::red);
    auto& skin = SkinIcons::instance();

    QLabel label;
    setSkinPixmap(&label, u":/icons/Kad.ico"_s);
    QVERIFY(label.pixmap().isNull());
    skin.setFiles({{u"kademlia"_s, red}});
    QCOMPARE(label.pixmap().toImage().pixelColor(8, 8), QColor(Qt::red));

    // chat HTML: the skin file instead of the built-in smiley
    QVERIFY(Smileys::render(u":-)"_s).contains(u"qrc:/smileys/Smiley_Smile.ico"_s));
    skin.setFiles({{u"smiley_smile"_s, red}});
    const QString html = Smileys::render(u":-)"_s);
    QVERIFY2(html.contains(QUrl::fromLocalFile(red).toString()), qPrintable(html));
}

QTEST_MAIN(tst_ToolbarSkin)
#include "tst_ToolbarSkin.moc"
