/// @file tst_ConnectionPresets.cpp
/// @brief First start wizard: line type -> capacity and limits.

#include "dialogs/ConnectionPresets.h"

#include <QTest>

using namespace eMule;

class tst_ConnectionPresets : public QObject {
    Q_OBJECT

private slots:
    void mbitConversion()
    {
        // decimal Mbit/s -> KiB/s
        QCOMPARE(mbitToKiB(1), 122u);
        QCOMPARE(mbitToKiB(100), 12207u);
        QCOMPARE(mbitToKiB(1000), 122070u);
        QCOMPARE(mbitToKiB(0), 0u);
        QCOMPARE(mbitToKiB(-5), 0u);
        // and back, close enough to show the line a capacity came from
        QVERIFY(std::abs(kiBToMbit(mbitToKiB(250)) - 250.0) < 0.01);
    }

    void limitsFollowTheMfcRule()
    {
        // VDSL 100: upload 80 % of the line, download 90 %
        const BandwidthSettings s = limitsForLine(100, 40);
        QCOMPARE(s.capDown, 12207u);
        QCOMPARE(s.capUp, 4883u);
        QCOMPARE(s.maxDown, 10986u);
        QCOMPARE(s.maxUp, 3906u);
    }

    void limitsNeverReachZero()
    {
        // 0 would mean "unlimited" — the opposite of what a tiny line asked for
        const BandwidthSettings s = limitsForLine(0.001, 0.001);
        QVERIFY(s.capDown >= 1 && s.capUp >= 1);
        QVERIFY(s.maxDown >= 1 && s.maxUp >= 1);
    }

    void gigabitDoesNotOverflow()
    {
        const BandwidthSettings s = limitsForLine(100000, 100000);
        QCOMPARE(s.capDown, 12207031u);
        QCOMPARE(s.maxDown, 10986327u);
        QCOMPARE(s.maxUp, 9765624u);
    }

    void everyPresetIsUsable()
    {
        for (const ConnectionPreset& preset : kConnectionPresets) {
            const BandwidthSettings s = limitsForLine(preset.downMbit, preset.upMbit);
            QVERIFY2(s.maxUp > 0 && s.maxUp <= s.capUp, preset.name);
            QVERIFY2(s.maxDown > 0 && s.maxDown <= s.capDown, preset.name);
            // the core clamps the upload limit to the capacity: both must fit its range
            QVERIFY2(preset.upMbit <= preset.downMbit, preset.name);
        }
    }

    void recommendedIsWhatAFreshInstallRunsWith()
    {
        Preferences prefs;
        const BandwidthSettings r = recommendedBandwidth();
        QCOMPARE(r.capDown, prefs.maxGraphDownloadRate());
        QCOMPARE(r.capUp, prefs.maxGraphUploadRate());
        QCOMPARE(r.maxDown, prefs.maxDownload());
        QCOMPARE(r.maxUp, prefs.maxUpload());
        QCOMPARE(r.maxDown, 0u);   // unlimited
        // and it is not the old shipped pair the wizard treats as "never tuned"
        QVERIFY(!(r == legacyDefaultBandwidth()));
    }
};

QTEST_GUILESS_MAIN(tst_ConnectionPresets)
#include "tst_ConnectionPresets.moc"
