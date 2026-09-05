#include "IndexerClient.h"

#include "net/HttpDefaults.h"
#include "net/HttpFileDownload.h"
#include "utils/Log.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace eMule::indexer {

IndexerClient::IndexerClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

IndexerClient::~IndexerClient()
{
    abortAll();
}

void IndexerClient::abortAll()
{
    // Copy first: abort() runs the finished handler synchronously for a reply
    // that has already errored, and that handler erases from m_pending.
    const auto pending = m_pending;
    m_pending.clear();
    for (QNetworkReply* reply : pending)
        reply->abort();
}

void IndexerClient::finish(QNetworkReply* reply, const QUrl& requestUrl,
                           const std::function<void(bool, const QByteArray&,
                                                    const QString&)>& done)
{
    m_pending.remove(reply);
    reply->deleteLater();

    const QByteArray body = reply->readAll();

    if (body.size() > kMaxResponseBytes) {
        done(false, {},
             QStringLiteral("the indexer returned more than %1 MB")
                 .arg(kMaxResponseBytes / (1024 * 1024)));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // Keep the body: an indexer commonly answers 4xx *and* explains itself
        // in an <error> document, and the explanation is the useful half.
        // redactApiKey on the error string is not optional — Qt embeds the
        // request URL in it, key and all.
        const QString transport = redactApiKey(reply->errorString());
        if (!body.isEmpty()) {
            done(false, body, transport);
            return;
        }
        logDebug(QStringLiteral("Indexer request failed: %1 (%2)")
                       .arg(redactApiKey(requestUrl), transport));
        done(false, {}, transport);
        return;
    }

    done(true, body, {});
}

void IndexerClient::probeCaps(const IndexerConfig& config, CapsCallback done)
{
    const QUrl url = buildIndexerCapsUrl(config);
    if (!url.isValid()) {
        done(false, {}, tr("\"%1\" is not a usable URL.").arg(config.url));
        return;
    }

    QNetworkRequest request = Http::makeRequest(url);
    request.setTransferTimeout(config.timeoutMs);

    QNetworkReply* reply = m_nam->get(request);
    m_pending.insert(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, done = std::move(done)]() {
        finish(reply, url, [&done](bool ok, const QByteArray& body, const QString& error) {
            if (!ok && body.isEmpty()) {
                done(false, {}, error);
                return;
            }

            QString parseError;
            const IndexerCaps caps = parseIndexerCaps(body, parseError);
            if (!parseError.isEmpty()) {
                // The indexer's own words beat the transport's every time:
                // "Incorrect user credentials" tells a user which field to fix,
                // "Unauthorized" does not.
                done(false, {}, parseError);
                return;
            }
            done(true, caps, {});
        });
    });
}

void IndexerClient::search(const IndexerConfig& config, const IndexerQuery& query,
                           const IndexerCaps* caps, SearchCallback done)
{
    const QUrl url = buildIndexerSearchUrl(config, query, caps);
    if (!url.isValid()) {
        done(false, {}, tr("\"%1\" is not a usable URL.").arg(config.url));
        return;
    }

    QNetworkRequest request = Http::makeRequest(url);
    request.setTransferTimeout(config.timeoutMs);

    QNetworkReply* reply = m_nam->get(request);
    m_pending.insert(reply);

    const QString name = config.displayName();
    const QString slug = config.slug();

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, name, slug, done = std::move(done)]() {
        finish(reply, url, [&](bool ok, const QByteArray& body, const QString& error) {
            if (!ok && body.isEmpty()) {
                done(false, {}, error);
                return;
            }

            IndexerSearchPage page = parseIndexerSearch(body, name, slug);
            if (!page.error.isEmpty()) {
                done(false, page, page.error);
                return;
            }
            done(true, page, {});
        });
    });
}

void IndexerClient::fetch(const IndexerConfig& config, const QUrl& url, FetchCallback done)
{
    if (!url.isValid()) {
        done(false, {}, tr("The result carries no download URL."));
        return;
    }

    // The one place HttpFileDownload is the right tool: a single shot with a
    // size cap and transparent unwrapping of a gzipped .nzb.
    HttpFileDownload::Options opts;
    opts.timeoutMs = config.timeoutMs;
    opts.maxBytes = kMaxResponseBytes;

    HttpFileDownload::get(this, url, opts,
                          [done = std::move(done)](bool ok, const QByteArray& data,
                                                   const QString&, const QString& error) {
        done(ok, data, redactApiKey(error));
    });
}

} // namespace eMule::indexer
