#pragma once

/// @file DateTimeFormat.h
/// @brief The GUI's live Display › Date format: every date/time the GUI shows (#250).
///
/// Header-only on purpose: GUI tests compile sources one by one, a .cpp here
/// would have to join every test target. Modes + formatting: core DateTimeText.h.

#include "utils/DateTimeText.h"

namespace eMule {

namespace detail {
struct DateFormatState {
    DateFormatMode mode = DateFormatMode::System;
    QLocale languageLocale = QLocale::system();
};
inline DateFormatState& dateFormatState()
{
    static DateFormatState s;
    return s;
}
} // namespace detail

/// Set at start (main.cpp) and when Options is applied
inline void setDateFormat(DateFormatMode mode, const QString& languageCode)
{
    auto& s = detail::dateFormatState();
    s.mode = mode;
    s.languageLocale = localeForLanguage(languageCode);
}

[[nodiscard]] inline DateFormatMode dateFormatMode() { return detail::dateFormatState().mode; }

[[nodiscard]] inline QString toDisplayString(const QDateTime& dt, QLocale::FormatType fmt = QLocale::ShortFormat)
{
    const auto& s = detail::dateFormatState();
    return formatDateTime(dt, s.mode, s.languageLocale, fmt);
}

[[nodiscard]] inline QString toDisplayString(const QDate& d, QLocale::FormatType fmt = QLocale::ShortFormat)
{
    const auto& s = detail::dateFormatState();
    return formatDate(d, s.mode, s.languageLocale, fmt);
}

/// Epoch seconds → display date + time
[[nodiscard]] inline QString epochToDisplayString(qint64 secs, QLocale::FormatType fmt = QLocale::ShortFormat)
{
    return toDisplayString(QDateTime::fromSecsSinceEpoch(secs), fmt);
}

/// Options combo sample: what @p mode shows for @p dt (Language mode for @p languageCode)
[[nodiscard]] inline QString dateFormatSample(DateFormatMode mode, const QString& languageCode, const QDateTime& dt)
{
    return formatDateTime(dt, mode, localeForLanguage(languageCode));
}

} // namespace eMule
