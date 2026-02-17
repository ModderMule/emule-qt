#include <QTest>

#include "TestHelpers.h"
#include "utils/StringUtils.h"

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

/// @brief Tests for StringUtils.h string conversion utilities.
class StringConversionTest : public QObject {
    Q_OBJECT

private slots:
    // ---- fromStdString / toStdString roundtrip ----

    void testRoundtripAscii()
    {
        const std::string orig = "Hello, eMule!";
        const QString q = eMule::fromStdString(orig);
        const std::string back = eMule::toStdString(q);
        QCOMPARE(back, orig);
    }

    void testRoundtripUtf8()
    {
        // German umlauts + Chinese characters
        const std::string orig = "Ä Ö Ü ß 你好世界";
        const QString q = eMule::fromStdString(orig);
        const std::string back = eMule::toStdString(q);
        QCOMPARE(back, orig);
    }

    void testEmptyString()
    {
        const QString q = eMule::fromStdString("");
        QVERIFY(q.isEmpty());
        const std::string s = eMule::toStdString(QStringView());
        QVERIFY(s.empty());
    }

    // ---- Hex conversion ----

    void testToHexString()
    {
        const std::vector<eMule::uint8> data = {0xDE, 0xAD, 0xBE, 0xEF};
        const QString hex = eMule::toHexString(data);
        QCOMPARE(hex, QStringLiteral("deadbeef"));
    }

    void testFromHexString()
    {
        const QByteArray result = eMule::fromHexString(QStringLiteral("DEADBEEF"));
        QCOMPARE(result.size(), 4);
        QCOMPARE(static_cast<unsigned char>(result[0]), 0xDE);
        QCOMPARE(static_cast<unsigned char>(result[1]), 0xAD);
        QCOMPARE(static_cast<unsigned char>(result[2]), 0xBE);
        QCOMPARE(static_cast<unsigned char>(result[3]), 0xEF);
    }

    void testHexRoundtrip()
    {
        const std::vector<eMule::uint8> original = {0x00, 0xFF, 0x42, 0x7F};
        const QString hex = eMule::toHexString(original);
        const QByteArray back = eMule::fromHexString(hex);
        QCOMPARE(back.size(), static_cast<qsizetype>(original.size()));
        for (std::size_t i = 0; i < original.size(); ++i) {
            QCOMPARE(static_cast<unsigned char>(back[static_cast<qsizetype>(i)]),
                     original[i]);
        }
    }

    void testFromHexStringInvalid()
    {
        // Odd length
        QVERIFY(eMule::fromHexString(QStringLiteral("ABC")).isEmpty());
        // Invalid character
        QVERIFY(eMule::fromHexString(QStringLiteral("ZZZZ")).isEmpty());
    }

    // ---- formatByteSize ----

    void testFormatByteSizeZero()
    {
        QCOMPARE(eMule::formatByteSize(0), QStringLiteral("0 B"));
    }

    void testFormatByteSizeBytes()
    {
        QCOMPARE(eMule::formatByteSize(512), QStringLiteral("512 B"));
    }

    void testFormatByteSizeKB()
    {
        QCOMPARE(eMule::formatByteSize(1024), QStringLiteral("1 KB"));
    }

    void testFormatByteSizeMB()
    {
        const QString result = eMule::formatByteSize(1024ULL * 1024 * 5 + 1024 * 512);
        // Should be approximately "5.50 MB"
        QVERIFY(result.contains(QStringLiteral("MB")));
    }

    void testFormatByteSizeGB()
    {
        const QString result = eMule::formatByteSize(1024ULL * 1024 * 1024 * 2);
        QCOMPARE(result, QStringLiteral("2.00 GB"));
    }

    void testFormatByteSizeTB()
    {
        const QString result = eMule::formatByteSize(1024ULL * 1024 * 1024 * 1024);
        QCOMPARE(result, QStringLiteral("1.00 TB"));
    }

    // ---- formatDuration ----

    void testFormatDurationZero()
    {
        QCOMPARE(eMule::formatDuration(0s), QStringLiteral("0s"));
    }

    void testFormatDurationSeconds()
    {
        QCOMPARE(eMule::formatDuration(45s), QStringLiteral("45s"));
    }

    void testFormatDurationMinutes()
    {
        QCOMPARE(eMule::formatDuration(125s), QStringLiteral("2m 5s"));
    }

    void testFormatDurationHours()
    {
        QCOMPARE(eMule::formatDuration(3661s), QStringLiteral("1h 1m 1s"));
    }

    void testFormatDurationDays()
    {
        QCOMPARE(eMule::formatDuration(std::chrono::seconds(90061)),
                 QStringLiteral("1d 1h 1m 1s"));
    }

    void testFormatDurationNegative()
    {
        QCOMPARE(eMule::formatDuration(std::chrono::seconds(-5)),
                 QStringLiteral("0s"));
    }

    // ---- EMUSTR macro ----

    void testEmustrMacro()
    {
        const QString s = EMUSTR("test string");
        QCOMPARE(s, QStringLiteral("test string"));
    }
};

QTEST_MAIN(StringConversionTest)
#include "tst_StringConversion.moc"
