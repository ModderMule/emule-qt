#pragma once

/// @file ServerMsgType.h
/// @brief Severity of a line destined for the GUI "Server Info" pane.
///
/// Standalone so the GUI log control can type its append API without pulling in
/// the whole server stack. Values mirror the reference's LOG_* constants
/// (srchybrid/Log.h:12-23) because they select the same colours.

namespace eMule {

/// Line kind for the Server Info pane. The pane is deliberately NOT the log:
/// the reference writes it through CemuleDlg::AddServerMessageLine, a channel
/// separate from AddLogText (no per-line timestamp, no disk log, no notifier).
///
/// Only Info and Success actually reach the pane — the reference diverts
/// ERROR/WARNING server lines to the log instead (srchybrid/ServerSocket.cpp:189-201).
/// The other two values exist so the wire format can carry them if that changes.
enum class ServerMsgType : int {
    Info    = 0,   ///< Default text colour.
    Warning = 1,   ///< Purple (m_crLogWarning).
    Error   = 2,   ///< Red (m_crLogError).
    Success = 3    ///< Blue (m_crLogSuccess) — used for the connection header.
};

} // namespace eMule
