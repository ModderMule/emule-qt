#include "IndexerQuery.h"

#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace eMule::indexer {

namespace {

constexpr QLatin1StringView kApiKeyParam{"apikey"};
constexpr QLatin1StringView kRedacted{"<redacted>"};

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
    if (!query.hasQueryItem(QString(kApiKeyParam)))
        return url.toString();

    query.removeAllQueryItems(QString(kApiKeyParam));
    query.addQueryItem(QString(kApiKeyParam), QString(kRedacted));

    QUrl copy = url;
    copy.setQuery(query);
    return copy.toString();
}

QString redactApiKey(const QString& text)
{
    // Qt's network error strings embed the request URL verbatim, so the key
    // arrives inside prose that no QUrl parse will reach.
    static const QRegularExpression re(
        QStringLiteral("([?&]apikey=)[^&\\s\"'<>]*"),
        QRegularExpression::CaseInsensitiveOption);

    QString out = text;
    out.replace(re, QStringLiteral("\\1") + QString(kRedacted));
    return out;
}

} // namespace eMule::indexer
