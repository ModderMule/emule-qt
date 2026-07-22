#include "pch.h"
/// @file DebugUtils.cpp
/// @brief Logging category definitions for eMule Qt.

#include "DebugUtils.h"

namespace eMule {

Q_LOGGING_CATEGORY(lcEmuleGeneral, "emule.general")
Q_LOGGING_CATEGORY(lcEmuleNet,     "emule.net")
Q_LOGGING_CATEGORY(lcEmuleFile,    "emule.file")
Q_LOGGING_CATEGORY(lcEmuleKad,     "emule.kad")
Q_LOGGING_CATEGORY(lcEmuleServer,  "emule.server")
// Dedicated channel for gated server TCP/UDP/search verbose lines. Routes to the
// GUI "Verbose" tab (any emule.* category that is not emule.kad/emule.server falls
// through there); a separate category keeps it independent of the global `verbose`.
Q_LOGGING_CATEGORY(lcEmuleServerVerbose, "emule.serverv")
Q_LOGGING_CATEGORY(lcEmuleCrypto,  "emule.crypto")

} // namespace eMule
