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
};

} // namespace eMule
