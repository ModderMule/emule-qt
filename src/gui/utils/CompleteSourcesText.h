#pragma once

/// @file CompleteSourcesText.h
/// @brief The complete-source range of a file, as MFC's lists print it.

#include <QString>

namespace eMule {

/// "n", "< hi" or "lo - hi". The shared list tests lo == hi first
/// (srchybrid/SharedFilesCtrl.cpp:641-648), the download list lo == 0
/// (srchybrid/DownloadListCtrl.cpp:2072-2077); they differ for 0/0.
[[nodiscard]] inline QString completeSourcesText(int lo, int hi, bool zeroFirst)
{
    if (zeroFirst ? lo == 0 : (lo != hi && lo == 0))
        return QStringLiteral("< %1").arg(hi);
    if (lo == hi)
        return QString::number(lo);
    return QStringLiteral("%1 - %2").arg(lo).arg(hi);
}

} // namespace eMule
