#pragma once

/// @file NzbUrlFetch.h
/// @brief Fetching one .nzb from an http(s) URL, daemon-side.
///
/// Pasting a link is the ordinary way an NZB arrives, and the daemon is what
/// fetches it — not the GUI. Two reasons, the same ones GrabIndexerResult has:
/// the URL is often only reachable from the daemon's own network (a self-hosted
/// indexer on the LAN it sits on), and the bytes have to reach UsenetQueue
/// anyway, so a GUI-side fetch is two hops and a few megabytes back through CBOR.
///
/// Validation and naming live here rather than in IpcClientHandler because that
/// class has no test harness. Everything decidable is therefore a static that
/// tst_UsenetNzbUrl can call directly.
///
/// Statics only — it declares no signals or slots, so moc never has to see it
/// and the header belongs in <ClInclude>. (Say that without naming the macro:
/// scripts/sync_module_vcxproj.py greps the whole file for the token, comments
/// included, and files the header for moc if it finds one.)

#include "utils/Types.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QUrl>

#include <functional>

class QObject;

namespace eMule::usenet {

class NzbUrlFetch {
    Q_DECLARE_TR_FUNCTIONS(NzbUrlFetch)

public:
    /// Ceiling on the downloaded .nzb, below HttpFileDownload's 64 MiB default.
    /// Even a 100 GB release is ~140k segments and roughly 15 MB of XML, so this
    /// has real headroom while halving what a hostile URL can make the daemon
    /// allocate.
    static constexpr uint64 kMaxNzbBytes = 32ull * 1024 * 1024;

    struct Result {
        QByteArray data;
        QString name;    ///< display name, empty when the URL carried none
        QString error;   ///< empty on success

        [[nodiscard]] bool ok() const { return error.isEmpty(); }
    };

    using Callback = std::function<void(const Result&)>;

    /// Why @p url may not be fetched, or empty when it may.
    ///
    /// **http and https only.** QNetworkAccessManager also speaks `file:` and
    /// `qrc:`, so without this a client naming `file:///etc/…` would be asking
    /// the daemon to read its own disk and hand the contents back.
    ///
    /// Private and loopback addresses are deliberately **allowed**: unlike a
    /// peer-supplied HTTP-cache URL, this one is the operator's own, and the
    /// commonest real deployment — a self-hosted indexer on the daemon's LAN —
    /// is exactly a private address. Blocking it would break the main use case
    /// to defend the operator against themselves.
    [[nodiscard]] static QString rejectReason(const QUrl& url);

    /// Display name from the URL's last path segment: percent-decoded, with a
    /// trailing `.gz` and then `.nzb` stripped.
    ///
    /// Empty is a normal answer, not a failure. An API-style URL
    /// (`…/api?t=get&id=12345`) carries no name at all, and the caller then
    /// passes an empty name to UsenetQueue::addNzb so the NZB's own
    /// `<meta type="name">` can win.
    [[nodiscard]] static QString nameFromUrl(const QUrl& url);

    /// Fetch @p url and hand the bytes to @p done.
    ///
    /// @p context owns the callback's lifetime exactly as HttpFileDownload::get
    /// does: destroy it before the reply arrives and @p done is never invoked.
    ///
    /// Deliberately does not parse. Turning bytes into a queue item is
    /// UsenetQueue::addNzb's job, and it is the only thing that can — keeping the
    /// two apart is what makes "the download failed" and "that was not an NZB"
    /// two different messages to the user.
    static void fetch(QObject* context, const QUrl& url, Callback done);
};

} // namespace eMule::usenet
