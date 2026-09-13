#pragma once

/// @file KnownTypeStyle.h
/// @brief How an "we already have this" verdict looks in a results list.
///
/// Shared by SearchResultsModel and IndexerResultsModel so the two search tabs
/// cannot drift apart. Both read the same numbering — SearchFile::KnownType on
/// the ED2K side, and GetUsenetKnownTypes reuses it deliberately so this one
/// helper serves both.

#include <QColor>
#include <QObject>
#include <QString>

namespace eMule {

/// Display string, matching MFC's Known column. Empty for "we know nothing",
/// which is the common case and reads better as a blank cell than as a word.
[[nodiscard]] inline QString knownTypeString(int knownType)
{
    switch (knownType) {
    case 1:  return QObject::tr("Shared");
    case 2:  return QObject::tr("Downloading");
    case 3:  return QObject::tr("Downloaded");
    case 4:  return QObject::tr("Cancelled");
    default: return {};
    }
}

/// Row colour, matching MFC eMule: red for something we have or are already
/// getting, green for something we finished or gave up on. An invalid QColor
/// means "leave the row alone".
[[nodiscard]] inline QColor knownTypeColor(int knownType)
{
    switch (knownType) {
    case 1:  return {0xFF, 0x00, 0x00};   // Shared
    case 2:  return {0xFF, 0x00, 0x00};   // Downloading
    case 3:  return {0x00, 0x80, 0x00};   // Downloaded
    case 4:  return {0x00, 0x80, 0x00};   // Cancelled
    default: return {};
    }
}

} // namespace eMule
