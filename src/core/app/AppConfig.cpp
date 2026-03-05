/// @file AppConfig.cpp
/// @brief Application config directory helpers — implementation.

#include "app/AppConfig.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <yaml-cpp/yaml.h>
#endif

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

// ---------------------------------------------------------------------------
// configDir
// ---------------------------------------------------------------------------

QString AppConfig::configDir()
{
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
// seedBundledData
// ---------------------------------------------------------------------------

void AppConfig::seedBundledData(const QString& configDir)
{
    const QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // In program-dir mode the config/ next to the exe IS the config
    // directory — nothing to seed.
    if (multiUserSharingMode() == 2)
        return;
#endif

    // Candidate locations for the bundled config directory
    const QStringList candidates = {
        appDir + QStringLiteral("/../Resources/config"),   // macOS app bundle
#ifdef Q_OS_WIN
        appDir + QStringLiteral("/config"),                // Windows bundle (non-portable)
#endif
#ifdef EMULE_DEV_BUILD
        appDir + QStringLiteral("/../../../data/config"),  // dev build tree
#endif
    };

    QString bundleDir;
    for (const auto& c : candidates) {
        if (QDir(c).exists()) {
            bundleDir = c;
            break;
        }
    }
    if (bundleDir.isEmpty())
        return;

    // Recursively copy files that don't already exist in configDir
    QDirIterator it(bundleDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString relPath = QDir(bundleDir).relativeFilePath(it.filePath());
        const QString destPath = configDir + QLatin1Char('/') + relPath;
        if (QFile::exists(destPath))
            continue;
        QDir().mkpath(QFileInfo(destPath).path());
        if (QFile::copy(it.filePath(), destPath))
            logInfo(QStringLiteral("Seeded %1").arg(relPath));
    }
}

} // namespace eMule
