#pragma once

/// @file CoreInfo.h
/// @brief Status snapshots of the running core, shared by IPC and REST.

#include <QCborArray>
#include <QCborMap>

namespace eMule::ops {

/// `netBlocked`, `netBlockReason`, `boundInterface`: the bound-interface state.
void insertBindState(QCborMap& info);

/// `ed2kUsers`, `ed2kFiles`: users/files of the working servers (MFC GetUserFileStatus),
/// 0 while not on a server. The status bar and Network Information show these.
void insertEd2kCounts(QCborMap& info);

/// Sections `client`, `ed2k`, `kad` and `portmap` of the network information.
[[nodiscard]] QCborMap networkInfo();

/// Every contact of the Kad routing table; empty while Kad is stopped.
[[nodiscard]] QCborArray kadContacts();

[[nodiscard]] QCborMap kadStatus();

/// Recent requests other Kad nodes sent us, newest first: {id, agoMs, type (kad::IncomingType),
/// target (hex, "" = none), name, addr, cc, contacts, results, records, stored, load (-1 = n/a),
/// answered}. The ed3k node sends the same shape (Ed3kKadBridge).
[[nodiscard]] QCborArray kadIncoming();
/// Row count of kadIncoming() without building the rows.
[[nodiscard]] qint64 kadIncomingCount();

/// One incoming request for the incoming search graph: {found, the row of kadIncoming, wanted,
/// own: {id}, asker: {id ("" = unknown), addr, cc, version}, handed: [{id, addr, cc, version}]}.
/// Ids hex; distances are XOR to target. The ed3k node sends the same shape.
[[nodiscard]] QCborMap kadIncomingGraph(quint64 id);

/// The Statistics window's Kademlia branch: `session` and `cumulative` are
/// KadCounters blocks, `current` the routing table as it stands, `seen` the
/// distinct-node census (estimates) with its per-country split.
[[nodiscard]] QCborMap kadStats();

/// The Statistics window's Clients > Session / Cumulative: `seen` is the
/// distinct-client census by user hash (estimates) with its per-country split.
[[nodiscard]] QCborMap clientStats();

} // namespace eMule::ops
