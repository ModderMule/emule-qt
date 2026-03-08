#include <QTest>

#include "TestHelpers.h"
#include "utils/Types.h"

/// @brief Smoke test — verifies the build system, Qt Test framework,
///        and core type definitions all work correctly.
class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void testQtVersion()
    {
        // Verify we are running against Qt 6
        QVERIFY(QT_VERSION >= QT_VERSION_CHECK(6, 5, 0));
    }

    void testTypeWidths()
    {
        // Verify fixed-width types match ED2K protocol expectations
        QCOMPARE(sizeof(eMule::uint8),  std::size_t{1});
        QCOMPARE(sizeof(eMule::uint16), std::size_t{2});
        QCOMPARE(sizeof(eMule::uint32), std::size_t{4});
        QCOMPARE(sizeof(eMule::uint64), std::size_t{8});
    }

    void testConfigHeader()
    {
        // Verify config.h was generated and EMULE_VERSION is defined
        QVERIFY(EMULE_VERSION_MAJOR >= 0);
        QCOMPARE(QString::fromLatin1(EMULE_VERSION_STRING), QStringLiteral("0.1.1"));
    }

    void testTempDir()
    {
        // Verify the test helper TempDir utility works
        eMule::testing::TempDir tmp;
        QVERIFY(QDir(tmp.path()).exists());
    }

    void testTestDataDir()
    {
        // Verify test data path is set by CMake
        const QString dir = eMule::testing::testDataDir();
        QVERIFY(!dir.isEmpty());
    }
};

QTEST_MAIN(SmokeTest)
#include "tst_Smoke.moc"