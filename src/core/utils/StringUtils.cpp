#include "pch.h"
/// @file StringUtils.cpp
/// @brief String conversion utility implementations.

#include "StringUtils.h"

#include <QLocale>

#include <array>

namespace eMule {

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

QString formatByteSize(uint64 bytes)
{
    constexpr std::array<const char*, 5> units = {"B", "KB", "MB", "GB", "TB"};
    constexpr double kFactor = 1024.0;

    if (bytes == 0)
        return QStringLiteral("0 B");

    auto value = static_cast<double>(bytes);
    std::size_t unitIdx = 0;
    while (value >= kFactor && unitIdx < units.size() - 1) {
        value /= kFactor;
        ++unitIdx;
    }

    // Use 2 decimal places for MB and above, 0 for B/KB
    const int decimals = (unitIdx >= 2) ? 2 : 0;
    return QStringLiteral("%1 %2")
        .arg(QLocale::c().toString(value, 'f', decimals),
             QLatin1StringView(units[unitIdx]));
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

} // namespace eMule
