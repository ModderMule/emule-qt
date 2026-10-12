#pragma once

/// @file LegacyIdentityImport.h
/// @brief One-time import of the MFC eMule user hash from preferences.dat.
///
/// MFC keeps the user hash in preferences.dat, this port in preferences.yml. A
/// preferences.dat dropped into the config dir is taken over once and deleted.
/// Nothing ties a hash to a key, so the check is structure + provenance: a well
/// formed eMule hash next to a working cryptkey.dat that MFC wrote (PKCS#8), not
/// one generated here (PKCS#1) — that pairing would lose the credits at every peer.

#include <QString>

namespace eMule {

class Preferences;

namespace LegacyIdentityImport {

inline constexpr auto kLegacyPrefsFileName = "preferences.dat";
inline constexpr auto kCryptKeyFileName = "cryptkey.dat";

enum class Result {
    NoFile,          ///< no preferences.dat, nothing to do
    Imported,        ///< hash taken over and saved, preferences.dat deleted
    AlreadyCurrent,  ///< hash already ours, preferences.dat deleted
    Rejected,        ///< validation or save failed; nothing changed, file kept
};

struct Outcome {
    Result result = Result::NoFile;
    QString error;   ///< set when Rejected
};

/// Run before anything reads the user hash. Logs the outcome.
[[nodiscard]] Outcome run(const QString& configDir, Preferences& prefs);

} // namespace LegacyIdentityImport
} // namespace eMule
