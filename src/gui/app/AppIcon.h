#pragma once

/// @file AppIcon.h
/// @brief App/dock icon: the avatar donkey, ears down while LowID.

#include <QIcon>

namespace eMule {

/// Donkey at every size in :/icons/app (scripts/generate_donkey_icons.py).
/// @p lowId picks the drooped-ears variant for the dock while firewalled.
[[nodiscard]] QIcon appIcon(bool lowId = false);

} // namespace eMule
