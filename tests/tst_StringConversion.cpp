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

    // ---- formatByteSize / formatByteRate: MFC CastItoXBytes ----

    void testFormatByteSizeZero()
    {
        QCOMPARE(eMule::formatByteSize(0), QStringLiteral("0 Bytes"));
    }

    void testFormatByteSizeNegativeIsZero()
    {
        QCOMPARE(eMule::formatByteSize(qint64{-5}), QStringLiteral("0 Bytes"));
        QCOMPARE(eMule::formatByteSize(-1.5), QStringLiteral("0 Bytes"));
    }

    void testFormatByteSizeBytes()
    {
        QCOMPARE(eMule::formatByteSize(512), QStringLiteral("512 Bytes"));
        QCOMPARE(eMule::formatByteSize(1023), QStringLiteral("1023 Bytes"));
    }

    void testFormatByteSizeKB()
    {
        QCOMPARE(eMule::formatByteSize(1024), QStringLiteral("1.00 KB"));
        QCOMPARE(eMule::formatByteSize(1536), QStringLiteral("1.50 KB"));
    }

    void testFormatByteSizeMB()
    {
        QCOMPARE(eMule::formatByteSize(1024ULL * 1024 * 5 + 1024 * 512),
                 QStringLiteral("5.50 MB"));
    }

    void testFormatByteSizeGB()
    {
        QCOMPARE(eMule::formatByteSize(1024ULL * 1024 * 1024 * 2), QStringLiteral("2.00 GB"));
    }

    void testFormatByteSizeTB()
    {
        QCOMPARE(eMule::formatByteSize(1024ULL * 1024 * 1024 * 1024), QStringLiteral("1.00 TB"));
    }

    /// A unit runs to 1000 of itself, not 1024: MFC never shows "1010.00 KB".
    void testFormatByteSizeStepsAtThousand()
    {
        QCOMPARE(eMule::formatByteSize(1023999), QStringLiteral("1000.00 KB"));
        QCOMPARE(eMule::formatByteSize(1024000), QStringLiteral("0.98 MB"));
        QCOMPARE(eMule::formatByteSize(1048576000ULL), QStringLiteral("0.98 GB"));
        QCOMPARE(eMule::formatByteSize(1073741824000ULL), QStringLiteral("0.98 TB"));
    }

    void testFormatByteSizeDecimals()
    {
        QCOMPARE(eMule::formatByteSize(1536, 1), QStringLiteral("1.5 KB"));
        QCOMPARE(eMule::formatByteSize(1536, 0), QStringLiteral("2 KB"));
        QCOMPARE(eMule::formatByteSize(1000, 1), QStringLiteral("1000 Bytes"));
    }

    void testFormatByteRate()
    {
        QCOMPARE(eMule::formatByteRate(0), QStringLiteral("0 B/s"));
        QCOMPARE(eMule::formatByteRate(500), QStringLiteral("500 B/s"));
        QCOMPARE(eMule::formatByteRate(1536), QStringLiteral("1.50 KB/s"));
        QCOMPARE(eMule::formatByteRate(2 * 1024 * 1024), QStringLiteral("2.00 MB/s"));
        QCOMPARE(eMule::formatByteRate(612.3 * 1024.0), QStringLiteral("612.30 KB/s"));
    }

    // ---- formatShortNumber (MFC CastItoIShort) ----

    void testFormatShortNumber()
    {
        QCOMPARE(eMule::formatShortNumber(0), QStringLiteral("0"));
        QCOMPARE(eMule::formatShortNumber(-5), QStringLiteral("0"));
        QCOMPARE(eMule::formatShortNumber(999), QStringLiteral("999"));
        QCOMPARE(eMule::formatShortNumber(1000), QStringLiteral("1.00 k"));   // 1000-based, not 1024
        QCOMPARE(eMule::formatShortNumber(1'234'567), QStringLiteral("1.23 M"));
        QCOMPARE(eMule::formatShortNumber(int64_t{5'000'000'000}), QStringLiteral("5.00 G"));
        QCOMPARE(eMule::formatShortNumber(int64_t{2'500'000'000'000}), QStringLiteral("2.50 T"));
    }

    // ---- formatSecondsHM (MFC CastSecondsToHM) ----

    void testFormatSecondsHM()
    {
        QCOMPARE(eMule::formatSecondsHM(-1), QStringLiteral("?"));
        QCOMPARE(eMule::formatSecondsHM(59), QStringLiteral("59 secs"));
        QCOMPARE(eMule::formatSecondsHM(60), QStringLiteral("1:00 mins"));
        QCOMPARE(eMule::formatSecondsHM(3599), QStringLiteral("59:59 mins"));
        QCOMPARE(eMule::formatSecondsHM(3600), QStringLiteral("1:00 h"));
        QCOMPARE(eMule::formatSecondsHM(86399), QStringLiteral("23:59 h"));
        QCOMPARE(eMule::formatSecondsHM(90000), QStringLiteral("1 d 1 h"));
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
