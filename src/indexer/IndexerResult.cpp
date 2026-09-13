#include "IndexerResult.h"

#include "IndexerCaps.h"

#include "utils/OtherFunctions.h"

#include <QXmlStreamReader>

namespace eMule::indexer {

namespace {

/// Parse a feed date, tolerantly.
///
/// Qt's RFC2822Date parser validates the day-of-week name against the date and
/// returns an invalid QDateTime when they disagree — and feeds in the wild get
/// that wrong routinely, because the name is generated separately from the
/// timestamp. Rejecting the date over it would blank the Age column, which is
/// the single most useful thing about a Usenet result.
///
/// So: try it as written, then again with the day name dropped, then ISO 8601 —
/// a few indexers emit that instead.
[[nodiscard]] QDateTime parseFeedDate(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};

    if (QDateTime dt = QDateTime::fromString(trimmed, Qt::RFC2822Date); dt.isValid())
        return dt;

    if (const qsizetype comma = trimmed.indexOf(u','); comma > 0) {
        const QString withoutDayName = trimmed.mid(comma + 1).trimmed();
        if (QDateTime dt = QDateTime::fromString(withoutDayName, Qt::RFC2822Date);
            dt.isValid()) {
            return dt;
        }
    }

    return QDateTime::fromString(trimmed, Qt::ISODate);
}

/// Read one `<newznab:attr name= value=/>` — or torznab's, or one whose prefix
/// the feed never bound.
///
/// Matching is by **local name plus the `name=` attribute**, not by prefix
/// string: a prefix is a document-local label, and Prowlarr, Jackett and
/// NZBHydra2 do not agree on it. Checking the namespace URI would be correct XML
/// but still too strict in practice — feeds exist that emit `<attr>` with no
/// declaration at all — and since the two vocabularies share no field name,
/// dispatching on `name=` cannot confuse them.
void applyAttr(IndexerResult& row, QStringView key, QStringView value)
{
    if (value.isEmpty())
        return;

    // -- shared --
    if (key == QLatin1String("size")) {
        row.size = value.toLongLong();
    } else if (key == QLatin1String("category")) {
        bool ok = false;
        if (const int id = value.toInt(&ok); ok)
            row.categoryIds.append(id);
    } else if (key == QLatin1String("guid")) {
        if (row.guid.isEmpty())
            row.guid = value.toString();

    // -- newznab --
    } else if (key == QLatin1String("grabs")) {
        row.grabs = value.toInt();
    } else if (key == QLatin1String("files")) {
        row.files = value.toInt();
    } else if (key == QLatin1String("poster")) {
        row.poster = value.toString();
    } else if (key == QLatin1String("group")) {
        row.group = value.toString();
    } else if (key == QLatin1String("password")) {
        // The field is specified as a flag — 0 none, 1 maybe, 2 yes — and "-1"
        // is common in the wild. But indexers also put the real passphrase here,
        // so the value has to be classified rather than just tested.
        //
        // Anything that parses as an integer is a flag and never a password: "1"
        // is not somebody's passphrase, and using it as one turns a release that
        // would have prompted the user into one that fails with "wrong
        // password". Everything else is a candidate, with a length floor for the
        // "n/a" and "?" placeholders — the same `size() < 5` idea ServerList and
        // IPFilter use for junk lines.
        //
        // Erring toward *protected* costs a colour in the results list; erring
        // the other way costs a download that cannot be unpacked.
        bool numeric = false;
        const int flag = value.toInt(&numeric);
        if (numeric) {
            row.passwordProtected = flag != 0;
        } else {
            row.passwordProtected = true;
            if (value.size() >= 4)
                row.password = value.toString();
        }
    } else if (key == QLatin1String("usenetdate")) {
        if (const QDateTime dt = parseFeedDate(value.toString()); dt.isValid())
            row.published = dt;

    // -- torznab --
    } else if (key == QLatin1String("seeders")) {
        row.seeders = value.toInt();
    } else if (key == QLatin1String("peers")) {
        row.peers = value.toInt();
    } else if (key == QLatin1String("magneturl")) {
        row.magnetUrl = QUrl(value.toString());
    } else if (key == QLatin1String("infohash")) {
        row.infoHash = value.toString();
    }
}

void readItem(QXmlStreamReader& xml, IndexerResult& row, qint64& enclosureLength)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();

        if (name == QLatin1String("title")) {
            row.title = xml.readElementText().trimmed();
            // `Release{{secret}}` — the same marker NZBGet writes into a .nzb
            // filename, which some indexers carry in the title instead of in an
            // attribute. Taken out of the title as well as read, or the release
            // is queued under a name with the password showing.
            if (const QString braced = takeBracedPassword(row.title); !braced.isEmpty()) {
                row.password = braced;
                row.passwordProtected = true;
            }
            continue;
        }
        if (name == QLatin1String("guid")) {
            row.guid = xml.readElementText().trimmed();
            continue;
        }
        if (name == QLatin1String("pubDate")) {
            const QString text = xml.readElementText();
            if (!row.published.isValid())
                row.published = parseFeedDate(text);
            continue;
        }
        if (name == QLatin1String("link")) {
            const QString text = xml.readElementText().trimmed();
            if (row.downloadUrl.isEmpty())
                row.downloadUrl = QUrl(text);
            continue;
        }
        if (name == QLatin1String("enclosure")) {
            const auto attrs = xml.attributes();
            const QString url = attrs.value(QLatin1String("url")).toString();
            if (!url.isEmpty())
                row.downloadUrl = QUrl(url);   // wins over <link>, which is often an HTML page
            enclosureLength = attrs.value(QLatin1String("length")).toLongLong();
            xml.skipCurrentElement();
            continue;
        }
        if (name == QLatin1String("attr")) {
            const auto attrs = xml.attributes();
            applyAttr(row, attrs.value(QLatin1String("name")),
                      attrs.value(QLatin1String("value")));
            xml.skipCurrentElement();
            continue;
        }

        xml.skipCurrentElement();
    }
}

} // namespace

int IndexerResult::ageDays() const
{
    if (!published.isValid())
        return -1;
    return int(published.daysTo(QDateTime::currentDateTimeUtc()));
}

QString IndexerResult::dedupKey() const
{
    return title.trimmed().toCaseFolded() + u'|' + QString::number(size);
}

IndexerSearchPage parseIndexerSearch(const QByteArray& xml, const QString& indexerName,
                                     const QString& slug)
{
    IndexerSearchPage page;

    // Errors arrive with HTTP 200, so this check is not an optimisation — it is
    // the only thing standing between "wrong API key" and "0 results".
    page.error = parseIndexerError(xml);
    if (!page.error.isEmpty())
        return page;

    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement()) {
        page.error = reader.errorString().isEmpty() ? QStringLiteral("empty response")
                                                    : reader.errorString();
        return page;
    }
    if (reader.name() != QLatin1String("rss")) {
        page.error = QStringLiteral("not an RSS document (root is <%1>)")
                         .arg(reader.name().toString());
        return page;
    }

    while (reader.readNextStartElement()) {
        if (reader.name() != QLatin1String("channel")) {
            reader.skipCurrentElement();
            continue;
        }

        while (reader.readNextStartElement()) {
            const auto name = reader.name();

            if (name == QLatin1String("response")) {
                const auto attrs = reader.attributes();
                page.offset = attrs.value(QLatin1String("offset")).toInt();
                page.total = attrs.value(QLatin1String("total")).toInt();
                reader.skipCurrentElement();
                continue;
            }

            if (name != QLatin1String("item")) {
                reader.skipCurrentElement();
                continue;
            }

            IndexerResult row;
            row.indexerName = indexerName;
            qint64 enclosureLength = 0;
            readItem(reader, row, enclosureLength);

            // The attr and the enclosure disagree on several indexers. The attr
            // is the one the indexer computed for the release; the enclosure
            // length is sometimes the size of the .nzb file itself.
            if (row.size <= 0)
                row.size = enclosureLength;

            if (row.title.isEmpty() || row.downloadUrl.isEmpty())
                continue;

            if (row.guid.isEmpty())
                row.guid = row.downloadUrl.toString(QUrl::RemoveQuery);
            row.id = slug + u'/' + row.guid;

            page.results.append(row);
        }
    }

    if (reader.hasError() && page.results.isEmpty())
        page.error = reader.errorString();

    return page;
}

} // namespace eMule::indexer
