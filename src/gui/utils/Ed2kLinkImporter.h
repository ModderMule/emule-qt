#pragma once

/// @file Ed2kLinkImporter.h
/// @brief Parse eD2K file links, drop the ones the core already has, and queue the rest.
///
/// Shared by every place that turns link text into downloads: the clipboard watcher,
/// the ed2k:// URL handler, the command line, PasteLinksDialog and the Transfers
/// "Paste eD2K Links" action. Filtering is authoritative — the daemon is asked what it
/// already knows (IpcMsgType::GetKnownTypes) rather than consulting the GUI's local
/// models, which only see downloading and currently-shared files.
///
/// It is also where `ed2k://|httpcache|` configuration links are recognised and
/// handed to HttpCacheLinkImporter, so every one of those callers accepts them
/// without knowing anything about the format.

#include "search/SearchFile.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <functional>

class QFileOpenEvent;
class QWidget;

namespace eMule {

class IpcClient;

class Ed2kLinkImporter {
    Q_DECLARE_TR_FUNCTIONS(Ed2kLinkImporter)

public:
    /// Where the links came from. Decides which files are silently skipped.
    enum class Source {
        Automatic,  ///< clipboard watcher — skip downloading, shared, downloaded and cancelled
        Manual      ///< explicit user action — skip only downloading/shared, so a downloaded
                    ///< or cancelled file can be deliberately re-downloaded
    };

    /// Whether to ask before starting the downloads.
    enum class Prompt {
        Ask,    ///< show the "Do you want to download…" confirmation
        Silent  ///< the caller already confirmed (e.g. the paste dialog itself)
    };

    struct Result {
        int added = 0;                 ///< links handed to the daemon
        int skipped = 0;               ///< links dropped because the core already has them
        QStringList invalid;           ///< lines that were not parseable file links
        QStringList skipDescriptions;  ///< "name — reason", one entry per skipped file
        /// HTTP Cache configuration links handed to HttpCacheLinkImporter. Counted
        /// separately from @a added: they start no download, so a caller must not
        /// switch to the Transfers tab over one.
        int httpCacheConfigs = 0;
    };

    /// Import every eD2K file link found in @p text (one per line).
    ///
    /// Asynchronous: one GetKnownTypes round-trip to the daemon happens before anything is
    /// prompted or queued, so @p done runs after this call returns. Nothing at all happens
    /// when @p ipc is null or disconnected, or when @p text holds no file links.
    ///
    /// @param parent       parent for the confirmation box; may be null
    /// @param done         invoked with the outcome once the import settles
    /// @param beforePrompt invoked immediately before the confirmation box appears
    ///                     (used to raise the main window on link clicks)
    static void importLinks(const QString& text, IpcClient* ipc, QWidget* parent,
                            Source source, Prompt prompt,
                            std::function<void(const Result&)> done = {},
                            std::function<void()> beforePrompt = {});

    /// True when a file in state @p type must not be re-added from @p source.
    /// Shared/downloading files are always skipped; downloaded and cancelled ones only
    /// for automatic imports, so a manual paste can still start a re-download.
    [[nodiscard]] static bool shouldSkip(SearchFile::KnownType type, Source source);

    /// Human-readable reason a file in state @p type was skipped, for logs and the
    /// status bar. Empty when @p type is not a skip-worthy state.
    [[nodiscard]] static QString skipReason(SearchFile::KnownType type);

    /// Full sentence telling the user why @p name was not added, for the log line and the
    /// popup. Empty when @p type is not a skip-worthy state. Wording follows MFC's
    /// IDS_ERR_ALREADY_DOWNLOADING / IDS_ERR_ALREADY_DOWNLOADED.
    [[nodiscard]] static QString skipMessage(SearchFile::KnownType type, const QString& name);

    /// Split pasted text into individual link candidates. Newlines are the usual separator,
    /// but several links can share one line, so cut at every "ed2k://" as well. File names
    /// may contain spaces, so splitting on whitespace is not an option.
    [[nodiscard]] static QStringList splitLinks(const QString& text);

    /// The kinds of eD2K link @p text carries.
    ///
    /// Only the kinds somebody acts on: a link type nothing in the GUI can import — search,
    /// nodeslist — sets no flag, so an action gated on this stays grey rather than opening
    /// a dialog that reports "invalid link".
    enum class LinkKind : quint8 {
        File      = 0x1,  ///< ed2k://|file|
        HttpCache = 0x2,  ///< ed2k://|httpcache| — a configuration link, not a download
        Server    = 0x4,  ///< ed2k://|server| (not serverlist)
    };
    Q_DECLARE_FLAGS(LinkKinds, LinkKind)

    /// Which link kinds @p text carries — for greying a "Paste eD2K Links" action and for
    /// the clipboard watcher's first-pass filter.
    ///
    /// Deliberately a prefix scan and not a parse. MFC decides the same question the same
    /// way (CemuleApp::IsEd2kLinkInClipboard, srchybrid/Emule.cpp:1509), and a malformed
    /// link should reach importLinks() and produce a real error rather than silently
    /// disable the menu entry that would have explained it. Magnet links are left out: the
    /// importer handles them, but flagging every "magnet:" would make the clipboard watcher
    /// fire on BitTorrent magnets that yield nothing.
    [[nodiscard]] static LinkKinds linkKindsIn(const QString& text);

    /// The raw link text carried by a macOS QFileOpenEvent, or empty when it carries none.
    ///
    /// Reads file() before url(), which looks backwards and is the whole point. macOS
    /// delivers a link click as a kAEGetURL Apple Event carrying a plain string, and the
    /// Cocoa plugin only wraps it in a QUrl if it parses — an eD2K link never does, because
    /// everything between "ed2k://" and the first '/' is read as the authority and '|' is
    /// not a legal host character (qcocoaapplicationdelegate.mm, getUrl:withReplyEvent:).
    /// Qt's fallback for that case hands the string to the QFileOpenEvent(QString) path,
    /// where it survives verbatim in file(); url() meanwhile holds a "file:" URL with the
    /// whole link buried in its path, which is what the old handler read and why the macOS
    /// route silently did nothing for every link type. url() is still consulted second, for
    /// the schemes that do parse — magnet: has no authority, so it arrives as a real QUrl.
    ///
    /// Requires Qt 6.6 or newer: before that the plugin passed an unparseable URL through as
    /// an invalid QUrl, which stringifies to nothing, and the link was lost inside Qt.
    ///
    /// A dropped file — a .emulecollection, a server.met — yields an empty string rather
    /// than its path, so the caller can let the event fall through to whatever handles it.
    [[nodiscard]] static QString linkFromFileOpenEvent(const QFileOpenEvent& event);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Ed2kLinkImporter::LinkKinds)

} // namespace eMule
