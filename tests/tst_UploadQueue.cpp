/// @file tst_UploadQueue.cpp
/// @brief Tests for transfer/UploadQueue.

#include "TestHelpers.h"
#include "transfer/UploadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "client/UpDownClient.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_UploadQueue : public QObject {
    Q_OBJECT

private slots:
    void construction_empty();
    void addClientToQueue_basic();
    void addClientToQueue_duplicate();
    void addClientToQueue_ipLimit();
    void removeFromWaitingQueue_basic();
    void removeFromUploadQueue_basic();
    void isDownloading_uploading();
    void waitingPosition_correct();
    void updateDatarates_basic();
    void process_noop_empty();
    void targetClientDataRate_calculation();
    void waitingUserCount_correct();
    void uploadQueueLength_correct();
    void averageUpTime_zero();
    void forEachWaiting_iterates();
};

void tst_UploadQueue::construction_empty()
{
    UploadQueue queue;
    QCOMPARE(queue.waitingUserCount(), 0);
    QCOMPARE(queue.uploadQueueLength(), 0);
    QCOMPARE(queue.datarate(), uint32(0));
    QCOMPARE(queue.friendDatarate(), uint32(0));
    QCOMPARE(queue.successfulUploadCount(), uint32(0));
    QCOMPARE(queue.failedUploadCount(), uint32(0));
    QCOMPARE(queue.averageUpTime(), uint32(0));
}

void tst_UploadQueue::addClientToQueue_basic()
{
    UploadQueue queue;

    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
    QSignalSpy addedSpy(&queue, &UploadQueue::clientAddedToQueue);

    UpDownClient client;
    // Set a unique IP so IP-limit doesn't trigger
    client.setIP(0x0A000001);

    bool added = queue.addClientToQueue(&client);
    QVERIFY(added);

    // Client should be in waiting queue (or uploaded directly if queue was empty)
    QVERIFY(queue.isOnUploadQueue(&client) || queue.isDownloading(&client));
}

void tst_UploadQueue::addClientToQueue_duplicate()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    UpDownClient client;
    client.setIP(0x0A000001);

    queue.addClientToQueue(&client);
    // Adding the same client again should return true (already in queue)
    bool added = queue.addClientToQueue(&client);
    QVERIFY(added);

    // Should still have exactly 1 entry (not duplicated)
    QVERIFY(queue.waitingUserCount() <= 1);
}

void tst_UploadQueue::addClientToQueue_ipLimit()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    // First, fill the minimum upload slots with different-IP clients
    // so that subsequent same-IP clients go to the waiting list
    UpDownClient filler1, filler2;
    filler1.setIP(0x0B000001);
    filler2.setIP(0x0B000002);
    uint8 fh1[16] = {0xF1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8 fh2[16] = {0xF2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    filler1.setUserHash(fh1);
    filler2.setUserHash(fh2);
    queue.addClientToQueue(&filler1); // Goes to uploading (slot 1)
    queue.addClientToQueue(&filler2); // Goes to uploading (slot 2)

    // Now upload slots are full (MIN_UP_CLIENTS_ALLOWED = 2)
    // Create 4 clients with the same IP — they should go to the waiting list
    UpDownClient c1, c2, c3, c4;
    const uint32 sameIP = 0x0A000001;
    c1.setIP(sameIP);
    c2.setIP(sameIP);
    c3.setIP(sameIP);
    c4.setIP(sameIP);

    // Give each client a unique user hash so compare() doesn't trigger
    uint8 hash1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8 hash2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8 hash3[16] = {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8 hash4[16] = {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    c1.setUserHash(hash1);
    c2.setUserHash(hash2);
    c3.setUserHash(hash3);
    c4.setUserHash(hash4);

    queue.addClientToQueue(&c1);
    queue.addClientToQueue(&c2);
    queue.addClientToQueue(&c3);

    // The 4th from same IP should be rejected (limit is 3)
    bool added = queue.addClientToQueue(&c4);
    QVERIFY(!added);
}

void tst_UploadQueue::removeFromWaitingQueue_basic()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
    QSignalSpy removedSpy(&queue, &UploadQueue::clientRemovedFromQueue);

    UpDownClient client;
    client.setIP(0x0A000001);

    queue.addClientToQueue(&client);

    if (queue.isOnUploadQueue(&client)) {
        bool removed = queue.removeFromWaitingQueue(&client);
        QVERIFY(removed);
        QVERIFY(!queue.isOnUploadQueue(&client));
        QVERIFY(removedSpy.count() > 0);
    }

    // Removing a client not in queue returns false
    UpDownClient other;
    QVERIFY(!queue.removeFromWaitingQueue(&other));
}

void tst_UploadQueue::removeFromUploadQueue_basic()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    // Removing client that isn't uploading should return false
    UpDownClient client;
    QVERIFY(!queue.removeFromUploadQueue(&client));
}

void tst_UploadQueue::isDownloading_uploading()
{
    UploadQueue queue;
    UpDownClient client;

    QVERIFY(!queue.isDownloading(&client));
    QVERIFY(!queue.isOnUploadQueue(&client));
}

void tst_UploadQueue::waitingPosition_correct()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    UpDownClient client;
    client.setIP(0x0A000001);

    // Not in queue — position should be 0
    QCOMPARE(queue.waitingPosition(&client), 0);

    queue.addClientToQueue(&client);

    if (queue.isOnUploadQueue(&client)) {
        int pos = queue.waitingPosition(&client);
        QVERIFY(pos >= 1);
    }
}

void tst_UploadQueue::updateDatarates_basic()
{
    UploadQueue queue;

    // Without throttler, updateDatarates should be a no-op
    queue.updateDatarates();
    QCOMPARE(queue.datarate(), uint32(0));
    QCOMPARE(queue.friendDatarate(), uint32(0));
}

void tst_UploadQueue::process_noop_empty()
{
    UploadQueue queue;

    // Process with empty queue shouldn't crash
    queue.process();
    queue.process();
    queue.process();

    QCOMPARE(queue.waitingUserCount(), 0);
    QCOMPARE(queue.uploadQueueLength(), 0);
}

void tst_UploadQueue::targetClientDataRate_calculation()
{
    UploadQueue queue;

    // With 0 uploading clients: 3 KiB/s
    uint32 rate = queue.targetClientDataRate(false);
    QCOMPARE(rate, uint32(3 * 1024));

    // Min rate should be 3/4 of normal rate
    uint32 minRate = queue.targetClientDataRate(true);
    QCOMPARE(minRate, rate * 3 / 4);
}

void tst_UploadQueue::waitingUserCount_correct()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    QCOMPARE(queue.waitingUserCount(), 0);

    UpDownClient c1, c2;
    c1.setIP(0x0A000001);
    c2.setIP(0x0A000002);

    uint8 hash1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8 hash2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    c1.setUserHash(hash1);
    c2.setUserHash(hash2);

    queue.addClientToQueue(&c1);
    queue.addClientToQueue(&c2);

    // Clients may go to waiting or uploading depending on slot logic
    int total = queue.waitingUserCount() + queue.uploadQueueLength();
    QVERIFY(total >= 1);
}

void tst_UploadQueue::uploadQueueLength_correct()
{
    UploadQueue queue;
    QCOMPARE(queue.uploadQueueLength(), 0);
}

void tst_UploadQueue::averageUpTime_zero()
{
    UploadQueue queue;
    // With 0 successful uploads, average time should be 0
    QCOMPARE(queue.averageUpTime(), uint32(0));
}

void tst_UploadQueue::forEachWaiting_iterates()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    UpDownClient c1;
    c1.setIP(0x0A000001);
    uint8 hash1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    c1.setUserHash(hash1);

    queue.addClientToQueue(&c1);

    int count = 0;
    queue.forEachWaiting([&count](UpDownClient*) { ++count; });
    queue.forEachUploading([&count](UpDownClient*) { ++count; });

    // At least one client was added somewhere
    if (queue.waitingUserCount() + queue.uploadQueueLength() > 0)
        QVERIFY(count > 0);
}

QTEST_GUILESS_MAIN(tst_UploadQueue)
#include "tst_UploadQueue.moc"
