#pragma once

/// @file ConnectionPresets.h
/// @brief Line types offered by the first start wizard and the limits derived from them.
///
/// Replaces MFC's CConnectionWizardDlg provider list (modem/ISDN/T-DSL) with 2026 lines.
/// Widget-free so the math is testable on its own.

#include "prefs/Preferences.h"
#include "utils/Types.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace eMule {

struct ConnectionPreset {
    const char* name;   ///< not translated: technology names
    double downMbit;
    double upMbit;
};

inline constexpr std::array<ConnectionPreset, 14> kConnectionPresets{{
    {"ADSL2+",             16,   1},
    {"VDSL 50",            50,   10},
    {"VDSL 100",           100,  40},
    {"VDSL 250",           250,  40},
    {"Cable 250",          250,  25},
    {"Cable 1000",         1000, 50},
    {"Fiber 300",          300,  150},
    {"Fiber 1000",         1000, 500},
    {"Fiber symmetric",    1000, 1000},
    {"LTE",                50,   10},
    {"5G",                 300,  50},
    {"Starlink 100",       100,  10},
    {"Starlink 200",       200,  20},
    {"Starlink Max",       400,  30},
}};

/// Capacity and limits, all KiB/s. A limit of 0 means unlimited.
struct BandwidthSettings {
    uint32 capDown = 0;
    uint32 capUp = 0;
    uint32 maxDown = 0;
    uint32 maxUp = 0;

    friend bool operator==(const BandwidthSettings&, const BandwidthSettings&) = default;
};

/// Line rate in Mbit/s (decimal) to KiB/s.
[[nodiscard]] inline uint32 mbitToKiB(double mbit)
{
    return mbit > 0 ? static_cast<uint32>(std::lround(mbit * 1'000'000.0 / 8.0 / 1024.0)) : 0;
}

/// KiB/s back to Mbit/s, for showing a stored capacity.
[[nodiscard]] inline double kiBToMbit(uint32 kib)
{
    return kib * 1024.0 * 8.0 / 1'000'000.0;
}

/// MFC rule: upload limit 80 % of the line, download 90 %. Its sub-20 KB/s ratio
/// tiers are gone — no line offered here gets near them.
[[nodiscard]] inline BandwidthSettings limitsForLine(double downMbit, double upMbit)
{
    BandwidthSettings s;
    s.capDown = std::max<uint32>(mbitToKiB(downMbit), 1);
    s.capUp = std::max<uint32>(mbitToKiB(upMbit), 1);
    s.maxDown = std::max<uint32>(s.capDown * 9ull / 10, 1);
    s.maxUp = std::max<uint32>(s.capUp * 4ull / 5, 1);
    return s;
}

/// What a fresh install runs with; the wizard's answer for an unknown line.
[[nodiscard]] constexpr BandwidthSettings recommendedBandwidth()
{
    return {kDefaultMaxGraphDownloadRate, kDefaultMaxGraphUploadRate,
            kDefaultMaxDownload, kDefaultMaxUpload};
}

/// Defaults shipped before the wizard had a speed page; an install still on exactly
/// these never tuned its bandwidth.
[[nodiscard]] constexpr BandwidthSettings legacyDefaultBandwidth()
{
    return {500, 250, 500, 250};
}

} // namespace eMule
