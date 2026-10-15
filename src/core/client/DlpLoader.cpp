#include "client/DlpLoader.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/PathUtils.h"

#include <QDir>
#include <QFileInfo>

namespace eMule {

namespace {

const sdlp_char16* u16(const QString& s)
{
    return reinterpret_cast<const sdlp_char16*>(s.utf16());
}

} // namespace

DlpLoader::DlpLoader() = default;

DlpLoader::~DlpLoader()
{
    unload();
}

bool DlpLoader::load(const QString& path)
{
    if (m_loaded)
        return true;

    if (!path.isEmpty())
        return tryLoad(path);

    for (const QString& dir : searchDirs()) {
        const QString candidate = QDir(dir).filePath(libraryFileName());
        if (QFileInfo::exists(candidate))
            return tryLoad(candidate);
    }
    logWarning(QStringLiteral("Anti-Leecher: %1 not found, DLP checks are off").arg(libraryFileName()));
    return false;
}

bool DlpLoader::reload(const QString& path)
{
    unload();
    return load(path);
}

void DlpLoader::unload()
{
    if (!m_lib.isLoaded())
        return;
    m_lib.unload();
    m_loaded = false;
    m_variant = -1;
}

uint32 DlpLoader::rulesVersion() const
{
    return m_loaded ? m_rulesVersion() : 0;
}

QString DlpLoader::libVersion() const
{
    return m_loaded ? QString::fromLatin1(m_libVersion()) : QString();
}

std::optional<QString> DlpLoader::checkModstringHard(const QString& mod, const QString& client)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_modHard(u16(mod), u16(client)));
}

std::optional<QString> DlpLoader::checkModstringSoft(const QString& mod, const QString& client)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_modSoft(u16(mod), u16(client)));
}

std::optional<QString> DlpLoader::checkUsernameHard(const QString& name)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_userHard(u16(name)));
}

std::optional<QString> DlpLoader::checkUsernameSoft(const QString& name)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_userSoft(u16(name)));
}

std::optional<QString> DlpLoader::checkNameHashMod(const QString& name, const QString& hashHex, const QString& mod)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_nameHashMod(u16(name), u16(hashHex), u16(mod)));
}

std::optional<QString> DlpLoader::checkMessageSpam(const QString& message)
{
    if (!m_loaded)
        return std::nullopt;
    syncVariant();
    return toResult(m_spam(u16(message)));
}

std::optional<QString> DlpLoader::checkUserhash(const uint8* hash16)
{
    if (!m_loaded || hash16 == nullptr)
        return std::nullopt;
    syncVariant();
    return toResult(m_userhash(hash16));
}

std::optional<QString> DlpLoader::checkHelloTag(uint32 tag)
{
    if (!m_loaded)
        return std::nullopt;
    return toResult(m_helloTag(tag));
}

std::optional<QString> DlpLoader::checkInfoTag(uint32 tag)
{
    if (!m_loaded)
        return std::nullopt;
    return toResult(m_infoTag(tag));
}

QString DlpLoader::libraryFileName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("specialdlp.dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("libspecialdlp.dylib");
#else
    return QStringLiteral("libspecialdlp.so");
#endif
}

QStringList DlpLoader::searchDirs()
{
    const QString exe = executableDir();
    return {exe,
            QDir::cleanPath(exe + QStringLiteral("/../Frameworks")),   // macOS bundle
            QDir::cleanPath(exe + QStringLiteral("/lib")),             // Linux tarball
            QDir::cleanPath(exe + QStringLiteral("/../lib"))};
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool DlpLoader::tryLoad(const QString& path)
{
    m_lib.setFileName(path);
    if (!m_lib.load()) {
        logWarning(QStringLiteral("Anti-Leecher: failed to load %1: %2").arg(path, m_lib.errorString()));
        return false;
    }

    auto abiVersion = reinterpret_cast<AbiVersionFn>(m_lib.resolve("sdlp_abi_version"));
    if (abiVersion == nullptr || abiVersion() != SDLP_ABI_VERSION) {
        logWarning(QStringLiteral("Anti-Leecher: %1 has ABI %2, need %3 - please update")
                       .arg(path)
                       .arg(abiVersion ? abiVersion() : 0)
                       .arg(SDLP_ABI_VERSION));
        m_lib.unload();
        return false;
    }
    if (!resolveAll()) {
        logWarning(QStringLiteral("Anti-Leecher: %1 lacks required functions").arg(path));
        m_lib.unload();
        return false;
    }

    m_loaded = true;
    m_variant = -1;
    syncVariant();
    logInfo(QStringLiteral("Anti-Leecher: specialdlp %1 (DLP v%2) loaded").arg(libVersion()).arg(rulesVersion()));
    return true;
}

bool DlpLoader::resolveAll()
{
    const auto get = [this](const char* name) { return m_lib.resolve(name); };
    m_rulesVersion = reinterpret_cast<RulesVersionFn>(get("sdlp_rules_version"));
    m_libVersion = reinterpret_cast<LibVersionFn>(get("sdlp_lib_version"));
    m_setVariant = reinterpret_cast<SetVariantFn>(get("sdlp_set_variant"));
    m_modHard = reinterpret_cast<CheckModFn>(get("sdlp_check_modstring_hard"));
    m_modSoft = reinterpret_cast<CheckModFn>(get("sdlp_check_modstring_soft"));
    m_userHard = reinterpret_cast<CheckNameFn>(get("sdlp_check_username_hard"));
    m_userSoft = reinterpret_cast<CheckNameFn>(get("sdlp_check_username_soft"));
    m_nameHashMod = reinterpret_cast<CheckNameHashModFn>(get("sdlp_check_name_hash_mod"));
    m_spam = reinterpret_cast<CheckNameFn>(get("sdlp_check_message_spam"));
    m_userhash = reinterpret_cast<CheckHashFn>(get("sdlp_check_userhash"));
    m_helloTag = reinterpret_cast<CheckTagFn>(get("sdlp_check_hello_tag"));
    m_infoTag = reinterpret_cast<CheckTagFn>(get("sdlp_check_info_tag"));
    return m_rulesVersion && m_libVersion && m_setVariant && m_modHard && m_modSoft && m_userHard
        && m_userSoft && m_nameHashMod && m_spam && m_userhash && m_helloTag && m_infoTag;
}

void DlpLoader::syncVariant()
{
    const int wanted = thePrefs.antiLeecherVariant();
    if (wanted == m_variant)
        return;
    if (m_setVariant(static_cast<uint32_t>(wanted)) == 0)
        m_variant = wanted;
}

std::optional<QString> DlpLoader::toResult(const sdlp_char16* reason)
{
    if (reason == nullptr)
        return std::nullopt;
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(reason));
}

} // namespace eMule
