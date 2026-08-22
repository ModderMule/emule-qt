/// @file tst_CorruptionBlackBox.cpp
/// @brief Tests for client/CorruptionBlackBox — transfer, verify, corrupt, evaluate.

#include "TestHelpers.h"
#include "client/CorruptionBlackBox.h"
#include "utils/Opcodes.h"

#include <QTest>

using namespace eMule;

class tst_CorruptionBlackBox : public QObject {
    Q_OBJECT

private slots:
    void init_partCount();
    void transferredData_basic();
    void transferredData_merge();
    void transferredData_overlap();
    void transferredData_crossPartBoundary();
    void verifiedData_basic();
    void corruptedData_basic();
    void evaluateData_noBan();
    void evaluateData_ban();
    void evaluateData_multipleClients();
    void evaluateData_noneCorrupted();
    void senderCount_countsDistinctSenders();
    void senderCount_ipv6SenderIsKept();
    void soleSenderOfWholePart_needsFullCoverage();
    void free_clearsAll();
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_CorruptionBlackBox::init_partCount()
{
    CorruptionBlackBox box;

    // Exactly 1 part
    box.init(PARTSIZE);
    QCOMPARE(box.partCount(), std::size_t{1});

    // Slightly more than 1 part
    box.free();
    box.init(PARTSIZE + 1);
    QCOMPARE(box.partCount(), std::size_t{2});

    // Large file
    box.free();
    box.init(PARTSIZE * 10);
    QCOMPARE(box.partCount(), std::size_t{10});

    // Small file (less than 1 part)
    box.free();
    box.init(1000);
    QCOMPARE(box.partCount(), std::size_t{1});
}

void tst_CorruptionBlackBox::transferredData_basic()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE * 2);

    // Record a transfer; should not crash
    box.transferredData(0, 999, Address::fromHostOrder(0x01020304));

    // Evaluate should produce nothing (no corruption yet)
    auto results = box.evaluateData(0);
    QVERIFY(results.empty());
}

void tst_CorruptionBlackBox::transferredData_merge()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    // Two adjacent blocks from same IP should be merged
    box.transferredData(0, 999, Address::fromHostOrder(0x01020304));
    box.transferredData(1000, 1999, Address::fromHostOrder(0x01020304));

    // Verify both are present and no crash on verification
    box.verifiedData(0, 1999);

    auto results = box.evaluateData(0);
    QVERIFY(results.empty());  // no corruption
}

void tst_CorruptionBlackBox::transferredData_overlap()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    // Two overlapping blocks from different IPs
    box.transferredData(0, 1999, Address::fromHostOrder(0x01020304));
    box.transferredData(500, 1499, Address::fromHostOrder(0x05060708));

    // No crash
    auto results = box.evaluateData(0);
    QVERIFY(results.empty());
}

void tst_CorruptionBlackBox::transferredData_crossPartBoundary()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE * 2);

    // Data crossing the part boundary
    uint64 boundary = PARTSIZE;
    box.transferredData(boundary - 500, boundary + 499, Address::fromHostOrder(0x01020304));

    // Should be split into two parts; no crash
    auto results0 = box.evaluateData(0);
    auto results1 = box.evaluateData(1);
    QVERIFY(results0.empty());
    QVERIFY(results1.empty());
}

void tst_CorruptionBlackBox::verifiedData_basic()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    box.transferredData(0, 9999, Address::fromHostOrder(0x01020304));
    box.verifiedData(0, 9999);

    // Verified data should appear in evaluation with 0% corruption
    // But only if there's a corrupted record to trigger evaluation — so no results
    auto results = box.evaluateData(0);
    QVERIFY(results.empty());
}

void tst_CorruptionBlackBox::corruptedData_basic()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    box.transferredData(0, 9999, Address::fromHostOrder(0x01020304));
    box.corruptedData(0, 9999);

    auto results = box.evaluateData(0);
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].addr, Address::fromHostOrder(0x01020304));
    QVERIFY(results[0].corruptBytes > 0);
}

void tst_CorruptionBlackBox::evaluateData_noBan()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    // Give the client a large amount of verified data and a small corrupted block.
    // Corrupted data is counted as at least EMBLOCKSIZE (184320 bytes),
    // so we need enough verified data to keep the ratio under 32%.
    // With 184320 corrupt and 800000 verified: 184320/(184320+800000) ≈ 18.7%
    const Address clientIP = Address::fromHostOrder(0x01020304);
    box.transferredData(0, 999999, clientIP);
    box.verifiedData(0, 799999);
    box.corruptedData(800000, 801000);

    auto results = box.evaluateData(0);
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].addr, clientIP);
    QVERIFY(results[0].corruptPercent <= 32);
    QVERIFY(!results[0].shouldBan);
}

void tst_CorruptionBlackBox::evaluateData_ban()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    const Address clientIP = Address::fromHostOrder(0x01020304);

    // Give the client mostly corrupted data
    // Corrupted data is counted as at least EMBLOCKSIZE (184320)
    box.transferredData(0, 999, clientIP);
    box.corruptedData(0, 999);

    auto results = box.evaluateData(0);
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].addr, clientIP);
    // 100% corruption
    QCOMPARE(results[0].corruptPercent, 100);
    QVERIFY(results[0].shouldBan);
}

void tst_CorruptionBlackBox::evaluateData_multipleClients()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    const Address goodIP = Address::fromHostOrder(0x01020304);
    const Address badIP  = Address::fromHostOrder(0x05060708);

    // Good client: large verified transfer
    box.transferredData(0, 499999, goodIP);
    box.verifiedData(0, 499999);

    // Bad client: all corrupted
    box.transferredData(500000, 500999, badIP);
    box.corruptedData(500000, 500999);

    auto results = box.evaluateData(0);

    // Only badIP should appear (only it has corrupted records)
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].addr, badIP);
    QVERIFY(results[0].shouldBan);
}

void tst_CorruptionBlackBox::evaluateData_noneCorrupted()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    box.transferredData(0, 9999, Address::fromHostOrder(0x01020304));
    box.verifiedData(0, 9999);

    auto results = box.evaluateData(0);
    QVERIFY(results.empty());
}

void tst_CorruptionBlackBox::senderCount_countsDistinctSenders()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE * 2);

    QCOMPARE(box.senderCount(0), std::size_t{0});

    // Two ranges from one peer are still one peer: this is what lets a failed part
    // hash be pinned on somebody without waiting for AICH to narrow it down.
    box.transferredData(0, 999, Address::fromHostOrder(0x01020304));
    box.transferredData(1000, 1999, Address::fromHostOrder(0x01020304));
    QCOMPARE(box.senderCount(0), std::size_t{1});

    box.transferredData(2000, 2999, Address::fromHostOrder(0x05060708));
    QCOMPARE(box.senderCount(0), std::size_t{2});

    // Per part, not per file.
    QCOMPARE(box.senderCount(1), std::size_t{0});

    // Out of range is 0, not a crash.
    QCOMPARE(box.senderCount(99), std::size_t{0});
}

void tst_CorruptionBlackBox::senderCount_ipv6SenderIsKept()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE);

    // The reason the key is an Address and not a uint32: an IPv6 peer used to
    // collapse to 0 and share a bucket with every other v6 peer and with "unknown".
    const Address v6a = Address::fromString(QStringLiteral("2001:db8::1"));
    const Address v6b = Address::fromString(QStringLiteral("2001:db8::2"));
    QVERIFY(v6a.isIPv6());
    QVERIFY(v6b.isIPv6());

    box.transferredData(0, 999, v6a);
    box.transferredData(1000, 1999, v6b);
    QCOMPARE(box.senderCount(0), std::size_t{2});

    box.corruptedData(1000, 1999);
    const auto results = box.evaluateData(0);
    QCOMPARE(results.size(), std::size_t{1});
    QCOMPARE(results[0].addr, v6b);
}

void tst_CorruptionBlackBox::soleSenderOfWholePart_needsFullCoverage()
{
    const Address one = Address::fromHostOrder(0x01020304);
    const Address two = Address::fromHostOrder(0x05060708);

    {
        // Whole part from one sender, in two touching pieces.
        CorruptionBlackBox box;
        box.init(PARTSIZE);
        box.transferredData(0, PARTSIZE / 2 - 1, one);
        box.transferredData(PARTSIZE / 2, PARTSIZE - 1, one);
        QCOMPARE(box.soleSenderOfWholePart(0, PARTSIZE), one);
    }

    {
        // A hole at the front is what a resumed download leaves behind: those bytes
        // came from a previous session and have no sender.
        CorruptionBlackBox box;
        box.init(PARTSIZE);
        box.transferredData(PARTSIZE / 2, PARTSIZE - 1, one);
        QVERIFY(box.soleSenderOfWholePart(0, PARTSIZE).isNull());
    }

    {
        // A hole at the end counts too.
        CorruptionBlackBox box;
        box.init(PARTSIZE);
        box.transferredData(0, PARTSIZE / 2 - 1, one);
        QVERIFY(box.soleSenderOfWholePart(0, PARTSIZE).isNull());
    }

    {
        // Two senders is never sole, however complete the cover.
        CorruptionBlackBox box;
        box.init(PARTSIZE);
        box.transferredData(0, PARTSIZE / 2 - 1, one);
        box.transferredData(PARTSIZE / 2, PARTSIZE - 1, two);
        QVERIFY(box.soleSenderOfWholePart(0, PARTSIZE).isNull());
    }

    {
        // A short tail part is measured against its own length, not PARTSIZE.
        CorruptionBlackBox box;
        box.init(PARTSIZE + 5000);
        box.transferredData(PARTSIZE, PARTSIZE + 4999, one);
        QCOMPARE(box.soleSenderOfWholePart(1, 5000), one);
    }
}

void tst_CorruptionBlackBox::free_clearsAll()
{
    CorruptionBlackBox box;
    box.init(PARTSIZE * 3);
    QCOMPARE(box.partCount(), std::size_t{3});

    box.transferredData(0, 999, Address::fromHostOrder(0x01020304));
    box.free();

    QCOMPARE(box.partCount(), std::size_t{0});
}

QTEST_MAIN(tst_CorruptionBlackBox)
#include "tst_CorruptionBlackBox.moc"
