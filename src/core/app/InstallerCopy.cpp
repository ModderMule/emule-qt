#include "pch.h"
/// @file InstallerCopy.cpp
/// @brief Copies the Windows setup the installer left behind into the Incoming dir.

#include "app/InstallerCopy.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace eMule::InstallerCopy {

namespace {

/// Name the installer gives its copy (emuleqt.iss StageSetupCopy)
constexpr QLatin1StringView kSetupPattern{"eMuleQt-v*-win64-setup.exe"};

[[nodiscard]] QString findStagedSetup(const QString& stagedDir);
[[nodiscard]] QString readState(const QString& statePath);
[[nodiscard]] bool writeState(const QString& statePath, const QString& fileName);
[[nodiscard]] bool copyInto(const QString& source, const QString& target);

} // namespace

QString stagedDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/setup");
}

Outcome run(const QString& stagedDir, const QString& configDir, const QString& incomingDir)
{
    Outcome out;
    const QString source = findStagedSetup(stagedDir);
    if (source.isEmpty() || configDir.isEmpty() || incomingDir.isEmpty())
        return out;

    out.fileName = QFileInfo(source).fileName();
    const QString statePath = QDir(configDir).filePath(QLatin1StringView(kStateFileName));
    if (readState(statePath) == out.fileName) {
        out.result = Result::AlreadyDone;
        return out;
    }

    const QString target = QDir(incomingDir).filePath(out.fileName);
    if (QFileInfo::exists(target)) {
        out.result = Result::AlreadyThere;
    } else if (!QDir().mkpath(incomingDir) || !copyInto(source, target)) {
        logWarning(QStringLiteral("Could not copy the installer %1 to %2")
                       .arg(out.fileName, QDir::toNativeSeparators(incomingDir)));
        out.result = Result::Failed;
        return out;
    } else {
        out.result = Result::Copied;
        logInfo(QStringLiteral("Copied the installer %1 to %2 for sharing")
                    .arg(out.fileName, QDir::toNativeSeparators(incomingDir)));
    }

    if (!writeState(statePath, out.fileName)) {
        logWarning(QStringLiteral("Could not write %1").arg(QDir::toNativeSeparators(statePath)));
        out.result = Result::Failed;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

namespace {

/// Newest match: setup cleans the folder, so there is normally only one
QString findStagedSetup(const QString& stagedDir)
{
    if (stagedDir.isEmpty())
        return {};
    const QFileInfoList found = QDir(stagedDir).entryInfoList(
        {QString(kSetupPattern)}, QDir::Files, QDir::Time);
    return found.isEmpty() ? QString() : found.first().absoluteFilePath();
}

QString readState(const QString& statePath)
{
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool writeState(const QString& statePath, const QString& fileName)
{
    QSaveFile file(statePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(fileName.toUtf8() + '\n');
    return file.commit();
}

/// Via a .part name: the shared-files scan must never hash half a setup
bool copyInto(const QString& source, const QString& target)
{
    const QString part = target + QStringLiteral(".part");
    QFile::remove(part);
    if (!QFile::copy(source, part))
        return false;
    if (QFile::rename(part, target))
        return true;
    QFile::remove(part);
    return false;
}

} // namespace

} // namespace eMule::InstallerCopy
