#pragma once

/// @file StringUtils.h
/// @brief String conversion utilities replacing CString, LPCTSTR, _T().
///
/// Provides:
///   EMUSTR(s)       — mechanical replacement for _T("...") (8944 sites)
///   fromStdString / toStdString — std::string ↔ QString
///   toHexString / fromHexString — binary data ↔ hex display
///   formatByteSize / formatByteRate — replace CastItoXBytes
///   formatDuration   — replaces CastSecondsToHM

#include <QString>
#include <QStringView>

#include <chrono>
#include <concepts>
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

/// Byte count as MFC CastItoXBytes(count) shows it: "734.53 MB", "512 Bytes".
/// 1024-based, but each unit runs up to 1000 of itself (1,024,000 bytes is
/// "0.98 MB"). Unit labels translate under context "Units"; <= 0 is "0 Bytes".
/// The one size formatter for GUI and web UI — don't hand-roll another.
[[nodiscard]] QString formatByteSize(double bytes, int decimals = 2);

/// Transfer rate as MFC CastItoXBytes(count, false, true): "12.40 KB/s",
/// "1.50 MB/s", "500 B/s". A KB/s input is MFC's isK — multiply by 1024.
[[nodiscard]] QString formatByteRate(double bytesPerSec, int decimals = 2);

/// Integer counts land here, so -Wconversion needs no cast at each call site.
template <std::integral T>
[[nodiscard]] QString formatByteSize(T bytes, int decimals = 2)
{
    return formatByteSize(static_cast<double>(bytes), decimals);
}

template <std::integral T>
[[nodiscard]] QString formatByteRate(T bytesPerSec, int decimals = 2)
{
    return formatByteRate(static_cast<double>(bytesPerSec), decimals);
}

/// A provider allowance in decimal GB, "500.0 GB" — what the plan and the
/// invoice say. Deliberately not formatByteSize(), which is 1024-based: a 500 GB
/// plan would read 465.66 GB beside the spin box it was typed into.
[[nodiscard]] QString formatQuotaGb(qint64 bytes);

/// Format a duration as "Xd Xh Xm Xs" or shorter forms.
/// Replaces MFC CastSecondsToHM.
[[nodiscard]] QString formatDuration(std::chrono::seconds duration);

} // namespace eMule
