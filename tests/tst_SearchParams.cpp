/// @file tst_SearchParams.cpp
/// @brief Tests for search/SearchParams — construction, serialization, enum values.

#include "TestHelpers.h"
#include "search/SearchParams.h"
#include "utils/SafeFile.h"

#include <QTest>

using namespace eMule;

class tst_SearchParams : public QObject {
    Q_OBJECT

private slots:
    void construct_default();
    void storeAndLoad_roundTrip();
    void searchType_values();
    void fields_initialValues();
};

void tst_SearchParams::construct_default()
{
    SearchParams p;
    QCOMPARE(p.searchID, UINT32_MAX);
    QCOMPARE(p.type, SearchType::Ed2kServer);
    QVERIFY(!p.clientSharedFiles);
    QVERIFY(!p.matchKeywords);
    QCOMPARE(p.minSize, uint64{0});
    QCOMPARE(p.maxSize, uint64{0});
    QCOMPARE(p.availability, uint32{0});
    QCOMPARE(p.completeSources, uint32{0});
    QVERIFY(p.expression.isEmpty());
    QVERIFY(p.fileType.isEmpty());
}

void tst_SearchParams::storeAndLoad_roundTrip()
{
    SearchParams original;
    original.searchID = 42;
    original.type = SearchType::Kademlia;
    original.clientSharedFiles = true;
    original.specialTitle = QStringLiteral("My Search");
    original.expression = QStringLiteral("test file AND audio");
    original.fileType = QStringLiteral("Audio");

    // Write to memory
    SafeMemFile mem;
    original.storePartially(mem);

    // Read back
    mem.seek(0, 0);
    SearchParams loaded(mem);

    QCOMPARE(loaded.searchID, uint32{42});
    QCOMPARE(loaded.type, SearchType::Kademlia);
    QVERIFY(loaded.clientSharedFiles);
    QCOMPARE(loaded.specialTitle, QStringLiteral("My Search"));
    QCOMPARE(loaded.expression, QStringLiteral("test file AND audio"));
    QCOMPARE(loaded.fileType, QStringLiteral("Audio"));
}

void tst_SearchParams::searchType_values()
{
    QCOMPARE(static_cast<uint8>(SearchType::Automatic), uint8{0});
    QCOMPARE(static_cast<uint8>(SearchType::Ed2kServer), uint8{1});
    QCOMPARE(static_cast<uint8>(SearchType::Ed2kGlobal), uint8{2});
    QCOMPARE(static_cast<uint8>(SearchType::Kademlia), uint8{3});
    QCOMPARE(static_cast<uint8>(SearchType::ContentDB), uint8{4});
}

void tst_SearchParams::fields_initialValues()
{
    SearchParams p;
    QVERIFY(p.searchTitle.isEmpty());
    QVERIFY(p.keyword.isEmpty());
    QVERIFY(p.booleanExpr.isEmpty());
    QVERIFY(p.extension.isEmpty());
    QVERIFY(p.minSizeStr.isEmpty());
    QVERIFY(p.maxSizeStr.isEmpty());
    QVERIFY(p.codec.isEmpty());
    QVERIFY(p.title.isEmpty());
    QVERIFY(p.album.isEmpty());
    QVERIFY(p.artist.isEmpty());
    QCOMPARE(p.minBitrate, uint32{0});
    QCOMPARE(p.minLength, uint32{0});
}

QTEST_MAIN(tst_SearchParams)
#include "tst_SearchParams.moc"
