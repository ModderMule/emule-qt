/// @file VersionedRecordFile.cpp
/// @brief Version byte + body store files — implementation.

#include "pch.h"

#include "utils/VersionedRecordFile.h"

#include "app/AppContext.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>
#include <QFileInfo>

namespace eMule {

bool VersionedRecordFile::readVersioned(const QString& path, uint8 version,
                                        const std::function<void(SafeFile&)>& readBody)
{
    if (!QFile::exists(path))
        return false;
    try {
        SafeFile file;
        if (!file.open(path, QIODevice::ReadOnly))
            return false;
        if (file.readUInt8() != version)
            return false;
        readBody(file);
    } catch (const std::exception& e) {
        logWarning(QStringLiteral("%1: read failed (%2) — ignoring the file")
                       .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
    return true;
}

bool VersionedRecordFile::writeVersioned(const QString& path, uint8 version,
                                         const std::function<void(SafeFile&)>& writeBody)
{
    const QString tmpPath = path + QStringLiteral(".tmp");
    try {
        QFile::remove(tmpPath);

        SafeFile file;
        if (!file.open(tmpPath, QIODevice::WriteOnly)) {
            logWarning(QStringLiteral("%1: cannot write %2").arg(QFileInfo(path).fileName(), tmpPath));
            return false;
        }
        file.writeUInt8(version);
        writeBody(file);
        commitAndReplace(file, tmpPath, path, theApp.commitFilesNow());
    } catch (const std::exception& e) {
        logError(QStringLiteral("%1: save failed (%2)")
                     .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace eMule
