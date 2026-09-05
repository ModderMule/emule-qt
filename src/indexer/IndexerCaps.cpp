#include "IndexerCaps.h"

#include <QXmlStreamReader>

namespace eMule::indexer {

namespace {

/// "yes"/"no" as well as "true"/"false" and "1"/"0". The caps schema uses words,
/// and an indexer that answers `available="yes"` would read as false to a naive
/// toBool.
[[nodiscard]] bool parseCapsBool(QStringView text, bool fallback = false)
{
    const QString t = text.trimmed().toString().toLower();
    if (t.isEmpty())
        return fallback;
    return t == QLatin1String("yes") || t == QLatin1String("true") || t == QLatin1String("1");
}

void readCategories(QXmlStreamReader& xml, QList<IndexerCategory>& out)
{
    // Positioned on <categories>.
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("category")) {
            xml.skipCurrentElement();
            continue;
        }

        IndexerCategory cat;
        const auto attrs = xml.attributes();
        cat.id = attrs.value(QLatin1String("id")).toInt();
        cat.name = attrs.value(QLatin1String("name")).toString();

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("subcat")) {
                IndexerCategory sub;
                const auto subAttrs = xml.attributes();
                sub.id = subAttrs.value(QLatin1String("id")).toInt();
                sub.name = subAttrs.value(QLatin1String("name")).toString();
                cat.subcategories.append(sub);
            }
            xml.skipCurrentElement();
        }

        out.append(cat);
    }
}

void readSearching(QXmlStreamReader& xml, QHash<QString, IndexerSearchMode>& out)
{
    // Positioned on <searching>. Its children are named for the mode:
    // <search>, <tv-search>, <movie-search>, …
    while (xml.readNextStartElement()) {
        IndexerSearchMode mode;
        const auto attrs = xml.attributes();
        mode.available = parseCapsBool(attrs.value(QLatin1String("available")));

        const QString params = attrs.value(QLatin1String("supportedParams")).toString();
        if (!params.isEmpty()) {
            mode.supportedParams =
                params.split(u',', Qt::SkipEmptyParts);
            for (QString& p : mode.supportedParams)
                p = p.trimmed();
        }

        out.insert(xml.name().toString(), mode);
        xml.skipCurrentElement();
    }
}

} // namespace

bool IndexerCaps::supportsMode(QStringView mode) const
{
    const auto it = modes.constFind(mode.toString());
    return it != modes.constEnd() && it->available;
}

bool IndexerCaps::supportsParam(QStringView mode, QStringView param) const
{
    const auto it = modes.constFind(mode.toString());
    if (it == modes.constEnd() || !it->available)
        return false;

    // An indexer that advertises the mode but lists no parameters is saying
    // nothing, not saying no. Several real ones omit supportedParams entirely,
    // and gating them to zero fields would make their search box unusable.
    if (it->supportedParams.isEmpty())
        return true;

    return it->supportedParams.contains(param.toString(), Qt::CaseInsensitive);
}

QString IndexerCaps::categoryName(int id) const
{
    for (const auto& cat : categories) {
        if (cat.id == id)
            return cat.name;
        for (const auto& sub : cat.subcategories) {
            if (sub.id == id)
                return sub.name;
        }
    }
    return {};
}

QString parseIndexerError(const QByteArray& xml)
{
    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement())
        return {};
    if (reader.name() != QLatin1String("error"))
        return {};

    const auto attrs = reader.attributes();
    const QString description = attrs.value(QLatin1String("description")).toString().trimmed();
    if (!description.isEmpty())
        return description;

    const QString code = attrs.value(QLatin1String("code")).toString().trimmed();
    return code.isEmpty() ? QStringLiteral("unspecified indexer error")
                          : QStringLiteral("indexer error %1").arg(code);
}

IndexerCaps parseIndexerCaps(const QByteArray& xml, QString& error)
{
    error.clear();
    IndexerCaps caps;

    if (const QString indexerError = parseIndexerError(xml); !indexerError.isEmpty()) {
        error = indexerError;
        return caps;
    }

    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement()) {
        error = reader.errorString().isEmpty()
                    ? QStringLiteral("empty response")
                    : reader.errorString();
        return caps;
    }
    if (reader.name() != QLatin1String("caps")) {
        error = QStringLiteral("not a caps document (root is <%1>)")
                    .arg(reader.name().toString());
        return caps;
    }

    while (reader.readNextStartElement()) {
        const auto name = reader.name();
        if (name == QLatin1String("server")) {
            caps.serverTitle = reader.attributes().value(QLatin1String("title")).toString();
            reader.skipCurrentElement();
        } else if (name == QLatin1String("limits")) {
            const auto attrs = reader.attributes();
            caps.limitMax = attrs.value(QLatin1String("max")).toInt();
            caps.limitDefault = attrs.value(QLatin1String("default")).toInt();
            if (caps.limitMax <= 0)
                caps.limitMax = 100;
            if (caps.limitDefault <= 0)
                caps.limitDefault = std::min(100, caps.limitMax);
            reader.skipCurrentElement();
        } else if (name == QLatin1String("searching")) {
            readSearching(reader, caps.modes);
        } else if (name == QLatin1String("categories")) {
            readCategories(reader, caps.categories);
        } else {
            reader.skipCurrentElement();
        }
    }

    if (reader.hasError()) {
        error = reader.errorString();
        return {};
    }

    caps.probedAt = QDateTime::currentDateTimeUtc();
    return caps;
}

} // namespace eMule::indexer
