#pragma once

/// @file CoreInfo.h
/// @brief Status snapshots of the running core, shared by IPC and REST.

#include <QCborArray>
#include <QCborMap>

namespace eMule::ops {

/// `netBlocked`, `netBlockReason`, `boundInterface`: the bound-interface state.
void insertBindState(QCborMap& info);

/// Sections `client`, `ed2k`, `kad` and `portmap` of the network information.
[[nodiscard]] QCborMap networkInfo();

/// Every contact of the Kad routing table; empty while Kad is stopped.
[[nodiscard]] QCborArray kadContacts();

[[nodiscard]] QCborMap kadStatus();

} // namespace eMule::ops
