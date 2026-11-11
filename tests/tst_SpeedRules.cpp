/// @file tst_SpeedRules.cpp
/// @brief UL:DL ratio (MFC), linked sliders, throttle floors; the core's stored-pair clamp.

#include "TestHelpers.h"

#include "prefs/Preferences.h"
#include "prefs/SpeedRules.h"

#include <QTest>

using namespace eMule;
using namespace eMule::SpeedRules;

class tst_SpeedRules : public QObject {
    Q_OBJECT

private slots:
    void ratioCoef_mfcTiers();
    void clampDownForUp_mfcCheckUp();
    void raiseUpForDown_mfcCheckDown();
    void raiseUpForDown_alwaysCarriesTheDownload();
    void throttle_halvesDownToTheFloors();
    void throttle_capacityBelowTheFloors();
    void enforceSpeedRatio_holdsTheStoredPair();
};

void tst_SpeedRules::ratioCoef_mfcTiers()
{
    // srchybrid/Preferences.cpp:693-714: 3x under 4, 4x under 10, 5x under 20
    QCOMPARE(ratioCoef(0), 0u);
    QCOMPARE(ratioCoef(1), 3u);
    QCOMPARE(ratioCoef(3), 3u);
    QCOMPARE(ratioCoef(4), 4u);
    QCOMPARE(ratioCoef(9), 4u);
    QCOMPARE(ratioCoef(10), 5u);
    QCOMPARE(ratioCoef(19), 5u);
    QCOMPARE(ratioCoef(20), 0u);
    QCOMPARE(ratioCoefBytes(10 * 1024 - 1), uint64{4});
}

void tst_SpeedRules::clampDownForUp_mfcCheckUp()
{
    QCOMPARE(clampDownForUp(3, 500), 9u);
    QCOMPARE(clampDownForUp(5, 500), 20u);
    QCOMPARE(clampDownForUp(10, 500), 50u);
    QCOMPARE(clampDownForUp(10, 30), 30u);       // already below
    QCOMPARE(clampDownForUp(20, 500), 500u);     // free upload
    QCOMPARE(clampDownForUp(0, 500), 500u);      // unlimited upload
    QCOMPARE(clampDownForUp(5, 0), 20u);         // unlimited download capped all the same
    QCOMPARE(clampDownForUp(50, 900, 600), 600u);   // capacity
}

void tst_SpeedRules::raiseUpForDown_mfcCheckDown()
{
    QCOMPARE(raiseUpForDown(1, 12), 4u);    // (12+2)/3
    QCOMPARE(raiseUpForDown(5, 40), 10u);   // (40+3)/4
    QCOMPARE(raiseUpForDown(5, 60), 12u);   // (60+4)/5
    QCOMPARE(raiseUpForDown(30, 900), 30u); // free upload
    QCOMPARE(raiseUpForDown(5, 60, 8), 8u); // capacity
    QCOMPARE(raiseUpForDown(0, 900), 0u);   // unlimited upload
}

void tst_SpeedRules::raiseUpForDown_alwaysCarriesTheDownload()
{
    for (uint32 down = 1; down <= 200; ++down) {
        for (uint32 up = 1; up <= 25; ++up) {
            const uint32 raised = raiseUpForDown(up, down);
            QVERIFY2(raised >= up, qPrintable(QStringLiteral("%1/%2").arg(up).arg(down)));
            QVERIFY2(clampDownForUp(raised, down) == down,
                     qPrintable(QStringLiteral("up %1 down %2 -> %3").arg(up).arg(down).arg(raised)));
        }
    }
}

void tst_SpeedRules::throttle_halvesDownToTheFloors()
{
    constexpr uint32 capUp = 1000;
    constexpr uint32 capDown = 10000;
    Limits cur{capUp, capDown};
    int clicks = 0;
    while (canThrottle(cur, capUp, capDown)) {
        const Limits next = throttleStep(cur, capUp, capDown);
        QVERIFY(next.up <= cur.up && next.down <= cur.down);
        QVERIFY(next.up >= kThrottleFloorUp && next.down >= kThrottleFloorDown);
        QCOMPARE(clampDownForUp(next.up, next.down), next.down);   // ratio kept
        cur = next;
        QVERIFY(++clicks < 20);
    }
    QCOMPARE(throttleStep({capUp, capDown}, capUp, capDown), (Limits{500, 5000}));
    // stops once either side reached its floor
    QVERIFY(cur.up == kThrottleFloorUp || cur.down == kThrottleFloorDown);
    QVERIFY(!canThrottle({10, 400}, capUp, capDown));
    QVERIFY(!canThrottle({100, 40}, capUp, capDown));
    QVERIFY(canThrottle({11, 41}, capUp, capDown));
    // unlimited halves from the capacity
    QCOMPARE(throttleStep({0, 0}, capUp, capDown), (Limits{500, 5000}));
}

void tst_SpeedRules::throttle_capacityBelowTheFloors()
{
    // 5 up / 30 down line: floors follow the capacity, the ratio wins over the DL floor
    QCOMPARE(floorUp(5), 5u);
    QCOMPARE(floorDown(5, 30), 20u);   // 4x of 5
    QCOMPARE(floorDown(100, 30), 30u);
    QCOMPARE(floorDown(100, 1000), 40u);
    const Limits l = throttleStep({5, 20}, 5, 30);
    QCOMPARE(l, (Limits{5, 20}));
    QVERIFY(!canThrottle({5, 20}, 5, 30));
}

void tst_SpeedRules::enforceSpeedRatio_holdsTheStoredPair()
{
    Preferences prefs;
    prefs.setMaxGraphUploadRate(100);
    prefs.setMaxGraphDownloadRate(1000);

    // upload moved: download pulled down
    prefs.setMaxUpload(5);
    prefs.setMaxDownload(500);
    QVERIFY(prefs.enforceSpeedRatio(false));
    QCOMPARE(prefs.configuredMaxUpload(), 5u);
    QCOMPARE(prefs.configuredMaxDownload(), 20u);

    // download moved: upload pushed up
    prefs.setMaxDownload(60);
    QVERIFY(prefs.enforceSpeedRatio(true));
    QCOMPARE(prefs.configuredMaxUpload(), 12u);
    QCOMPARE(prefs.configuredMaxDownload(), 60u);

    // within the ratio, or unlimited upload: untouched
    QVERIFY(!prefs.enforceSpeedRatio(false));
    prefs.setMaxUpload(0);
    prefs.setMaxDownload(0);
    QVERIFY(!prefs.enforceSpeedRatio(false));
    QCOMPARE(prefs.configuredMaxDownload(), 0u);
}

QTEST_GUILESS_MAIN(tst_SpeedRules)
#include "tst_SpeedRules.moc"
