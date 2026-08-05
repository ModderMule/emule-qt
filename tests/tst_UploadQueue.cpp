/// @file tst_UploadQueue.cpp
/// @brief Tests for transfer/UploadQueue.

#include "TestFixtures.h"
#include "TestHelpers.h"
#include "transfer/UploadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

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
    void addClientToQueue_ipv6LimitIsOnePerAddress();
    void addClientToQueue_trackedClientsGate();
    void slotAssignment_alternatesBetweenFamilies();
    void process_flushesPendingIPChangeForWaitingClients();

    // UploadState::Connecting must not be a dead end — MFC BaseClient.cpp:1558-1565, :1118-1120
    void connectingSlot_activatedByHandshake();
    void connectingSlot_notGrantedASlot_staysConnecting();
    void connectingSlot_releasedOnDisconnect();
};

namespace {

/// A queued peer: distinct user hash (so compare() doesn't fire) plus an address.
void setupClient(UpDownClient& c, const QString& ip, uint8 hashByte, uint16 port = 4662)
{
    c.setUserAddress(Address::fromString(ip));
    c.setUserPort(port);
    uint8 hash[16]{};
    std::memset(hash, hashByte, sizeof(hash));
    c.setUserHash(hash);
}

} // namespace

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
    client.setUserAddress(Address::fromNetworkOrder(0x0A000001));

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
    client.setUserAddress(Address::fromNetworkOrder(0x0A000001));

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
    filler1.setUserAddress(Address::fromNetworkOrder(0x0B000001));
    filler2.setUserAddress(Address::fromNetworkOrder(0x0B000002));
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
    c1.setUserAddress(Address::fromNetworkOrder(sameIP));
    c2.setUserAddress(Address::fromNetworkOrder(sameIP));
    c3.setUserAddress(Address::fromNetworkOrder(sameIP));
    c4.setUserAddress(Address::fromNetworkOrder(sameIP));

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
    client.setUserAddress(Address::fromNetworkOrder(0x0A000001));

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
    client.setUserAddress(Address::fromNetworkOrder(0x0A000001));

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
    c1.setUserAddress(Address::fromNetworkOrder(0x0A000001));
    c2.setUserAddress(Address::fromNetworkOrder(0x0A000002));

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
    c1.setUserAddress(Address::fromNetworkOrder(0x0A000001));
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

// IPv6 gets a stricter rule than IPv4's "3 per address": exactly one client per
// address, counting active uploads as well as the waiting list. Exact 128-bit match —
// no prefix logic, since a whole /64 legitimately belongs to one subscriber.
void tst_UploadQueue::addClientToQueue_ipv6LimitIsOnePerAddress()
{
    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    const QString v6 = QStringLiteral("2606:4700::1");

    UpDownClient first;
    setupClient(first, v6, 0x11);
    QVERIFY(queue.addClientToQueue(&first));
    QVERIFY(queue.isOnUploadQueue(&first) || queue.isDownloading(&first));

    // Same IPv6, different port and hash → refused, wherever the first one landed
    // (waiting list or an upload slot).
    UpDownClient second;
    setupClient(second, v6, 0x22, 4663);
    QVERIFY(!queue.addClientToQueue(&second));
    QVERIFY(!queue.isOnUploadQueue(&second));

    // A different IPv6 is unaffected — even one in the same /64, since we deliberately
    // do not do prefix matching.
    UpDownClient sameSubnet;
    setupClient(sameSubnet, QStringLiteral("2606:4700::2"), 0x33, 4664);
    QVERIFY(queue.addClientToQueue(&sameSubnet));

    // IPv4 keeps its own, looser rule: three from one address are still accepted.
    UpDownClient v4a, v4b;
    setupClient(v4a, QStringLiteral("10.20.30.40"), 0x44, 4665);
    setupClient(v4b, QStringLiteral("10.20.30.40"), 0x55, 4666);
    QVERIFY(queue.addClientToQueue(&v4a));
    QVERIFY(queue.addClientToQueue(&v4b));
}

// The second per-address gate (MFC UploadQueue.cpp:614) counts DISTINCT TCP ports seen
// from an IPv4 address over KEEPTRACK_TIME — not clients queued right now — so a peer
// that keeps reconnecting from fresh ports locks itself out.
void tst_UploadQueue::addClientToQueue_trackedClientsGate()
{
    ClientList clientList;
    theApp.clientList = &clientList;

    UploadQueue queue;
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

    const QString ip = QStringLiteral("10.20.30.40");

    // Three distinct ports from one address, recorded as if they had each held a slot.
    std::vector<std::unique_ptr<UpDownClient>> seen;
    for (int i = 0; i < 3; ++i) {
        auto c = std::make_unique<UpDownClient>();
        setupClient(*c, ip, static_cast<uint8>(0xA0 + i), static_cast<uint16>(5000 + i));
        clientList.addTrackClient(c.get());
        seen.push_back(std::move(c));
    }
    QCOMPARE(clientList.clientsFromIP(Address::fromString(ip)), 3);

    // Re-recording an already-known port updates in place rather than growing the list.
    clientList.addTrackClient(seen.front().get());
    QCOMPARE(clientList.clientsFromIP(Address::fromString(ip)), 3);

    // A fourth client from that address is now refused — even though the queue itself
    // is empty and the three tracked clients are long gone.
    UpDownClient fourth;
    setupClient(fourth, ip, 0xB0, 5003);
    QVERIFY(!queue.addClientToQueue(&fourth));
    QVERIFY(!queue.isOnUploadQueue(&fourth));

    // A different address is untouched.
    UpDownClient other;
    setupClient(other, QStringLiteral("10.20.30.41"), 0xC0, 5004);
    QVERIFY(queue.addClientToQueue(&other));

    // The gate is IPv4-only: an IPv6 address with three tracked ports still gets in,
    // because IPv6 is governed by the 1-per-address rule instead.
    const QString v6 = QStringLiteral("2606:4700::5");
    std::vector<std::unique_ptr<UpDownClient>> seenV6;
    for (int i = 0; i < 3; ++i) {
        auto c = std::make_unique<UpDownClient>();
        setupClient(*c, v6, static_cast<uint8>(0xD0 + i), static_cast<uint16>(6000 + i));
        clientList.addTrackClient(c.get());
        seenV6.push_back(std::move(c));
    }
    QCOMPARE(clientList.clientsFromIP(Address::fromString(v6)), 3);

    UpDownClient v6client;
    setupClient(v6client, v6, 0xE0, 6003);
    QVERIFY(queue.addClientToQueue(&v6client));

    theApp.clientList = nullptr;
}

// With separateIPv6Queue on and clients of both families waiting, each freed slot goes
// to the family that did NOT get the previous one. Without it the higher-scoring family
// wins every slot, which is how a small IPv6 population gets starved indefinitely.
//
// score() needs real scaffolding to be non-zero (shared upload file + credits carrying a
// wait start time), otherwise every candidate scores 0 and nobody is ever selected.
void tst_UploadQueue::slotAssignment_alternatesBetweenFamilies()
{
    KnownFileList knownFiles;
    SharedFileList sharedFiles(&knownFiles);
    theApp.sharedFileList = &sharedFiles;

    uint8 fileHash[16];
    std::memset(fileHash, 0x77, sizeof(fileHash));
    auto* file = new KnownFile();
    file->setFileHash(fileHash);
    file->setFileName(QStringLiteral("shared.bin"));
    QVERIFY(sharedFiles.safeAddKFile(file));

    // Runs one full scenario and reports the family of each promotion, in order.
    const auto runScenario = [&](bool separateQueue) {
        thePrefs.setSeparateIPv6Queue(separateQueue);

        UploadQueue queue;
        qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");

        // Occupy the free slots first. addClientToQueue promotes directly while the
        // waiting list is empty, so without these the real clients would never queue up
        // and selection would have nothing to choose between.
        UpDownClient filler1, filler2;
        setupClient(filler1, QStringLiteral("172.31.0.1"), 0xF1, 4000);
        setupClient(filler2, QStringLiteral("172.31.0.2"), 0xF2, 4001);
        queue.addClientToQueue(&filler1);
        queue.addClientToQueue(&filler2);

        std::vector<std::unique_ptr<UpDownClient>> clients;
        std::vector<std::unique_ptr<ClientCredits>> credits;

        // Two of each family, the IPv4 pair queued first so it accrues the longer wait
        // and the higher score. On plain score ordering both v4 clients are promoted
        // before either v6 one — which is exactly what the pref is meant to change.
        const QStringList addrs = {
            QStringLiteral("10.20.30.40"), QStringLiteral("10.20.30.41"),
            QStringLiteral("2606:4700::1"), QStringLiteral("2606:4700::2"),
        };
        for (int i = 0; i < addrs.size(); ++i) {
            auto c = std::make_unique<UpDownClient>();
            setupClient(*c, addrs.at(i), static_cast<uint8>(0x10 + i),
                        static_cast<uint16>(4662 + i));
            // A HighID: findBestClientInQueue only considers a low-ID client when it has
            // a live socket, and these have none. A dual-stack peer connected to us over
            // IPv6 still carries its IPv4 HighID, so this holds for the v6 pair too.
            c->setUserIDHybrid(0x0A141E28u + static_cast<uint32>(i));

            uint8 credHash[16]{};
            std::memset(credHash, static_cast<uint8>(0x10 + i), sizeof(credHash));
            auto cr = std::make_unique<ClientCredits>(credHash);
            c->setCredits(cr.get());
            c->setUploadFileID(file);
            c->setReqUpFileId(fileHash);
            c->setWaitStartTime();

            credits.push_back(std::move(cr));
            clients.push_back(std::move(c));
        }

        std::vector<bool> promotedV6;
        // Every client must actually score above zero, or selection can't run at all.
        for (const auto& c : clients) {
            if (c->score(false) == 0)
                return promotedV6;   // caller's QVERIFY on the size will report this
        }

        for (const auto& c : clients)
            queue.addClientToQueue(c.get());
        if (queue.waitingUserCount() != static_cast<int>(clients.size()))
            return promotedV6;

        // forceNewClient() throttles promotions to one per second while the slots stay
        // occupied, so each pass has to wait the throttle out.
        QSignalSpy startedSpy(&queue, &UploadQueue::uploadStarted);
        for (size_t i = 0; i < clients.size() && promotedV6.size() < clients.size(); ++i) {
            QTest::qWait(1100);
            const auto before = startedSpy.count();
            queue.process();
            for (auto j = before; j < startedSpy.count(); ++j) {
                if (auto* c = startedSpy.at(j).at(0).value<eMule::UpDownClient*>())
                    promotedV6.push_back(c->isIPv6Connection());
            }
        }
        return promotedV6;
    };

    // --- Pref ON: families alternate ---
    const std::vector<bool> withPref = runScenario(true);
    QVERIFY2(withPref.size() >= 3, "expected at least three promotions to observe alternation");

    // Consecutive promotions must not come from the same family while both are waiting.
    // The tail may repeat once one family is exhausted — that is the no-wasted-slot rule.
    for (size_t i = 1; i < std::min(withPref.size(), size_t{3}); ++i) {
        QVERIFY2(withPref[i] != withPref[i - 1],
                 qPrintable(QStringLiteral("promotion %1 repeated family %2")
                                .arg(i).arg(withPref[i] ? "v6" : "v4")));
    }

    // --- Pref OFF: plain score ordering, so the longer-waiting v4 pair goes first ---
    // This is what makes the assertion above meaningful rather than vacuous: the same
    // scenario must produce a different, non-alternating order.
    const std::vector<bool> withoutPref = runScenario(false);
    QVERIFY(withoutPref.size() >= 3);
    QVERIFY2(withoutPref[0] == withoutPref[1],
             "with the pref off the two highest-scoring clients share a family, "
             "so alternation must not happen");
    QVERIFY(withoutPref != withPref);

    thePrefs.setSeparateIPv6Queue(true);   // restore the default
    theApp.sharedFileList = nullptr;
}

// The upload-side send point for a queued OP_CHANGE_CLIENT_IP: findBestClientInQueue()
// walks the waiting list anyway, so telling those peers our new IPv6 costs no extra
// wakeup. Two things are pinned here — that the walk really does flush, and that a client
// purged as stale is skipped, because the purge `continue`s above the flush.
void tst_UploadQueue::process_flushesPendingIPChangeForWaitingClients()
{
    IPv6AdvertiseGuard guard;
    ClientList clientList;
    theApp.clientList = &clientList;

    UploadQueue queue;   // deliberately no shared file list — the noFile purge stays off

    // Occupy the free slots, or addClientToQueue promotes directly and nothing ever
    // reaches the waiting list. Same trick as slotAssignment_alternatesBetweenFamilies.
    UpDownClient filler1, filler2;
    setupClient(filler1, QStringLiteral("172.31.0.1"), 0xF1, 4000);
    setupClient(filler2, QStringLiteral("172.31.0.2"), 0xF2, 4001);
    QVERIFY(queue.addClientToQueue(&filler1));
    QVERIFY(queue.addClientToQueue(&filler2));

    QTcpServer freshServer;
    UpDownClient fresh;
    setupClient(fresh, QStringLiteral("10.20.30.40"), 0x21, 4662);
    QTcpSocket* freshPeer = wireLoopbackSocket(freshServer, fresh);
    QVERIFY(freshPeer != nullptr);
    feedIPv6CapableHello(fresh, 0x21);

    QTcpServer staleServer;
    UpDownClient stale;
    setupClient(stale, QStringLiteral("10.20.30.41"), 0x22, 4663);
    QTcpSocket* stalePeer = wireLoopbackSocket(staleServer, stale);
    QVERIFY(stalePeer != nullptr);
    feedIPv6CapableHello(stale, 0x22);

    QVERIFY(queue.addClientToQueue(&fresh));
    QVERIFY(queue.addClientToQueue(&stale));
    QCOMPARE(queue.waitingUserCount(), 2);

    // addClientToQueue stamps lastUpRequest, so the purge has to be armed afterwards.
    stale.setLastUpRequest(0);

    // Drain the ranking info both peers were sent when they queued up.
    QCoreApplication::processEvents();
    freshPeer->readAll();
    stalePeer->readAll();

    fresh.markSendIPPending();
    stale.markSendIPPending();

    QTest::qWait(1100);          // forceNewClient() throttles promotions to one per second
    queue.process();

    QDeadlineTimer deadline(2000);
    while (freshPeer->bytesAvailable() < 22 && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        freshPeer->waitForReadyRead(10);
    }
    const QByteArray raw = freshPeer->readAll();
    QVERIFY2(raw.size() >= 22, "the waiting client was walked but never told our new IPv6");
    // First frame only: a client that also wins the slot is sent OP_ACCEPTUPLOADREQ right
    // after, and the IP change is what has to come out of the walk itself.
    QCOMPARE(static_cast<uint8>(raw[0]), static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(static_cast<uint8>(raw[5]), static_cast<uint8>(OP_CHANGE_CLIENT_IP));

    // The purge `continue`s above the flush, so the stale client's flag is still armed —
    // it was never even offered the notice — and nothing reached its socket.
    QVERIFY2(stale.sendIPPending(), "a purged client must be skipped before the flush");
    QVERIFY(!waitForBytes(stalePeer, 1, 300));

    // The stale client left the queue; the fresh one is still on it. Neither is promoted —
    // with no credits and no upload file it scores 0 — which is the point: the flush comes
    // out of the walk itself, not out of winning the slot.
    QCOMPARE(queue.waitingUserCount(), 1);

    fresh.setSocket(nullptr);
    stale.setSocket(nullptr);
    freshPeer->close();
    stalePeer->close();
    QCoreApplication::processEvents();
    theApp.clientList = nullptr;
}

// ---------------------------------------------------------------------------
// UploadState::Connecting lifecycle
// ---------------------------------------------------------------------------

void tst_UploadQueue::connectingSlot_activatedByHandshake()
{
    // The bug this pins: addUpNextClient() puts a client that still has to be dialled
    // into UploadState::Connecting AND onto the uploading list, but nothing ever moved
    // it on to Uploading. The peer was never sent OP_ACCEPTUPLOADREQ, never started
    // downloading, and the slot stayed occupied for the life of the client.
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
    ClientList clientList;
    theApp.clientList = &clientList;

    UploadQueue queue;
    theApp.uploadQueue = &queue;   // block (b) resolves the queue through theApp

    UpDownClient client;
    setupClient(client, QStringLiteral("10.0.0.1"), 0x11);

    // An empty waiting queue promotes straight to a slot; with no socket, that promotion
    // takes addUpNextClient()'s "must connect first" branch.
    QVERIFY(queue.addClientToQueue(&client));
    QCOMPARE(client.uploadState(), UploadState::Connecting);
    QVERIFY2(queue.isDownloading(&client),
             "the slot is granted up-front; that is what makes it a leak if never activated");

    client.onHandshakeCompleted();

    QCOMPARE(client.uploadState(), UploadState::Uploading);

    queue.removeFromUploadQueue(&client);
    theApp.uploadQueue = nullptr;
    theApp.clientList = nullptr;
}

void tst_UploadQueue::connectingSlot_notGrantedASlot_staysConnecting()
{
    // The isDownloading() guard is load-bearing: without it any client that happened to
    // be in Connecting would promote itself to Uploading on handshake, bypassing the
    // queue entirely. MFC guards the same way (BaseClient.cpp:1558).
    UploadQueue queue;
    theApp.uploadQueue = &queue;

    UpDownClient client;
    setupClient(client, QStringLiteral("10.0.0.2"), 0x22);
    client.setUploadState(UploadState::Connecting);
    QVERIFY(!queue.isDownloading(&client));   // never granted a slot

    client.onHandshakeCompleted();

    QCOMPARE(client.uploadState(), UploadState::Connecting);
    theApp.uploadQueue = nullptr;
}

void tst_UploadQueue::connectingSlot_releasedOnDisconnect()
{
    // ...and a connect that never completes has to give the slot back, or making
    // Connecting a consumed state just moves the leak. MFC BaseClient.cpp:1118-1120
    // removes for Connecting as well as Uploading.
    qRegisterMetaType<eMule::UpDownClient*>("eMule::UpDownClient*");
    ClientList clientList;
    theApp.clientList = &clientList;

    UploadQueue queue;
    theApp.uploadQueue = &queue;

    UpDownClient client;
    setupClient(client, QStringLiteral("10.0.0.3"), 0x33);

    QVERIFY(queue.addClientToQueue(&client));
    QCOMPARE(client.uploadState(), UploadState::Connecting);
    QCOMPARE(queue.uploadQueueLength(), 1);

    client.disconnected(QStringLiteral("connect timed out"));

    QCOMPARE(client.uploadState(), UploadState::None);
    QVERIFY2(!queue.isDownloading(&client), "a failed connect must release the upload slot");
    QCOMPARE(queue.uploadQueueLength(), 0);

    theApp.uploadQueue = nullptr;
    theApp.clientList = nullptr;
}

QTEST_GUILESS_MAIN(tst_UploadQueue)
#include "tst_UploadQueue.moc"
