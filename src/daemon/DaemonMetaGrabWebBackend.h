#pragma once

/// @file DaemonMetaGrabWebBackend.h
/// @brief Grabs the web page's meta rows: MetaSearchService fetches, the engines add.
///
/// An NZB goes to the Usenet queue, a torrent to BitTorrent, a web link set to the
/// hoster downloads — the paths the GUI's grabs take, without its dialogs.

#include "webserver/MetaGrabWebBackend.h"

namespace eMule {

class DaemonMetaGrabWebBackend final : public MetaGrabWebBackend {
public:
    DaemonMetaGrabWebBackend() = default;

    void grab(uint32 searchID, const std::array<uint8, 16>& hash,
              std::function<void(bool ok, const QString& message)> done) override;
};

} // namespace eMule
