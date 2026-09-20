#pragma once

/// @file PriorityText.h
/// @brief Upload and download priority labels, as MFC shows them.

#include <QString>

namespace eMule {

/// MFC CKnownFile::GetUpPriorityDisplayString (srchybrid/KnownFile.cpp:1703-1726).
/// Wire values: 4 Very Low, 0 Low, 1 Normal, 2 High, 3 Release. Very Low and
/// Release are never shown as auto; the rest read "Auto [Lo]" and so on.
[[nodiscard]] QString uploadPriorityText(int prio, bool isAuto);

/// Inverse of CborSerializers.h priorityToString(): the token the daemon sends
/// back to the level it came from. Anything unrecognised reads as Normal.
[[nodiscard]] int priorityFromWireName(const QString& wireName);

/// The downloads list's Priority column (MFC srchybrid/DownloadListCtrl.cpp:2038-2056).
/// Same scale and the same auto forms as the upload side, except level 3 is
/// "Very High" rather than "Release" — that label is upload-only. MFC leaves Very
/// Low and Very High blank here, but this port offers both in its download priority
/// menus, so it names them instead of hiding a level the user set.
[[nodiscard]] QString downloadPriorityText(const QString& wireName, bool isAuto);

} // namespace eMule
