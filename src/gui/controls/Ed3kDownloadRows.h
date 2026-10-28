#pragma once

/// @file Ed3kDownloadRows.h
/// @brief The ed3k downloads of GetEd3kDownloads / PushEd3kDownloads (Ed3kDownloadsBridge) as
/// rows of the Transfers download list, merged after the eD2K ones by TransferPanel.
///
/// The bar is the eD2K one (paintDownloadBar): `blocks` → gaps, `pending` → pending,
/// `avail` → partFreq per ed3k piece (DownloadRow::partFreqUnit).

#include "controls/DownloadListModel.h"

#include <QCborMap>

#include <vector>

namespace eMule {

/// One row of the `downloads` array.
[[nodiscard]] DownloadRow ed3kDownloadRow(const QCborMap& m);
/// Every row of a GetEd3kDownloads answer (its map: `running`, `downloads`).
[[nodiscard]] std::vector<DownloadRow> ed3kDownloadRows(const QCborMap& list);

} // namespace eMule
