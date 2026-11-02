/// @file tst_KadNetworkWatch.cpp
/// @brief Own network down: offline detection, back online, undoing contact aging.

#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadNetworkWatch.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

namespace {

using Event = NetworkWatch::Event;

// host order, asymmetric so a byte-order slip shows
constexpr uint32 ip(uint32 a, uint32 b, uint32 c, uint32 d)
{
    return (a << 24) | (b << 16) | (c << 8) | d;
}

void askSites(NetworkWatch& w, int n, time_t at)
{
    for (int i = 0; i < n; ++i)
        w.requestSent(ip(81, 20, static_cast<uint32>(10 + i), 7), at);
}

} // namespace

class tst_KadNetworkWatch : public QObject {
    Q_OBJECT

private slots:
    void staysOnlineWhileSilentButNotAsked();
    void goesOfflineAfterSilenceAndFourSites();
    void oneSiteAskedTwiceIsOneSite();
    void answersStillDueDontCount();
    void lanAddressesDontCountOnPublicNetwork();
    void packetEndsOffline();
    void resetStartsNewWindow();
    void evidenceSurvivesReset();
    void voidChecksRestoresType();
    void voidChecksKeepsOlderFailure();
    void recheckMark();
};

void tst_KadNetworkWatch::staysOnlineWhileSilentButNotAsked()
{
    NetworkWatch w(1000);
    QCOMPARE(w.update(1000 + 600), Event::None);
    QVERIFY(!w.isOffline());
}

void tst_KadNetworkWatch::goesOfflineAfterSilenceAndFourSites()
{
    NetworkWatch w(1000);
    askSites(w, 4, 1010);
    QCOMPARE(w.update(1000 + kOfflineSilence - 1), Event::None);
    QCOMPARE(w.update(1000 + kOfflineSilence), Event::WentOffline);
    QVERIFY(w.isOffline());
    QCOMPARE(w.silenceSince(), time_t(1000));
    QCOMPARE(w.update(1000 + kOfflineSilence + 1), Event::None);   // reported once

    NetworkWatch three(1000);
    askSites(three, 3, 1010);
    QCOMPARE(three.update(1000 + 600), Event::None);
}

void tst_KadNetworkWatch::oneSiteAskedTwiceIsOneSite()
{
    NetworkWatch w(1000);
    w.requestSent(ip(81, 20, 10, 7), 1010);
    w.requestSent(ip(81, 20, 10, 99), 1010);   // same /24
    askSites(w, 2, 1010);                       // 81.20.10.x and 81.20.11.x
    w.requestSent(ip(93, 4, 2, 1), 1010);
    QCOMPARE(w.failedSites(1200), 3);
    QCOMPARE(w.update(1200), Event::None);
}

void tst_KadNetworkWatch::answersStillDueDontCount()
{
    NetworkWatch w(1000);
    askSites(w, 4, 1000 + kOfflineSilence - 2);   // asked just now
    QCOMPARE(w.update(1000 + kOfflineSilence), Event::None);
    QCOMPARE(w.update(1000 + kOfflineSilence - 2 + kOfflineReplyWait), Event::WentOffline);
}

void tst_KadNetworkWatch::lanAddressesDontCountOnPublicNetwork()
{
    NetworkWatch w(1000);
    for (uint32 i = 0; i < 4; ++i)
        w.requestSent(ip(192, 168, i, 5), 1010);
    QCOMPARE(w.update(1300), Event::None);

    askSites(w, 4, 1010);
    // a LAN packet is no evidence of the uplink
    QCOMPARE(w.packetReceived(ip(192, 168, 1, 5), 1100), Event::None);
    QCOMPARE(w.update(1300), Event::WentOffline);

    NetworkWatch lan(1000);
    lan.setLanMode(true);
    for (uint32 i = 0; i < 4; ++i)
        lan.requestSent(ip(192, 168, 1, 10 + i), 1010);   // each LAN host its own site
    QCOMPARE(lan.update(1300), Event::WentOffline);
}

void tst_KadNetworkWatch::packetEndsOffline()
{
    NetworkWatch w(1000);
    askSites(w, 4, 1010);
    QCOMPARE(w.update(1200), Event::WentOffline);
    w.requestSent(ip(81, 20, 50, 1), 1250);   // probes while offline don't pile up
    QCOMPARE(w.packetReceived(ip(81, 20, 10, 7), 1300), Event::BackOnline);
    QVERIFY(!w.isOffline());
    QCOMPARE(w.silenceSince(), time_t(1300));
    QCOMPARE(w.failedSites(2000), 0);
    QCOMPARE(w.packetReceived(ip(81, 20, 10, 7), 1301), Event::None);
}

void tst_KadNetworkWatch::resetStartsNewWindow()
{
    NetworkWatch w(1000);
    askSites(w, 4, 1010);
    w.reset(5000);   // resumed from sleep
    QCOMPARE(w.update(5000 + kOfflineSilence), Event::None);
    QVERIFY(!w.isOffline());
}

void tst_KadNetworkWatch::evidenceSurvivesReset()
{
    NetworkWatch w(1000);
    QVERIFY(!w.hasEvidence());
    w.packetReceived(ip(192, 168, 1, 5), 1001);   // LAN on a public network: no evidence
    QVERIFY(!w.hasEvidence());
    w.packetReceived(ip(81, 20, 10, 7), 1002);
    QVERIFY(w.hasEvidence());
    w.reset(5000);   // sleep resume keeps it
    QVERIFY(w.hasEvidence());
    QVERIFY(!NetworkWatch(6000).hasEvidence());   // Kad start
}

void tst_KadNetworkWatch::voidChecksRestoresType()
{
    const time_t t0 = time(nullptr);
    Contact c;
    c.updateType();                 // seen: type 2 (young)
    const uint8 seen = c.getType();
    c.checkingType(t0 + 20);        // unanswered checks after the silence began
    c.checkingType(t0 + 40);
    QCOMPARE(c.getType(), uint8(seen + 2));

    c.voidChecksSince(t0, t0 + 50);
    QCOMPARE(c.getType(), seen);
    QCOMPARE(c.getExpireTime(), t0 + 50);   // due for a check once back
    c.checkingType(t0 + 60);        // counting starts afresh
    QCOMPARE(c.getType(), uint8(seen + 1));
}

void tst_KadNetworkWatch::voidChecksKeepsOlderFailure()
{
    const time_t t0 = time(nullptr);
    Contact c;
    c.checkingType(t0 + 20);
    const uint8 failing = c.getType();
    // silence began after this contact was already failing: its raise stays
    c.voidChecksSince(t0 + 60, t0 + 70);
    QCOMPARE(c.getType(), failing);
}

void tst_KadNetworkWatch::recheckMark()
{
    Contact c;
    QVERIFY(!c.isRecheckDue(1000));
    c.markRecheck(1000);
    QVERIFY(!c.isRecheckDue(999));
    QVERIFY(c.isRecheckDue(1000));
    c.updateType();                 // answered: mark done
    QVERIFY(!c.isRecheckDue(time(nullptr) + 1));
}

QTEST_GUILESS_MAIN(tst_KadNetworkWatch)
#include "tst_KadNetworkWatch.moc"
