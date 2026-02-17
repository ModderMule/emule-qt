/// @file tst_StatisticsReset.cpp
/// @brief Tests for stats/Statistics — session reset vs. cumulative stats.

#include "TestHelpers.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_StatisticsReset : public QObject {
    Q_OBJECT

private slots:
    void resetDownOverhead_preservesCumulativeCounters();
    void resetUpOverhead_preservesCumulativeCounters();
    void sessionCounters_independentOfOverhead();
    void cumulativeRates_survivesMultipleUpdates();
    void overheadReset_doesNotAffectRates();
    void multipleInits_reloadsFromPrefs();
    void statsAverageMinutes_defaultValue();
    void statsAverageMinutes_roundTrip();
    void connRates_roundTrip();
};

void tst_StatisticsReset::resetDownOverhead_preservesCumulativeCounters()
{
    Statistics stats;

    // Accumulate some overhead
    stats.addDownDataOverheadSourceExchange(100);
    stats.addDownDataOverheadFileRequest(200);
    stats.addDownDataOverheadServer(300);
    stats.addDownDataOverheadKad(400);
    stats.addDownDataOverheadOther(500);

    // Reset only clears the averaging lists and rate, not cumulative counters
    stats.resetDownDatarateOverhead();

    // Rate should be cleared
    QCOMPARE(stats.downDatarateOverhead(), uint64{0});

    // Cumulative byte/packet counters should be preserved
    QCOMPARE(stats.downDataOverheadSourceExchange(), uint64{100});
    QCOMPARE(stats.downDataOverheadFileRequest(), uint64{200});
    QCOMPARE(stats.downDataOverheadServer(), uint64{300});
    QCOMPARE(stats.downDataOverheadKad(), uint64{400});
    QCOMPARE(stats.downDataOverheadOther(), uint64{500});
}

void tst_StatisticsReset::resetUpOverhead_preservesCumulativeCounters()
{
    Statistics stats;

    stats.addUpDataOverheadSourceExchange(150);
    stats.addUpDataOverheadFileRequest(250);
    stats.addUpDataOverheadServer(350);
    stats.addUpDataOverheadKad(450);
    stats.addUpDataOverheadOther(550);

    stats.resetUpDatarateOverhead();

    QCOMPARE(stats.upDatarateOverhead(), uint64{0});

    // Cumulative counters preserved
    QCOMPARE(stats.upDataOverheadSourceExchange(), uint64{150});
    QCOMPARE(stats.upDataOverheadFileRequest(), uint64{250});
    QCOMPARE(stats.upDataOverheadServer(), uint64{350});
    QCOMPARE(stats.upDataOverheadKad(), uint64{450});
    QCOMPARE(stats.upDataOverheadOther(), uint64{550});
}

void tst_StatisticsReset::sessionCounters_independentOfOverhead()
{
    Statistics stats;

    stats.addSessionReceivedBytes(5000);
    stats.addSessionSentBytes(3000);
    stats.addDownDataOverheadServer(100);

    stats.resetDownDatarateOverhead();
    stats.resetUpDatarateOverhead();

    // Session counters unaffected by overhead reset
    QCOMPARE(stats.sessionReceivedBytes(), uint64{5000});
    QCOMPARE(stats.sessionSentBytes(), uint64{3000});
}

void tst_StatisticsReset::cumulativeRates_survivesMultipleUpdates()
{
    Preferences prefs;
    Statistics stats;
    stats.init(prefs);

    // First update sets max rates
    stats.updateConnectionStats(10.0f, 20.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    // Lower rates don't change max
    stats.updateConnectionStats(5.0f, 15.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);

    // Higher rates update max and cumulative max
    stats.updateConnectionStats(30.0f, 40.0f);
    QCOMPARE(stats.maxUp(), 30.0f);
    QCOMPARE(stats.maxDown(), 40.0f);
    QCOMPARE(stats.maxCumUp(), 30.0f);
    QCOMPARE(stats.maxCumDown(), 40.0f);

    // Verify preferences were updated
    QCOMPARE(prefs.connMaxUpRate(), 30.0f);
    QCOMPARE(prefs.connMaxDownRate(), 40.0f);
}

void tst_StatisticsReset::overheadReset_doesNotAffectRates()
{
    Statistics stats;

    stats.updateConnectionStats(10.0f, 20.0f);
    stats.addDownDataOverheadServer(1000);

    stats.resetDownDatarateOverhead();
    stats.resetUpDatarateOverhead();

    // Connection rates unaffected
    QCOMPARE(stats.rateUp(), 10.0f);
    QCOMPARE(stats.rateDown(), 20.0f);
    QCOMPARE(stats.maxUp(), 10.0f);
    QCOMPARE(stats.maxDown(), 20.0f);
}

void tst_StatisticsReset::multipleInits_reloadsFromPrefs()
{
    Preferences prefs;
    prefs.setConnMaxDownRate(50.0f);
    prefs.setConnMaxUpRate(30.0f);

    Statistics stats;
    stats.init(prefs);
    QCOMPARE(stats.maxCumDown(), 50.0f);
    QCOMPARE(stats.maxCumUp(), 30.0f);

    // Change prefs and re-init
    prefs.setConnMaxDownRate(100.0f);
    prefs.setConnMaxUpRate(80.0f);
    stats.init(prefs);
    QCOMPARE(stats.maxCumDown(), 100.0f);
    QCOMPARE(stats.maxCumUp(), 80.0f);
}

void tst_StatisticsReset::statsAverageMinutes_defaultValue()
{
    Preferences prefs;
    QCOMPARE(prefs.statsAverageMinutes(), uint32{5});
}

void tst_StatisticsReset::statsAverageMinutes_roundTrip()
{
    TempDir tmp;
    const QString filePath = tmp.path() + QStringLiteral("/prefs.yaml");

    {
        Preferences prefs;
        prefs.setStatsAverageMinutes(15);
        prefs.saveTo(filePath);
    }
    {
        Preferences prefs;
        prefs.load(filePath);
        QCOMPARE(prefs.statsAverageMinutes(), uint32{15});
    }
}

void tst_StatisticsReset::connRates_roundTrip()
{
    TempDir tmp;
    const QString filePath = tmp.path() + QStringLiteral("/prefs.yaml");

    {
        Preferences prefs;
        prefs.setConnMaxDownRate(100.5f);
        prefs.setConnAvgDownRate(50.25f);
        prefs.setConnMaxAvgDownRate(80.75f);
        prefs.setConnAvgUpRate(40.125f);
        prefs.setConnMaxAvgUpRate(60.5f);
        prefs.setConnMaxUpRate(90.0f);
        prefs.saveTo(filePath);
    }
    {
        Preferences prefs;
        prefs.load(filePath);
        QVERIFY(qFuzzyCompare(prefs.connMaxDownRate(), 100.5f));
        QVERIFY(qFuzzyCompare(prefs.connAvgDownRate(), 50.25f));
        QVERIFY(qFuzzyCompare(prefs.connMaxAvgDownRate(), 80.75f));
        QVERIFY(qFuzzyCompare(prefs.connAvgUpRate(), 40.125f));
        QVERIFY(qFuzzyCompare(prefs.connMaxAvgUpRate(), 60.5f));
        QVERIFY(qFuzzyCompare(prefs.connMaxUpRate(), 90.0f));
    }
}

QTEST_MAIN(tst_StatisticsReset)
#include "tst_StatisticsReset.moc"
