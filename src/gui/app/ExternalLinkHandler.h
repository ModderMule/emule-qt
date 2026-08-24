#pragma once

/// @file ExternalLinkHandler.h
/// @brief The single door for a link that arrived from outside the GUI.
///
/// Three sources, one destination:
///
///   - the operating system, as a QEvent::FileOpen — on macOS a browser or Finder click
///     on an ed2k:// link is a kAEGetURL Apple Event, which the Cocoa plugin turns into
///     this event (the Info.plist CFBundleURLTypes entry is what registers us for it)
///   - the command line, as a positional argument (CommandLineExec)
///   - an anchor clicked in the Server Info, chat or IRC panes, which arrives as plain
///     text rather than a QUrl (see TextLinks.h for why it cannot be one)
///
/// All three end in Ed2kLinkImporter, so a file link, a server link and an
/// ed2k://|httpcache| configuration link behave identically no matter how they got here.
///
/// It exists as an object rather than a lambda in main() for two reasons, both about
/// timing. It has to be installed on QApplication *before* the splash screen's
/// processEvents(), which runs long before there is a MainWindow to show a dialog on;
/// and a link that opens the application arrives while the IPC connection to the daemon
/// is still being made, at which point importing is impossible — the known-file lookup
/// and the HTTP Cache handshake are both the daemon's work. So links are queued until
/// there is somewhere to put them, and released when the daemon answers. Dropping them
/// instead is what made a cold start from a browser click do nothing at all.

#include <QObject>
#include <QPointer>
#include <QStringList>

namespace eMule {

class IpcClient;
class MainWindow;

class ExternalLinkHandler : public QObject {
    Q_OBJECT

public:
    /// Install on @p app. Safe to construct before the main window exists — that is the
    /// point — but it must be an event filter by then or startup clicks are lost.
    explicit ExternalLinkHandler(QObject* parent = nullptr);

    /// The window dialogs are parented to and tabs are switched on. Until it is set,
    /// links queue.
    void setMainWindow(MainWindow* mainWindow);

    /// The daemon connection every import needs. Queued links are released when it
    /// reports connected, and again on every later reconnect.
    void setIpcClient(IpcClient* ipc);

    /// Act on @p link, or queue it when the window or the daemon is not ready yet.
    ///
    /// Handles the panes' "emuleqt:versioncheck" sentinel, eD2K and magnet links, and
    /// hands anything else to the desktop's browser.
    void open(const QString& link);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void openNow(const QString& link);
    void flushPending();

    // Guarded: main() destroys the window and the IPC client before this handler, which
    // it constructs first so that a link arriving during startup has somewhere to land.
    QPointer<MainWindow> m_mainWindow;
    QPointer<IpcClient> m_ipc;
    /// Links that arrived before there was anywhere to put them, oldest first.
    QStringList m_pending;
};

} // namespace eMule
