#pragma once

/// @file ConfidenceStyle.h
/// @brief How a fake-file verdict looks in a list: colour and sort key.
///
/// Shared by the search and the download list; the words are in
/// search/ConfidenceText.h, which the web pages use too.

#include "search/ConfidenceText.h"

#include <QColor>

namespace eMule {

/// Sort key: band first, then the score inside it (higher score = worse).
[[nodiscard]] inline int confidenceSortKey(const QString& id, int score)
{
    const int rank = confidenceRank(id);
    return rank < 0 ? -1 : rank * 1000 + (100 - score);
}

/// Cell colour; invalid = leave it alone.
[[nodiscard]] inline QColor confidenceColor(const QString& id)
{
    switch (confidenceRank(id)) {
    case 1:  return {0xD0, 0x00, 0x00};
    case 2:  return {0xD0, 0x60, 0x00};
    case 3:  return {0xA0, 0x80, 0x00};
    case 5:  return {0x00, 0x80, 0x00};
    default: return {};
    }
}

} // namespace eMule
