#include "IndexerQuery.h"

#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <array>

namespace eMule::indexer {

namespace {

constexpr QLatin1StringView kApiKeyParam{"apikey"};
constexpr QLatin1StringView kRedacted{"<redacted>"};

/// Every query parameter that has been seen carrying a credential.
///
/// `apikey` is what buildIndexerSearchUrl() writes, but a feed URL is *pasted*
/// from an indexer's own RSS page and those use whatever the installation
/// chose — `r` is the newznab default on the RSS endpoint, and Jackett and the
/// torrent trackers use the rest. Over-redacting a harmless parameter costs a
/// display artefact in a log line; under-redacting costs the key, and this is
/// the only layer.
constexpr std::array kSecretParams{
    QLatin1StringView{"apikey"}, QLatin1StringView{"api_key"},
    QLatin1StringView{"r"},      QLatin1StringView{"token"},
    QLatin1StringView{"passkey"}, QLatin1StringView{"rss_token"},
};

/// Start from whatever the user configured, keeping any query they typed — a
/// self-hosted endpoint may need an extra parameter we know nothing about.
[[nodiscard]] QUrlQuery baseQuery(const QUrl& url)
{
    return QUrlQuery(url.query());
}

void applyApiKey(QUrlQuery& query, const IndexerConfig& config)
{
    if (config.apiKey.isEmpty())
        return;
    query.removeAllQueryItems(QString(kApiKeyParam));
    query.addQueryItem(QString(kApiKeyParam), config.apiKey);
}

} // namespace

QUrl buildIndexerCapsUrl(const IndexerConfig& config)
{
    QUrl url = config.apiUrl();
    if (!url.isValid())
        return {};

    QUrlQuery query = baseQuery(url);
    query.removeAllQueryItems(QStringLiteral("t"));
    query.addQueryItem(QStringLiteral("t"), QStringLiteral("caps"));

    // Several indexers refuse t=caps without a key even though the spec says it
    // is public, and none object to being given one.
    applyApiKey(query, config);

    url.setQuery(query);
    return url;
}

QUrl buildIndexerSearchUrl(const IndexerConfig& config, const IndexerQuery& query,
                           const IndexerCaps* caps)
{
    QUrl url = config.apiUrl();
    if (!url.isValid())
        return {};

    const QString mode = query.mode.isEmpty() ? kModeSearch.toString() : query.mode;
    const QStringView modeView(mode);

    QUrlQuery out = baseQuery(url);
    out.removeAllQueryItems(QStringLiteral("t"));
    out.addQueryItem(QStringLiteral("t"), mode);

    if (!query.text.isEmpty())
        out.addQueryItem(QStringLiteral("q"), query.text);

    if (!query.categories.isEmpty()) {
        QStringList ids;
        ids.reserve(query.categories.size());
        for (const int id : query.categories)
            ids.append(QString::number(id));
        out.addQueryItem(QStringLiteral("cat"), ids.join(u','));
    }

    // extended=1 is what turns on the newznab:attr elements. Without it the feed
    // carries a title and a link and nothing else worth showing.
    out.addQueryItem(QStringLiteral("extended"), QStringLiteral("1"));

    // Asking for more than the indexer allows is not an error — it silently
    // truncates, and then the offsets we page with no longer line up.
    int limit = std::max(1, query.limit);
    if (caps && caps->limitMax > 0)
        limit = std::min(limit, caps->limitMax);
    out.addQueryItem(QStringLiteral("limit"), QString::number(limit));

    if (query.offset > 0)
        out.addQueryItem(QStringLiteral("offset"), QString::number(query.offset));

    // Capability gating. A parameter the mode does not advertise is dropped
    // rather than sent and hoped for: some indexers ignore an unknown parameter,
    // and some reject the whole request.
    const auto allow = [caps, modeView](QStringView param) {
        return !caps || caps->isEmpty() || caps->supportsParam(modeView, param);
    };

    if (!query.season.isEmpty() && allow(u"season"))
        out.addQueryItem(QStringLiteral("season"), query.season);
    if (!query.episode.isEmpty() && allow(u"ep"))
        out.addQueryItem(QStringLiteral("ep"), query.episode);
    if (!query.imdbId.isEmpty() && allow(u"imdbid"))
        out.addQueryItem(QStringLiteral("imdbid"), query.imdbId);

    applyApiKey(out, config);

    url.setQuery(out);
    return url;
}

QString redactApiKey(const QUrl& url)
{
    if (!url.isValid())
        return url.toString();

    QUrlQuery query(url.query());
    bool touched = false;
    for (const auto param : kSecretParams) {
        const QString name(param);
        if (!query.hasQueryItem(name))
            continue;
        query.removeAllQueryItems(name);
        query.addQueryItem(name, QString(kRedacted));
        touched = true;
    }

    if (!touched)
        return url.toString();

    QUrl copy = url;
    copy.setQuery(query);
    return copy.toString();
}

QString redactApiKey(const QString& text)
{
    // Qt's network error strings embed the request URL verbatim, so the key
    // arrives inside prose that no QUrl parse will reach.
    static const QRegularExpression re(
        QStringLiteral("([?&](?:apikey|api_key|r|token|passkey|rss_token)=)[^&\\s\"'<>]*"),
        QRegularExpression::CaseInsensitiveOption);

    QString out = text;
    out.replace(re, QStringLiteral("\\1") + QString(kRedacted));
    return out;
}

} // namespace eMule::indexer
