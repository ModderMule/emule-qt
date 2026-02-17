/// @file ImportParts.cpp
/// @brief Import completed parts from a source file into a PartFile.
///
/// Iterates over each part, checks the gap list for incomplete regions,
/// reads data from the source file, verifies MD4 hash, and writes matching
/// parts to the PartFile.

#include "transfer/ImportParts.h"
#include "crypto/FileIdentifier.h"
#include "crypto/MD4Hash.h"
#include "files/PartFile.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace eMule {

int importParts(PartFile* partFile, const QString& sourceFilePath,
                std::function<void(int percent)> progressCallback)
{
    if (!partFile)
        return 0;

    if (sourceFilePath.isEmpty())
        return 0;

    QFileInfo fi(sourceFilePath);
    if (!fi.exists() || !fi.isFile())
        return 0;

    // Verify source file size is compatible with the part file
    uint64 sourceSize = static_cast<uint64>(fi.size());
    if (sourceSize == 0)
        return 0;

    if (sourceSize != partFile->fileSize())
        return 0;

    if (progressCallback)
        progressCallback(0);

    QFile sourceFile(sourceFilePath);
    if (!sourceFile.open(QIODevice::ReadOnly))
        return 0;

    // Determine the part file's data path (fullName without .met suffix)
    QString partDataPath = partFile->fullName();
    if (partDataPath.endsWith(QStringLiteral(".met")))
        partDataPath.chop(4); // remove .met suffix to get .part data file

    const uint16 totalParts = partFile->partCount();
    if (totalParts == 0)
        return 0;

    int importedCount = 0;

    for (uint32 part = 0; part < totalParts; ++part) {
        // Skip already-complete parts
        if (partFile->isComplete(part))
            continue;

        // Get the expected hash for this part from the hashset
        const uint8* expectedHash = partFile->fileIdentifier().getMD4PartHash(part);
        // For single-part files the part hash may be the file hash itself
        if (!expectedHash && totalParts == 1)
            expectedHash = partFile->fileHash();
        if (!expectedHash)
            continue;

        // Calculate part boundaries
        uint64 partStart = static_cast<uint64>(part) * PARTSIZE;
        uint64 partEnd = std::min(partStart + PARTSIZE, sourceSize);
        uint64 partLen = partEnd - partStart;

        // Check that the entire part is a gap (incomplete)
        // We import parts only when the full part is missing to avoid partial overwrites
        if (!partFile->isPureGap(partStart, partEnd - 1))
            continue;

        // Read data from source file
        if (!sourceFile.seek(static_cast<qint64>(partStart)))
            continue;

        QByteArray data = sourceFile.read(static_cast<qint64>(partLen));
        if (static_cast<uint64>(data.size()) != partLen)
            continue;

        // Compute MD4 hash of the read data
        MD4Hasher hasher;
        hasher.add(reinterpret_cast<const uint8*>(data.constData()), data.size());
        hasher.finish();

        // Compare with expected hash
        if (!md4equ(hasher.getHash(), expectedHash))
            continue;

        // Hash matches — write the data to the part file
        QFile partDataFile(partDataPath);
        if (!partDataFile.open(QIODevice::ReadWrite))
            continue;

        if (!partDataFile.seek(static_cast<qint64>(partStart))) {
            partDataFile.close();
            continue;
        }

        qint64 written = partDataFile.write(data);
        partDataFile.close();

        if (static_cast<uint64>(written) != partLen)
            continue;

        // Fill the gap in the part file's gap list
        partFile->fillGap(partStart, partEnd - 1);
        ++importedCount;

        // Report progress
        if (progressCallback) {
            int percent = static_cast<int>((static_cast<uint64>(part + 1) * 100) / totalParts);
            progressCallback(percent);
        }
    }

    sourceFile.close();

    // Update completed info after importing
    if (importedCount > 0)
        partFile->updateCompletedInfos();

    if (progressCallback)
        progressCallback(100);

    return importedCount;
}

} // namespace eMule
