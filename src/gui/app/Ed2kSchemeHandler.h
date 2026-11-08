#pragma once

/// @file Ed2kSchemeHandler.h
/// @brief Platform-specific ed2k:// URL scheme registration, and the desktop integration at start.

#include "utils/FileAssociation.h"

namespace eMule {

/// Register this application as the handler for ed2k:// URLs, and ed3k: ones with them.
/// Windows, Linux: stores the switch on and applies it now.
void registerEd2kUrlScheme();

/// Check whether this application is currently registered as the ed2k:// handler.
[[nodiscard]] bool isEd2kSchemeRegistered();

/// The file types and links the user chose to claim (Windows, Linux; uistate.yml).
[[nodiscard]] gui::FileAssociation::FileTypes savedFileTypes();

/// The types whose network is switched on: Usenet → nzb, BitTorrent → torrent,
/// eD2K/Kad/ed3k → ed2k links, hoster downloads → dlc. A switched-off network claims nothing (#121).
[[nodiscard]] gui::FileAssociation::FileTypes enabledNetworkTypes();

/// savedFileTypes() of the enabled networks only: what gets claimed.
[[nodiscard]] gui::FileAssociation::FileTypes wantedFileTypes();

/// Launcher entry, icons and wantedFileTypes() with the desktop. Not before the first
/// start wizard ran or was dismissed: nothing is claimed before the user could say no (#111).
/// Touches the desktop only when the wanted types changed since the last time, or the
/// install moved (#121). @p createLauncher: write the bare launcher if missing (first run only).
void applyDesktopIntegration(bool createLauncher = false);

/// Wizard done but nothing recorded (profile from before #121): takes what our entries
/// claim now as the stored choice and the applied state. Writes nothing (#153).
void adoptClaimedFileTypes();

/// First start wizard closed without Finish on a fresh install: every switch on, every
/// type of the enabled networks claimed (all of them by default), launcher created.
void claimAllFileTypes();

/// Applies wantedFileTypes() now and remembers it. @p setDefault false: rewrite only,
/// without becoming the default app again.
bool applyFileTypesNow(bool setDefault);

/// UiState::appliedFileTypes() bits as types.
[[nodiscard]] gui::FileAssociation::FileTypes fromBits(int bits);

} // namespace eMule
