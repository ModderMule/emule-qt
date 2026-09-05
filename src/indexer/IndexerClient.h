#pragma once

/// @file IndexerClient.h
/// @brief One HTTP client for every configured indexer.
///
/// Holds its own QNetworkAccessManager rather than going through
/// core/net/HttpFileDownload, for two reasons that both matter here and neither
/// of which matters to that class's callers:
///
///   - HttpFileDownload::finishReply() **discards the body on any HTTP error**,
///     and a newznab error *is* the body. Losing it turns "Incorrect user
///     credentials" into "server replied: Unauthorized".
///   - It mints a fresh QNetworkAccessManager per call, so a paged search across
///     several indexers could not reuse a single connection.
///
/// It does reuse eMule::Http::makeRequest(), which carries the User-Agent and
/// the NoLessSafeRedirectPolicy — an https indexer redirected down to http would
/// put the API key on the wire in clear.
///
/// The one-shot NZB fetch is a different job and does go through
/// HttpFileDownload: it already caps the response size and gunzips a compressed
/// payload.

#include "IndexerCaps.h"
#include "IndexerConfig.h"
#include "IndexerQuery.h"
#include "IndexerResult.h"

#include <QByteArray>
#include <QObject>
#include <QSet>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule::indexer {

class IndexerClient : public QObject {
    Q_OBJECT

public:
    explicit IndexerClient(QObject* parent = nullptr);
    ~IndexerClient() override;

    using CapsCallback = std::function<void(bool ok, const IndexerCaps& caps,
                                            const QString& error)>;
    using SearchCallback = std::function<void(bool ok, const IndexerSearchPage& page,
                                              const QString& error)>;
    using FetchCallback = std::function<void(bool ok, const QByteArray& body,
                                             const QString& error)>;

    /// `t=caps`. Also the implementation of the Options page's Test button, so
    /// its error text is what a user reads when their key is wrong.
    void probeCaps(const IndexerConfig& config, CapsCallback done);

    /// One page of a `t=search`. Paging is the caller's business — IndexerSearch
    /// owns the loop, because only it knows the quota budget.
    void search(const IndexerConfig& config, const IndexerQuery& query,
                const IndexerCaps* caps, SearchCallback done);

    /// Fetch a result's payload (an .nzb, or a .torrent later). Runs daemon-side
    /// so the API key embedded in the URL never has to leave the daemon.
    void fetch(const IndexerConfig& config, const QUrl& url, FetchCallback done);

    /// Abort everything in flight. Called when a search is stopped and at
    /// teardown; a reply outliving its callback's captures is the usual way this
    /// kind of class crashes.
    void abortAll();

    /// Ceiling on any single response. A caps document is a few hundred KB at
    /// worst and a search page far less; a multi-megabyte answer is a
    /// misconfigured URL pointing at something else entirely.
    static constexpr qint64 kMaxResponseBytes = 16 * 1024 * 1024;

private:
    /// Common tail: pull the body out whatever the HTTP status said, then let
    /// the caller decide what it means. @p body is filled even on an error,
    /// because that is where the indexer explains itself.
    void finish(QNetworkReply* reply, const QUrl& requestUrl,
                const std::function<void(bool ok, const QByteArray& body,
                                         const QString& error)>& done);

    QNetworkAccessManager* m_nam = nullptr;
    QSet<QNetworkReply*> m_pending;
};

} // namespace eMule::indexer
