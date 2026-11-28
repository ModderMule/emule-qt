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
#include "transfer/DownloadQueue.h"
#include "app/AppContext.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QTimer>


namespace eMule {

namespace {

/// Background imports: download hash → source path and next part.
struct PartImport {
    QString path;
    uint32 nextPart = 0;
    int imported = 0;
};

QHash<QString, PartImport>& partImports()
{
    static QHash<QString, PartImport> imports;
    return imports;
}

PartFile* partFileOf(const QString& hashHex)
{
    uint8 hash[16]{};
    if (!theApp.downloadQueue || decodeBase16(hashHex, hash, 16) != 16)
        return nullptr;
    PartFile* file = theApp.downloadQueue->fileByID(hash);
    return file && file->status() != PartFileStatus::Complete ? file : nullptr;
}

void stepPartImport(const QString& hashHex);

} // namespace

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

    // Any part not yet complete, a partly downloaded one too (MFC ImportParts.cpp:62):
    // a source part that passes MD4 is the right data, so overwriting what is there is safe

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

    // Through the write buffer, as MFC OnImportPart: flush, part verification and
    // file completion all run as for downloaded data
    return partFile->writeToBuffer(partLen, reinterpret_cast<const uint8*>(data.constData()),
                                   partStart, partEnd - 1, nullptr, Address{}) > 0;
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

bool startPartImport(PartFile* partFile, const QString& sourceFilePath)
{
    if (!partFile || !canImportParts(partFile, sourceFilePath))
        return false;
    const QString key = md4str(partFile->fileHash());
    if (partImports().contains(key))
        return false;
    partFile->flushBuffer();
    partFile->setFileOp(PartFileOp::ImportParts);
    partImports().insert(key, PartImport{sourceFilePath, 0, 0});
    QTimer::singleShot(0, qApp, [key] { stepPartImport(key); });
    return true;
}

bool stopPartImport(PartFile* partFile)
{
    if (!partFile || partImports().remove(md4str(partFile->fileHash())) == 0)
        return false;
    partFile->setFileOp(PartFileOp::None);
    partFile->updateCompletedInfos();
    partFile->savePartFile();
    return true;
}

bool partImportRunning(const PartFile* partFile)
{
    return partFile && partImports().contains(md4str(partFile->fileHash()));
}

namespace {

/// One part per turn of the event loop: a part is 9.28 MB to read, hash and write.
void stepPartImport(const QString& hashHex)
{
    auto it = partImports().find(hashHex);
    if (it == partImports().end())
        return;   // stopped

    PartFile* file = partFileOf(hashHex);
    if (!file || it->nextPart >= file->partCount()) {
        if (file) {
            file->setFileOp(PartFileOp::None);
            if (it->imported > 0) {
                file->flushBuffer();
                file->updateCompletedInfos();
                file->savePartFile();
            }
            logInfo(QStringLiteral("Imported %1 part(s) into %2").arg(it->imported).arg(file->fileName()));
        }
        partImports().erase(it);
        return;
    }

    if (importPart(file, it->path, it->nextPart))
        ++it->imported;
    ++it->nextPart;
    QTimer::singleShot(0, qApp, [hashHex] { stepPartImport(hashHex); });
}

} // namespace

} // namespace eMule
