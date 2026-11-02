#pragma once

/// @file SecretCompare.h
/// @brief Constant-time secret compare and the stored web password format.
///
/// The GUI sends a web password as SHA-256 hex. What is stored is that hex run
/// through PBKDF2-HMAC-SHA256 with a random salt:
///   `pbkdf2$<iterations>$<saltHex>$<hashHex>`
/// A bare 64-hex value is the old format (plain SHA-256); it still verifies,
/// and Preferences wraps it on load.

#include <QByteArrayView>
#include <QString>

namespace eMule {

/// PBKDF2 rounds for a new web password hash.
inline constexpr int kWebPasswordIterations = 200000;

/// True when @p a and @p b are equal; time depends on length only.
[[nodiscard]] bool constantTimeEquals(QByteArrayView a, QByteArrayView b);

/// `pbkdf2$…` of @p sha256Hex with a fresh salt. Empty in, empty out.
[[nodiscard]] QString hashWebPassword(const QString& sha256Hex,
                                      int iterations = kWebPasswordIterations);

/// Does @p sha256Hex match @p stored (`pbkdf2$…` or legacy 64-hex)?
/// An empty @p stored matches nothing.
[[nodiscard]] bool verifyWebPassword(const QString& sha256Hex, const QString& stored);

/// A bare SHA-256 hex value, the format before PBKDF2.
[[nodiscard]] bool isLegacyWebPasswordHash(const QString& stored);

/// @p stored in the current format: a legacy value wrapped, anything else as is.
[[nodiscard]] QString upgradeWebPasswordHash(const QString& stored);

} // namespace eMule
