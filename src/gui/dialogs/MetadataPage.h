#pragma once

/// @file MetadataPage.h
/// @brief The ED2K-tag list page shared by the file and search detail dialogs.
///
/// MFC's CMetaDataDlg is a property page hosted by both CFileDetailDialog and the
/// search-result sheet (srchybrid/SearchListCtrl.cpp:121-124), so the builder
/// lives here rather than inside either dialog.

#include <QCborMap>
#include <QString>

class QWidget;

namespace eMule {

/// Build a Tag Name / Type / Value list from a details map's `tags[]` array, or a
/// placeholder label when there are none.
/// @param stateKey  UiState key for the column layout — distinct per host dialog.
[[nodiscard]] QWidget* createMetadataPage(const QCborMap& details, const QString& stateKey);

} // namespace eMule
