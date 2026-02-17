#include <QTest>

#include "TestHelpers.h"
#include "utils/Types.h"
#include "utils/WinCompat.h"

#include <type_traits>

/// @brief Tests for Types.h and WinCompat.h type aliases.
class TypeDefsTest : public QObject {
    Q_OBJECT

private slots:
    // ---- Types.h ----

    void testUint8Size()  { QCOMPARE(sizeof(eMule::uint8),  std::size_t{1}); }
    void testUint16Size() { QCOMPARE(sizeof(eMule::uint16), std::size_t{2}); }
    void testUint32Size() { QCOMPARE(sizeof(eMule::uint32), std::size_t{4}); }
    void testUint64Size() { QCOMPARE(sizeof(eMule::uint64), std::size_t{8}); }

    void testInt8Size()  { QCOMPARE(sizeof(eMule::int8),  std::size_t{1}); }
    void testInt16Size() { QCOMPARE(sizeof(eMule::int16), std::size_t{2}); }
    void testInt32Size() { QCOMPARE(sizeof(eMule::int32), std::size_t{4}); }
    void testInt64Size() { QCOMPARE(sizeof(eMule::int64), std::size_t{8}); }

    void testUcharSize() { QCOMPARE(sizeof(eMule::uchar), std::size_t{1}); }

    void testSint8Size()  { QCOMPARE(sizeof(eMule::sint8),  std::size_t{1}); }
    void testSint16Size() { QCOMPARE(sizeof(eMule::sint16), std::size_t{2}); }
    void testSint32Size() { QCOMPARE(sizeof(eMule::sint32), std::size_t{4}); }
    void testSint64Size() { QCOMPARE(sizeof(eMule::sint64), std::size_t{8}); }

    void testUsizeSize() { QCOMPARE(sizeof(eMule::usize), sizeof(std::size_t)); }
    void testIsizeSize() { QCOMPARE(sizeof(eMule::isize), sizeof(std::ptrdiff_t)); }

    void testEMFileSizeIsUint64()
    {
        QVERIFY((std::is_same_v<eMule::EMFileSize, eMule::uint64>));
    }

    void testUnsignedTypes()
    {
        QVERIFY(std::is_unsigned_v<eMule::uint8>);
        QVERIFY(std::is_unsigned_v<eMule::uint16>);
        QVERIFY(std::is_unsigned_v<eMule::uint32>);
        QVERIFY(std::is_unsigned_v<eMule::uint64>);
        QVERIFY(std::is_unsigned_v<eMule::uchar>);
    }

    void testSignedTypes()
    {
        QVERIFY(std::is_signed_v<eMule::int8>);
        QVERIFY(std::is_signed_v<eMule::int16>);
        QVERIFY(std::is_signed_v<eMule::int32>);
        QVERIFY(std::is_signed_v<eMule::int64>);
        QVERIFY(std::is_signed_v<eMule::sint8>);
        QVERIFY(std::is_signed_v<eMule::sint16>);
        QVERIFY(std::is_signed_v<eMule::sint32>);
        QVERIFY(std::is_signed_v<eMule::sint64>);
    }

    // ---- WinCompat.h ----

    void testDwordSize()  { QCOMPARE(sizeof(eMule::DWORD), std::size_t{4}); }
    void testUintSize()   { QCOMPARE(sizeof(eMule::UINT),  std::size_t{4}); }
    void testByteSize()   { QCOMPARE(sizeof(eMule::BYTE),  std::size_t{1}); }
    void testWordSize()   { QCOMPARE(sizeof(eMule::WORD),  std::size_t{2}); }
    void testLongSize()   { QCOMPARE(sizeof(eMule::LONG),  std::size_t{4}); }

    void testBoolIsBool()
    {
        QVERIFY((std::is_same_v<eMule::BOOL, bool>));
    }

    void testColorrefSize()
    {
        QCOMPARE(sizeof(eMule::COLORREF), std::size_t{4});
        QVERIFY(std::is_unsigned_v<eMule::COLORREF>);
    }

    void testLonglongSize()
    {
        QCOMPARE(sizeof(eMule::LONGLONG),  std::size_t{8});
        QCOMPARE(sizeof(eMule::ULONGLONG), std::size_t{8});
        QVERIFY(std::is_signed_v<eMule::LONGLONG>);
        QVERIFY(std::is_unsigned_v<eMule::ULONGLONG>);
    }

    void testPointerSizedTypes()
    {
        QCOMPARE(sizeof(eMule::LPARAM),  sizeof(void*));
        QCOMPARE(sizeof(eMule::WPARAM),  sizeof(void*));
        QCOMPARE(sizeof(eMule::LRESULT), sizeof(void*));
    }
};

QTEST_MAIN(TypeDefsTest)
#include "tst_TypeDefs.moc"
