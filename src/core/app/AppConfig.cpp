#include "pch.h"
/// @file AppConfig.cpp
/// @brief Application config directory helpers — implementation.

#include "app/AppConfig.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <utility>

namespace eMule {

// ---------------------------------------------------------------------------
// Windows: multiUserSharing bootstrap
// ---------------------------------------------------------------------------

#ifdef Q_OS_WIN

namespace {
    int s_multiUserSharing = -1; // -1 = not yet determined
}

/// Read multiUserSharing from <exe-dir>/config/preferences.yml without
/// loading the full Preferences object.  Returns 2 (program-dir) if the
/// file does not exist or the key is absent.
static int peekMultiUserSharing()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString prefsPath = appDir + QStringLiteral("/config/preferences.yml");

    if (!QFile::exists(prefsPath))
        return 2; // default: program-dir (portable)

    try {
        const YAML::Node root = YAML::LoadFile(prefsPath.toStdString());
        if (auto t = root["transfer"])
            return t["multiUserSharing"].as<int>(2);
    } catch (...) {
        // Malformed YAML — fall back to default
    }
    return 2;
}

int AppConfig::multiUserSharingMode()
{
    if (s_multiUserSharing < 0)
        s_multiUserSharing = peekMultiUserSharing();
    return s_multiUserSharing;
}

#endif // Q_OS_WIN

namespace {
    QString s_configDirOverride;
}

void AppConfig::setConfigDirOverride(const QString& path)
{
    s_configDirOverride = path;
}

QString AppConfig::configDirOverride()
{
    return s_configDirOverride;
}

// ---------------------------------------------------------------------------
// configDir
// ---------------------------------------------------------------------------

QString AppConfig::configDir()
{
    if (!s_configDirOverride.isEmpty()) {
        QDir().mkpath(s_configDirOverride);
        return s_configDirOverride;
    }
#ifdef Q_OS_MACOS
    const QString dir = QDir::homePath() + QStringLiteral("/eMuleQt/Config");
#elif defined(Q_OS_WIN)
    QString dir;
    switch (multiUserSharingMode()) {
    case 0: // per-user
        dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        break;
    case 1: // all-users
        dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
              + QStringLiteral("/eMule/eMule Qt");
        break;
    default: // 2 = program-dir (portable)
        dir = QCoreApplication::applicationDirPath() + QStringLiteral("/config");
        break;
    }
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
    QDir().mkpath(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Bundled data seeding
// ---------------------------------------------------------------------------

namespace {

/// Live data the app itself rewrites. Seeded when missing, never refreshed --
/// a refresh would wipe the user's server list and Kad bootstrap contacts.
/// Everything else in the bundle is a program asset that tracks the build.
constexpr std::array kSeedOnce{"nodes.dat", "server.met", "webservices.dat"};

/// Records what the last pass wrote, so we can tell a stale file from an edited
/// one. Without it the two are indistinguishable and we would have to either
/// clobber edits or never update anything.
constexpr QLatin1StringView kManifestName{"bundled.yml"};
constexpr QLatin1StringView kLockName{"bundled.lock"};
constexpr int kManifestVersion = 1;

/// A packaging accident that ships a short config/ must not turn into deleting
/// working assets, so prune stands down when the bundle looks truncated rather
/// than deliberately trimmed. Same guard as generate_sprites.py's source count.
constexpr int kPruneMinBundleRatio = 2;   // bundle must hold > manifest/2 files

/// What we last wrote, keyed by path relative to the bundle root.
///
/// Content hash only. A size+mtime pair was tried as a "skip the re-read" hint
/// and removed: an edit of the same length within the same second is
/// indistinguishable from an untouched file, and that lie is destructive in both
/// directions -- it lets a refresh overwrite a hand-edit and lets the prune step
/// delete one. Hashing the whole bundle is ~126 KB and under 10 ms.
using ManifestHashes = QHash<QString, QByteArray>;

[[nodiscard]] bool isSeedOnce(const QString& relPath)
{
    return std::ranges::any_of(kSeedOnce, [&relPath](const char* name) {
        return relPath == QLatin1StringView(name);
    });
}

[[nodiscard]] QByteArray hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

[[nodiscard]] ManifestHashes readManifest(const QString& path)
{
    ManifestHashes out;
    if (!QFile::exists(path))
        return out;
    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root["files"] || !root["files"].IsMap())
            return out;
        for (const auto& it : root["files"]) {
            const auto sha = QByteArray::fromStdString(
                it.second["sha256"].as<std::string>(std::string{}));
            if (!sha.isEmpty())
                out.insert(QString::fromStdString(it.first.as<std::string>()), sha);
        }
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Bundled-data manifest unreadable (%1); treating every "
                                  "file as user-owned")
                       .arg(QString::fromStdString(ex.what())));
        out.clear();
    }
    return out;
}

bool writeManifest(const QString& path, const ManifestHashes& entries)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << kManifestVersion;
    out << YAML::Key << "files" << YAML::Value << YAML::BeginMap;
    // Sorted so the file is stable across runs and a diff means a real change.
    QStringList keys = entries.keys();
    keys.sort();
    for (const QString& k : std::as_const(keys)) {
        out << YAML::Key << k.toStdString() << YAML::Value << YAML::BeginMap
            << YAML::Key << "sha256" << YAML::Value << entries.value(k).toStdString()
            << YAML::EndMap;
    }
    out << YAML::EndMap << YAML::EndMap;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logWarning(QStringLiteral("Cannot write %1").arg(path));
        return false;
    }
    file.write(out.c_str());
    file.close();
    return true;
}

/// Copy via a temp file + rename, so a crash mid-copy cannot leave a truncated
/// template behind -- the destination either is the old file or the new one.
bool copyAtomic(const QString& src, const QString& dst)
{
    QDir().mkpath(QFileInfo(dst).path());
    const QString tmp = dst + QStringLiteral(".seed-tmp");
    QFile::remove(tmp);
    if (!QFile::copy(src, tmp))
        return false;
    QFile::remove(dst);
    if (QFile::rename(tmp, dst))
        return true;
    QFile::remove(tmp);
    return false;
}

} // namespace

QStringList AppConfig::bundleCandidates(const QString& appDir)
{
    QStringList candidates{
        appDir + QStringLiteral("/../Resources/config"),   // macOS .app bundle
        appDir + QStringLiteral("/config"),                // Windows zip and Linux tarball
    };
    // Local build straight from the source tree. Empty in packaged builds, and
    // always last so a real bundle wins.
    if (const QString devDir = QStringLiteral(EMULE_SOURCE_CONFIG_DIR); !devDir.isEmpty())
        candidates << devDir;
    return candidates;
}

QStringList AppConfig::langCandidates(const QString& appDir)
{
    QStringList candidates{
        appDir + QStringLiteral("/lang"),                // zip, tarball, bare local build
        appDir + QStringLiteral("/../Resources/lang"),   // macOS .app bundle
    };
    // The build tree's lrelease output -- flat in <build>/src/gui, which is not one
    // of the shipped layouts, so nothing above finds it. Empty in packaged builds,
    // and always last so a real bundle wins. Absolute on purpose: it is the only
    // entry that works for a dev .app, where the binary sits six levels below the
    // build dir, and for the multi-config generators, which add a per-config subdir.
    // Counting "../" hops cannot get all four layouts right, which is what the
    // EMULE_DEV_BUILD fallback this replaced tried to do.
    if (const QString devDir = QStringLiteral(EMULE_DEV_LANG_DIR); !devDir.isEmpty())
        candidates << devDir;
    return candidates;
}

QList<AppLanguage> AppConfig::availableLanguages(const QStringList& dirs)
{
    QSet<QString> codes;
    for (const QString& dir : dirs) {
        QDirIterator it(dir, {QStringLiteral("emuleqt_*.qm")}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            // "emuleqt_xx_YY.qm" -> "xx_YY"
            const QString code = it.fileName().mid(8).chopped(3);
            if (!code.isEmpty() && code != QLatin1String("en"))
                codes.insert(code);
        }
    }

    QList<AppLanguage> out;
    for (const QString& code : std::as_const(codes)) {
        const QLocale loc(code);
        QString label = loc.nativeLanguageName();
        if (!loc.nativeTerritoryName().isEmpty())
            label += QStringLiteral(" (") + loc.nativeTerritoryName() + u')';
        out.append({code, label});
    }
    std::ranges::sort(out, {}, &AppLanguage::label);
    return out;
}

AppConfig::SeedReport AppConfig::seedBundledData(const QString& configDir)
{
#ifdef Q_OS_WIN
    // In program-dir mode the config/ next to the exe IS the config
    // directory — nothing to seed.
    if (multiUserSharingMode() == 2)
        return {};
#endif

    for (const QString& c : bundleCandidates(QCoreApplication::applicationDirPath())) {
        if (QDir(c).exists())
            return seedFrom(c, configDir);
    }
    return {};
}

AppConfig::SeedReport AppConfig::seedFrom(const QString& bundleDir, const QString& configDir)
{
    SeedReport report;

    const QDir bundle(bundleDir);
    const QDir config(configDir);
    if (!bundle.exists())
        return report;

    // A layout where the bundle *is* the config dir (Windows portable) would
    // otherwise hash every file against itself and write a manifest into the
    // shipped tree.
    if (!config.canonicalPath().isEmpty() && bundle.canonicalPath() == config.canonicalPath())
        return report;

    // The daemon and the GUI both seed, moments apart. Whoever loses the race
    // finds everything current and does nothing.
    QLockFile lock(configDir + QLatin1Char('/') + kLockName);
    if (!lock.tryLock(5000)) {
        logWarning(QStringLiteral("Bundled data: another process holds %1, skipping")
                       .arg(kLockName));
        return report;
    }

    const QString manifestPath = configDir + QLatin1Char('/') + kManifestName;
    const ManifestHashes was = readManifest(manifestPath);
    ManifestHashes now;
    QSet<QString> inBundle;   ///< what this bundle actually ships, for the prune step

    QDirIterator it(bundleDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString relPath = bundle.relativeFilePath(it.filePath());

        // .DS_Store and friends are build-machine litter, not payload.
        if (QFileInfo(relPath).fileName().startsWith(QLatin1Char('.')))
            continue;

        inBundle.insert(relPath);
        const QString destPath = configDir + QLatin1Char('/') + relPath;
        const bool exists = QFile::exists(destPath);

        if (!exists) {
            if (copyAtomic(it.filePath(), destPath)) {
                ++report.seeded;
                now.insert(relPath, hashFile(destPath));
                logInfo(QStringLiteral("Seeded %1").arg(relPath));
            }
            continue;
        }

        // Live data the app owns: present, so leave the file alone. Its manifest
        // entry is carried forward untouched -- re-hashing a server.met on every
        // start would cost a full read to answer a question we never ask.
        if (isSeedOnce(relPath)) {
            if (const auto seeded = was.constFind(relPath); seeded != was.cend())
                now.insert(relPath, *seeded);
            continue;
        }

        const auto knownIt = was.constFind(relPath);
        const QByteArray known   = (knownIt != was.cend()) ? *knownIt : QByteArray();
        const QByteArray bundled = hashFile(it.filePath());
        const QByteArray live    = hashFile(destPath);
        if (bundled.isEmpty() || live.isEmpty())
            continue;

        if (bundled == live) {                       // already current
            now.insert(relPath, live);
            continue;
        }

        if (known.isEmpty()) {
            // Pre-manifest install: we have no record of what we wrote, so keep
            // a copy of theirs before adopting the bundle. Happens once; from
            // here on the manifest carries the provenance.
            const QString backup = destPath + QStringLiteral(".bak");
            QFile::remove(backup);
            QFile::copy(destPath, backup);
            if (copyAtomic(it.filePath(), destPath)) {
                ++report.refreshed;
                now.insert(relPath, bundled);
                logInfo(QStringLiteral("Refreshed %1 (previous copy kept as %2.bak)")
                            .arg(relPath, relPath));
            }
            continue;
        }

        if (live == known) {                         // ours to replace
            if (copyAtomic(it.filePath(), destPath)) {
                ++report.refreshed;
                now.insert(relPath, bundled);
                logInfo(QStringLiteral("Refreshed %1").arg(relPath));
            }
            continue;
        }

        // The user edited this file. Keep it. If the bundle also moved on, put
        // the new version beside it rather than silently withholding it.
        now.insert(relPath, known);
        if (bundled == known) {
            ++report.preserved;
        } else {
            ++report.conflicts;
            const QString sidecar = destPath + QStringLiteral(".new");
            if (copyAtomic(it.filePath(), sidecar))
                logWarning(QStringLiteral("%1 has local edits; the updated version is "
                                          "in %2.new").arg(relPath, relPath));
        }
    }

    // Prune what we once seeded and no longer ship -- but only when the bundle
    // looks whole. A truncated bundle is a packaging bug, not an instruction to
    // delete the user's working assets.
    if (!was.isEmpty() && inBundle.size() * kPruneMinBundleRatio < was.size()) {
        logWarning(QStringLiteral("Bundled data: only %1 of %2 known files found in %3; "
                                  "skipping prune")
                       .arg(inBundle.size()).arg(was.size()).arg(bundleDir));
        for (auto it2 = was.cbegin(); it2 != was.cend(); ++it2)
            if (!now.contains(it2.key()))
                now.insert(it2.key(), it2.value());
    } else {
        for (auto it2 = was.cbegin(); it2 != was.cend(); ++it2) {
            const QString& relPath = it2.key();
            if (inBundle.contains(relPath) || isSeedOnce(relPath))
                continue;
            const QString destPath = configDir + QLatin1Char('/') + relPath;
            if (!QFile::exists(destPath))
                continue;
            if (hashFile(destPath) != it2.value()) {
                now.insert(relPath, it2.value());    // edited orphan: keep it
                continue;
            }
            if (QFile::remove(destPath)) {
                ++report.pruned;
                logInfo(QStringLiteral("Removed %1 (no longer bundled)").arg(relPath));
            }
        }
    }

    if (now != was)
        writeManifest(manifestPath, now);

    return report;
}

} // namespace eMule
