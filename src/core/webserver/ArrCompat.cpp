#include "pch.h"
/// @file ArrCompat.cpp
/// @brief Pure helpers of the Sonarr/Radarr bridge — implementation.

#include "webserver/ArrCompat.h"

#include "protocol/ED2KLink.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamWriter>

#include <algorithm>

namespace eMule::arr {

namespace {

using L = QLatin1StringView;

constexpr int kTvCategory = 5000;
constexpr int kMovieCategory = 2000;

[[nodiscard]] bool isHex(QStringView text)
{
    return std::ranges::all_of(text, [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
    });
}

[[nodiscard]] QString hexOf(const std::array<uint8, 16>& hash)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(hash.data()), 16).toHex());
}

/// Lowercase words, every non-alnum a separator.
[[nodiscard]] QStringList words(const QString& text)
{
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return text.toLower().split(sep, Qt::SkipEmptyParts);
}

[[nodiscard]] int group(int cat) { return cat / 1000 * 1000; }

} // namespace

QString btihForMd4(const QString& md4Hex)
{
    return md4Hex.toLower() + QStringLiteral("00000000");
}

QString md4FromBtih(const QString& btih)
{
    if (btih.size() != 40 || !isHex(btih) || !btih.endsWith(L("00000000")))
        return {};
    return btih.left(32).toLower();
}

QString magnetFor(const QString& md4Hex, const QString& name, quint64 size)
{
    return QStringLiteral("magnet:?xt=urn:btih:") + btihForMd4(md4Hex)
        + QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(name))
        + QStringLiteral("&xl=") + QString::number(size);
}

QString ed2kLinkFor(const Ed2kRef& ref)
{
    QString name = ref.name;
    name.replace(u'|', u'_');
    return QStringLiteral("ed2k://|file|%1|%2|%3|/").arg(name, QString::number(ref.size), ref.hashHex.toUpper());
}

std::optional<Ed2kRef> parseArrLink(const QString& link)
{
    const QString text = link.trimmed();
    if (text.startsWith(L("magnet:"), Qt::CaseInsensitive)) {
        Ed2kRef ref;
        const QStringList pairs = text.mid(text.indexOf(u'?') + 1).split(u'&', Qt::SkipEmptyParts);
        for (const QString& pair : pairs) {
            const qsizetype eq = pair.indexOf(u'=');
            if (eq <= 0)
                continue;
            const QString key = pair.left(eq).toLower();
            QByteArray raw = pair.mid(eq + 1).toUtf8();
            raw.replace('+', ' ');
            const QString rawValue = QString::fromUtf8(QByteArray::fromPercentEncoding(raw));
            const QString value = rawValue.trimmed();
            if (key == L("xt")) {
                if (value.startsWith(L("urn:btih:"), Qt::CaseInsensitive)) {
                    if (const QString md4 = md4FromBtih(value.mid(9)); !md4.isEmpty())
                        ref.hashHex = md4;
                } else if (value.startsWith(L("urn:ed2k:"), Qt::CaseInsensitive)) {
                    const QString md4 = value.mid(9);
                    if (md4.size() == 32 && isHex(md4))
                        ref.hashHex = md4.toLower();
                }
            } else if (key == L("dn")) {
                ref.name = value;
            } else if (key == L("xl")) {
                ref.size = value.toULongLong();
            }
        }
        if (ref.hashHex.isEmpty() || ref.size == 0)
            return std::nullopt;
        if (ref.name.isEmpty())
            ref.name = ref.hashHex;
        return ref;
    }

    const auto parsed = parseED2KLink(normalizeEd2kLink(text));
    if (!parsed)
        return std::nullopt;
    const auto* file = std::get_if<ED2KFileLink>(&*parsed);
    if (!file || file->size == 0)
        return std::nullopt;
    return Ed2kRef{hexOf(file->hash), file->name, file->size};
}

QString qbitState(const DownloadState& s)
{
    if (s.error)
        return QStringLiteral("error");
    if (s.complete)
        return QStringLiteral("pausedUP");   // done; ratio_limit 0 lets Arr remove it
    if (s.completing)
        return QStringLiteral("moving");
    if (s.hashing)
        return QStringLiteral("checkingDL");
    if (s.paused)
        return QStringLiteral("pausedDL");
    if (s.datarate > 0)
        return QStringLiteral("downloading");
    return s.sources > 0 ? QStringLiteral("queuedDL") : QStringLiteral("stalledDL");
}

// --- Torznab -------------------------------------------------------------------

QByteArray torznabCaps()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("caps"));
    w.writeEmptyElement(QStringLiteral("server"));
    w.writeAttribute(QStringLiteral("title"), QStringLiteral("eMule Qt (eD2K)"));
    w.writeEmptyElement(QStringLiteral("limits"));
    w.writeAttribute(QStringLiteral("default"), QStringLiteral("100"));
    w.writeAttribute(QStringLiteral("max"), QStringLiteral("100"));

    w.writeStartElement(QStringLiteral("searching"));
    const auto mode = [&w](const char* name, const char* params) {
        w.writeEmptyElement(QString::fromLatin1(name));
        w.writeAttribute(QStringLiteral("available"), QStringLiteral("yes"));
        w.writeAttribute(QStringLiteral("supportedParams"), QString::fromLatin1(params));
    };
    mode("search", "q");
    mode("tv-search", "q,season,ep");
    mode("movie-search", "q");
    mode("music-search", "q");
    mode("book-search", "q");
    w.writeEndElement();

    w.writeStartElement(QStringLiteral("categories"));
    const auto category = [&w](int id, const char* name, std::initializer_list<std::pair<int, const char*>> subs) {
        w.writeStartElement(QStringLiteral("category"));
        w.writeAttribute(QStringLiteral("id"), QString::number(id));
        w.writeAttribute(QStringLiteral("name"), QString::fromLatin1(name));
        for (const auto& [subId, subName] : subs) {
            w.writeEmptyElement(QStringLiteral("subcat"));
            w.writeAttribute(QStringLiteral("id"), QString::number(subId));
            w.writeAttribute(QStringLiteral("name"), QString::fromLatin1(subName));
        }
        w.writeEndElement();
    };
    category(2000, "Movies", {{2030, "Movies/SD"}, {2040, "Movies/HD"}});
    category(3000, "Audio", {{3010, "Audio/MP3"}, {3040, "Audio/Lossless"}});
    category(4000, "PC", {});
    category(5000, "TV", {{5030, "TV/SD"}, {5040, "TV/HD"}});
    category(7000, "Books", {{7020, "Books/EBook"}});
    category(8000, "Other", {});
    w.writeEndElement();

    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray torznabFeed(const QList<TorznabItem>& items, const QString& selfUrl)
{
    const QString torznabNs = QStringLiteral("http://torznab.com/schemas/2015/feed");
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("rss"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));
    w.writeNamespace(QStringLiteral("http://www.w3.org/2005/Atom"), QStringLiteral("atom"));
    w.writeNamespace(torznabNs, QStringLiteral("torznab"));
    w.writeStartElement(QStringLiteral("channel"));
    w.writeEmptyElement(QStringLiteral("http://www.w3.org/2005/Atom"), QStringLiteral("link"));
    w.writeAttribute(QStringLiteral("href"), selfUrl);
    w.writeAttribute(QStringLiteral("rel"), QStringLiteral("self"));
    w.writeAttribute(QStringLiteral("type"), QStringLiteral("application/rss+xml"));
    w.writeTextElement(QStringLiteral("title"), QStringLiteral("eMule Qt"));
    w.writeTextElement(QStringLiteral("description"), QStringLiteral("eD2K and Kad search results"));

    for (const TorznabItem& item : items) {
        const QString magnet = magnetFor(item.hashHex, item.fileName, item.size);
        w.writeStartElement(QStringLiteral("item"));
        w.writeTextElement(QStringLiteral("title"), item.title);
        w.writeStartElement(QStringLiteral("guid"));
        w.writeAttribute(QStringLiteral("isPermaLink"), QStringLiteral("false"));
        w.writeCharacters(item.hashHex.toLower());
        w.writeEndElement();
        w.writeTextElement(QStringLiteral("link"), magnet);
        // comments is a details URL to Arr: the ed2k link goes in the description
        w.writeTextElement(QStringLiteral("description"), ed2kLinkFor({item.hashHex, item.fileName, item.size}));
        w.writeTextElement(QStringLiteral("pubDate"),
                           QLocale::c().toString(item.published.toUTC(),
                                                 QStringLiteral("ddd, dd MMM yyyy HH:mm:ss +0000")));
        w.writeTextElement(QStringLiteral("size"), QString::number(item.size));
        w.writeTextElement(QStringLiteral("category"), QString::number(item.category));
        w.writeEmptyElement(QStringLiteral("enclosure"));
        w.writeAttribute(QStringLiteral("url"), magnet);
        w.writeAttribute(QStringLiteral("length"), QString::number(item.size));
        w.writeAttribute(QStringLiteral("type"), QStringLiteral("application/x-bittorrent"));
        const auto attr = [&w, &torznabNs](const QString& name, const QString& value) {
            w.writeEmptyElement(torznabNs, QStringLiteral("attr"));
            w.writeAttribute(QStringLiteral("name"), name);
            w.writeAttribute(QStringLiteral("value"), value);
        };
        attr(QStringLiteral("category"), QString::number(item.category));
        if (const int g = group(item.category); g != item.category)
            attr(QStringLiteral("category"), QString::number(g));
        attr(QStringLiteral("size"), QString::number(item.size));
        attr(QStringLiteral("seeders"), QString::number(item.seeders));
        attr(QStringLiteral("peers"), QString::number(std::max(item.peers, item.seeders)));
        attr(QStringLiteral("infohash"), btihForMd4(item.hashHex));
        attr(QStringLiteral("magneturl"), magnet);
        attr(QStringLiteral("files"), QStringLiteral("1"));
        attr(QStringLiteral("downloadvolumefactor"), QStringLiteral("1"));
        attr(QStringLiteral("uploadvolumefactor"), QStringLiteral("1"));
        w.writeEndElement();
    }

    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray torznabError(int code, const QString& description)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeEmptyElement(QStringLiteral("error"));
    w.writeAttribute(QStringLiteral("code"), QString::number(code));
    w.writeAttribute(QStringLiteral("description"), description);
    w.writeEndDocument();
    return out;
}

QString ed2kTypeForCategories(const QList<int>& cats)
{
    bool video = false, audio = false, books = false, pc = false, other = false;
    for (const int cat : cats) {
        switch (group(cat)) {
        case 2000: case 5000: video = true; break;
        case 3000: audio = true; break;
        case 7000: books = true; break;
        case 4000: pc = true; break;
        default: other = true; break;
        }
    }
    // one network type per search: mixed asks leave it open
    const int kinds = int(video) + int(audio) + int(books) + int(pc) + int(other);
    if (kinds != 1)
        return {};
    if (video) return QStringLiteral("Video");
    if (audio) return QStringLiteral("Audio");
    if (books) return QStringLiteral("Doc");
    if (pc) return QStringLiteral("Pro");
    return {};
}

int categoryForResult(const QString& fileName, const QList<int>& requested)
{
    static const QRegularExpression episode(QStringLiteral("(?i)\\bS\\d{1,2}E\\d{1,3}\\b|\\b\\d{1,2}x\\d{2}\\b"));
    const bool tv = episode.match(fileName).hasMatch();
    const int guess = tv ? kTvCategory : kMovieCategory;
    for (const int cat : requested) {
        if (group(cat) == guess)
            return cat;
    }
    // asked for one group only: the row answers that ask
    return requested.isEmpty() ? guess : requested.first();
}

QList<int> parseCategories(const QString& text)
{
    QList<int> out;
    for (const QString& part : text.split(u',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = part.trimmed().toInt(&ok);
        if (ok && v > 0 && !out.contains(v))
            out.append(v);
    }
    return out;
}

QString tvQuery(const QString& q, const QString& season, const QString& episode)
{
    QString out = q.trimmed();
    bool okS = false, okE = false;
    const int s = season.toInt(&okS);
    const int e = episode.toInt(&okE);
    if (okS && s >= 0) {
        out += QStringLiteral(" S%1").arg(s, 2, 10, QLatin1Char('0'));
        if (okE && e >= 0)
            out += QStringLiteral("E%1").arg(e, 2, 10, QLatin1Char('0'));
    }
    return out.trimmed();
}

QString releaseTitle(const QString& fileName)
{
    static const QRegularExpression ext(QStringLiteral("\\.[A-Za-z0-9]{2,4}$"));
    QString title = fileName;
    title.remove(ext);
    return title.trimmed();
}

bool nameMatchesQuery(const QString& fileName, const QString& query)
{
    const QStringList have = words(fileName);
    const QStringList want = words(query);
    return !want.isEmpty() && std::ranges::all_of(want, [&have](const QString& w) { return have.contains(w); });
}

// --- Arr Wanted lists ----------------------------------------------------------

QList<WantedTerm> parseSonarrMissing(const QJsonDocument& doc, const QString& source)
{
    QList<WantedTerm> out;
    for (const QJsonValue& v : doc.object().value(L("records")).toArray()) {
        const QJsonObject rec = v.toObject();
        if (rec.contains(L("monitored")) && !rec.value(L("monitored")).toBool())
            continue;
        const QString title = rec.value(L("series")).toObject().value(L("title")).toString().trimmed();
        const int season = rec.value(L("seasonNumber")).toInt(-1);
        const int episode = rec.value(L("episodeNumber")).toInt(-1);
        if (title.isEmpty() || season < 0 || episode < 0)
            continue;
        out.append({tvQuery(title, QString::number(season), QString::number(episode)), kTvCategory, source});
    }
    return out;
}

QList<WantedTerm> parseRadarrMovies(const QJsonDocument& doc, const QString& source, int max)
{
    QList<WantedTerm> out;
    // Radarr v5 wanted/missing pages "records"; /movie is a plain array
    const QJsonArray rows = doc.isArray() ? doc.array() : doc.object().value(L("records")).toArray();
    for (const QJsonValue& v : rows) {
        if (out.size() >= max)
            break;
        const QJsonObject movie = v.toObject();
        if (!movie.value(L("monitored")).toBool(true) || movie.value(L("hasFile")).toBool(false))
            continue;
        if (movie.contains(L("isAvailable")) && !movie.value(L("isAvailable")).toBool())
            continue;
        const QString title = movie.value(L("title")).toString().trimmed();
        if (title.isEmpty())
            continue;
        const int year = movie.value(L("year")).toInt();
        out.append({year > 0 ? QStringLiteral("%1 %2").arg(title).arg(year) : title, kMovieCategory, source});
    }
    return out;
}

// --- Gluetun -------------------------------------------------------------------

quint16 parseGluetunPort(const QByteArray& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    int port = o.value(L("port")).toInt();
    if (port <= 0) {
        const QJsonArray ports = o.value(L("ports")).toArray();
        if (!ports.isEmpty())
            port = ports.first().toInt();
    }
    return port > 0 && port <= 65535 ? quint16(port) : quint16(0);
}

QString parseGluetunStatus(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object().value(L("status")).toString();
}

QString parseGluetunPublicIp(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object().value(L("public_ip")).toString();
}

} // namespace eMule::arr
