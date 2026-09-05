#include "pch.h"
/// @file IndexerConfig.cpp
/// @brief One configured newznab/torznab indexer account — implementation.

#include "prefs/IndexerConfig.h"

namespace eMule {

QString indexerKindToString(IndexerKind kind)
{
    switch (kind) {
    case IndexerKind::Torznab: return QStringLiteral("torznab");
    case IndexerKind::Both:    return QStringLiteral("both");
    case IndexerKind::Newznab: break;
    }
    return QStringLiteral("newznab");
}

IndexerKind indexerKindFromString(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("torznab"))
        return IndexerKind::Torznab;
    if (t == QLatin1String("both"))
        return IndexerKind::Both;
    return IndexerKind::Newznab;
}

QString IndexerConfig::displayName() const
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        return trimmed;
    const QString host = QUrl(url).host();
    return host.isEmpty() ? url : host;
}

QString IndexerConfig::slug() const
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
    return out.isEmpty() ? QStringLiteral("indexer") : out;
}

QUrl IndexerConfig::apiUrl() const
{
    QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.host().isEmpty())
        return {};

    // A bare host is the one case we can safely complete. Anything with a real
    // path was typed deliberately — Jackett's endpoint already ends in "/api"
    // and NZBHydra2's is "/api" under a mount point — so appending there would
    // turn a working URL into a 404 the user cannot correct.
    const QString path = parsed.path();
    if (path.isEmpty() || path == QLatin1String("/"))
        parsed.setPath(QStringLiteral("/api"));

    return parsed;
}

} // namespace eMule
