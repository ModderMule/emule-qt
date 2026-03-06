/// @file ArchiveRecovery.cpp
/// @brief Archive recovery from partial downloads — port of MFC ArchiveRecovery.
///
/// Custom binary scanning of partial downloads to extract valid ZIP/RAR entries.

#include "archive/ArchiveRecovery.h"
#include "files/PartFile.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QThread>

#include <algorithm>
#include <cstring>

#include <zlib.h>

namespace eMule {

// ZIP format constants
static constexpr uint32 kZipLocalFileHeader  = 0x04034b50;
static constexpr uint32 kZipCentralDirHeader = 0x02014b50;
static constexpr uint32 kZipEndOfCentralDir  = 0x06054b50;

// RAR format constants
static constexpr uint8 kRarFileHeaderType = 0x74;
static constexpr uint32 kRarSignature = 0x21726152; // "Rar!" LE

// ISO 9660 magic: "CD001" at offset 0x8001 (sector 16, byte 1)
static constexpr uint64 kIsoMagicOffset = 0x8001;
static constexpr char kIsoMagic[] = "CD001";

// ACE magic: "**ACE**" at offset 7
static constexpr uint64 kAceMagicOffset = 7;
static constexpr char kAceMagic[] = "**ACE**";

// ---------------------------------------------------------------------------
// isFilled — check if [start, end] is fully within filled regions
// ---------------------------------------------------------------------------

bool ArchiveRecovery::isFilled(uint64 start, uint64 end,
                               const std::vector<Gap>& filled)
{
    for (const auto& g : filled) {
        if (g.start <= start && g.end >= end)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// recover — main entry point
// ---------------------------------------------------------------------------

bool ArchiveRecovery::recover(PartFile* partFile, bool /*preview*/,
                              bool createCopy)
{
    if (!partFile)
        return false;

    // Get filled regions (complement of gap list)
    std::vector<Gap> filled;
    partFile->getFilledArray(filled);

    if (filled.empty())
        return false;

    const uint64 fileSize = static_cast<uint64>(partFile->fileSize());
    const QString srcPath = partFile->filePath();

    QFile input(srcPath);
    if (!input.open(QIODevice::ReadOnly))
        return false;

    // Determine archive type by scanning first filled region
    if (filled[0].start == 0 && filled[0].end >= 3) {
        char header[4]{};
        input.read(header, 4);
        input.seek(0);

        uint32 sig = 0;
        std::memcpy(&sig, header, 4);

        // Check for ZIP
        if (sig == kZipLocalFileHeader) {
            QString outPath;
            if (createCopy) {
                outPath = srcPath + QStringLiteral(".recovered.zip");
            } else {
                outPath = srcPath;
            }
            QFile output(outPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return recoverZip(input, output, filled, fileSize);
        }

        // Check for RAR
        if (sig == kRarSignature) {
            QString outPath;
            if (createCopy) {
                outPath = srcPath + QStringLiteral(".recovered.rar");
            } else {
                outPath = srcPath;
            }
            QFile output(outPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return recoverRar(input, output, filled);
        }
    }

    // Check for ISO 9660: magic "CD001" at offset 0x8001
    if (fileSize > kIsoMagicOffset + 5 && isFilled(kIsoMagicOffset, kIsoMagicOffset + 4, filled)) {
        input.seek(static_cast<qint64>(kIsoMagicOffset));
        char isoBuf[5]{};
        if (input.read(isoBuf, 5) == 5 && std::memcmp(isoBuf, kIsoMagic, 5) == 0) {
            QString outPath = createCopy ? srcPath + QStringLiteral(".recovered.iso") : srcPath;
            QFile output(outPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return recoverISO(input, output, filled, fileSize);
        }
    }

    // Check for ACE: magic "**ACE**" at offset 7
    if (fileSize > kAceMagicOffset + 7 && isFilled(kAceMagicOffset, kAceMagicOffset + 6, filled)) {
        input.seek(static_cast<qint64>(kAceMagicOffset));
        char aceBuf[7]{};
        if (input.read(aceBuf, 7) == 7 && std::memcmp(aceBuf, kAceMagic, 7) == 0) {
            QString outPath = createCopy ? srcPath + QStringLiteral(".recovered.ace") : srcPath;
            QFile output(outPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return recoverACE(input, output, filled);
        }
    }

    logWarning(QStringLiteral("ArchiveRecovery: unsupported archive format"));
    return false;
}

// ---------------------------------------------------------------------------
// recoverZip — scan for valid ZIP local file headers and rebuild archive
// ---------------------------------------------------------------------------

bool ArchiveRecovery::recoverZip(QFile& input, QFile& output,
                                 const std::vector<Gap>& filled,
                                 uint64 fileSize)
{
    std::vector<uint64> centralDirEntries;
    uint64 pos = 0;
    int entriesRecovered = 0;

    while (pos < fileSize) {
        // Seek to pos
        if (!input.seek(static_cast<qint64>(pos)))
            break;

        // Check if we have enough data for a local file header (30 bytes minimum)
        if (!isFilled(pos, pos + 29, filled)) {
            ++pos;
            continue;
        }

        // Read local file header signature
        char sigBuf[4]{};
        if (input.read(sigBuf, 4) != 4)
            break;

        uint32 sig = 0;
        std::memcpy(&sig, sigBuf, 4);

        if (sig == kZipLocalFileHeader) {
            // Read rest of local file header
            char headerBuf[26]{};
            if (input.read(headerBuf, 26) != 26) {
                ++pos;
                continue;
            }

            uint16 fileNameLen = 0;
            std::memcpy(&fileNameLen, headerBuf + 22, 2);

            uint16 extraFieldLen = 0;
            std::memcpy(&extraFieldLen, headerBuf + 24, 2);

            uint32 compressedSize = 0;
            std::memcpy(&compressedSize, headerBuf + 14, 4);

            uint64 entryTotalSize = 30 + fileNameLen + extraFieldLen + compressedSize;

            // Check if entire entry is available
            if (isFilled(pos, pos + entryTotalSize - 1, filled)) {
                // Write this entry to output
                centralDirEntries.push_back(pos);

                // Copy the entry data
                input.seek(static_cast<qint64>(pos));
                QByteArray entryData = input.read(static_cast<qint64>(entryTotalSize));
                output.write(entryData);

                ++entriesRecovered;
                pos += entryTotalSize;
            } else {
                pos += 4; // Skip past signature
            }
        } else if (sig == kZipCentralDirHeader || sig == kZipEndOfCentralDir) {
            // Reached central directory — stop scanning local headers
            break;
        } else {
            ++pos;
        }
    }

    if (entriesRecovered == 0)
        return false;

    // Write central directory
    writeZipCentralDirectory(output, centralDirEntries, input, filled);

    logInfo(QStringLiteral("ZIP recovery: recovered %1 entries").arg(entriesRecovered));
    return true;
}

// ---------------------------------------------------------------------------
// writeZipCentralDirectory
// ---------------------------------------------------------------------------

void ArchiveRecovery::writeZipCentralDirectory(
    QFile& output, const std::vector<uint64>& centralDirEntries,
    QFile& input, const std::vector<Gap>& /*filled*/)
{
    uint64 centralDirStart = static_cast<uint64>(output.pos());
    uint32 centralDirSize = 0;

    for (uint64 localOffset : centralDirEntries) {
        // Read the local file header to build central directory entry
        input.seek(static_cast<qint64>(localOffset));
        char localHeader[30]{};
        if (input.read(localHeader, 30) != 30)
            continue;

        // Extract fields from local header
        uint16 versionNeeded = 0, flags = 0, method = 0;
        uint16 modTime = 0, modDate = 0;
        uint32 crc32Val = 0, compSize = 0, uncompSize = 0;
        uint16 fnLen = 0, extraLen = 0;

        std::memcpy(&versionNeeded, localHeader + 4, 2);
        std::memcpy(&flags, localHeader + 6, 2);
        std::memcpy(&method, localHeader + 8, 2);
        std::memcpy(&modTime, localHeader + 10, 2);
        std::memcpy(&modDate, localHeader + 12, 2);
        std::memcpy(&crc32Val, localHeader + 14, 4);
        std::memcpy(&compSize, localHeader + 18, 4);
        std::memcpy(&uncompSize, localHeader + 22, 4);
        std::memcpy(&fnLen, localHeader + 26, 2);
        std::memcpy(&extraLen, localHeader + 28, 2);

        QByteArray fileName = input.read(fnLen);

        // Build central directory file header (46 + fnLen bytes)
        char centralEntry[46]{};
        uint32 centralSig = kZipCentralDirHeader;
        std::memcpy(centralEntry, &centralSig, 4);
        uint16 versionMadeBy = 20;
        std::memcpy(centralEntry + 4, &versionMadeBy, 2);
        std::memcpy(centralEntry + 6, &versionNeeded, 2);
        std::memcpy(centralEntry + 8, &flags, 2);
        std::memcpy(centralEntry + 10, &method, 2);
        std::memcpy(centralEntry + 12, &modTime, 2);
        std::memcpy(centralEntry + 14, &modDate, 2);
        std::memcpy(centralEntry + 16, &crc32Val, 4);
        std::memcpy(centralEntry + 20, &compSize, 4);
        std::memcpy(centralEntry + 24, &uncompSize, 4);
        std::memcpy(centralEntry + 28, &fnLen, 2);
        // extra field len, comment len, disk start, internal/external attrs = 0
        uint32 relOffset = static_cast<uint32>(localOffset);
        std::memcpy(centralEntry + 42, &relOffset, 4);

        output.write(centralEntry, 46);
        output.write(fileName);
        centralDirSize += 46 + fnLen;
    }

    // Write End of Central Directory record
    char eocd[22]{};
    uint32 eocdSig = kZipEndOfCentralDir;
    std::memcpy(eocd, &eocdSig, 4);
    uint16 entryCount = static_cast<uint16>(centralDirEntries.size());
    std::memcpy(eocd + 8, &entryCount, 2);   // entries on this disk
    std::memcpy(eocd + 10, &entryCount, 2);  // total entries
    std::memcpy(eocd + 12, &centralDirSize, 4);
    uint32 cdOffset = static_cast<uint32>(centralDirStart);
    std::memcpy(eocd + 16, &cdOffset, 4);
    output.write(eocd, 22);
}

// ---------------------------------------------------------------------------
// scanForZipMarker
// ---------------------------------------------------------------------------

bool ArchiveRecovery::scanForZipMarker(QFile& input, uint32 marker,
                                       uint64 searchRange)
{
    uint64 startPos = static_cast<uint64>(input.pos());
    uint64 endPos = std::min(startPos + searchRange,
                             static_cast<uint64>(input.size()));

    char buf[4096];
    uint64 pos = startPos;

    while (pos < endPos) {
        qint64 toRead = static_cast<qint64>(std::min(endPos - pos, uint64{4096}));
        input.seek(static_cast<qint64>(pos));
        qint64 got = input.read(buf, toRead);
        if (got <= 0)
            break;

        for (qint64 i = 0; i <= got - 4; ++i) {
            uint32 val = 0;
            std::memcpy(&val, buf + i, 4);
            if (val == marker) {
                input.seek(static_cast<qint64>(pos + static_cast<uint64>(i)));
                return true;
            }
        }
        pos += static_cast<uint64>(got) - 3; // overlap by 3 bytes
    }

    return false;
}

// ---------------------------------------------------------------------------
// recoverRar — scan for valid RAR file header blocks
// ---------------------------------------------------------------------------

bool ArchiveRecovery::recoverRar(QFile& input, QFile& output,
                                 const std::vector<Gap>& filled)
{
    int blocksRecovered = 0;

    // Write RAR signature + archive header first (7 + 13 bytes)
    // RAR5 archive header is simpler; we write the classic RAR4 signature
    static constexpr uint8 rarSignature[] = {
        0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00
    };
    output.write(reinterpret_cast<const char*>(rarSignature), 7);

    // Scan for file header blocks (type 0x74)
    uint64 pos = 7; // Skip signature
    const uint64 fileSize = static_cast<uint64>(input.size());

    while (pos < fileSize) {
        if (!isFilled(pos, pos + 6, filled)) {
            ++pos;
            continue;
        }

        input.seek(static_cast<qint64>(pos));

        // Read RAR4 block header (7 bytes minimum)
        char blockHeader[7]{};
        if (input.read(blockHeader, 7) != 7)
            break;

        uint8 blockType = static_cast<uint8>(blockHeader[2]);

        uint16 blockSize = 0;
        std::memcpy(&blockSize, blockHeader + 5, 2);

        if (blockType == kRarFileHeaderType && blockSize >= 7) {
            // File header block — check if entire block is available
            uint64 totalBlockSize = blockSize;

            // Check for ADD_SIZE flag (bit 15 of flags)
            uint16 flags = 0;
            std::memcpy(&flags, blockHeader + 3, 2);

            if (flags & 0x8000) {
                // Has additional data size
                if (isFilled(pos + 7, pos + 10, filled)) {
                    char addSizeBuf[4]{};
                    input.seek(static_cast<qint64>(pos + 7));
                    input.read(addSizeBuf, 4);
                    uint32 addSize = 0;
                    std::memcpy(&addSize, addSizeBuf, 4);
                    totalBlockSize += addSize;
                }
            }

            if (isFilled(pos, pos + totalBlockSize - 1, filled)) {
                input.seek(static_cast<qint64>(pos));
                QByteArray blockData = input.read(static_cast<qint64>(totalBlockSize));
                output.write(blockData);
                ++blocksRecovered;
                pos += totalBlockSize;
                continue;
            }
        }

        // Skip to next position
        if (blockSize > 0)
            pos += blockSize;
        else
            ++pos;
    }

    if (blocksRecovered == 0)
        return false;

    logInfo(QStringLiteral("RAR recovery: recovered %1 blocks").arg(blocksRecovered));
    return true;
}

// ---------------------------------------------------------------------------
// recoverISO — stub for ISO 9660 recovery
// ---------------------------------------------------------------------------

bool ArchiveRecovery::recoverISO(QFile& input, QFile& output,
                                  const std::vector<Gap>& filled,
                                  uint64 fileSize)
{
    // ISO 9660 uses 2048-byte sectors
    static constexpr uint64 kIsoSectorSize = 2048;

    // Read Primary Volume Descriptor (sector 16) for total volume size
    static constexpr uint64 kPvdSector = 16;
    static constexpr uint64 kPvdOffset = kPvdSector * kIsoSectorSize;

    uint64 totalIsoSize = fileSize; // fallback to file size

    if (isFilled(kPvdOffset, kPvdOffset + kIsoSectorSize - 1, filled)) {
        input.seek(static_cast<qint64>(kPvdOffset));
        char pvd[kIsoSectorSize];
        if (input.read(pvd, kIsoSectorSize) == kIsoSectorSize) {
            // Volume space size is at bytes 80-87 (both-endian uint32)
            // Little-endian at offset 80
            uint32 volumeSpaceSize = 0;
            std::memcpy(&volumeSpaceSize, pvd + 80, 4);
            if (volumeSpaceSize > 0)
                totalIsoSize = static_cast<uint64>(volumeSpaceSize) * kIsoSectorSize;
        }
    }

    // Clamp to actual file size
    if (totalIsoSize > fileSize)
        totalIsoSize = fileSize;

    const uint64 sectorCount = (totalIsoSize + kIsoSectorSize - 1) / kIsoSectorSize;
    int recoveredSectors = 0;
    int zeroFilledSectors = 0;

    // Zero buffer for missing sectors
    char zeroBuf[kIsoSectorSize]{};
    char sectorBuf[kIsoSectorSize];

    for (uint64 s = 0; s < sectorCount; ++s) {
        const uint64 sectorStart = s * kIsoSectorSize;
        const uint64 sectorEnd = std::min(sectorStart + kIsoSectorSize - 1, totalIsoSize - 1);
        const uint64 sectorLen = sectorEnd - sectorStart + 1;

        if (isFilled(sectorStart, sectorEnd, filled)) {
            // Sector is available — copy it
            input.seek(static_cast<qint64>(sectorStart));
            qint64 got = input.read(sectorBuf, static_cast<qint64>(sectorLen));
            if (got > 0) {
                output.write(sectorBuf, got);
                // Pad to full sector size if partial
                if (static_cast<uint64>(got) < kIsoSectorSize)
                    output.write(zeroBuf, static_cast<qint64>(kIsoSectorSize - static_cast<uint64>(got)));
                ++recoveredSectors;
            } else {
                output.write(zeroBuf, kIsoSectorSize);
                ++zeroFilledSectors;
            }
        } else {
            // Sector is missing — zero-fill
            output.write(zeroBuf, static_cast<qint64>(sectorLen < kIsoSectorSize ? kIsoSectorSize : sectorLen));
            ++zeroFilledSectors;
        }
    }

    if (recoveredSectors == 0)
        return false;

    logInfo(QStringLiteral("ISO recovery: %1 sectors recovered, %2 sectors zero-filled")
                .arg(recoveredSectors).arg(zeroFilledSectors));
    return true;
}

// ---------------------------------------------------------------------------
// recoverACE — stub for ACE archive recovery
// ---------------------------------------------------------------------------

bool ArchiveRecovery::recoverACE(QFile& input, QFile& output,
                                  const std::vector<Gap>& filled)
{
    // ACE archive format: block-based with headers
    // Each block: [CRC16: 2][size: 2][type: 1][flags: 2][...header data][...optional data]
    // Type 0x00 = main archive header
    // Type 0x01 = file header + compressed data

    static constexpr uint8 kAceMainHeader = 0x00;
    static constexpr uint8 kAceFileHeader = 0x01;

    const uint64 fileSize = static_cast<uint64>(input.size());
    uint64 pos = 0;
    int blocksRecovered = 0;

    // First, write the ACE main header (if available)
    // The main header starts at offset 0
    if (isFilled(0, 6, filled)) {
        input.seek(0);
        char headerPrefix[7]{};
        if (input.read(headerPrefix, 7) == 7) {
            uint16 headerSize = 0;
            std::memcpy(&headerSize, headerPrefix + 2, 2);

            if (headerSize > 0 && headerSize < 65535) {
                uint64 totalHeaderSize = headerSize + 4; // +4 for CRC and size fields
                if (isFilled(0, totalHeaderSize - 1, filled)) {
                    input.seek(0);
                    QByteArray headerData = input.read(static_cast<qint64>(totalHeaderSize));
                    output.write(headerData);
                    pos = totalHeaderSize;
                    ++blocksRecovered;
                }
            }
        }
    }

    // Scan for file header blocks
    while (pos < fileSize) {
        // Need at least 7 bytes for a block header
        if (!isFilled(pos, pos + 6, filled)) {
            ++pos;
            continue;
        }

        input.seek(static_cast<qint64>(pos));
        char blockHeader[7]{};
        if (input.read(blockHeader, 7) != 7)
            break;

        uint16 blockSize = 0;
        std::memcpy(&blockSize, blockHeader + 2, 2);
        uint8 blockType = static_cast<uint8>(blockHeader[4]);

        if (blockSize < 3 || blockSize > 65534) {
            ++pos;
            continue;
        }

        uint64 totalBlockSize = blockSize + 4; // +4 for CRC and size fields

        // Check for ADD_SIZE flag (bit 0 of flags for data after header)
        uint16 flags = 0;
        std::memcpy(&flags, blockHeader + 5, 2);

        if (flags & 0x0001) {
            // Block has additional packed data size after header
            uint64 headerEnd = pos + totalBlockSize;
            if (headerEnd + 4 <= fileSize && isFilled(headerEnd, headerEnd + 3, filled)) {
                input.seek(static_cast<qint64>(headerEnd));
                char addSizeBuf[4]{};
                input.read(addSizeBuf, 4);
                uint32 addSize = 0;
                std::memcpy(&addSize, addSizeBuf, 4);
                totalBlockSize += addSize;
            }
        }

        if (blockType == kAceFileHeader || blockType == kAceMainHeader) {
            if (isFilled(pos, pos + totalBlockSize - 1, filled)) {
                input.seek(static_cast<qint64>(pos));
                QByteArray blockData = input.read(static_cast<qint64>(totalBlockSize));
                output.write(blockData);
                ++blocksRecovered;
                pos += totalBlockSize;
                continue;
            }
        }

        // Skip to next block
        if (totalBlockSize > 0)
            pos += totalBlockSize;
        else
            ++pos;
    }

    if (blocksRecovered == 0)
        return false;

    logInfo(QStringLiteral("ACE recovery: recovered %1 blocks").arg(blocksRecovered));
    return true;
}

// ---------------------------------------------------------------------------
// recoverAsync — run recovery on a background thread
// ---------------------------------------------------------------------------

void ArchiveRecovery::recoverAsync(PartFile* partFile, bool preview,
                                    bool createCopy,
                                    std::function<void(bool)> callback)
{
    if (!partFile) {
        if (callback)
            callback(false);
        return;
    }

    if (partFile->isRecoveringArchive()) {
        logWarning(QStringLiteral("ArchiveRecovery: recovery already in progress"));
        if (callback)
            callback(false);
        return;
    }

    partFile->setRecoveringArchive(true);

    auto* thread = QThread::create([partFile, preview, createCopy, cb = std::move(callback)]() {
        bool result = recover(partFile, preview, createCopy);
        partFile->setRecoveringArchive(false);
        if (cb)
            cb(result);
    });

    thread->setObjectName(QStringLiteral("ArchiveRecoveryThread"));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace eMule
