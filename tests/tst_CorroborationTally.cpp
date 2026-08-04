/// @file tst_CorroborationTally.cpp
/// @brief Tests for utils/CorroborationTally.h — distinct-voter agreement with a window.

#include "TestHelpers.h"

#include "utils/CorroborationTally.h"

#include <QTest>

#include <string>

using namespace eMule;

namespace {

using Tally = CorroborationTally<std::string, std::string>;

constexpr std::int64_t kWindow = 300 * 1000;   // 5 minutes, as the IPv6 tier uses

} // namespace

class tst_CorroborationTally : public QObject {
    Q_OBJECT

private slots:
    void adoptsOnlyAtThreshold();
    void repeatFromSameVoterIsNotASecondVote();
    void changingVoteRetractsTheOldOne();
    void bestCountReportedBelowThreshold();
    void expiredVotesDropTheCandidate();
    void stickyKeepsAdoptedAfterVotesExpire();
    void stickyReplacesOnlyOnANewWinner();
    void warnLatchFiresOncePerCandidate();
    void eraseCandidatesIfDropsWholeCandidates();
    void clearForgetsEverything();
};

void tst_CorroborationTally::adoptsOnlyAtThreshold()
{
    Tally t;                                   // Reelect by default
    QVERIFY(!t.adopted().has_value());

    t.record("1.2.3.4", "voterA", 0);
    QCOMPARE(t.recompute(0, 2, kWindow).adoptedChanged, false);
    QVERIFY(!t.adopted().has_value());

    t.record("1.2.3.4", "voterB", 0);
    const auto out = t.recompute(0, 2, kWindow);
    QCOMPARE(out.adoptedChanged, true);
    QCOMPARE(out.bestCount, std::size_t{2});
    QCOMPARE(*t.adopted(), std::string("1.2.3.4"));
}

void tst_CorroborationTally::repeatFromSameVoterIsNotASecondVote()
{
    Tally t;
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterA", 100);        // refreshes, does not add
    t.record("1.2.3.4", "voterA", 200);

    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{1});
    QCOMPARE(t.recompute(200, 2, kWindow).bestCount, std::size_t{1});
    QVERIFY(!t.adopted().has_value());
}

void tst_CorroborationTally::changingVoteRetractsTheOldOne()
{
    // One host, one vote — including across candidates. A voter that names a new value
    // has changed its mind, and must not go on backing the old one.
    Tally t;
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{2});

    t.record("5.6.7.8", "voterA", 10);
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{1});
    QCOMPARE(t.voterCount("5.6.7.8"), std::size_t{1});

    // With the retraction the second candidate takes a clear lead rather than tying and
    // losing to map order, so a real address change is adopted as soon as it is agreed.
    t.record("5.6.7.8", "voterB", 20);
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{0});
    QVERIFY(t.recompute(20, 2, kWindow).adoptedChanged);
    QCOMPARE(*t.adopted(), std::string("5.6.7.8"));
}

void tst_CorroborationTally::bestCountReportedBelowThreshold()
{
    // The count is what callers log ("2/5 agree"), so it must be reported whether or
    // not the leader cleared the bar.
    Tally t;
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    t.record("5.6.7.8", "voterC", 0);

    const auto out = t.recompute(0, 5, kWindow);
    QCOMPARE(out.bestCount, std::size_t{2});
    QCOMPARE(out.adoptedChanged, false);
    QVERIFY(!t.adopted().has_value());
}

void tst_CorroborationTally::expiredVotesDropTheCandidate()
{
    Tally t;                                   // Reelect
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    QVERIFY(t.recompute(0, 2, kWindow).adoptedChanged);
    QCOMPARE(*t.adopted(), std::string("1.2.3.4"));

    // Everything is now older than the window: votes go, and in Reelect mode so does
    // the adopted value.
    const auto out = t.recompute(kWindow + 1, 2, kWindow);
    QCOMPARE(out.adoptedChanged, true);
    QCOMPARE(out.bestCount, std::size_t{0});
    QVERIFY(!t.adopted().has_value());
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{0});
}

void tst_CorroborationTally::stickyKeepsAdoptedAfterVotesExpire()
{
    Tally t(CorroborationMode::Sticky);
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    QVERIFY(t.recompute(0, 2, kWindow).adoptedChanged);

    // Same expiry as above — the votes are gone, but the conclusion they supported is
    // kept, because in this mode fresh evidence is not expected to keep arriving.
    const auto out = t.recompute(kWindow + 1, 2, kWindow);
    QCOMPARE(out.adoptedChanged, false);
    QCOMPARE(out.bestCount, std::size_t{0});
    QCOMPARE(*t.adopted(), std::string("1.2.3.4"));
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{0});   // held by value, not by entry
}

void tst_CorroborationTally::stickyReplacesOnlyOnANewWinner()
{
    Tally t(CorroborationMode::Sticky);
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    t.recompute(0, 2, kWindow);
    QCOMPARE(*t.adopted(), std::string("1.2.3.4"));

    // One vote for a rival is not enough to unseat the incumbent.
    t.record("5.6.7.8", "voterA", 10);
    QCOMPARE(t.recompute(10, 2, kWindow).adoptedChanged, false);
    QCOMPARE(*t.adopted(), std::string("1.2.3.4"));

    // A rival that clears the threshold does replace it.
    t.record("5.6.7.8", "voterC", 20);
    QCOMPARE(t.recompute(20, 2, kWindow).adoptedChanged, true);
    QCOMPARE(*t.adopted(), std::string("5.6.7.8"));
}

void tst_CorroborationTally::warnLatchFiresOncePerCandidate()
{
    Tally t;
    QVERIFY(!t.markWarnedOnce("1.2.3.4"));     // unknown candidate never latches

    t.record("1.2.3.4", "voterA", 0);
    QVERIFY(t.markWarnedOnce("1.2.3.4"));
    QVERIFY(!t.markWarnedOnce("1.2.3.4"));

    t.record("5.6.7.8", "voterA", 0);
    QVERIFY(t.markWarnedOnce("5.6.7.8"));      // per candidate, not global

    // The latch dies with the entry, so a candidate that comes back can warn again.
    t.recompute(kWindow + 1, 2, kWindow);
    t.record("1.2.3.4", "voterA", kWindow + 1);
    QVERIFY(t.markWarnedOnce("1.2.3.4"));
}

void tst_CorroborationTally::eraseCandidatesIfDropsWholeCandidates()
{
    Tally t;
    t.record("keep", "voterA", 0);
    t.record("keep", "voterB", 0);
    t.record("drop", "voterC", 0);   // a distinct voter — voterA would retract its own

    std::size_t seenVotes = 0;
    const std::size_t dropped = t.eraseCandidatesIf([&](const std::string& c, std::size_t votes) {
        if (c == "drop")
            seenVotes = votes;                 // the predicate is told the vote count
        return c == "drop";
    });

    QCOMPARE(dropped, std::size_t{1});
    QCOMPARE(seenVotes, std::size_t{1});
    QCOMPARE(t.voterCount("drop"), std::size_t{0});
    QCOMPARE(t.voterCount("keep"), std::size_t{2});
}

void tst_CorroborationTally::clearForgetsEverything()
{
    Tally t(CorroborationMode::Sticky);
    t.record("1.2.3.4", "voterA", 0);
    t.record("1.2.3.4", "voterB", 0);
    t.recompute(0, 2, kWindow);
    QVERIFY(t.adopted().has_value());

    t.clear();
    QVERIFY(!t.adopted().has_value());         // sticky does not survive an explicit clear
    QCOMPARE(t.voterCount("1.2.3.4"), std::size_t{0});
}

QTEST_MAIN(tst_CorroborationTally)
#include "tst_CorroborationTally.moc"
