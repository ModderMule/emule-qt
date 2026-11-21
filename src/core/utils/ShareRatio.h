#pragma once

/// @file ShareRatio.h
/// @brief Share ratio of a torrent, the qBittorrent way (TorrentImpl::realRatio).

#include <QtGlobal>

namespace eMule {

/// Ratios at or above this show as infinite (qBittorrent MAX_RATIO).
inline constexpr double kMaxShareRatio = 9999.0;

/// Ratio sentinels of shareRatio()
inline constexpr double kShareRatioNone = -1.0;      ///< nothing up or down yet
inline constexpr double kShareRatioInfinite = -2.0;  ///< up but no base, or >= kMaxShareRatio

/// @p uploaded / base, base = @p downloaded, or @p haveBytes when downloaded is under 1 %
/// of it: seeding files that were on disk already (or lost stats) must not read infinite (#218).
[[nodiscard]] constexpr double shareRatio(qint64 uploaded, qint64 downloaded, qint64 haveBytes)
{
    const qint64 base = double(downloaded) < double(haveBytes) * 0.01 ? haveBytes : downloaded;
    if (base <= 0)
        return uploaded > 0 ? kShareRatioInfinite : kShareRatioNone;
    const double ratio = double(uploaded) / double(base);
    return ratio >= kMaxShareRatio ? kShareRatioInfinite : ratio;
}

} // namespace eMule
