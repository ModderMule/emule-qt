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

#include "IpcMessage.h"
#include "protocol/ED2KLink.h"
#include "search/SearchFile.h"

#include <QByteArray>
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
        /// serverlist / nodeslist / search / server links handed to the handler.
        int otherLinks = 0;
        /// BitTorrent magnets (btih/btmh, no eD2K hash) handed to the torrent
        /// handler — or silently dropped by an automatic import. Never invalid.
        int torrentLinks = 0;
        /// ed3k:pass links (batch 14) handed to their handler (manual only). Never invalid:
        /// a voucher is a credential, the invalid list is logged and shown.
        int ed3kPassLinks = 0;
        /// Other ed3k: links (batch 15: friend, node, cache, set, live, …) handed one by one to
        /// the ed3k link handler (manual only). Never invalid: they may carry credentials.
        int ed3kLinks = 0;
    };

    /// The link kinds that start no download (MFC CemuleDlg::ProcessED2KLink,
    /// srchybrid/EmuleDlg.cpp:1259-1311).
    struct OtherLinks {
        QStringList serverLists;                   ///< server.met URLs
        QStringList nodesLists;                    ///< nodes.dat URLs
        QStringList searches;                      ///< search terms
        QList<ED2KServerLink> servers;             ///< with a dual-stack v6= when given

        [[nodiscard]] int count() const
        {
            return static_cast<int>(serverLists.size() + nodesLists.size() + searches.size()
                                    + servers.size());
        }
    };

    /// Who acts on OtherLinks — the main window. Called for manual imports only: the
    /// clipboard watcher never updates a server list or starts a search by itself.
    using OtherLinkHandler = std::function<void(const OtherLinks&, QWidget* parent)>;
    static void setOtherLinkHandler(OtherLinkHandler handler);

    /// Who takes BitTorrent magnets found in pasted text — the Torrents tab. Manual
    /// imports only, like OtherLinkHandler. Unset (no BitTorrent build), they count as invalid.
    using TorrentLinkHandler = std::function<void(const QStringList& magnets, QWidget* parent)>;
    static void setTorrentLinkHandler(TorrentLinkHandler handler);
    /// Hand @p magnets to the torrent handler directly (search rows). False when unset.
    static bool sendTorrentLinks(const QStringList& magnets, QWidget* parent);

    /// Who takes a fetched .torrent (search rows) — the Torrents tab. Unset without BitTorrent.
    /// @p wantedFiles: the file indices to download; empty = all.
    using TorrentFileHandler = std::function<void(const QString& name, const QByteArray& data,
                                                  const QList<int>& wantedFiles, QWidget* parent)>;
    static void setTorrentFileHandler(TorrentFileHandler handler);
    /// Whether the Torrents tab takes torrents now (search rows): both handlers set
    /// and the engine running.
    static void setTorrentsReady(std::function<bool()> ready);
    [[nodiscard]] static bool torrentsReady();
    /// Hand @p data (a .torrent called @p name) over. False when unset.
    static bool sendTorrentFile(const QString& name, const QByteArray& data, QWidget* parent,
                                const QList<int>& wantedFiles = {});

    /// Who takes hoster links of a web search row — the Hoster tab: one list of mirrors a
    /// file, and the package they go under (empty: none). Unset without EMULE_HAVE_HOSTER_JD;
    /// @p ready says whether hoster downloads are on.
    using HosterLinkHandler = std::function<void(const QList<QStringList>& files,
                                                 const QString& package, QWidget* parent)>;
    static void setHosterLinkHandler(HosterLinkHandler handler, std::function<bool()> ready);
    /// ed3k:pass links (subscription.md §9.3) pasted by hand go here; an automatic import (the
    /// clipboard watcher) drops them: a voucher copied to be handed on must not be used up.
    using Ed3kPassLinkHandler = std::function<void(const QStringList& links, QWidget* parent)>;
    static void setEd3kPassLinkHandler(Ed3kPassLinkHandler handler);
    /// Every other ed3k: link pasted by hand goes here, one at a time (ExternalLinkHandler,
    /// which asks where the link wants it); an automatic import drops them. Unset: invalid
    /// (redacted).
    using Ed3kLinkHandler = std::function<void(const QString& link, QWidget* parent)>;
    static void setEd3kLinkHandler(Ed3kLinkHandler handler);
    /// Whether hoster links have somewhere to go now.
    [[nodiscard]] static bool hosterReady();
    /// Hand @p files to the Hoster tab, as a package named @p package (empty: none).
    /// False when not hosterReady().
    static bool sendHosterLinks(const QList<QStringList>& files, QWidget* parent,
                                const QString& package = {});
    /// Who opens a fetched link container (search rows) — the Hoster tab. Unset without
    /// EMULE_HAVE_HOSTER_JD; it goes only while hosterReady().
    using HosterContainerHandler = std::function<void(const QString& name, const QByteArray& data, QWidget* parent)>;
    static void setHosterContainerHandler(HosterContainerHandler handler);
    /// Hand @p data (a .dlc/.ccf/.rsdf called @p name) to the Hoster tab. False when not
    /// hosterReady() or no handler.
    static bool sendHosterContainer(const QString& name, const QByteArray& data, QWidget* parent);

    /// The non-download links in @p text.
    [[nodiscard]] static OtherLinks otherLinksIn(const QString& text);

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
    /// @param category     download category for the files, 0 for none — which the
    ///                     daemon reads as "nobody chose", so auto-categories still apply
    static void importLinks(const QString& text, IpcClient* ipc, QWidget* parent,
                            Source source, Prompt prompt,
                            std::function<void(const Result&)> done = {},
                            std::function<void()> beforePrompt = {}, int category = 0);

    /// The DownloadSearchFile request for one link: [hash, name, size, raw link, category].
    [[nodiscard]] static Ipc::IpcMessage downloadRequest(const QString& hashHex, const QString& name,
                                                         quint64 size, const QString& rawLink,
                                                         int category);

    /// True when a file in state @p type must not be re-added from @p source.
    /// Shared/downloading files are always skipped; downloaded and cancelled ones only
    /// for automatic imports, so a manual paste can still start a re-download.
    [[nodiscard]] static bool shouldSkip(SearchFile::KnownType type, Source source);

    /// True when a file in state @p type should be added only after asking.
    ///
    /// The middle ground shouldSkip() has no room for: downloaded and cancelled
    /// files are not re-added by an automatic import at all, and a manual one may
    /// re-add them — but silently is the wrong way. Never true for a state
    /// shouldSkip() already refuses, so the two are safe to test in either order.
    [[nodiscard]] static bool shouldConfirm(SearchFile::KnownType type, Source source);

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
    enum class LinkKind : quint8 {
        File      = 0x1,  ///< ed2k://|file|
        HttpCache = 0x2,  ///< ed2k://|httpcache| — a configuration link, not a download
        Server    = 0x4,  ///< ed2k://|server| (not serverlist)
        Other     = 0x8,  ///< serverlist, nodeslist, search — acted on for manual imports
        Ed3k      = 0x10, ///< an ed3k: link (batch 15) — manual imports only
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
