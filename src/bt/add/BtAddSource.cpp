#include "add/BtAddSource.h"

#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>

#include <QUrlQuery>

namespace lt = libtorrent;

namespace eMule::bt {

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
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(uri.trimmed().toStdString(), ec);
    if (ec)
        return std::unexpected(QString::fromStdString(ec.message()));
    if (!atp.info_hashes.has_v1() && !atp.info_hashes.has_v2())
        return std::unexpected(QStringLiteral("magnet link without an infohash"));
    return atp;
}

bool BtAddSource::isBtMagnet(const QString& text)
{
    const QString t = text.trimmed();
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

} // namespace eMule::bt
