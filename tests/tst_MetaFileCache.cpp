/// @file tst_MetaFileCache.cpp
/// @brief net/MetaFileCache: a fetched .torrent/.nzb kept a few hours, by size.

#include "net/MetaFileCache.h"

#include <QTest>

using namespace eMule;

class tst_MetaFileCache : public QObject {
    Q_OBJECT

private slots:
    void hitReturnsEntry();
    void expiresAfterTtl();
    void sizeCapEvictsOldest();
    void ignoresEmptyAndOversized();
    void keys();
};

void tst_MetaFileCache::hitReturnsEntry()
{
    MetaFileCache cache;
    QVERIFY(!cache.find(QStringLiteral("a")));
    cache.insert(QStringLiteral("a"), "payload", QStringLiteral("x.torrent"), QStringLiteral("application/x-bittorrent"));
    const auto hit = cache.find(QStringLiteral("a"));
    QVERIFY(hit);
    QCOMPARE(hit->content, QByteArray("payload"));
    QCOMPARE(hit->fileName, QStringLiteral("x.torrent"));
    QCOMPARE(hit->contentType, QStringLiteral("application/x-bittorrent"));

    // replaced, not counted twice
    cache.insert(QStringLiteral("a"), "other");
    QCOMPARE(cache.count(), 1);
    QCOMPARE(cache.totalBytes(), 5);
}

void tst_MetaFileCache::expiresAfterTtl()
{
    qint64 now = 1'000'000;
    MetaFileCache cache(1000, 1024);
    cache.setClock([&now] { return now; });
    cache.insert(QStringLiteral("a"), "payload");
    now += 999;
    QVERIFY(cache.find(QStringLiteral("a")));
    now += 1;
    QVERIFY(!cache.find(QStringLiteral("a")));
    QCOMPARE(cache.totalBytes(), 0);
}

void tst_MetaFileCache::sizeCapEvictsOldest()
{
    qint64 now = 0;
    MetaFileCache cache(MetaFileCache::kDefaultTtlMs, 10);
    cache.setClock([&now] { return now; });
    cache.insert(QStringLiteral("old"), "1234");
    ++now;
    cache.insert(QStringLiteral("mid"), "1234");
    ++now;
    cache.insert(QStringLiteral("new"), "1234");   // 12 > 10: the oldest goes
    QVERIFY(!cache.find(QStringLiteral("old")));
    QVERIFY(cache.find(QStringLiteral("mid")));
    QVERIFY(cache.find(QStringLiteral("new")));
    QCOMPARE(cache.totalBytes(), 8);
}

void tst_MetaFileCache::ignoresEmptyAndOversized()
{
    MetaFileCache cache(MetaFileCache::kDefaultTtlMs, 4);
    cache.insert(QStringLiteral("empty"), {});
    cache.insert(QStringLiteral("big"), "12345");
    cache.insert({}, "1");
    QCOMPARE(cache.count(), 0);
}

void tst_MetaFileCache::keys()
{
    QCOMPARE(MetaFileCache::enodeKey(QStringLiteral("1.2.3.4:4661"), QByteArray(2, '\x01')),
             QStringLiteral("enode:1.2.3.4:4661:0101"));
    QCOMPARE(MetaFileCache::indexerKey(QStringLiteral("slug:guid")), QStringLiteral("indexer:slug:guid"));
}

QTEST_GUILESS_MAIN(tst_MetaFileCache)
#include "tst_MetaFileCache.moc"
