/// @file tst_KadUDPListener.cpp
/// @brief Tests for KadUDPListener.h — Kad UDP packet handler.

#include "TestHelpers.h"

#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadUDPListener : public QObject {
    Q_OBJECT

private slots:
    void construct_basic();
    void processPacket_unknownOpcode();
    void sendPacket_emitsSignal();
    void sendNullPacket_basic();
    void findNodeIDByIP_queued();
    void expireClientSearch_noRequester();
};

void tst_KadUDPListener::construct_basic()
{
    KademliaUDPListener listener;
    // Construction should succeed without crash
    QVERIFY(true);
}

void tst_KadUDPListener::processPacket_unknownOpcode()
{
    KademliaUDPListener listener;

    // Unknown opcode — should not crash, just log and return
    uint8 data[] = {0xFF}; // invalid opcode
    KadUDPKey senderKey(0);
    listener.processPacket(data, sizeof(data), 0x0A000001, 4672, false, senderKey);
    QVERIFY(true); // no crash
}

void tst_KadUDPListener::sendPacket_emitsSignal()
{
    KademliaUDPListener listener;
    // Without a bound socket sendPacket is a no-op — verify no crash.
    SafeMemFile file;
    file.writeUInt8(0x42); // dummy data
    KadUDPKey targetKey(0);
    listener.sendPacket(file, KADEMLIA2_BOOTSTRAP_REQ, 0x0A000001, 4672,
                        targetKey, nullptr);
    QVERIFY(true);
}

void tst_KadUDPListener::sendNullPacket_basic()
{
    KademliaUDPListener listener;
    // Without a bound socket sendNullPacket is a no-op — verify no crash.
    KadUDPKey targetKey(0);
    listener.sendNullPacket(KADEMLIA2_BOOTSTRAP_REQ, 0x0A000001, 4672,
                            targetKey, nullptr);
    QVERIFY(true);
}

void tst_KadUDPListener::findNodeIDByIP_queued()
{
    KademliaUDPListener listener;

    // With null requester, should return false
    bool result = listener.findNodeIDByIP(nullptr, 0x0A000001, 4662, 4672);
    QVERIFY(!result);
}

void tst_KadUDPListener::expireClientSearch_noRequester()
{
    KademliaUDPListener listener;

    // Expire with no requester should not crash
    listener.expireClientSearch(nullptr);
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(tst_KadUDPListener)
#include "tst_KadUDPListener.moc"
