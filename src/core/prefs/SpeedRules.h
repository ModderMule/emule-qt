#pragma once

/// @file SpeedRules.h
/// @brief Upload/download limit rules shared by core and GUI.
///
/// MFC's UL:DL ratio ("don't be a Lam3r", srchybrid/Preferences.cpp:693-714) and the
/// linked Connection sliders (CPPgConnection::CheckUp/CheckDown, PPgConnection.cpp:386-418),
/// plus the throttle floors of the speed controls. KB/s unless named Bytes; 0 = unlimited.

#include "utils/Types.h"

#include <algorithm>

namespace eMule::SpeedRules {

/// Below this upload limit the download is tied to it
inline constexpr uint32 kRatioFreeUpload = 20;
/// Throttle never goes below these, unless the line capacity is lower
inline constexpr uint32 kThrottleFloorUp = 10;
inline constexpr uint32 kThrottleFloorDown = 40;

/// Download multiple allowed for an upload of @p upBytes per second; 0 = no ratio.
[[nodiscard]] constexpr uint64 ratioCoefBytes(uint64 upBytes)
{
    if (upBytes == 0 || upBytes >= uint64{kRatioFreeUpload} * 1024)
        return 0;
    if (upBytes < 4 * 1024)
        return 3;
    if (upBytes < 10 * 1024)
        return 4;
    return 5;
}

[[nodiscard]] constexpr uint32 ratioCoef(uint32 upKB)
{
    return static_cast<uint32>(ratioCoefBytes(uint64{upKB} * 1024));
}

/// Download limit in force after the ratio, bytes/s (MFC GetMaxDownloadInBytesPerSec)
[[nodiscard]] constexpr uint64 ratioLimitedDownloadBytes(uint64 maxDownBytes, uint64 maxUpBytes)
{
    const uint64 coef = ratioCoefBytes(maxUpBytes);
    if (coef == 0)
        return maxDownBytes;
    const uint64 cap = coef * maxUpBytes;
    return maxDownBytes == 0 ? cap : std::min(cap, maxDownBytes);
}

/// MFC CheckUp: the upload moved, pull the download down to the ratio.
/// @p capDown 0 = no capacity cap.
[[nodiscard]] constexpr uint32 clampDownForUp(uint32 up, uint32 down, uint32 capDown = 0)
{
    if (const uint32 coef = ratioCoef(up); coef != 0 && (down == 0 || down > up * coef))
        down = up * coef;
    if (capDown != 0 && (down == 0 || down > capDown))
        down = capDown;
    return down;
}

/// MFC CheckDown: the download moved, push the upload up to carry it.
/// @p capUp 0 = no capacity cap.
[[nodiscard]] constexpr uint32 raiseUpForDown(uint32 up, uint32 down, uint32 capUp = 0)
{
    if (up == 0 || down == 0)
        return up;   // unlimited upload: no ratio; unlimited download: clampDownForUp caps it
    if (down < 13 && up * 3 < down) {
        up = (down + 2) / 3;
    } else if (down < 41 && up * 4 < down) {
        up = (down + 3) / 4;
    } else if (up < kRatioFreeUpload && up * 5 < down) {
        up = (down + 4) / 5;
    }
    // MFC's 5x branch assumes 5x below 10 too (up 9, down 41): step up until it carries
    while (up < kRatioFreeUpload && up * ratioCoef(up) < down)
        ++up;
    if (capUp != 0 && up > capUp)
        up = capUp;
    return up;
}

/// Lowest upload the throttle goes to
[[nodiscard]] constexpr uint32 floorUp(uint32 capUp)
{
    return capUp != 0 ? std::min(kThrottleFloorUp, capUp) : kThrottleFloorUp;
}

/// Lowest download the throttle goes to; the ratio wins over the floor
[[nodiscard]] constexpr uint32 floorDown(uint32 capUp, uint32 capDown)
{
    uint32 floor = capDown != 0 ? std::min(kThrottleFloorDown, capDown) : kThrottleFloorDown;
    return clampDownForUp(floorUp(capUp), floor);
}

struct Limits {
    uint32 up = 0;
    uint32 down = 0;
    constexpr bool operator==(const Limits&) const = default;
};

/// Throttle can still lower something
[[nodiscard]] constexpr bool canThrottle(Limits cur, uint32 capUp, uint32 capDown)
{
    return cur.up > floorUp(capUp) && cur.down > floorDown(capUp, capDown);
}

/// One throttle click: both halved, held at the floors and the ratio.
/// 0 (unlimited) halves from the capacity.
[[nodiscard]] constexpr Limits throttleStep(Limits cur, uint32 capUp, uint32 capDown)
{
    const uint32 up = cur.up != 0 ? cur.up : capUp;
    const uint32 down = cur.down != 0 ? cur.down : capDown;
    Limits next{std::max(up / 2, floorUp(capUp)), std::max(down / 2, floorDown(capUp, capDown))};
    next.down = clampDownForUp(next.up, next.down, capDown);
    return next;
}

} // namespace eMule::SpeedRules
