/// @file tst_StatsHistory.cpp
/// @brief Tests for stats/StatsHistory — sample cadence, ring eviction, seq replay.
///
/// theApp is empty in these tests, so every sample carries zeros. That is exactly
/// what makes the bookkeeping testable in isolation: what matters here is which
/// samples exist, what they are numbered, and which ones a viewer gets back.

#include "TestHelpers.h"
#include "prefs/Preferences.h"
#include "stats/StatsHistory.h"

#include <QTest>

using namespace eMule;

namespace {

/// Restores graphsUpdateSec, which these tests drive directly.
class ScopedGraphInterval {
public:
    explicit ScopedGraphInterval(uint32 secs)
        : m_saved(thePrefs.graphsUpdateSec())
    {
        thePrefs.setGraphsUpdateSec(secs);
    }
    ~ScopedGraphInterval() { thePrefs.setGraphsUpdateSec(m_saved); }

    ScopedGraphInterval(const ScopedGraphInterval&) = delete;
    ScopedGraphInterval& operator=(const ScopedGraphInterval&) = delete;

private:
    uint32 m_saved;
};

/// Feed @p count seconds of samples starting at @p from.
void sampleSeconds(StatsHistory& hist, uint32 from, uint32 count)
{
    for (uint32 i = 0; i < count; ++i)
        hist.sample(from + i);
}

} // namespace

class tst_StatsHistory : public QObject {
    Q_OBJECT

private slots:
    void speed_seqStartsAtOneAndIsMonotonic();
    void speed_sinceReturnsOnlyNewer();
    void speed_ringEvictsOldestAndAdvancesOldestSeq();
    void stats_sinceReturnsOnlyNewer();
    void stats_firstSampleIsImmediate();
    void stats_intervalSpacesSamples();
    void stats_zeroIntervalDisablesStatsButNotSpeed();
    void stats_ringEvictsAtCapacity();
    void reset_clearsBothAndChangesEpoch();
    void fieldsAreCarriedThroughSince();
};

void tst_StatsHistory::speed_seqStartsAtOneAndIsMonotonic()
{
    StatsHistory hist;
    QCOMPARE(hist.lastSpeedSeq(), 0u);
    QCOMPARE(hist.oldestSpeedSeq(), 0u);

    sampleSeconds(hist, 1000, 3);

    QCOMPARE(hist.lastSpeedSeq(), 3u);
    QCOMPARE(hist.oldestSpeedSeq(), 1u);

    const auto all = hist.speedSince(0);
    QCOMPARE(all.size(), size_t{3});
    QCOMPARE(all[0].seq, 1u);
    QCOMPARE(all[1].seq, 2u);
    QCOMPARE(all[2].seq, 3u);
}

void tst_StatsHistory::speed_sinceReturnsOnlyNewer()
{
    StatsHistory hist;
    sampleSeconds(hist, 1000, 5);

    QCOMPARE(hist.speedSince(2).size(), size_t{3});
    QCOMPARE(hist.speedSince(2).front().seq, 3u);

    // A viewer that is already current gets nothing back.
    QVERIFY(hist.speedSince(hist.lastSpeedSeq()).empty());
    QVERIFY(hist.speedSince(999).empty());
}

void tst_StatsHistory::speed_ringEvictsOldestAndAdvancesOldestSeq()
{
    StatsHistory hist;
    const auto cap = static_cast<uint32>(StatsHistory::kSpeedCapacity);
    sampleSeconds(hist, 0, cap + 10);

    QCOMPARE(hist.lastSpeedSeq(), cap + 10);
    QCOMPARE(hist.speedSince(0).size(), StatsHistory::kSpeedCapacity);

    // The first 10 aged out, so oldestSeq is what tells a viewer holding seq 5
    // that it can no longer just append.
    QCOMPARE(hist.oldestSpeedSeq(), 11u);
    QVERIFY(hist.oldestSpeedSeq() > 5 + 1);
}

void tst_StatsHistory::stats_sinceReturnsOnlyNewer()
{
    const ScopedGraphInterval interval(1);
    StatsHistory hist;
    sampleSeconds(hist, 1000, 5);
    QCOMPARE(hist.lastStatsSeq(), 5u);

    const auto after2 = hist.statsSince(2);
    QCOMPARE(after2.size(), size_t{3});
    QCOMPARE(after2.front().seq, 3u);

    // A viewer that is already current gets nothing back — otherwise every poll
    // would re-append the newest sample and the trace would run at N times speed.
    QVERIFY(hist.statsSince(hist.lastStatsSeq()).empty());
    QVERIFY(hist.statsSince(999).empty());
}

void tst_StatsHistory::stats_firstSampleIsImmediate()
{
    const ScopedGraphInterval interval(3);
    StatsHistory hist;

    hist.sample(1000);
    QCOMPARE(hist.lastStatsSeq(), 1u);
    QCOMPARE(hist.oldestStatsSeq(), 1u);
}

void tst_StatsHistory::stats_intervalSpacesSamples()
{
    const ScopedGraphInterval interval(3);
    StatsHistory hist;

    sampleSeconds(hist, 1000, 9);   // t = 1000..1008

    // One at t=1000, then every third second: 1003 and 1006.
    QCOMPARE(hist.lastStatsSeq(), 3u);
    QCOMPARE(hist.lastSpeedSeq(), 9u);

    const auto samples = hist.statsSince(0);
    QCOMPARE(samples.size(), size_t{3});
    QCOMPARE(samples[0].timestamp, 1000u);
    QCOMPARE(samples[1].timestamp, 1003u);
    QCOMPARE(samples[2].timestamp, 1006u);
}

void tst_StatsHistory::stats_zeroIntervalDisablesStatsButNotSpeed()
{
    const ScopedGraphInterval interval(0);
    StatsHistory hist;

    sampleSeconds(hist, 1000, 10);

    QCOMPARE(hist.lastStatsSeq(), 0u);
    QVERIFY(hist.statsSince(0).empty());
    QCOMPARE(hist.lastSpeedSeq(), 10u);   // the toolbar graph keeps running
}

void tst_StatsHistory::stats_ringEvictsAtCapacity()
{
    const ScopedGraphInterval interval(1);
    StatsHistory hist;

    const auto cap = static_cast<uint32>(StatsHistory::kStatsCapacity);
    sampleSeconds(hist, 0, cap + 5);

    QCOMPARE(hist.lastStatsSeq(), cap + 5);
    QCOMPARE(hist.statsSince(0).size(), StatsHistory::kStatsCapacity);
    QCOMPARE(hist.oldestStatsSeq(), 6u);
}

void tst_StatsHistory::reset_clearsBothAndChangesEpoch()
{
    const ScopedGraphInterval interval(1);
    StatsHistory hist;

    sampleSeconds(hist, 1000, 5);
    const uint32 epochBefore = hist.epoch();
    QVERIFY(hist.lastStatsSeq() > 0);

    hist.reset();

    QCOMPARE(hist.lastStatsSeq(), 0u);
    QCOMPARE(hist.lastSpeedSeq(), 0u);
    QCOMPARE(hist.oldestStatsSeq(), 0u);
    QCOMPARE(hist.oldestSpeedSeq(), 0u);
    QVERIFY(hist.statsSince(0).empty());
    QVERIFY(hist.speedSince(0).empty());

    // The renumbering is why the epoch has to move: a viewer sitting on seq 5 would
    // otherwise see nothing new until the sequence climbed past 5 again, and would
    // then splice the new run onto the old trace.
    QVERIFY(hist.epoch() != epochBefore);

    sampleSeconds(hist, 2000, 2);
    QCOMPARE(hist.lastStatsSeq(), 2u);
}

void tst_StatsHistory::fieldsAreCarriedThroughSince()
{
    const ScopedGraphInterval interval(1);
    StatsHistory hist;

    hist.sample(4242);

    const auto samples = hist.statsSince(0);
    QCOMPARE(samples.size(), size_t{1});
    QCOMPARE(samples[0].seq, 1u);
    QCOMPARE(samples[0].timestamp, 4242u);

    // theApp is empty here, so every series reads zero rather than garbage.
    QCOMPARE(samples[0].downCurrent, 0.0f);
    QCOMPARE(samples[0].upCurrent, 0.0f);
    QCOMPARE(samples[0].upNoOverhead, 0.0f);
    QCOMPARE(samples[0].connActive, 0u);
    QCOMPARE(samples[0].downTransferring, 0u);
}

QTEST_MAIN(tst_StatsHistory)
#include "tst_StatsHistory.moc"
