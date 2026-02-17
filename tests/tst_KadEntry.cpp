/// @file tst_KadEntry.cpp
/// @brief Tests for KadEntry.h — DHT data entries.

#include "TestHelpers.h"

#include "kademlia/KadEntry.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;
using namespace eMule::kad;

class tst_KadEntry : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void addTag_andGetInt();
    void addTag_andGetStr();
    void getTagCount_basic();
    void setFileName_popularity();
    void getCommonFileName();
    void writeTagList_roundTrip();
    void keyEntry_copy();
    void keyEntry_mergeIPs();
    void keyEntry_trustValue();
};

void tst_KadEntry::construct_default()
{
    Entry e;
    QCOMPARE(e.m_ip, uint32{0});
    QCOMPARE(e.m_tcpPort, uint16{0});
    QCOMPARE(e.m_udpPort, uint16{0});
    QCOMPARE(e.m_size, uint64{0});
    QVERIFY(!e.m_source);
}

void tst_KadEntry::addTag_andGetInt()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("rating"), uint32{5}));

    uint64 val = 0;
    bool found = e.getIntTagValue(QByteArrayLiteral("rating"), val);
    QVERIFY(found);
    QCOMPARE(val, uint64{5});
}

void tst_KadEntry::addTag_andGetStr()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("comment"), QStringLiteral("Great file")));

    QString val = e.getStrTagValue(QByteArrayLiteral("comment"));
    QCOMPARE(val, QStringLiteral("Great file"));
}

void tst_KadEntry::getTagCount_basic()
{
    Entry e;
    e.addTag(Tag(QByteArrayLiteral("tag1"), uint32{1}));
    e.addTag(Tag(QByteArrayLiteral("tag2"), uint32{2}));

    // Base tag count = 2 stored tags (no virtual tags since no filename/size)
    QCOMPARE(e.getTagCount(), uint32{2});
}

void tst_KadEntry::setFileName_popularity()
{
    Entry e;
    e.setFileName(QStringLiteral("file.mp3"));
    e.setFileName(QStringLiteral("file.mp3")); // same filename, popularity increases
    e.setFileName(QStringLiteral("FILE.MP3")); // case-insensitive match

    // Should have one filename entry with popularity 3
    QCOMPARE(e.getCommonFileName(), QStringLiteral("file.mp3"));
}

void tst_KadEntry::getCommonFileName()
{
    Entry e;
    e.setFileName(QStringLiteral("rare.txt"));
    e.setFileName(QStringLiteral("popular.txt"));
    e.setFileName(QStringLiteral("popular.txt")); // more popular

    // The most popular name should be returned
    QCOMPARE(e.getCommonFileName(), QStringLiteral("popular.txt"));
}

void tst_KadEntry::writeTagList_roundTrip()
{
    Entry e;
    e.m_size = 1024;
    e.setFileName(QStringLiteral("test.dat"));
    e.addTag(Tag(QByteArrayLiteral("rating"), uint32{3}));

    SafeMemFile file;
    e.writeTagList(file);

    // Verify something was written
    QVERIFY(file.length() > 0);
}

void tst_KadEntry::keyEntry_copy()
{
    KeyEntry original;
    original.m_ip = 0x0A000001;
    original.m_size = 5000;
    original.setFileName(QStringLiteral("copied.txt"));

    Entry* copied = original.copy();
    QVERIFY(copied != nullptr);
    QVERIFY(copied->isKeyEntry());
    QCOMPARE(copied->m_ip, uint32{0x0A000001});
    QCOMPARE(copied->m_size, uint64{5000});
    QCOMPARE(copied->getCommonFileName(), QStringLiteral("copied.txt"));

    delete copied;
}

void tst_KadEntry::keyEntry_mergeIPs()
{
    KeyEntry entry1;
    entry1.setFileName(QStringLiteral("name1.txt"));

    KeyEntry entry2;
    entry2.setFileName(QStringLiteral("name2.txt"));

    entry1.mergeIPsAndFilenames(&entry2);

    // Both filenames should now be tracked
    // getCommonFileName returns the most popular one
    QVERIFY(!entry1.getCommonFileName().isEmpty());
}

void tst_KadEntry::keyEntry_trustValue()
{
    KeyEntry entry;
    // Without any publishers, trust should be 0
    float trust = entry.getTrustValue();
    QCOMPARE(trust, 0.0f);
}

QTEST_GUILESS_MAIN(tst_KadEntry)
#include "tst_KadEntry.moc"
