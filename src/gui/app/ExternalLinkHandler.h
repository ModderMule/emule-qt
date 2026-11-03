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
/// A .nzb file comes through the same door for the same reason. It is not a link and
/// does not reach the importer, but it needs the daemon just as much — its *contents*
/// travel over IPC — so it waits in the same queue instead of meeting the Usenet
/// panel's modal "Not connected" warning.
///
/// It exists as an object rather than a lambda in main() for two reasons, both about
/// timing. It has to be installed on QApplication *before* the splash screen's
/// processEvents(), which runs long before there is a MainWindow to show a dialog on;
/// and a link that opens the application arrives while the IPC connection to the daemon
/// is still being made, at which point importing is impossible — the known-file lookup
/// and the HTTP Cache handshake are both the daemon's work. So links are queued until
/// there is somewhere to put them, and released when the daemon answers. Dropping them
/// instead is what made a cold start from a browser click do nothing at all.

#include "app/PendingOpenQueue.h"

#include <QObject>
#include <QPointer>
#include <QString>

namespace eMule {

namespace Ipc {
class IpcMessage;
}
class Ed3kNameOpener;
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
    /// Handles the panes' "emuleqt:versioncheck" sentinel, eD2K and magnet links
    /// (BitTorrent magnets go to the Torrents tab), and
    /// hands anything else to the desktop's browser.
    void open(const QString& link);

    /// Queue @p path, a .nzb (Usenet panel), .torrent (Torrents panel) or .ed3knod (ed3k node
    /// list opened by hand, bootstrap.md §3; batch 15) file —
    /// waiting for the daemon the same way a link does, and unconditionally, since
    /// there is no version of adding either that does not go through it.
    ///
    /// Both routes in land early on a cold start: a Finder double-click arrives during
    /// the splash screen, with no main window yet, and a command-line argument is read
    /// on the line after the *asynchronous* connectToDaemon().
    void openFile(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void openNow(const QString& link);
    /// PushEd3kBootstrapAsk: the user decides whether an ed3k:nodes link adds its source.
    void askBootstrapSource(const Ipc::IpcMessage& msg);
    /// An ed3k:set link (also PushEd3kSetOffer): follow and download, open as website, or not.
    void openEd3kSet(const QString& link, bool noWebsite);
    /// PushEd3kSetOffer: a set a website pointed at, not opened as one.
    void onEd3kSetOffered(const Ipc::IpcMessage& msg);
    /// An ed3k:live link: watched, opened in the system's player (local core).
    void watchEd3kLive(const QString& link);
    /// PushEd3kSetVersion: a new version of a set with policy ask waits for the user.
    void tellEd3kSetVersion(const Ipc::IpcMessage& msg);
    /// PushEd3kPaidScopesWarn: a bought pass covers more subscriptions (subscription.md §5.1).
    void askEd3kPassScopes(const Ipc::IpcMessage& msg);
    void openFileNow(const QString& path);
    /// An ed3k:friend link (friend.md §2, §3): the Add dialog of the Messages tab, filled in;
    /// added on the user's Add only. An `s` link is a credential: never logged.
    void openEd3kFriend(const QString& link);
    /// An ed3k:node link (Protocol.md §13.2, bootstrap.md §1 source 3): asked, then bootstrapped.
    void openEd3kNode(const QString& link);
    /// An ed3k:cache link (http-source.md §4.1): asked (host, dn, kid; never the key), then
    /// configured and kept (AddEd3kCacheLink).
    void addEd3kCache(const QString& link);
    /// A clicked download link (magnet, ed3k:file): asked unless "add clicked links without
    /// asking" is on; the window comes to the front only for the box (#119).
    [[nodiscard]] bool confirmClickedDownload(const QString& title, const QString& name);
    void flushPending();
    /// Whether there is both a window to show something on and a daemon to ask.
    [[nodiscard]] bool canOpenNow() const;
    /// Park @p item until there is, logging either the wait or the drop.
    void queueForLater(PendingOpen item);

    // Guarded: main() destroys the window and the IPC client before this handler, which
    // it constructs first so that a link arriving during startup has somewhere to land.
    QPointer<MainWindow> m_mainWindow;
    QPointer<IpcClient> m_ipc;
    /// Links and files that arrived before there was anywhere to put them.
    PendingOpenQueue m_pending;
    /// ed3k:name links (made with the IPC client)
    QPointer<Ed3kNameOpener> m_names;
};

} // namespace eMule
