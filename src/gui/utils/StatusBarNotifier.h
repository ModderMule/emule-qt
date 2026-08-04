#pragma once

/// @file StatusBarNotifier.h
/// @brief Process-wide sink for one-line status-bar text, installed once by MainWindow.
///
/// Lets GUI code that must not know about MainWindow (utils/, panels/, dialogs/) put a line
/// in the bottom status bar. This is the Qt stand-in for MFC's LOG_STATUSBAR flag, which
/// routes through CemuleDlg::AddLogText into status-bar pane 0 (srchybrid/EmuleDlg.cpp:891).
///
/// GUI thread only — every caller is a widget slot or an IpcClient reply. With no sink
/// installed (unit tests) every post() is a silent no-op.

#include <QString>

#include <functional>

namespace eMule::StatusBarNotifier {

/// Receives (text, timeoutMs). A timeout of 0 means "persist until the next message",
/// which is what MFC does; a positive timeout is a transient QStatusBar::showMessage.
using Sink = std::function<void(const QString& text, int timeoutMs)>;

/// Install the sink. MainWindow calls this once after building its status bar;
/// pass {} to remove it again.
void setSink(Sink sink);

/// Post @p text to the status bar. No-op when no sink is installed or @p text is empty.
void post(const QString& text, int timeoutMs = 0);

} // namespace eMule::StatusBarNotifier
