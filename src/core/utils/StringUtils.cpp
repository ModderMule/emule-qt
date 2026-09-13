#include "pch.h"
/// @file StringUtils.cpp
/// @brief String conversion utility implementations.

#include "StringUtils.h"

#include <QCoreApplication>

#include <array>


namespace eMule {

namespace {

using UnitLabels = std::array<const char*, 5>;

QString castItoXBytes(double count, int decimals, const UnitLabels& units);

} // namespace

QString fromStdString(std::string_view sv)
{
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

std::string toStdString(QStringView qsv)
{
    const QByteArray utf8 = qsv.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString toHexString(std::span<const uint8> data)
{
    QString result;
    result.reserve(static_cast<qsizetype>(data.size()) * 2);
    for (auto byte : data) {
        constexpr std::array<char16_t, 16> digits = {
            u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7',
            u'8', u'9', u'a', u'b', u'c', u'd', u'e', u'f'
        };
        result += QChar(digits[byte >> 4]);
        result += QChar(digits[byte & 0x0F]);
    }
    return result;
}

QByteArray fromHexString(QStringView hex)
{
    if (hex.size() % 2 != 0)
        return {};

    QByteArray result;
    result.reserve(static_cast<qsizetype>(hex.size() / 2));

    for (qsizetype i = 0; i < hex.size(); i += 2) {
        bool ok1 = false;
        bool ok2 = false;
        const int hi = QString(hex[i]).toInt(&ok1, 16);
        const int lo = QString(hex[i + 1]).toInt(&ok2, 16);
        if (!ok1 || !ok2)
            return {};
        result.append(static_cast<char>((hi << 4) | lo));
    }
    return result;
}

QString formatByteSize(double bytes, int decimals)
{
    // MFC IDS_BYTES..IDS_TBYTES (emule.rc:2498-2502)
    static constexpr UnitLabels kUnits = {
        QT_TRANSLATE_NOOP("Units", "Bytes"),
        QT_TRANSLATE_NOOP("Units", "KB"),
        QT_TRANSLATE_NOOP("Units", "MB"),
        QT_TRANSLATE_NOOP("Units", "GB"),
        QT_TRANSLATE_NOOP("Units", "TB"),
    };
    return castItoXBytes(bytes, decimals, kUnits);
}

QString formatByteRate(double bytesPerSec, int decimals)
{
    // MFC IDS_BYTESPERSEC..IDS_TBYTESPERSEC (emule.rc:3444-3448)
    static constexpr UnitLabels kUnits = {
        QT_TRANSLATE_NOOP("Units", "B/s"),
        QT_TRANSLATE_NOOP("Units", "KB/s"),
        QT_TRANSLATE_NOOP("Units", "MB/s"),
        QT_TRANSLATE_NOOP("Units", "GB/s"),
        QT_TRANSLATE_NOOP("Units", "TB/s"),
    };
    return castItoXBytes(bytesPerSec, decimals, kUnits);
}

QString formatQuotaGb(qint64 bytes)
{
    return QStringLiteral("%1 GB").arg(double(bytes) / 1e9, 0, 'f', 1);
}

QString formatDuration(std::chrono::seconds duration)
{
    using namespace std::chrono;

    if (duration.count() < 0)
        return QStringLiteral("0s");

    const auto totalSecs = duration.count();
    const auto d = totalSecs / 86400;
    const auto h = (totalSecs % 86400) / 3600;
    const auto m = (totalSecs % 3600) / 60;
    const auto s = totalSecs % 60;

    QString result;
    if (d > 0) result += QStringLiteral("%1d ").arg(d);
    if (h > 0 || d > 0) result += QStringLiteral("%1h ").arg(h);
    if (m > 0 || h > 0 || d > 0) result += QStringLiteral("%1m ").arg(m);
    result += QStringLiteral("%1s").arg(s);

    return result;
}

namespace {

/// MFC CastItoXBytes (OtherFunctions.cpp:129-172). 1024-based, but a unit runs
/// up to 1000 of itself before the next takes over; Bytes never get decimals.
QString castItoXBytes(double count, int decimals, const UnitLabels& units)
{
    static constexpr std::array<double, 5> kDivisor = {
        1.0, 1024.0, 1048576.0, 1073741824.0, 1099511627776.0};

    std::size_t idx = 0;
    if (count >= 1024.0) {
        idx = 1;
        while (idx + 1 < kDivisor.size() && count >= kDivisor[idx] * 1000.0)
            ++idx;
    }
    const double value = count > 0.0 ? count / kDivisor[idx] : 0.0;
    return QStringLiteral("%1 %2").arg(
        QString::number(value, 'f', idx == 0 ? 0 : decimals),
        QCoreApplication::translate("Units", units[idx]));
}

} // namespace

} // namespace eMule
