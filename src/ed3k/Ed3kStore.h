#pragma once

/// @file Ed3kStore.h
/// @brief Files of the ed3k node under Config/ed3k/: the key file and the state blobs.
///
/// The key file (node.key) is 32 random bytes, created once with owner-only
/// permissions and never overwritten. State blobs are written atomically.

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace eMule::ed3k {

using Seed = std::array<std::uint8_t, 32>;

/// Config/ed3k inside @p configDir.
[[nodiscard]] QString stateDir(const QString& configDir);

/// Reads node.key in @p dir, or creates it (32 bytes from the CSPRNG, 0600).
/// nullopt: unreadable, wrong size or not creatable; an existing file is never
/// replaced. Never logs the seed. The caller wipes the result (wipe()).
[[nodiscard]] std::optional<Seed> loadOrCreateKey(const QString& dir, QString* error = nullptr);

/// The same for any 32-byte secret file @p name in @p dir (credit.key ...): created from
/// the CSPRNG until @p accept takes the bytes (null: any), 0600, never replaced here.
[[nodiscard]] std::optional<Seed> loadOrCreateSecret(const QString& dir, const QString& name,
                                                     const std::function<bool(const Seed&)>& accept,
                                                     QString* error = nullptr);
/// Reads the 32-byte secret file @p name in @p dir; nullopt when missing or not 32 bytes.
[[nodiscard]] std::optional<Seed> loadSecret(const QString& dir, const QString& name);
/// Replaces the secret file @p name in @p dir with @p secret atomically, owner-only (import,
/// a new key). Never logs it.
bool replaceSecret(const QString& dir, const QString& name, const Seed& secret, QString* error = nullptr);

/// The bytes of @p path; empty when missing or unreadable.
[[nodiscard]] QByteArray readBlob(const QString& path);

/// Writes @p data to @p path atomically (QSaveFile), owner-only permissions.
bool writeBlob(const QString& path, std::span<const std::byte> data);

/// Overwrites @p buf with zeros in a way the compiler keeps.
void wipe(std::span<std::uint8_t> buf);
void wipe(QByteArray& buf);

} // namespace eMule::ed3k
