/// @file tst_RepeatChunkGuard.cpp
/// @brief Tests for client/RepeatChunkGuard — full part deliveries per peer and file.

#include "TestHelpers.h"
#include "client/RepeatChunkGuard.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <array>

using namespace eMule;

namespace {

std::array<uint8, 16> hashOf(uint8 seed)
{
    std::array<uint8, 16> h{};
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = static_cast<uint8>(seed * 31 + i);
    return h;
}

// Send [start, end) in EMBLOCKSIZE blocks.
void sendRange(RepeatChunkGuard& g, const uint8* user, const uint8* file, uint64 size,
               uint64 start, uint64 end)
{
    for (uint64 o = start; o < end; o += EMBLOCKSIZE)
        g.noteSent(user, file, size, o, std::min<uint64>(o + EMBLOCKSIZE, end));
}

} // namespace

class tst_RepeatChunkGuard : public QObject {
    Q_OBJECT

private slots:
    void fullPartCounts();
    void partialPartDoesNotCount();
    void resentBlockDoesNotCount();
    void outOfOrderBlocksCount();
    void lastShortPart();
    void blockAcrossPartBoundary();
    void peersAndFilesSeparate();
    void lruEviction();
};

void tst_RepeatChunkGuard::fullPartCounts()
{
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    const uint64 size = PARTSIZE * 3;
    sendRange(g, u.data(), f.data(), size, PARTSIZE, 2 * PARTSIZE);
    QCOMPARE(g.deliveries(u.data(), f.data(), PARTSIZE + 5), 1u);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 0u);
    sendRange(g, u.data(), f.data(), size, PARTSIZE, 2 * PARTSIZE);
    QCOMPARE(g.deliveries(u.data(), f.data(), 2 * PARTSIZE - 1), 2u);
}

void tst_RepeatChunkGuard::partialPartDoesNotCount()
{
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    sendRange(g, u.data(), f.data(), PARTSIZE * 2, 0, PARTSIZE - 1);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 0u);
    g.noteSent(u.data(), f.data(), PARTSIZE * 2, PARTSIZE - 1, PARTSIZE);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 1u);
}

void tst_RepeatChunkGuard::resentBlockDoesNotCount()
{
    // AICH repair: one block again after a full part is not a second delivery.
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    sendRange(g, u.data(), f.data(), PARTSIZE, 0, PARTSIZE);
    for (int i = 0; i < 10; ++i)
        g.noteSent(u.data(), f.data(), PARTSIZE, 0, EMBLOCKSIZE);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 1u);
}

void tst_RepeatChunkGuard::outOfOrderBlocksCount()
{
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    const uint64 half = PARTSIZE / 2;
    sendRange(g, u.data(), f.data(), PARTSIZE, half, PARTSIZE);
    sendRange(g, u.data(), f.data(), PARTSIZE, 0, half);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 1u);
}

void tst_RepeatChunkGuard::lastShortPart()
{
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    const uint64 size = PARTSIZE + 1000;
    g.noteSent(u.data(), f.data(), size, PARTSIZE, size);
    QCOMPARE(g.deliveries(u.data(), f.data(), size - 1), 1u);
}

void tst_RepeatChunkGuard::blockAcrossPartBoundary()
{
    RepeatChunkGuard g;
    const auto u = hashOf(1), f = hashOf(2);
    const uint64 size = PARTSIZE * 2;
    sendRange(g, u.data(), f.data(), size, 0, PARTSIZE - 100);
    sendRange(g, u.data(), f.data(), size, PARTSIZE + 100, size);
    g.noteSent(u.data(), f.data(), size, PARTSIZE - 100, PARTSIZE + 100);
    QCOMPARE(g.deliveries(u.data(), f.data(), 0), 1u);
    QCOMPARE(g.deliveries(u.data(), f.data(), PARTSIZE), 1u);
}

void tst_RepeatChunkGuard::peersAndFilesSeparate()
{
    RepeatChunkGuard g;
    const auto u1 = hashOf(1), u2 = hashOf(3), f1 = hashOf(2), f2 = hashOf(4);
    sendRange(g, u1.data(), f1.data(), PARTSIZE, 0, PARTSIZE);
    QCOMPARE(g.deliveries(u1.data(), f1.data(), 0), 1u);
    QCOMPARE(g.deliveries(u2.data(), f1.data(), 0), 0u);
    QCOMPARE(g.deliveries(u1.data(), f2.data(), 0), 0u);
}

void tst_RepeatChunkGuard::lruEviction()
{
    RepeatChunkGuard g;
    const auto f = hashOf(2);
    const auto first = hashOf(100);
    g.noteSent(first.data(), f.data(), PARTSIZE, 0, PARTSIZE);
    for (uint32 i = 0; i < RepeatChunkGuard::kMaxEntries - 1; ++i) {
        std::array<uint8, 16> u{};
        u[0] = static_cast<uint8>(i);
        u[1] = static_cast<uint8>(i >> 8);
        u[15] = 0xEE;
        g.noteSent(u.data(), f.data(), PARTSIZE, 0, EMBLOCKSIZE);
    }
    QCOMPARE(g.count(), RepeatChunkGuard::kMaxEntries);
    QCOMPARE(g.deliveries(first.data(), f.data(), 0), 1u);

    const auto newcomer = hashOf(200);
    g.noteSent(newcomer.data(), f.data(), PARTSIZE, 0, EMBLOCKSIZE);
    QCOMPARE(g.count(), RepeatChunkGuard::kMaxEntries);
    QCOMPARE(g.deliveries(first.data(), f.data(), 0), 0u);   // oldest dropped
}

QTEST_MAIN(tst_RepeatChunkGuard)
#include "tst_RepeatChunkGuard.moc"
