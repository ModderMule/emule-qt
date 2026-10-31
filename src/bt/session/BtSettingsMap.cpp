#include "session/BtSettingsMap.h"

#include <libtorrent/alert.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <limits>

namespace lt = libtorrent;

namespace eMule::bt {

lt::settings_pack BtSettingsMap::toPack(const BtSettings& s)
{
    lt::settings_pack p;
    p.set_str(lt::settings_pack::listen_interfaces, listenInterfaces(s));
    p.set_str(lt::settings_pack::outgoing_interfaces, outgoingInterfaces(s));
    p.set_bool(lt::settings_pack::enable_dht, s.dht);
    p.set_bool(lt::settings_pack::enable_lsd, s.lsd);
    // the app's PortMapper maps the port (research doc §7.5)
    p.set_bool(lt::settings_pack::enable_upnp, false);
    p.set_bool(lt::settings_pack::enable_natpmp, false);

    p.set_int(lt::settings_pack::download_rate_limit, rateBytes(s.maxDownloadKb));
    p.set_int(lt::settings_pack::upload_rate_limit, rateBytes(s.maxUploadKb));
    // limits count protocol bytes only, as eD2K's do: with overhead on, a fast download's
    // TCP ACKs ate the whole upload share and nothing was uploaded (#87)
    p.set_bool(lt::settings_pack::rate_limit_ip_overhead, false);
    p.set_int(lt::settings_pack::active_downloads, std::max(1, s.maxActiveDownloads));
    p.set_int(lt::settings_pack::active_seeds, std::max(0, s.maxActiveSeeds));
    p.set_int(lt::settings_pack::active_limit, std::max(1, s.maxActiveDownloads) + std::max(0, s.maxActiveSeeds));
    p.set_int(lt::settings_pack::connections_limit, std::max(2, s.maxConnections));

    int policy = lt::settings_pack::pe_enabled;
    if (s.encryption == 1)
        policy = lt::settings_pack::pe_forced;
    else if (s.encryption == 2)
        policy = lt::settings_pack::pe_disabled;
    p.set_int(lt::settings_pack::out_enc_policy, policy);
    p.set_int(lt::settings_pack::in_enc_policy, policy);
    p.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);

    if (!s.userAgent.isEmpty())
        p.set_str(lt::settings_pack::user_agent, s.userAgent.toStdString() + " libtorrent/" LIBTORRENT_VERSION);
    p.set_str(lt::settings_pack::peer_fingerprint, lt::generate_fingerprint("EQ", s.versionMajor, s.versionMinor, s.versionRevision));

    // narrow: the log categories are a firehose (research doc §5.2)
    p.set_int(lt::settings_pack::alert_mask,
              static_cast<int>(static_cast<std::uint32_t>(lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage
                                                          | lt::alert_category::tracker | lt::alert_category::performance_warning
                                                          | lt::alert_category::dht | lt::alert_category::ip_block
                                                          // piece_finished_alert, for an ed3k join
                                                          | lt::alert_category::piece_progress)));
    return p;
}

int BtSettingsMap::rateBytes(quint32 kb)
{
    const qint64 bytes = qint64(kb) * 1024;
    return static_cast<int>(std::min<qint64>(bytes, std::numeric_limits<int>::max()));
}

std::string BtSettingsMap::listenInterfaces(const BtSettings& s)
{
    // blocked: no listen socket, so no DHT, tracker or peer traffic either
    if (s.networkBlocked)
        return {};
    const std::string port = std::to_string(s.listenPort);
    if (s.bindAddresses.isEmpty())
        return "0.0.0.0:" + port + ",[::]:" + port;
    std::string out;
    for (const QString& a : s.bindAddresses) {
        if (!out.empty())
            out += ',';
        const std::string ip = a.toStdString();
        out += a.contains(QLatin1Char(':')) ? "[" + ip + "]:" + port : ip + ":" + port;
    }
    return out;
}

std::string BtSettingsMap::outgoingInterfaces(const BtSettings& s)
{
    if (s.networkBlocked || s.bindAddresses.isEmpty())
        return {};
    return s.bindAddresses.join(QLatin1Char(',')).toStdString();
}

} // namespace eMule::bt
