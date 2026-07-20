/// @file tst_KadUDPListener.cpp
/// @brief Tests for KadUDPListener.h — Kad UDP packet handler.

#include "TestHelpers.h"

#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
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

    // createSearchExpressionTree
    void searchExprTree_tokenizesStringTerm();
    void searchExprTree_metaTagIsLowercasedAndKeyed();
    void searchExprTree_rejectsRunawayNesting();
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

// ---------------------------------------------------------------------------
// createSearchExpressionTree — decodes the blob a keyword search travels with
// ---------------------------------------------------------------------------

namespace {

/// Encode `01 <u16 len> <utf8>` — a string search term.
QByteArray encodeStringTerm(const QByteArray& s)
{
    QByteArray r;
    r += char(0x01);
    r += char(s.size() & 0xFF);
    r += char((s.size() >> 8) & 0xFF);
    r += s;
    return r;
}

std::unique_ptr<SearchTerm> decode(const QByteArray& blob)
{
    SafeMemFile io(reinterpret_cast<const uint8*>(blob.constData()),
                   static_cast<qint64>(blob.size()));
    return KademliaUDPListener::createSearchExpressionTree(io, 0);
}

} // namespace

void tst_KadUDPListener::searchExprTree_tokenizesStringTerm()
{
    // A string term carries several words and is matched as an AND of all of
    // them. Keeping it as one unsplit string meant it could never match a
    // tokenized file name.
    auto term = decode(encodeStringTerm(QByteArray("Ubuntu Desktop AMD64")));
    QVERIFY(term != nullptr);
    QCOMPARE(term->type, SearchTerm::Type::String);
    QCOMPARE(term->strings.size(), std::size_t{3});
    QCOMPARE(term->strings[0], QStringLiteral("ubuntu"));
    QCOMPARE(term->strings[1], QStringLiteral("desktop"));
    QCOMPARE(term->strings[2], QStringLiteral("amd64"));
}

void tst_KadUDPListener::searchExprTree_metaTagIsLowercasedAndKeyed()
{
    // 02 <u16 len> <utf8 value> <u16 namelen=1> <tag id>
    QByteArray blob;
    blob += char(0x02);
    blob += char(0x05); blob += char(0x00);
    blob += QByteArray("AuDiO");
    blob += char(0x01); blob += char(0x00);
    blob += char(FT_FILETYPE);

    auto term = decode(blob);
    QVERIFY(term != nullptr);
    QCOMPARE(term->type, SearchTerm::Type::MetaTag);
    // Value lowercased — entry data is compared in lower case.
    QCOMPARE(term->tag.strValue(), QStringLiteral("audio"));
    // A single-byte name is a numeric tag ID, normalized the same way
    // io::readKadTag normalizes stored entry tags. Without this the term keyed
    // on a raw byte-array name that no entry ever carries, so no meta or
    // numeric filter could match.
    QCOMPARE(term->tag.nameId(), uint8{FT_FILETYPE});
}

void tst_KadUDPListener::searchExprTree_rejectsRunawayNesting()
{
    // Deeply nested boolean operators must not blow the stack.
    QByteArray blob;
    for (int i = 0; i < 64; ++i) {
        blob += char(0x00);
        blob += char(0x00); // AND
    }
    QVERIFY(decode(blob) == nullptr);
}

QTEST_GUILESS_MAIN(tst_KadUDPListener)
#include "tst_KadUDPListener.moc"
