#pragma once

/// @file DlpLoader.h
/// @brief Runtime loader for the specialdlp anti-leecher library (external/specialdlp).
///
/// Port of eMule Xtreme's CDLP (srchybrid/DLP.cpp): the rules live in a separate
/// shared library behind a C ABI so they can be updated without a new eMuleQt build.
/// Without the library every check reports "clean".
///
/// ToDo: independent auto-update of the library binary, see
/// docs/specialdlp-auto-update.local.md (download, verify signature, hot-swap via reload()).

#include "utils/Types.h"

#include <QLibrary>
#include <QString>
#include <QStringList>

#include <optional>

#include "specialdlp.h"

namespace eMule {

class DlpLoader {
public:
    DlpLoader();
    ~DlpLoader();

    DlpLoader(const DlpLoader&) = delete;
    DlpLoader& operator=(const DlpLoader&) = delete;

    /// Load @p path, or the first library found in searchDirs(). Logs the outcome.
    bool load(const QString& path = {});
    /// Unload and load again; reason strings from before are gone afterwards.
    bool reload(const QString& path = {});
    void unload();

    [[nodiscard]] bool isLoaded() const { return m_loaded; }
    /// Upstream DLPVERSION (e.g. 44006), 0 when not loaded.
    [[nodiscard]] uint32 rulesVersion() const;
    [[nodiscard]] QString libVersion() const;
    [[nodiscard]] QString libraryPath() const { return m_loaded ? m_lib.fileName() : QString(); }

    // Checks; nullopt = clean or not loaded. Variant follows thePrefs.antiLeecherVariant().
    [[nodiscard]] std::optional<QString> checkModstringHard(const QString& mod, const QString& client);
    [[nodiscard]] std::optional<QString> checkModstringSoft(const QString& mod, const QString& client);
    [[nodiscard]] std::optional<QString> checkUsernameHard(const QString& name);
    [[nodiscard]] std::optional<QString> checkUsernameSoft(const QString& name);
    [[nodiscard]] std::optional<QString> checkNameHashMod(const QString& name, const QString& hashHex,
                                                          const QString& mod);
    [[nodiscard]] std::optional<QString> checkMessageSpam(const QString& message);
    [[nodiscard]] std::optional<QString> checkUserhash(const uint8* hash16);
    [[nodiscard]] std::optional<QString> checkHelloTag(uint32 tag);
    [[nodiscard]] std::optional<QString> checkInfoTag(uint32 tag);

    /// Platform file name, e.g. libspecialdlp.dylib.
    [[nodiscard]] static QString libraryFileName();
    /// Where a bundled library sits: next to the binary, Contents/Frameworks, lib/.
    [[nodiscard]] static QStringList searchDirs();

private:
    using AbiVersionFn = uint32_t (*)();
    using RulesVersionFn = uint32_t (*)();
    using LibVersionFn = const char* (*)();
    using SetVariantFn = int32_t (*)(uint32_t);
    using CheckModFn = const sdlp_char16* (*)(const sdlp_char16*, const sdlp_char16*);
    using CheckNameFn = const sdlp_char16* (*)(const sdlp_char16*);
    using CheckNameHashModFn = const sdlp_char16* (*)(const sdlp_char16*, const sdlp_char16*, const sdlp_char16*);
    using CheckHashFn = const sdlp_char16* (*)(const uint8_t*);
    using CheckTagFn = const sdlp_char16* (*)(uint32_t);

    bool tryLoad(const QString& path);
    bool resolveAll();
    void syncVariant();
    static std::optional<QString> toResult(const sdlp_char16* reason);

    QLibrary m_lib;
    bool m_loaded = false;
    int m_variant = -1;

    RulesVersionFn m_rulesVersion = nullptr;
    LibVersionFn m_libVersion = nullptr;
    SetVariantFn m_setVariant = nullptr;
    CheckModFn m_modHard = nullptr;
    CheckModFn m_modSoft = nullptr;
    CheckNameFn m_userHard = nullptr;
    CheckNameFn m_userSoft = nullptr;
    CheckNameHashModFn m_nameHashMod = nullptr;
    CheckNameFn m_spam = nullptr;
    CheckHashFn m_userhash = nullptr;
    CheckTagFn m_helloTag = nullptr;
    CheckTagFn m_infoTag = nullptr;
};

} // namespace eMule
