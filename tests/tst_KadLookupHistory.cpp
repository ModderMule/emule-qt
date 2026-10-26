/// @file tst_KadLookupHistory.cpp
/// @brief Tests for KadLookupHistory.h — search history tracking.

#include "TestHelpers.h"

#include "kademlia/KadContact.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::kad;

class tst_KadLookupHistory : public QObject {
    Q_OBJECT

private slots:
    void contactReceived_notShownUntilInteresting();
    void contactReceived_forcedIsShown();
    void contactAskedKad_recorded();
    void response_creditsResponder();
    void responseOnly_creditsWithoutEntry();
    void contactAskedKeyword_makesInteresting();
    void contactRespondedKeyword_accumulates();
    void setSearchStopped_flag();
};

namespace {

struct Node {
    UInt128 id;
    UInt128 distance;
    std::unique_ptr<Contact> contact;
};

Node makeNode(uint32 idValue, uint32 ip, const UInt128& target)
{
    Node n;
    n.id = UInt128(idValue);
    n.distance = target;
    n.distance.xorWith(n.id);
    n.contact = std::make_unique<Contact>(n.id, ip, 4672, 4662, 8, KadUDPKey(0), false, target);
    return n;
}

UInt128 randomTarget()
{
    UInt128 t;
    t.setValueRandom();
    return t;
}

} // namespace

void tst_KadLookupHistory::contactReceived_notShownUntilInteresting()
{
    // MFC: the graph lists only asked or forced entries
    LookupHistory history;
    const auto n = makeNode(100, 0x0A000001, randomTarget());
    history.contactReceived(n.contact.get(), nullptr, n.distance, true);
    QCOMPARE(history.allEntryCount(), size_t{1});
    QVERIFY(history.getHistoryEntries().empty());
}

void tst_KadLookupHistory::contactReceived_forcedIsShown()
{
    LookupHistory history;
    const auto n = makeNode(101, 0x0A000001, randomTarget());
    history.contactReceived(n.contact.get(), nullptr, n.distance, false, true);
    QCOMPARE(history.getHistoryEntries().size(), size_t{1});
    QVERIFY(history.getHistoryEntries()[0]->forcedInteresting);
}

void tst_KadLookupHistory::contactAskedKad_recorded()
{
    LookupHistory history;
    const auto n = makeNode(200, 0x0A000002, randomTarget());
    history.contactReceived(n.contact.get(), nullptr, n.distance, false);
    history.contactAskedKad(n.contact.get());

    const auto& entries = history.getHistoryEntries();
    QCOMPARE(entries.size(), size_t{1});
    QVERIFY(entries[0]->askedContactsTime > 0);
    QVERIFY(entries[0]->isInteresting());
    QVERIFY(entries[0]->receivedFromIdx.empty());   // "Routing Table"
}

void tst_KadLookupHistory::response_creditsResponder()
{
    // The responder (not the reported contact) counts the answer and the "closer" flag
    const UInt128 target = randomTarget();
    LookupHistory history;
    const auto asked = makeNode(300, 0x0A000003, target);
    const auto found = makeNode(301, 0x0A000004, target);
    history.contactReceived(asked.contact.get(), nullptr, asked.distance, false);
    history.contactAskedKad(asked.contact.get());

    history.contactReceived(found.contact.get(), asked.contact.get(), found.distance, true);
    // Same contact reported again: one more edge, one more answer
    history.contactReceived(found.contact.get(), asked.contact.get(), found.distance, true);

    const auto& entries = history.getHistoryEntries();
    QCOMPARE(entries.size(), size_t{1});
    QCOMPARE(entries[0]->respondedContact, uint32{2});
    QVERIFY(entries[0]->providedCloser);

    history.contactAskedKad(found.contact.get());
    QCOMPARE(entries.size(), size_t{2});
    QCOMPARE(entries[1]->receivedFromIdx, (std::vector<int>{0, 0}));
    QVERIFY(!entries[1]->providedCloser);
}

void tst_KadLookupHistory::responseOnly_creditsWithoutEntry()
{
    // NODEFWCHECKUDP: received == nullptr only marks the responder (MFC Search.cpp:367)
    LookupHistory history;
    const auto asked = makeNode(400, 0x0A000005, randomTarget());
    history.contactReceived(asked.contact.get(), nullptr, asked.distance, false);
    history.contactAskedKad(asked.contact.get());
    history.contactReceived(nullptr, asked.contact.get(), UInt128(), true);

    QCOMPARE(history.allEntryCount(), size_t{1});
    QCOMPARE(history.getHistoryEntries()[0]->respondedContact, uint32{1});
}

void tst_KadLookupHistory::contactAskedKeyword_makesInteresting()
{
    LookupHistory history;
    const auto n = makeNode(500, 0x0A000006, randomTarget());
    history.contactReceived(n.contact.get(), nullptr, n.distance, false);
    history.contactAskedKeyword(n.contact.get());
    QCOMPARE(history.getHistoryEntries().size(), size_t{1});
    QVERIFY(history.getHistoryEntries()[0]->askedSearchItemTime > 0);
}

void tst_KadLookupHistory::contactRespondedKeyword_accumulates()
{
    LookupHistory history;
    const auto n = makeNode(600, 0x0A000007, randomTarget());
    history.contactReceived(n.contact.get(), nullptr, n.distance, false);
    history.contactAskedKeyword(n.contact.get());
    history.contactRespondedKeyword(0x0A000007, 4672, 40);
    history.contactRespondedKeyword(0x0A000007, 4672, 2);
    history.contactRespondedKeyword(0x0A000007, 9999, 7);   // other port: not this node

    QCOMPARE(history.getHistoryEntries()[0]->respondedSearchItem, uint32{42});
}

void tst_KadLookupHistory::setSearchStopped_flag()
{
    LookupHistory history;
    QVERIFY(!history.isSearchStopped());
    history.setSearchStopped();
    QVERIFY(history.isSearchStopped());
}

QTEST_GUILESS_MAIN(tst_KadLookupHistory)
#include "tst_KadLookupHistory.moc"
