#pragma once

/// @file LocateFilesAction.h
/// @brief "Locate Files..." (#224, BiglyBT's folder scan): asks for a folder on the core's disk
/// and sends LocateLocalCopies; files of the open ed3k downloads found there are copied from
/// disk instead of downloaded.

#include <QString>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;

namespace LocateFilesAction {

/// Asks for a folder (starting at @p startDir), scans it in the core, says what was found.
/// @p refresh runs after the answer.
void run(QWidget* parent, IpcClient* ipc, const QString& startDir, std::function<void()> refresh);

/// The menu entry's text, one for all lists.
[[nodiscard]] QString menuText();

} // namespace LocateFilesAction
} // namespace eMule
