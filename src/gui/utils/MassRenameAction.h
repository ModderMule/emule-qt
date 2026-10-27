#pragma once

/// @file MassRenameAction.h
/// @brief "Mass Rename..." for the Shared Files and Downloads lists (MorphXT MP_MASSRENAME).

#include "dialogs/MassRenameDialog.h"

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;

namespace MassRenameAction {

/// Opens the dialog on @p items, sends the new names in one RenameFiles request,
/// lists the failures in one box and calls @p refresh once the daemon is done.
void run(QWidget* parent, IpcClient* ipc, QList<MassRenameDialog::Item> items,
         std::function<void()> refresh);

} // namespace MassRenameAction
} // namespace eMule
