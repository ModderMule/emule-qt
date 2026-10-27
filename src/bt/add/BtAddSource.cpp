#include "add/BtAddSource.h"

#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>

#include <QUrlQuery>

#include <algorithm>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

/// The decoded query items of a link after its '?'.
QList<std::pair<QString, QString>> queryOf(const QString& link)
{
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    return q < 0 ? QList<std::pair<QString, QString>>{} : QUrlQuery(link.mid(q + 1)).queryItems(QUrl::FullyDecoded);
}

bool isEd3kFileLink(const QString& t)
{
    return t.startsWith(QLatin1StringView("ed3k:file?"), Qt::CaseInsensitive);
}

/// `bt` of an ed3k:file link: 40 or 64 hex; empty when absent or malformed.
QString btOf(const QString& link)
{
    for (const auto& [key, value] : queryOf(link)) {
        if (key != QLatin1StringView("bt"))
            continue;
        const bool hex = std::ranges::all_of(value, [](QChar c) { return c.isDigit() || (c.toLower() >= u'a' && c.toLower() <= u'f'); });
        if (hex && (value.size() == 40 || value.size() == 64))
            return value.toLower();
    }
    return {};
}

} // namespace

std::expected<lt::add_torrent_params, QString> BtAddSource::fromTorrentFile(const QByteArray& data)
{
    if (data.isEmpty())
        return std::unexpected(QStringLiteral("empty torrent file"));
    if (data.size() > kMaxTorrentFile)
        return std::unexpected(QStringLiteral("torrent file too large"));
    try {
        lt::add_torrent_params atp = lt::load_torrent_buffer(lt::span<char const>(data.constData(), static_cast<std::ptrdiff_t>(data.size())));
        if (!atp.ti || !atp.ti->is_valid())
            return std::unexpected(QStringLiteral("not a torrent file"));
        return atp;
    } catch (const lt::system_error& e) {
        return std::unexpected(QString::fromStdString(e.code().message()));
    } catch (const std::exception& e) {
        return std::unexpected(QString::fromUtf8(e.what()));
    }
}

std::expected<lt::add_torrent_params, QString> BtAddSource::fromMagnet(const QString& uri)
{
    if (!isBtMagnet(uri))
        return std::unexpected(QStringLiteral("not a BitTorrent magnet link"));
    QString magnet = uri.trimmed();
    if (isEd3kFileLink(magnet)) {
        // the infohash only; the link itself goes to the join (ed3kLinkOf)
        const QString bt = btOf(magnet);
        magnet = bt.size() == 40 ? QStringLiteral("magnet:?xt=urn:btih:") + bt : QStringLiteral("magnet:?xt=urn:btmh:1220") + bt;
        for (const auto& [key, value] : queryOf(uri.trimmed()))
            if (key == QLatin1StringView("dn"))
                magnet += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(value));
    }
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(magnet.toStdString(), ec);
    if (ec)
        return std::unexpected(QString::fromStdString(ec.message()));
    if (!atp.info_hashes.has_v1() && !atp.info_hashes.has_v2())
        return std::unexpected(QStringLiteral("magnet link without an infohash"));
    return atp;
}

bool BtAddSource::isBtMagnet(const QString& text)
{
    const QString t = text.trimmed();
    if (isEd3kFileLink(t))
        return !btOf(t).isEmpty();
    if (!t.startsWith(QLatin1StringView("magnet:?"), Qt::CaseInsensitive))
        return false;
    const QUrlQuery query(t.mid(8));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        if (key.compare(QLatin1StringView("xt"), Qt::CaseInsensitive) != 0 && !key.startsWith(QLatin1StringView("xt."), Qt::CaseInsensitive))
            continue;
        if (value.startsWith(QLatin1StringView("urn:btih:"), Qt::CaseInsensitive) || value.startsWith(QLatin1StringView("urn:btmh:"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString BtAddSource::ed3kLinkOf(const QString& uri)
{
    const QString t = uri.trimmed();
    if (isEd3kFileLink(t))
        return t;
    if (!t.startsWith(QLatin1StringView("magnet:?"), Qt::CaseInsensitive))
        return {};
    for (const auto& [key, value] : queryOf(t))
        if (key == QLatin1StringView("xs") && isEd3kFileLink(value))
            return value;
    return {};
}

} // namespace eMule::bt
