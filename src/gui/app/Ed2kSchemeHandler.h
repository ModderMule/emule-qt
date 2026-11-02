#pragma once

/// @file Ed2kSchemeHandler.h
/// @brief Platform-specific ed2k:// URL scheme registration, and the desktop integration at start.

#include "utils/FileAssociation.h"

namespace eMule {

/// Register this application as the handler for ed2k:// URLs, and ed3k: ones with them.
/// Windows, Linux: stores the switch on, so every start re-applies it.
void registerEd2kUrlScheme();

/// Check whether this application is currently registered as the ed2k:// handler.
[[nodiscard]] bool isEd2kSchemeRegistered();

/// The file types and links the user chose to claim (Windows, Linux; uistate.yml).
[[nodiscard]] gui::FileAssociation::FileTypes savedFileTypes();

/// Launcher entry, icons and savedFileTypes() with the desktop. Not before the first
/// start wizard ran or was dismissed: nothing is claimed before the user could say no (#111).
void applyDesktopIntegration();

} // namespace eMule
