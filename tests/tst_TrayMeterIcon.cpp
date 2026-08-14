/// @file tst_TrayMeterIcon.cpp
/// @brief Tray icon download meter — eMule::TrayMeterIcon.
///
/// The port of MFC's CMeterIcon (srchybrid/MeterIcon.cpp:159-167): the connection-state
/// icon with a bar down its right edge, as tall as the download rate is close to the
/// configured capacity. Two properties carry the feature:
///
///   - the bar says what it means — nothing at idle, half height at half rate, the full
///     edge at capacity, in the colour it was handed;
///   - equal levels render identically, which is what lets MainWindow skip the repaint
///     at 1 Hz instead of pushing a new icon across DBus every second.

#include "controls/TrayMeterIcon.h"

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTest>

using namespace eMule;

namespace {

constexpr int kSize = 64;

/// A plain filled icon, so any pixel the meter changes is unambiguous.
QIcon makeBaseIcon(const QColor& fill = QColor(0, 0, 255))
{
    QPixmap pm(16, 16);
    pm.fill(fill);
    return QIcon(pm);
}

/// Height in pixels of the coloured bar on the right edge of @p image.
int barHeight(const QImage& image, const QColor& barColor)
{
    const int x = image.width() - 1;
    int height = 0;
    for (int y = image.height() - 1; y >= 0; --y) {
        if (image.pixelColor(x, y) != barColor)
            break;
        ++height;
    }
    return height;
}

} // namespace

class tst_TrayMeterIcon : public QObject {
    Q_OBJECT

private slots:
    void idle_drawsNoBar();
    void capacity_fillsTheWholeEdge();
    void halfRate_drawsHalfABar();
    void barKeepsOffTheLeftOfTheIcon();
    void sameLevel_rendersIdentically();
    void invalidColor_leavesTheIconAlone();
};

void tst_TrayMeterIcon::idle_drawsNoBar()
{
    const QColor base(0, 0, 255);
    const QImage image =
        TrayMeterIcon::render(makeBaseIcon(base), 0, Qt::white, kSize).toImage();

    QCOMPARE(TrayMeterIcon::levelForPercent(0), 0);
    QCOMPARE(barHeight(image, Qt::white), 0);
    // The base icon is still all that is there.
    QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), base);
}

void tst_TrayMeterIcon::capacity_fillsTheWholeEdge()
{
    const QImage image =
        TrayMeterIcon::render(makeBaseIcon(), 100, Qt::white, kSize).toImage();

    QCOMPARE(TrayMeterIcon::levelForPercent(100), TrayMeterIcon::kLevels);
    QCOMPARE(barHeight(image, Qt::white), kSize);
}

void tst_TrayMeterIcon::halfRate_drawsHalfABar()
{
    const QImage image =
        TrayMeterIcon::render(makeBaseIcon(), 50, Qt::white, kSize).toImage();

    // 50% lands on level 8 of 16, which is the bar's midpoint to the pixel.
    QCOMPARE(TrayMeterIcon::levelForPercent(50), TrayMeterIcon::kLevels / 2);
    QCOMPARE(barHeight(image, Qt::white), kSize / 2);
}

void tst_TrayMeterIcon::barKeepsOffTheLeftOfTheIcon()
{
    const QColor base(0, 0, 255);
    const QImage image =
        TrayMeterIcon::render(makeBaseIcon(base), 100, Qt::white, kSize).toImage();

    // MFC's bar is the rightmost eighth; the icon has to stay readable beside it.
    QCOMPARE(image.pixelColor(0, kSize - 1), base);
    QCOMPARE(image.pixelColor(kSize / 2, kSize - 1), base);
}

void tst_TrayMeterIcon::sameLevel_rendersIdentically()
{
    // 50 and 53 share a level, so MainWindow's cache may skip the second one — which is
    // only safe while the two would have drawn the same picture.
    QCOMPARE(TrayMeterIcon::levelForPercent(50), TrayMeterIcon::levelForPercent(53));
    QCOMPARE(TrayMeterIcon::render(makeBaseIcon(), 50, Qt::white, kSize).toImage(),
             TrayMeterIcon::render(makeBaseIcon(), 53, Qt::white, kSize).toImage());

    // ...and a level apart really does look different.
    QVERIFY(TrayMeterIcon::render(makeBaseIcon(), 50, Qt::white, kSize).toImage()
            != TrayMeterIcon::render(makeBaseIcon(), 60, Qt::white, kSize).toImage());
}

void tst_TrayMeterIcon::invalidColor_leavesTheIconAlone()
{
    const QColor base(0, 0, 255);
    const QImage plain =
        TrayMeterIcon::render(makeBaseIcon(base), 0, QColor(), kSize).toImage();
    const QImage busy =
        TrayMeterIcon::render(makeBaseIcon(base), 100, QColor(), kSize).toImage();

    QCOMPARE(plain, busy);
}

QTEST_MAIN(tst_TrayMeterIcon)
#include "tst_TrayMeterIcon.moc"
