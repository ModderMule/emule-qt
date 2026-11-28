#pragma once

/// @file DateTimeText.h
/// @brief Display › Date format modes, shared by the GUI and the web server (#250).
///
/// Pure functions; the GUI keeps its live choice in gui/utils/DateTimeFormat.h,
/// the web server reads Preferences::dateFormat(). Log stamps stay as they are.

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QString>

namespace eMule {

/// Values of uistate.yml / preferences.yml "dateFormat"; Display page combo index
enum class DateFormatMode : int {
    System   = 0, ///< operating system region settings
    Language = 1, ///< locale of the chosen language (web: the session's)
    Iso      = 2  ///< yyyy-MM-dd HH:mm:ss, 24 h
};

[[nodiscard]] inline DateFormatMode dateFormatModeFromInt(int v)
{
    return (v >= 0 && v <= 2) ? static_cast<DateFormatMode>(v) : DateFormatMode::System;
}

/// Locale for a language code; "" (system default) = OS locale
[[nodiscard]] inline QLocale localeForLanguage(const QString& languageCode)
{
    return languageCode.isEmpty() ? QLocale::system() : QLocale(languageCode);
}

[[nodiscard]] inline QString formatDateTime(const QDateTime& dt, DateFormatMode mode, const QLocale& languageLocale,
                                            QLocale::FormatType fmt = QLocale::ShortFormat)
{
    switch (mode) {
    case DateFormatMode::Iso:      return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case DateFormatMode::Language: return languageLocale.toString(dt, fmt);
    case DateFormatMode::System:   break;
    }
    return QLocale().toString(dt, fmt);
}

[[nodiscard]] inline QString formatDate(const QDate& d, DateFormatMode mode, const QLocale& languageLocale,
                                        QLocale::FormatType fmt = QLocale::ShortFormat)
{
    switch (mode) {
    case DateFormatMode::Iso:      return d.toString(QStringLiteral("yyyy-MM-dd"));
    case DateFormatMode::Language: return languageLocale.toString(d, fmt);
    case DateFormatMode::System:   break;
    }
    return QLocale().toString(d, fmt);
}

} // namespace eMule
