#pragma once

/// @file ArgStr.h
/// @brief argStr() for one-pass QString::arg(a, b, c).
///
/// A chained .arg(name).arg(n) re-scans the text the first .arg inserted: a name
/// holding "%2" (e.g. URL-encoded ',' = "%2C") gets eaten by the next .arg (#272).
/// Multi-arg .arg() substitutes in one pass but takes only strings; argStr()
/// turns numbers, enums and chars into QString like the single .arg overloads.

#include <QByteArray>
#include <QChar>
#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <concepts>
#include <type_traits>
#include <utility>

namespace eMule {

template <typename T>
[[nodiscard]] decltype(auto) argStr(T&& v)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::same_as<U, QString> || std::same_as<U, QStringView> || std::same_as<U, QLatin1StringView>)
        return std::forward<T>(v);
    else if constexpr (std::same_as<U, QChar> || std::same_as<U, char> || std::same_as<U, char16_t>)
        return QString(QChar(v));
    else if constexpr (std::same_as<U, bool>)
        return QString::number(int(v));
    else if constexpr (std::is_arithmetic_v<U>)
        return QString::number(v);
    else if constexpr (std::is_enum_v<U>)
        return QString::number(static_cast<std::underlying_type_t<U>>(v));
    else if constexpr (std::same_as<U, QByteArray> || std::is_convertible_v<U, const char*>)
        return QString::fromUtf8(v);
    else
        return QString(std::forward<T>(v));
}

/// One value with width/base/fill/precision, pre-formatted for a one-pass arg(a, b, c).
template <typename T, typename... F>
[[nodiscard]] QString argFmt(const T& v, F&&... fmt)
{
    return QStringLiteral("%1").arg(v, std::forward<F>(fmt)...);
}

} // namespace eMule
