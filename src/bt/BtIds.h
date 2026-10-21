#pragma once

/// @file BtIds.h
/// @brief The namespaced id of a torrent (research doc §7.1): "bt:v1:<40 hex>" for a
/// version 1 or hybrid torrent (the swarm's identity), "bt:v2:<64 hex>" for version 2
/// only. Upper-case hex; this is the only place that formats or parses one.

#include <QByteArray>
#include <QString>

#include <optional>

namespace eMule::bt {

struct BtInfoHash {
    /// 20 bytes, or empty
    QByteArray v1;
    /// 32 bytes, or empty
    QByteArray v2;
};

class BtIds {
public:
    /// The id of a torrent; empty when it has neither hash.
    static QString idOf(const BtInfoHash& hash);

    /// Whether `id` is a torrent id (and not an ED2K MD4 or an NZB id).
    static bool isTorrentId(const QString& id);

    /// The hash an id names: one of v1 or v2. nullopt: not a torrent id.
    static std::optional<BtInfoHash> parse(const QString& id);
};

} // namespace eMule::bt
