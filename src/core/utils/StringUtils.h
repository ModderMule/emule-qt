#pragma once

/// @file StringUtils.h
/// @brief String conversion utilities replacing CString, LPCTSTR, _T().
///
/// Provides:
///   EMUSTR(s)       — mechanical replacement for _T("...") (8944 sites)
///   fromStdString / toStdString — std::string ↔ QString
///   toHexString / fromHexString — binary data ↔ hex display
///   formatByteSize   — replaces CastItoXBytes
///   formatDuration   — replaces CastSecondsToHM

#include <QString>
#include <QStringView>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "Types.h"

namespace eMule {

/// Mechanical porting aid: replaces _T("literal") with QStringLiteral("literal").
#define EMUSTR(s) QStringLiteral(s)

/// Convert a UTF-8 std::string_view to QString.
[[nodiscard]] QString fromStdString(std::string_view sv);

/// Convert a QStringView to a UTF-8 std::string.
[[nodiscard]] std::string toStdString(QStringView qsv);

/// Convert binary data to a lowercase hex string (e.g. hash display).
[[nodiscard]] QString toHexString(std::span<const uint8> data);

/// Parse a hex string back to binary data.  Returns empty on invalid input.
[[nodiscard]] QByteArray fromHexString(QStringView hex);

/// Format a byte count as a human-readable string (e.g. "1.23 GB").
/// Replaces MFC CastItoXBytes.
[[nodiscard]] QString formatByteSize(uint64 bytes);

/// Format a duration as "Xd Xh Xm Xs" or shorter forms.
/// Replaces MFC CastSecondsToHM.
[[nodiscard]] QString formatDuration(std::chrono::seconds duration);

} // namespace eMule
