#include "session/BtSettingsMap.h"

#include <libtorrent/alert.hpp>
#include <libtorrent/config.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <limits>

// libtorrent's `i2p` build option; off = the I2P settings below do nothing
static_assert(TORRENT_USE_I2P, "libtorrent built without I2P");

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
    // most 16 KiB block requests in flight to one peer. libtorrent sizes the queue to
    // rate * request_queue_time (3 s) and caps it here: the default 500 caps a peer at
    // ~2.7 MB/s and posts outstanding_request_limit_reached each second. 1500 = ~8 MB/s
    p.set_int(lt::settings_pack::max_out_request_queue, 1500);
    p.set_int(lt::settings_pack::active_downloads, std::max(1, s.maxActiveDownloads));
    p.set_int(lt::settings_pack::active_seeds, std::max(0, s.maxActiveSeeds));
    p.set_int(lt::settings_pack::active_limit, std::max(1, s.maxActiveDownloads) + std::max(0, s.maxActiveSeeds));
    p.set_int(lt::settings_pack::connections_limit, std::max(2, s.maxConnections));
    // off = libtorrent's default: the first working tracker per tier, the rest stay
    // "not contacted". On = every tracker, as qBittorrent, BiglyBT and Tixati (#144)
    p.set_bool(lt::settings_pack::announce_to_all_trackers, s.announceToAll);
    p.set_bool(lt::settings_pack::announce_to_all_tiers, s.announceToAll);

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

    // I2P. No SAM yet (router starting or failed, or I2P off): a bridge nothing answers on
    // loopback port 0, never an empty hostname. Empty, libtorrent fetches .i2p trackers as
    // plain HTTP: a DNS lookup of the .i2p name, and the announce in clear if anything
    // resolves it. With a bridge set they wait for it; libtorrent retries it with backoff.
    // Mixed only while I2P is on: with it off an I2P torrent waits rather than going clearnet
    const bool sam = s.hasSam();
    p.set_str(lt::settings_pack::i2p_hostname, sam ? s.i2pSamHost.toStdString() : std::string("127.0.0.1"));
    p.set_int(lt::settings_pack::i2p_port, sam ? s.i2pSamPort : 0);
    p.set_bool(lt::settings_pack::allow_i2p_mixed, s.i2pEnabled && s.i2pMixed);
    const int length = std::clamp(s.i2pTunnelLength, 1, 7);
    const int quantity = std::clamp(s.i2pTunnelQuantity, 1, 16);
    p.set_int(lt::settings_pack::i2p_inbound_length, length);
    p.set_int(lt::settings_pack::i2p_outbound_length, length);
    p.set_int(lt::settings_pack::i2p_inbound_quantity, quantity);
    p.set_int(lt::settings_pack::i2p_outbound_quantity, quantity);

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
