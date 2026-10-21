#pragma once

/// @file BtSettingsMap.h
/// @brief BtSettings → libtorrent settings_pack, in one place (research doc §5.3, §7.5).
/// Internal to src/bt.

#include "BtTypes.h"

#include <libtorrent/settings_pack.hpp>

namespace eMule::bt {

class BtSettingsMap {
public:
    /// Everything the session takes from the settings. UPnP and NAT-PMP stay off: the
    /// app's PortMapper maps the port.
    static libtorrent::settings_pack toPack(const BtSettings& s);

    /// KB/s of the preferences → bytes/s of libtorrent; 0 = unlimited both ways.
    static int rateBytes(quint32 kb);
};

} // namespace eMule::bt
