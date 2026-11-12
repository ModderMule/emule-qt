#pragma once

/// @file OwnerOnly.h
/// @brief Owner-only access for files that hold a secret (keys, tokens, credentials).
///
/// - Windows: a protected DACL with one ACE, all access for the current user, nothing
///   inherited from the folder (SetNamedSecurityInfoW). QFile::setPermissions only flips
///   the read-only attribute there.
/// - Elsewhere: mode 0600 when the file has any group/other bit.

#include <QString>

namespace eMule {

/// Makes @p path readable and writable by the current user alone. false (with @p error):
/// not done; the caller decides.
bool makeOwnerOnly(const QString& path, QString* error = nullptr);

} // namespace eMule
