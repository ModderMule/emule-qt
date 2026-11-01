#pragma once

/// @file Ed3kKeyFile.h
/// @brief Owner-only access for files that hold a secret (node.key, credit keys, paid holder
/// and seller keys, rooms' keys): one helper for all of them.
///
/// - Windows: a protected DACL with one ACE, all access for the current user, nothing
///   inherited from the folder (SetNamedSecurityInfoW, as packmule-node disk.rs `win_acl`).
/// - Elsewhere: mode 0600 when the file has any group/other bit (the files are made 0600
///   already; this mends one copied in).

#include <QString>

namespace eMule::ed3k {

/// Makes @p path readable and writable by the current user alone. false (with @p error):
/// not done; the caller decides (a new key file is removed then).
bool ownerOnly(const QString& path, QString* error = nullptr);

} // namespace eMule::ed3k
