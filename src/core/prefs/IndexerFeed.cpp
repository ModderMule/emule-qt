#include "pch.h"
/// @file IndexerFeed.cpp
/// @brief One saved feed — implementation.

#include "prefs/IndexerFeed.h"

namespace eMule {

QString indexerFeedKindToString(IndexerFeedKind kind)
{
    if (kind == IndexerFeedKind::Url)
        return QStringLiteral("url");
    return QStringLiteral("search");
}

IndexerFeedKind indexerFeedKindFromString(const QString& text)
{
    if (text.trimmed().toLower() == QLatin1String("url"))
        return IndexerFeedKind::Url;
    return IndexerFeedKind::SavedSearch;
}

QString IndexerFeed::slug() const
{
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name.trimmed().toLower()) {
        if (ch.isLetterOrNumber())
            out.append(ch);
        else if (!out.endsWith(u'_'))
            out.append(u'_');
    }
    while (out.endsWith(u'_'))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("feed") : out;
}

bool IndexerFeed::isValid() const
{
    if (name.trimmed().isEmpty())
        return false;
    if (kind == IndexerFeedKind::Url)
        return feedUrl().isValid();
    return true;
}

QUrl IndexerFeed::feedUrl() const
{
    if (kind != IndexerFeedKind::Url)
        return {};

    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.host().isEmpty())
        return {};

    // http and https only. QNetworkAccessManager also speaks file: and qrc:, and
    // a feed naming one of those would be asking the daemon to poll its own disk
    // on a timer — the same restriction AddNzbUrl makes, for the same reason.
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return {};

    return parsed;
}

bool remapFeedCategories(QList<IndexerFeed>& feeds, const QHash<uint32, uint32>& oldToNew)
{
    bool changed = false;
    for (auto& feed : feeds) {
        const int mapped = remapCategoryIndex(feed.downloadCategory, oldToNew);
        if (mapped == feed.downloadCategory)
            continue;
        feed.downloadCategory = mapped;
        changed = true;
    }
    return changed;
}

} // namespace eMule
