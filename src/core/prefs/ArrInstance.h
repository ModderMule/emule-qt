#pragma once

/// @file ArrInstance.h
/// @brief One Sonarr or Radarr whose Wanted list feeds the synthetic Torznab RSS.
///
/// The Arr side calls us (qBittorrent API, Torznab); this is the other way round:
/// we poll its `wanted/missing` list, search eD2K for those titles in the
/// background and serve the hits as the no-query feed (ArrSearchBridge).

#include <QString>

namespace eMule {

struct ArrInstance {
    QString name;
    QString kind = QStringLiteral("sonarr");   ///< "sonarr" or "radarr"
    QString url;                                ///< base, e.g. http://localhost:8989
    QString apiKey;                             ///< plaintext in memory, apiKeyEnc in YAML
    bool enabled = true;

    [[nodiscard]] bool isRadarr() const { return kind == QLatin1StringView("radarr"); }
    bool operator==(const ArrInstance&) const = default;
};

} // namespace eMule
