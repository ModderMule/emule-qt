#pragma once

/// @file HttpCacheLinkImporter.h
/// @brief Apply an `ed2k://|httpcache|` configuration link.
///
/// The counterpart to Ed2kLinkImporter, which turns links into downloads: this
/// one turns a link into a stored HTTP Cache credential. Ed2kLinkImporter routes
/// config links here, so every place that accepts link text — the clipboard
/// watcher, the ed2k:// URL handler, the command line, PasteLinksDialog and the
/// Transfers paste action — gets this for free.
///
/// The flow is docs/protocol/http-cache-spec.md §8.1, in that order: handshake
/// with the server, then ask the user, then store. The link carries an upload
/// credential, so it is never logged and never shown back.

#include <QCoreApplication>
#include <QString>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;
struct ED2KHttpCacheLink;

class HttpCacheLinkImporter {
    Q_DECLARE_TR_FUNCTIONS(HttpCacheLinkImporter)

public:
    /// Probe the server, confirm with the user, then store the configuration.
    ///
    /// Asynchronous — two IPC round trips, one of them waiting on the daemon's
    /// HTTP handshake — so @p done runs well after this returns, and not at all
    /// when @p ipc is null or disconnected.
    ///
    /// The confirmation is unconditional. Pasting a batch of links is consent to
    /// download them; it is not consent to store somebody's upload credential.
    ///
    /// @param parent  parent for the dialogs; may be null
    /// @param done    true when the configuration was stored
    static void apply(const ED2KHttpCacheLink& link, IpcClient* ipc, QWidget* parent,
                      std::function<void(bool)> done = {},
                      std::function<void()> beforePrompt = {});
};

} // namespace eMule
