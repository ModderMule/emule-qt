/// @file tst_ClientAnalyzer.cpp
/// @brief Tests for client/ClientAnalyzer — behaviour score, sticky flags, clientanalyzer.met.

#include "TestHelpers.h"
#include "client/ClientAnalyzer.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <array>
#include <ctime>

using namespace eMule;

namespace {

std::array<uint8, 16> hashOf(uint8 seed)
{
    std::array<uint8, 16> h{};
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = static_cast<uint8>(seed * 31 + i);
    return h;
}

// Real clock: record() and addCorruptSender() stamp std::time
const int64 kNow = static_cast<int64>(std::time(nullptr));

} // namespace

class tst_ClientAnalyzer : public QObject {
    Q_OBJECT

private slots:
    void neutralRecordScoresOne();
    void transferBalanceOnlyWhenItHasSomething();
    void noAgeBonus();
    void fastReaskNeedsThreeAsks();
    void xsExploiter();
    void flagsAreHeldTwoHours();
    void scoreIsClamped();
    void reaskTimePerSoftware();
    void serializeRoundTrip();
    void neutralRecordIsNotSaved();
    void expiredRecordsDropped();
};

void tst_ClientAnalyzer::neutralRecordScoresOne()
{
    AnalyzerRecord r;
    QCOMPARE(r.score(true, FILEREASKTIME), 1.0f);
    QCOMPARE(r.score(false, FILEREASKTIME), 1.0f);
    QVERIFY(r.reasons(true, FILEREASKTIME).isEmpty());
}

void tst_ClientAnalyzer::transferBalanceOnlyWhenItHasSomething()
{
    AnalyzerRecord r;
    r.addUploaded(10 * PARTSIZE, false);   // took 10 chunks, gave nothing
    QCOMPARE(r.score(false, FILEREASKTIME), 1.0f);   // nothing for us: even score
    QCOMPARE(r.score(true, FILEREASKTIME), 0.01f);   // 1 - 10 × 0.1, clamped

    AnalyzerRecord giver;
    giver.addDownloaded(4 * PARTSIZE, false, 100);   // full-file data counts 1.5×: 6 chunks
    QVERIFY(qFuzzyCompare(giver.score(true, FILEREASKTIME), 1.6f));

    AnalyzerRecord rare;
    rare.addDownloaded(2 * PARTSIZE, true, 5);       // partial 2 + rare 2 = 4 chunks
    QVERIFY(qFuzzyCompare(rare.score(true, FILEREASKTIME), 1.4f));
}

void tst_ClientAnalyzer::noAgeBonus()
{
    // ClientAnalyzer's +0.025 per week of a kept hash is dropped on purpose
    AnalyzerRecord r;
    r.lastSeen = static_cast<uint32>(kNow);
    QCOMPARE(r.score(true, FILEREASKTIME), 1.0f);
}

void tst_ClientAnalyzer::fastReaskNeedsThreeAsks()
{
    AnalyzerRecord r;
    QVERIFY(!r.noteReask(MIN2MS(5), FILEREASKTIME));
    QVERIFY(!r.noteReask(MIN2MS(5), FILEREASKTIME));
    QVERIFY(r.noteReask(MIN2MS(5), FILEREASKTIME));   // third ask: avg 5 min + 5 < 29
    QVERIFY(r.isFastAsker(FILEREASKTIME));
    QVERIFY(qFuzzyCompare(r.score(false, FILEREASKTIME), 1.0f - 3 * 0.025f));
    // Within the buffer is no fast asker
    AnalyzerRecord ok;
    for (int i = 0; i < 5; ++i)
        ok.noteReask(MIN2MS(25), FILEREASKTIME);
    QVERIFY(!ok.isFastAsker(FILEREASKTIME));
}

void tst_ClientAnalyzer::xsExploiter()
{
    AnalyzerRecord r;
    bool reached = false;
    for (int i = 0; i < 11; ++i)
        reached = r.incXsAsks() || reached;
    QVERIFY(reached);
    QVERIFY(r.isXsExploiter());
    QVERIFY(qFuzzyCompare(r.score(false, FILEREASKTIME), 0.5f));
    for (int i = 0; i < 3; ++i)
        r.incXsAnswers();
    QVERIFY(r.incXsAnswers());   // 11 asks, 4 answers: 11 ≤ 12
    QVERIFY(!r.isXsExploiter());
}

void tst_ClientAnalyzer::flagsAreHeldTwoHours()
{
    AnalyzerRecord r;
    QVERIFY(r.setFlag(AnalyzerRecord::kFlagDlp, kNow));
    QVERIFY(!r.setFlag(AnalyzerRecord::kFlagDlp, kNow + 10));   // already set, hold restarts
    QVERIFY(qFuzzyCompare(r.score(false, FILEREASKTIME), 0.7f));
    QVERIFY(!r.clearFlag(AnalyzerRecord::kFlagDlp, kNow + 3600));
    QVERIFY(r.flags & AnalyzerRecord::kFlagDlp);
    QVERIFY(r.clearFlag(AnalyzerRecord::kFlagDlp, kNow + 10 + AnalyzerRecord::kBadFlagHoldS));
    QCOMPARE(r.flags, 0u);
}

void tst_ClientAnalyzer::scoreIsClamped()
{
    AnalyzerRecord r;
    r.addDownloaded(1000 * PARTSIZE, false, 100);
    QCOMPARE(r.score(true, FILEREASKTIME), 10.0f);
    r.setFlag(AnalyzerRecord::kFlagFileFaker, kNow);
    r.spams = 100;
    QCOMPARE(r.score(false, FILEREASKTIME), 0.01f);
}

void tst_ClientAnalyzer::reaskTimePerSoftware()
{
    QCOMPARE(ClientAnalyzer::reaskTimeFor(ClientSoftware::eMule), static_cast<uint32>(FILEREASKTIME));
    QCOMPARE(ClientAnalyzer::reaskTimeFor(ClientSoftware::MLDonkey), static_cast<uint32>(MIN_REQUESTTIME));
    QCOMPARE(ClientAnalyzer::reaskTimeFor(ClientSoftware::aMule), static_cast<uint32>(MIN_REQUESTTIME));
}

void tst_ClientAnalyzer::serializeRoundTrip()
{
    ClientAnalyzer ca;
    const auto h1 = hashOf(1);
    const auto h2 = hashOf(2);
    AnalyzerRecord& r = ca.record(h1.data());
    r.lastSeen = static_cast<uint32>(kNow);
    r.addUploaded(3 * PARTSIZE, true);
    r.addDownloaded(5 * PARTSIZE, false, 3);
    r.setFlag(AnalyzerRecord::kFlagUdpFnfFaker, kNow);
    r.spams = 2;
    r.badDlSessions = 4;
    ca.addCorruptSender(h2.data());

    ClientAnalyzer back;
    QVERIFY(back.deserialize(ca.serialize(kNow), kNow));
    QCOMPARE(back.count(), std::size_t{1});
    const AnalyzerRecord* b = back.find(h1.data());
    QVERIFY(b);
    QCOMPARE(b->sent, r.sent);
    QCOMPARE(b->sentPartial, r.sentPartial);
    QCOMPARE(b->got, r.got);
    QCOMPARE(b->gotRare, r.gotRare);
    QCOMPARE(b->flags, r.flags);
    QCOMPARE(b->badSince, r.badSince);
    QCOMPARE(b->spams, 2u);
    QCOMPARE(b->badDlSessions, uint8{4});
    QCOMPARE(back.corruptSenderCount(), std::size_t{1});
}

void tst_ClientAnalyzer::neutralRecordIsNotSaved()
{
    ClientAnalyzer ca;
    const auto h = hashOf(3);
    AnalyzerRecord& r = ca.record(h.data());
    r.lastSeen = static_cast<uint32>(kNow);
    r.addUploaded(PARTSIZE / 2, false);   // under a chunk: nothing to remember

    ClientAnalyzer back;
    QVERIFY(back.deserialize(ca.serialize(kNow), kNow));
    QCOMPARE(back.count(), std::size_t{0});
}

void tst_ClientAnalyzer::expiredRecordsDropped()
{
    ClientAnalyzer ca;
    const auto h = hashOf(4);
    AnalyzerRecord& r = ca.record(h.data());
    r.lastSeen = static_cast<uint32>(kNow - ClientAnalyzer::kExpireS - 1);
    r.spams = 5;

    ClientAnalyzer back;
    QVERIFY(back.deserialize(ca.serialize(kNow), kNow));
    QCOMPARE(back.count(), std::size_t{0});
}

QTEST_MAIN(tst_ClientAnalyzer)
#include "tst_ClientAnalyzer.moc"
