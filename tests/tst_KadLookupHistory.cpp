/// @file tst_KadLookupHistory.cpp
/// @brief Tests for KadLookupHistory.h — search history tracking.

#include "TestHelpers.h"

#include "kademlia/KadContact.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadLookupHistory : public QObject {
    Q_OBJECT

private slots:
    void contactReceived_tracked();
    void contactAskedKad_recorded();
    void contactRespondedKeyword_counted();
    void setSearchStopped_flag();
};

void tst_KadLookupHistory::contactReceived_tracked()
{
    LookupHistory history;
    UInt128 localId;
    localId.setValueRandom();

    UInt128 clientId(uint32{100});
    Contact contact(clientId, 0x0A000001, 4672, 4662, 8, KadUDPKey(0), false, localId);

    UInt128 distance(localId);
    distance.xorWith(clientId);

    history.contactReceived(&contact, nullptr, distance, true);

    auto& entries = history.getHistoryEntries();
    QCOMPARE(entries.size(), size_t{1});
    QCOMPARE(entries[0]->contactID, clientId);
    QCOMPARE(entries[0]->providedCloser, true);
}

void tst_KadLookupHistory::contactAskedKad_recorded()
{
    LookupHistory history;
    UInt128 localId;
    localId.setValueRandom();

    UInt128 clientId(uint32{200});
    Contact contact(clientId, 0x0A000002, 4672, 4662, 8, KadUDPKey(0), false, localId);

    UInt128 distance(localId);
    distance.xorWith(clientId);

    history.contactReceived(&contact, nullptr, distance, false);
    history.contactAskedKad(&contact);

    auto& entries = history.getHistoryEntries();
    QCOMPARE(entries.size(), size_t{1});
    QVERIFY(entries[0]->askedContactsTime > 0);
    QVERIFY(entries[0]->isInteresting());
}

void tst_KadLookupHistory::contactRespondedKeyword_counted()
{
    LookupHistory history;
    UInt128 localId;
    localId.setValueRandom();

    UInt128 clientId(uint32{300});
    Contact contact(clientId, 0x0A000003, 4672, 4662, 8, KadUDPKey(0), false, localId);

    UInt128 distance(localId);
    distance.xorWith(clientId);

    history.contactReceived(&contact, nullptr, distance, false);
    history.contactRespondedKeyword(0x0A000003, 4672, 42);

    auto& entries = history.getHistoryEntries();
    QCOMPARE(entries.size(), size_t{1});
    QCOMPARE(entries[0]->respondedSearchItem, uint32{42});
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
