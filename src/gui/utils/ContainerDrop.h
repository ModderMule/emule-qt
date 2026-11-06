#pragma once

/// @file ContainerDrop.h
/// @brief Link container files (DLC, RSDF, CCF) for the Hoster tab: what a path or a drop is.
///
/// One answer for the Hoster panel, the main window's drop and the Finder / command-line
/// opens, like NzbDrop for .nzb. Local files only: their bytes go to the core
/// (OpenHosterContainer), which hands them to JDownloader's container plugins.

#include <QStringList>

class QMimeData;

namespace eMule::gui {

/// Whether @p path ends in a container extension (.dlc, .rsdf, .ccf).
[[nodiscard]] bool isContainerFilePath(const QString& path);

/// The local container files of a drop that exist; empty: the drop is not ours.
[[nodiscard]] QStringList containerDropFiles(const QMimeData* mime);

/// File dialog filter: "Link containers (*.dlc *.rsdf *.ccf)".
[[nodiscard]] QString containerFileFilter();

} // namespace eMule::gui
