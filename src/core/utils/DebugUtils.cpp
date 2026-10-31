#include "pch.h"
/// @file DebugUtils.cpp
/// @brief Logging category definitions for eMule Qt.

#include "DebugUtils.h"

namespace eMule {

Q_LOGGING_CATEGORY(lcEmuleGeneral, "emule.general")
Q_LOGGING_CATEGORY(lcEmuleNet,     "emule.net")
Q_LOGGING_CATEGORY(lcEmuleFile,    "emule.file")
Q_LOGGING_CATEGORY(lcEmuleKad,     "emule.kad")
// Usenet/NNTP: own GUI tab and own _Usenet file, like Kad.
Q_LOGGING_CATEGORY(lcEmuleUsenet,  "emule.usenet")
// ed3k node (packmule): own GUI tabs (ed3k, ed3k Verbose) and own _Ed3k file
Q_LOGGING_CATEGORY(lcEmuleEd3k,    "emule.ed3k")
// the ed3k node's Kad: the ed3k Kad tab and the _Ed3kKad file
Q_LOGGING_CATEGORY(lcEmuleEd3kKad, "emule.ed3k.kad")
// hoster downloads (resolver, Java runtime, hoster join): the Hoster tab and the _Hoster file
Q_LOGGING_CATEGORY(lcEmuleHoster,  "emule.hoster")
// libtorrent alerts, forwarded as they come: the Torrent tab and the _Torrent file
Q_LOGGING_CATEGORY(lcEmuleTorrent, "emule.torrent")
// bans and anti-leecher hits, one line each: the GUI Leecher tab and the _Leecher file
Q_LOGGING_CATEGORY(lcEmuleLeecher, "emule.leecher")
Q_LOGGING_CATEGORY(lcEmuleServer,  "emule.server")
// Dedicated channel for gated server TCP/UDP/search verbose lines. Routes to the
// GUI "Verbose" tab (any emule.* category that is not emule.kad/emule.usenet/emule.server falls
// through there); a separate category keeps it independent of the global `verbose`.
Q_LOGGING_CATEGORY(lcEmuleServerVerbose, "emule.serverv")
Q_LOGGING_CATEGORY(lcEmuleCrypto,  "emule.crypto")
// MFC LOG_STATUSBAR: the GUI puts these in the main Log tab and the status bar.
Q_LOGGING_CATEGORY(lcEmuleStatus,  "emule.status")

} // namespace eMule
