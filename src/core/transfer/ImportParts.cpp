#include "pch.h"
/// @file ImportParts.cpp
/// @brief Import completed parts from a source file into a PartFile.
///
/// Iterates over each part, checks the gap list for incomplete regions,
/// reads data from the source file, verifies MD4 hash, and writes matching
/// parts to the PartFile.

#include "transfer/ImportParts.h"
#include "crypto/MD4Hash.h"
#include "files/PartFile.h"

#include <QFile>
#include <QFileInfo>


namespace eMule {

bool canImportParts(const PartFile* partFile, const QString& sourceFilePath)
{
    if (!partFile || sourceFilePath.isEmpty() || partFile->partCount() == 0)
        return false;
    const QFileInfo fi(sourceFilePath);
    // Parts are matched by offset, so the source has to be the same size
    return fi.exists() && fi.isFile() && fi.size() > 0
        && static_cast<uint64>(fi.size()) == partFile->fileSize();
}

bool importPart(PartFile* partFile, const QString& sourceFilePath, uint32 part)
{
    if (!partFile || part >= partFile->partCount() || partFile->isComplete(part))
        return false;

    // The expected hash; a single-part file is hashed as a whole
    const uint8* expectedHash = partFile->fileIdentifier().getMD4PartHash(part);
    if (!expectedHash && partFile->partCount() == 1)
        expectedHash = partFile->fileHash();
    if (!expectedHash)
        return false;

    const uint64 partStart = static_cast<uint64>(part) * PARTSIZE;
    const uint64 partEnd = std::min<uint64>(partStart + PARTSIZE, partFile->fileSize());
    const uint64 partLen = partEnd - partStart;

    // Only a part that is missing entirely: nothing downloaded is overwritten
    if (!partFile->isPureGap(partStart, partEnd - 1))
        return false;

    QFile sourceFile(sourceFilePath);
    if (!sourceFile.open(QIODevice::ReadOnly) || !sourceFile.seek(static_cast<qint64>(partStart)))
        return false;
    const QByteArray data = sourceFile.read(static_cast<qint64>(partLen));
    if (static_cast<uint64>(data.size()) != partLen)
        return false;

    MD4Hasher hasher;
    hasher.add(reinterpret_cast<const uint8*>(data.constData()), static_cast<size_t>(data.size()));
    hasher.finish();
    if (!md4equ(hasher.getHash(), expectedHash))
        return false;

    // The .part data file is fullName without its .met suffix
    QString partDataPath = partFile->fullName();
    if (partDataPath.endsWith(QStringLiteral(".met")))
        partDataPath.chop(4);
    QFile partDataFile(partDataPath);
    if (!partDataFile.open(QIODevice::ReadWrite) || !partDataFile.seek(static_cast<qint64>(partStart)))
        return false;
    if (static_cast<uint64>(partDataFile.write(data)) != partLen)
        return false;
    partDataFile.close();

    partFile->fillGap(partStart, partEnd - 1);
    return true;
}

int importParts(PartFile* partFile, const QString& sourceFilePath,
                std::function<void(int percent)> progressCallback)
{
    if (!canImportParts(partFile, sourceFilePath))
        return 0;

    if (progressCallback)
        progressCallback(0);

    const uint16 totalParts = partFile->partCount();
    int importedCount = 0;
    for (uint32 part = 0; part < totalParts; ++part) {
        if (!importPart(partFile, sourceFilePath, part))
            continue;
        ++importedCount;
        if (progressCallback)
            progressCallback(static_cast<int>((static_cast<uint64>(part + 1) * 100) / totalParts));
    }

    // Update completed info after importing
    if (importedCount > 0)
        partFile->updateCompletedInfos();

    if (progressCallback)
        progressCallback(100);

    return importedCount;
}

} // namespace eMule
