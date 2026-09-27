#pragma once

/// @file ClientIcons.h
/// @brief Client software icon (MFC GetDisplayImage) for every list showing peers.

#include <QIcon>

namespace eMule {

/// "Plus" variants mean the client has credit (scoreRatio > 1.0). Friends keep
/// the software icon with a small friend badge at the bottom-right.
[[nodiscard]] QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend);

} // namespace eMule
