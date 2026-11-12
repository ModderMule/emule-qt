#pragma once

/// @file Ed3kKeyFile.h
/// @brief Owner-only access for files that hold a secret (node.key, credit keys, paid holder
/// and seller keys, rooms' keys): one helper for all of them, core's makeOwnerOnly
/// (utils/OwnerOnly.h; Windows DACL as packmule-node disk.rs `win_acl`).

#include <QString>

namespace eMule::ed3k {

/// Makes @p path readable and writable by the current user alone. false (with @p error):
/// not done; the caller decides (a new key file is removed then).
bool ownerOnly(const QString& path, QString* error = nullptr);

} // namespace eMule::ed3k
