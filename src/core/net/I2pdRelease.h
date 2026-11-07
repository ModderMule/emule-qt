#pragma once

/// @file I2pdRelease.h
/// @brief Pinned release of the I2P router (emuleqt-i2pd) the core downloads on demand.
///
/// Written by scripts/update-i2p.sh from the release's SHA256SUMS — don't edit by hand.
/// A release of github.com/ed3k-protocol/i2pd, built by its emuleqt-release.yml on a tag
/// emuleqt-<version>-<n>.

namespace eMule::i2pd_release {

inline constexpr char kRepo[] = "ed3k-protocol/i2pd";
inline constexpr char kTag[] = "emuleqt-3.0.0-1";

inline constexpr char kLinuxX64[] = "680656392ca9b535b62051f7d071cce0dc4df209d439f406b813f463bf4e788d";
inline constexpr char kLinuxArm64[] = "906cd1e999d09b5cc519e0b94d72d4b3aa9092083323b8e2f378cc466bb8e6e8";
inline constexpr char kMacUniversal[] = "aca52fb15a3c2ed1b079607ca6ff81a65f40a0f81e8260fcf7cb7b0a0df474dc";
inline constexpr char kWindowsX64[] = "1fb3ee76e95a1020c9832c6ae5c2b965b0e5c97d8e4696de978234c8c201b8ed";

} // namespace eMule::i2pd_release
