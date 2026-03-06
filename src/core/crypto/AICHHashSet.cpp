#include "pch.h"
#include "AICHHashSet.h"
#include "SHAHash.h"
#include "utils/DebugUtils.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QMutexLocker>

#include <algorithm>

namespace eMule {

// Trust thresholds (from original eMule source)
inline constexpr int kMinUniqueIPsToTrust = 10;
inline constexpr int kMinPercentageToTrust = 92;

// Static member definitions
QMutex AICHRecoveryHashSet::s_mutKnown2File;
QString AICHRecoveryHashSet::s_known2MetPath;
std::unordered_map<AICHHash, uint64> AICHRecoveryHashSet::s_storedHashes;

// ---------------------------------------------------------------------------
// AICHUntrustedHash
// ---------------------------------------------------------------------------

bool AICHUntrustedHash::addSigningIP(uint32 ip, bool testOnly)
{
    ip &= 0x00F0FFFF; // Use only the 20 most significant bits for unique IPs
    for (auto it = m_signingIPs.rbegin(); it != m_signingIPs.rend(); ++it) {
        if (*it == ip)
            return false;
    }
    if (!testOnly)
        m_signingIPs.push_back(ip);
    return true;
}

// ---------------------------------------------------------------------------
// AICHRecoveryHashSet — construction & basic ops
// ---------------------------------------------------------------------------

AICHRecoveryHashSet::AICHRecoveryHashSet(EMFileSize fileSize)
    : m_hashTree(0, true, PARTSIZE)
{
    if (fileSize != 0)
        setFileSize(fileSize);
}

void AICHRecoveryHashSet::setFileSize(EMFileSize size)
{
    m_hashTree.m_dataSize = size;
    m_hashTree.setBaseSize((size <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
}

void AICHRecoveryHashSet::setMasterHash(const AICHHash& hash, EAICHStatus newStatus)
{
    m_hashTree.m_hash = hash;
    m_hashTree.m_hashValid = true;
    setStatus(newStatus);
}

void AICHRecoveryHashSet::freeHashSet()
{
    m_hashTree.m_left.reset();
    m_hashTree.m_right.reset();
}

AICHHashAlgo* AICHRecoveryHashSet::getNewHashAlgo()
{
    return new ShaHasher();
}

bool AICHRecoveryHashSet::reCalculateHash(bool dontReplace)
{
    std::unique_ptr<AICHHashAlgo> algo(getNewHashAlgo());
    return m_hashTree.reCalculateHash(algo.get(), dontReplace);
}

bool AICHRecoveryHashSet::verifyHashTree(bool deleteBadTrees)
{
    std::unique_ptr<AICHHashAlgo> algo(getNewHashAlgo());
    return m_hashTree.verifyHashTree(algo.get(), deleteBadTrees);
}

void AICHRecoveryHashSet::untrustedHashReceived(const AICHHash& hash, uint32 fromIP)
{
    switch (m_status) {
    case EAICHStatus::Empty:
    case EAICHStatus::Untrusted:
    case EAICHStatus::Trusted:
        break;
    default:
        return;
    }

    bool found = false;
    for (auto& uh : m_untrustedHashes) {
        if (uh.m_hash == hash) {
            uh.addSigningIP(fromIP, false);
            found = true;
            break;
        }
    }

    if (!found) {
        // Check if this IP already signed a different hash
        for (auto& uh : m_untrustedHashes) {
            if (!uh.addSigningIP(fromIP, true)) {
                // IP already signed another hash — ignore
                return;
            }
        }
        AICHUntrustedHash newEntry;
        newEntry.m_hash = hash;
        newEntry.addSigningIP(fromIP, false);
        m_untrustedHashes.push_back(std::move(newEntry));
    }

    // Evaluate trust
    int64 totalSigningIPs = 0;
    std::size_t mostTrustedIdx = 0;
    int64 mostTrustedIPs = 0;
    bool anyFound = false;

    for (std::size_t i = 0; i < m_untrustedHashes.size(); ++i) {
        const auto signings = static_cast<int64>(m_untrustedHashes[i].m_signingIPs.size());
        totalSigningIPs += signings;
        if (signings > mostTrustedIPs) {
            mostTrustedIPs = signings;
            mostTrustedIdx = i;
            anyFound = true;
        }
    }

    if (!anyFound || totalSigningIPs == 0)
        return;

    if (mostTrustedIPs >= kMinUniqueIPsToTrust
        && (100 * mostTrustedIPs) / totalSigningIPs >= kMinPercentageToTrust)
    {
        setStatus(EAICHStatus::Trusted);
        if (!hasValidMasterHash()
            || getMasterHash() != m_untrustedHashes[mostTrustedIdx].m_hash)
        {
            setMasterHash(m_untrustedHashes[mostTrustedIdx].m_hash, EAICHStatus::Trusted);
            freeHashSet();
        }
    } else {
        setStatus(EAICHStatus::Untrusted);
        if (!hasValidMasterHash()
            || getMasterHash() != m_untrustedHashes[mostTrustedIdx].m_hash)
        {
            setMasterHash(m_untrustedHashes[mostTrustedIdx].m_hash, EAICHStatus::Untrusted);
            freeHashSet();
        }
    }
}

bool AICHRecoveryHashSet::isPartDataAvailable(uint64 partStartPos, EMFileSize fileSize)
{
    if (!(m_status == EAICHStatus::Verified
          || m_status == EAICHStatus::Trusted
          || m_status == EAICHStatus::HashSetComplete))
        return false;

    auto partSize = static_cast<uint32>(std::min<uint64>(PARTSIZE, fileSize - partStartPos));
    for (uint64 pos = 0; pos < partSize; pos += EMBLOCKSIZE) {
        const AICHHashTree* node = m_hashTree.findExistingHash(
            partStartPos + pos, std::min<uint64>(EMBLOCKSIZE, partSize - pos));
        if (!node || !node->m_hashValid)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Static configuration
// ---------------------------------------------------------------------------

void AICHRecoveryHashSet::setKnown2MetPath(const QString& path)
{
    s_known2MetPath = path;
}

uint64 AICHRecoveryHashSet::addStoredAICHHash(const AICHHash& hash, uint64 filePos)
{
    auto it = s_storedHashes.find(hash);
    if (it != s_storedHashes.end()) {
        if (filePos <= it->second)
            return 0; // older hash; ignore
        uint64 oldPos = it->second;
        it->second = filePos;
        return oldPos; // non-zero if old hash existed
    }
    s_storedHashes[hash] = filePos;
    return 0;
}

bool AICHRecoveryHashSet::isLargeFile() const
{
    return m_hashTree.m_dataSize > OLD_MAX_EMULE_FILE_SIZE;
}

// ---------------------------------------------------------------------------
// getPartHashes — extract part-level hashes from tree
// ---------------------------------------------------------------------------

bool AICHRecoveryHashSet::getPartHashes(std::vector<AICHHash>& result) const
{
    result.clear();
    if (m_status != EAICHStatus::HashSetComplete) {
        EMULE_ASSERT(false);
        return false;
    }

    const uint64 fileSize = m_hashTree.m_dataSize;
    const auto partCount = static_cast<uint32>((fileSize + PARTSIZE - 1) / PARTSIZE);
    if (partCount <= 1)
        return true; // No AICH part hashes for single-part files

    for (uint32 part = 0; part < partCount; ++part) {
        const uint64 partStartPos = static_cast<uint64>(part) * PARTSIZE;
        const auto partSize = static_cast<uint32>(
            std::min<uint64>(PARTSIZE, fileSize - partStartPos));
        const AICHHashTree* partTree = m_hashTree.findExistingHash(partStartPos, partSize);
        if (!partTree || !partTree->m_hashValid) {
            result.clear();
            EMULE_ASSERT(false);
            return false;
        }
        result.push_back(partTree->m_hash);
    }
    return true;
}

// ---------------------------------------------------------------------------
// findPartHash — find hash tree node for a given part
// ---------------------------------------------------------------------------

const AICHHashTree* AICHRecoveryHashSet::findPartHash(uint16 part)
{
    const uint64 fileSize = m_hashTree.m_dataSize;
    if (fileSize <= PARTSIZE)
        return &m_hashTree;

    const uint64 partStartPos = static_cast<uint64>(part) * PARTSIZE;
    const auto partSize = static_cast<uint32>(
        std::min<uint64>(PARTSIZE, fileSize - partStartPos));
    return m_hashTree.findHash(partStartPos, partSize);
}

// ---------------------------------------------------------------------------
// saveHashSet — persist to known2_64.met
// ---------------------------------------------------------------------------

bool AICHRecoveryHashSet::saveHashSet()
{
    if (m_status != EAICHStatus::HashSetComplete
        || !m_hashTree.m_hashValid
        || m_hashTree.m_dataSize == 0)
    {
        EMULE_ASSERT(false);
        return false;
    }

    QMutexLocker lock(&s_mutKnown2File);

    SafeFile file;
    if (!file.open(s_known2MetPath,
                   QIODevice::ReadWrite | QIODevice::Append)) {
        // Try creating the file
        if (!file.open(s_known2MetPath,
                       QIODevice::ReadWrite | QIODevice::NewOnly)) {
            qCWarning(lcEmuleGeneral, "Failed to open %s for writing",
                       qUtf8Printable(s_known2MetPath));
            return false;
        }
    }

    try {
        if (file.length() <= 0) {
            file.seek(0, 0);
            file.writeUInt8(kKnown2MetVersion);
        } else {
            file.seek(0, 0);
            const uint8 header = file.readUInt8();
            if (header != kKnown2MetVersion) {
                qCWarning(lcEmuleGeneral, "known2_64.met has wrong version");
                return false;
            }
        }

        // Check if already stored
        if (s_storedHashes.contains(m_hashTree.m_hash)) {
            qCDebug(lcEmuleGeneral, "AICH hashset already present in known2.met - %s",
                     qUtf8Printable(m_hashTree.m_hash.getString()));
            return true;
        }

        // Write hashset at end of file
        file.seek(0, 2); // SEEK_END
        const qint64 hashSetWritePos = file.position();
        m_hashTree.m_hash.write(file);

        // Calculate expected hash count
        uint32 hashCount = static_cast<uint32>(
            (PARTSIZE / EMBLOCKSIZE + (PARTSIZE % EMBLOCKSIZE != 0 ? 1u : 0u))
            * (m_hashTree.m_dataSize / PARTSIZE));
        if (m_hashTree.m_dataSize % PARTSIZE != 0) {
            const uint64 lastPartSize = m_hashTree.m_dataSize % PARTSIZE;
            hashCount += static_cast<uint32>(
                lastPartSize / EMBLOCKSIZE + (lastPartSize % EMBLOCKSIZE != 0 ? 1u : 0u));
        }

        file.writeUInt32(hashCount);

        if (!m_hashTree.writeLowestLevelHashes(file, 0, true, true)) {
            qCWarning(lcEmuleGeneral, "Failed to save HashSet: WriteLowestLevelHashes() failed!");
            return false;
        }

        addStoredAICHHash(m_hashTree.m_hash, static_cast<uint64>(hashSetWritePos));
        qCDebug(lcEmuleGeneral, "Saved AICH hashset: %u hashes + 1 master", hashCount);
    } catch (const std::exception& ex) {
        qCWarning(lcEmuleGeneral, "Exception saving AICH hashset: %s", ex.what());
        freeHashSet();
        return false;
    }

    freeHashSet();
    return true;
}

// ---------------------------------------------------------------------------
// loadHashSet — load from known2_64.met
// ---------------------------------------------------------------------------

bool AICHRecoveryHashSet::loadHashSet()
{
    if (m_status != EAICHStatus::HashSetComplete) {
        EMULE_ASSERT(false);
        return false;
    }
    if (!m_hashTree.m_hashValid || m_hashTree.m_dataSize == 0) {
        EMULE_ASSERT(false);
        return false;
    }

    SafeFile file;
    if (!file.open(s_known2MetPath, QIODevice::ReadOnly)) {
        qCWarning(lcEmuleGeneral, "Failed to open %s for reading",
                   qUtf8Printable(s_known2MetPath));
        return false;
    }

    try {
        const uint8 header = file.readUInt8();
        if (header != kKnown2MetVersion) {
            qCWarning(lcEmuleGeneral, "known2_64.met has wrong version");
            return false;
        }

        const qint64 fileLength = file.length();

        // Try indexed lookup first
        bool useExpectedPos = true;
        uint64 expectedPos = 0;
        auto it = s_storedHashes.find(m_hashTree.m_hash);
        if (it == s_storedHashes.end() || static_cast<qint64>(it->second) >= fileLength) {
            useExpectedPos = false;
        } else {
            expectedPos = it->second;
        }

        while (file.position() < fileLength) {
            AICHHash currentHash;

            if (useExpectedPos) {
                const qint64 fallbackPos = file.position();
                file.seek(static_cast<qint64>(expectedPos), 0);
                currentHash.read(file);
                if (m_hashTree.m_hash != currentHash) {
                    // Index was stale, fall back to sequential scan
                    file.seek(fallbackPos, 0);
                    currentHash.read(file);
                }
                useExpectedPos = false;
            } else {
                currentHash.read(file);
            }

            if (m_hashTree.m_hash == currentHash) {
                // Found matching hashset — calculate expected hash count
                uint32 expectedCount = static_cast<uint32>(
                    (PARTSIZE / EMBLOCKSIZE + (PARTSIZE % EMBLOCKSIZE != 0 ? 1u : 0u))
                    * (m_hashTree.m_dataSize / PARTSIZE));
                if (m_hashTree.m_dataSize % PARTSIZE != 0) {
                    const uint64 lastPart = m_hashTree.m_dataSize % PARTSIZE;
                    expectedCount += static_cast<uint32>(
                        lastPart / EMBLOCKSIZE + (lastPart % EMBLOCKSIZE != 0 ? 1u : 0u));
                }

                const uint32 hashCount = file.readUInt32();
                if (hashCount != expectedCount) {
                    qCWarning(lcEmuleGeneral, "HashSet count mismatch: %u vs expected %u",
                               hashCount, expectedCount);
                    return false;
                }

                if (!m_hashTree.loadLowestLevelHashes(file)) {
                    qCWarning(lcEmuleGeneral, "Failed to load lowest level hashes");
                    return false;
                }
                if (!reCalculateHash(false)) {
                    qCWarning(lcEmuleGeneral, "Failed to recalculate loaded hashes");
                    return false;
                }
                if (currentHash != m_hashTree.m_hash) {
                    qCWarning(lcEmuleGeneral, "Calculated master hash differs from stored - corrupt hashset");
                    return false;
                }
                return true;
            }

            // Skip this hashset
            const uint32 hashCount = file.readUInt32();
            if (file.position() + static_cast<qint64>(hashCount) * kAICHHashSize > fileLength) {
                qCWarning(lcEmuleGeneral, "known2_64.met truncated");
                return false;
            }
            file.seek(static_cast<qint64>(hashCount) * kAICHHashSize, 1); // SEEK_CUR
        }
        qCDebug(lcEmuleGeneral, "HashSet not found in known2_64.met");
    } catch (const std::exception& ex) {
        qCWarning(lcEmuleGeneral, "Exception loading AICH hashset: %s", ex.what());
    }
    return false;
}

// ---------------------------------------------------------------------------
// createPartRecoveryData — generate recovery data for a part
// ---------------------------------------------------------------------------

bool AICHRecoveryHashSet::createPartRecoveryData(uint64 partStartPos,
                                                  FileDataIO& out,
                                                  bool dbgDontLoad)
{
    if (m_status != EAICHStatus::HashSetComplete) {
        EMULE_ASSERT(false);
        return false;
    }
    if (m_hashTree.m_dataSize <= EMBLOCKSIZE) {
        EMULE_ASSERT(false);
        return false;
    }

    if (!dbgDontLoad && !loadHashSet()) {
        qCWarning(lcEmuleGeneral, "Create recovery data error: failed to load hashset");
        setStatus(EAICHStatus::Error);
        return false;
    }

    const uint64 fileSize = m_hashTree.m_dataSize;
    const auto partSize = static_cast<uint32>(
        std::min<uint64>(PARTSIZE, fileSize - partStartPos));

    // Determine nesting level
    uint8 level = 0;
    m_hashTree.findHash(partStartPos, partSize, &level);
    const auto hashesToWrite = static_cast<uint16>(
        (level - 1) + partSize / EMBLOCKSIZE + (partSize % EMBLOCKSIZE != 0 ? 1u : 0u));
    const bool use32Bit = isLargeFile();

    if (use32Bit)
        out.writeUInt16(0); // no 16-bit hashes
    out.writeUInt16(hashesToWrite);

    const qint64 checkPos = out.position();
    bool result = m_hashTree.createPartRecoveryData(
        partStartPos, partSize, out, 0, use32Bit);

    if (result) {
        const qint64 expectedBytes = hashesToWrite
            * static_cast<qint64>(kAICHHashSize + (use32Bit ? 4 : 2));
        result = (out.position() - checkPos == expectedBytes);
        if (!result) {
            EMULE_ASSERT(false);
            qCWarning(lcEmuleGeneral, "Recovery data has wrong length");
            setStatus(EAICHStatus::Error);
        }
    } else {
        qCWarning(lcEmuleGeneral, "Failed to create recovery data");
        setStatus(EAICHStatus::Error);
    }

    if (!use32Bit)
        out.writeUInt16(0); // no 32-bit hashes

    if (!dbgDontLoad)
        freeHashSet();

    return result;
}

// ---------------------------------------------------------------------------
// readRecoveryData — parse recovery data from another client
// ---------------------------------------------------------------------------

bool AICHRecoveryHashSet::readRecoveryData(uint64 partStartPos, SafeMemFile& in)
{
    if (!(m_status == EAICHStatus::Verified || m_status == EAICHStatus::Trusted)) {
        EMULE_ASSERT(false);
        return false;
    }

    /* V2 AICH Hash Packet:
        <count1 uint16>                             16bit-hashes-to-read
        (<identifier uint16><hash HASHSIZE>)[count1]
        <count2 uint16>                             32bit-hashes-to-read
        (<identifier uint32><hash HASHSIZE>)[count2]
    */

    const uint64 fileSize = m_hashTree.m_dataSize;
    const auto partSize = static_cast<uint32>(
        std::min<uint64>(PARTSIZE, fileSize - partStartPos));

    uint8 level = 0;
    m_hashTree.findHash(partStartPos, partSize, &level);
    const auto hashesToRead = static_cast<uint16>(
        (level - 1) + partSize / EMBLOCKSIZE + (partSize % EMBLOCKSIZE != 0 ? 1u : 0u));

    // Read 16-bit identifier hashes
    uint16 hashesAvailable = in.readUInt16();
    if (in.length() - in.position() < static_cast<qint64>(hashesToRead) * (kAICHHashSize + 2)
        || (hashesToRead != hashesAvailable && hashesAvailable != 0))
    {
        qCWarning(lcEmuleGeneral, "Recovery data: invalid size/count (16-bit)");
        return false;
    }

    for (uint32 i = 0; i < hashesAvailable; ++i) {
        const uint16 hashIdent = in.readUInt16();
        if (hashIdent == 1 /* never overwrite master hash */
            || !m_hashTree.setHash(in, hashIdent, -1, false))
        {
            qCWarning(lcEmuleGeneral, "Recovery data: error reading hash into tree (16-bit)");
            verifyHashTree(true);
            return false;
        }
    }

    // Read 32-bit identifier hashes
    if (hashesAvailable == 0 && in.length() - in.position() >= 2) {
        hashesAvailable = in.readUInt16();
        if (in.length() - in.position() < static_cast<qint64>(hashesToRead) * (kAICHHashSize + 4)
            || (hashesToRead != hashesAvailable && hashesAvailable != 0))
        {
            qCWarning(lcEmuleGeneral, "Recovery data: invalid size/count (32-bit)");
            return false;
        }

        for (uint32 i = 0; i < hashesToRead; ++i) {
            const uint32 hashIdent = in.readUInt32();
            if (hashIdent == 1 || hashIdent > 0x400000
                || !m_hashTree.setHash(in, hashIdent, -1, false))
            {
                qCWarning(lcEmuleGeneral, "Recovery data: error reading hash into tree (32-bit)");
                verifyHashTree(true);
                return false;
            }
        }
    } else if (in.length() - in.position() >= 2) {
        in.readUInt16(); // skip unused count
    }

    if (hashesAvailable == 0) {
        qCWarning(lcEmuleGeneral, "Recovery data: packet contained no hashes");
        return false;
    }

    if (verifyHashTree(true)) {
        // Verify all lowest-level hashes are present
        for (uint32 partPos = 0; partPos < partSize; partPos += EMBLOCKSIZE) {
            const AICHHashTree* node = m_hashTree.findExistingHash(
                partStartPos + partPos,
                std::min<uint64>(EMBLOCKSIZE, partSize - partPos));
            if (!node || !node->m_hashValid) {
                qCWarning(lcEmuleGeneral, "Recovery data: missing lowest level hashes after verify");
                return false;
            }
        }
        return true;
    }

    qCWarning(lcEmuleGeneral, "Recovery data: hash tree verification failed");
    return false;
}

} // namespace eMule
