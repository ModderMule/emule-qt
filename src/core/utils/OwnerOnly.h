#pragma once

/// @file OwnerOnly.h
/// @brief Owner-only access for files that hold a secret (keys, tokens, credentials).
///
/// - Windows: a protected DACL with one ACE, all access for the current user, nothing
///   inherited from the folder (SetNamedSecurityInfoW). QFile::setPermissions only flips
///   the read-only attribute there.
/// - Elsewhere: mode 0600 when the file has any group/other bit.

#include <QByteArrayView>
#include <QString>

namespace eMule {

/// Makes @p path readable and writable by the current user alone. false (with @p error):
/// not done; the caller decides.
bool makeOwnerOnly(const QString& path, QString* error = nullptr);

/// True when only the current user may access @p path (Windows: the DACL itself, as
/// QFile::permissions() reports 7777 there even under QNtfsPermissionCheckGuard).
bool isOwnerOnly(const QString& path);

/// Writes @p data to @p path atomically (temp file + rename), owner-only from the temp
/// file's creation on: no moment where the new file has its folder's access.
bool writeOwnerOnly(const QString& path, QByteArrayView data, QString* error = nullptr);

} // namespace eMule
