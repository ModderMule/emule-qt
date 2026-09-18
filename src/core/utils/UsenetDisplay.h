#pragma once

/// @file UsenetDisplay.h
/// @brief How a Usenet queue row reads, for surfaces that cannot link eMule::Usenet.
///
/// The GUI and the web server both get a release's status as a bare int off the
/// wire (usenet::UsenetItemStatus). The values are persisted and never move.

#include <QString>

#include <array>

namespace eMule {

namespace UsenetWireStatus {
inline constexpr int Queued = 0;
inline constexpr int Downloading = 1;
inline constexpr int Paused = 2;
inline constexpr int Complete = 3;
inline constexpr int Failed = 4;
inline constexpr int Verifying = 5;
inline constexpr int Repairing = 6;
inline constexpr int Unpacking = 7;
inline constexpr int Checking = 8;
} // namespace UsenetWireStatus

/// The five levels the daemon accepts, highest first. Duplicated from
/// usenet::kUsenetPriorityLevels because the wire carries a bare int.
inline constexpr std::array<int, 5> kUsenetPriorityLevels{2, 1, 0, -1, -2};

[[nodiscard]] inline bool usenetStatusIsPostProcessing(int status)
{
    return status == UsenetWireStatus::Verifying || status == UsenetWireStatus::Repairing
        || status == UsenetWireStatus::Unpacking;
}

/// Most finished first. Deliberately not the wire order, where Paused sits
/// between Downloading and Complete.
[[nodiscard]] inline int usenetStatusRank(int status)
{
    switch (status) {
    case UsenetWireStatus::Complete:    return 0;
    case UsenetWireStatus::Unpacking:   return 1;
    case UsenetWireStatus::Repairing:   return 2;
    case UsenetWireStatus::Verifying:   return 3;
    case UsenetWireStatus::Downloading: return 4;
    case UsenetWireStatus::Checking:    return 5;
    case UsenetWireStatus::Queued:      return 6;
    case UsenetWireStatus::Paused:      return 7;
    case UsenetWireStatus::Failed:      return 8;
    default:                            return 9;
    }
}

} // namespace eMule
