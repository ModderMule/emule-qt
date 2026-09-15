#pragma once

/// @file PriorityText.h
/// @brief Upload priority labels, as MFC shows them.

#include <QString>

namespace eMule {

/// MFC CKnownFile::GetUpPriorityDisplayString (srchybrid/KnownFile.cpp:1703-1726).
/// Wire values: 4 Very Low, 0 Low, 1 Normal, 2 High, 3 Release. Very Low and
/// Release are never shown as auto; the rest read "Auto [Lo]" and so on.
[[nodiscard]] QString uploadPriorityText(int prio, bool isAuto);

} // namespace eMule
