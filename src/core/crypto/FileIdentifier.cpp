#include "pch.h"
#include "FileIdentifier.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QCryptographicHash>

#include <algorithm>

namespace eMule {

// File size       Data parts      ED2K parts      ED2K part hashes    AICH part hashes
// -------------------------------------------------------------------------------------------
// 1..PARTSIZE-1   1               1               0(!)                0 (!)
// PARTSIZE        1               2(!)            2(!)                0 (!)
// PARTSIZE+1      2               2               2                   2
// PARTSIZE*2      2               3(!)            3(!)                2
// PARTSIZE*2+1    3               3               3                   3

// ---------------------------------------------------------------------------
// FileIdentifierBase
// ---------------------------------------------------------------------------

FileIdentifierBase::FileIdentifierBase() = default;

FileIdentifierBase::FileIdentifierBase(const FileIdentifierBase& other)
{
    *this = other;
}

FileIdentifierBase& FileIdentifierBase::operator=(const FileIdentifierBase& other)
{
    if (this != &other) {
        md4cpy(m_md4Hash.data(), other.m_md4Hash.data());
        m_aichHash = other.m_aichHash;
        m_hasValidAICHHash = other.m_hasValidAICHHash;
    }
    return *this;
}

EMFileSize FileIdentifierBase::getFileSize() const
{
    return 0;
}

void FileIdentifierBase::setMD4Hash(FileDataIO& file)
{
    file.readHash16(m_md4Hash.data());
}

void FileIdentifierBase::setAICHHash(const AICHHash& hash)
{
    m_aichHash = hash;
    m_hasValidAICHHash = true;
}

bool FileIdentifierBase::compareRelaxed(const FileIdentifierBase& other) const
{
    return md4equ(m_md4Hash.data(), other.m_md4Hash.data())
        && (!getFileSize() || !other.getFileSize() || getFileSize() == other.getFileSize())
        && (!m_hasValidAICHHash || !other.m_hasValidAICHHash || m_aichHash == other.m_aichHash);
}

bool FileIdentifierBase::compareStrict(const FileIdentifierBase& other) const
{
    return md4equ(m_md4Hash.data(), other.m_md4Hash.data())
        && getFileSize() == other.getFileSize()
        && !(m_hasValidAICHHash ^ other.m_hasValidAICHHash)
        && m_aichHash == other.m_aichHash;
}

void FileIdentifierBase::writeIdentifier(FileDataIO& file, bool kadExcludeMD4) const
{
    const uint32 includesMD4  = static_cast<uint32>(!kadExcludeMD4);
    const uint32 includesSize = static_cast<uint32>(getFileSize() != 0);
    const uint32 includesAICH = static_cast<uint32>(hasAICHHash());
    const uint32 mandatoryOptions = 0;
    const uint32 options = 0;

    auto desc = static_cast<uint8>(
        (options          << 5) |
        (mandatoryOptions << 3) |
        (includesAICH     << 2) |
        (includesSize     << 1) |
        (includesMD4      << 0));

    file.writeUInt8(desc);
    if (!kadExcludeMD4)
        file.writeHash16(m_md4Hash.data());
    if (getFileSize() != 0)
        file.writeUInt64(getFileSize());
    if (hasAICHHash())
        m_aichHash.write(file);
}

// ---------------------------------------------------------------------------
// FileIdentifier
// ---------------------------------------------------------------------------

FileIdentifier::FileIdentifier(EMFileSize& fileSize)
    : m_fileSize(fileSize)
{
}

FileIdentifier::FileIdentifier(const FileIdentifier& other, EMFileSize& fileSize)
    : FileIdentifierBase(other)
    , m_fileSize(fileSize)
    , m_md4HashSet(other.m_md4HashSet)
    , m_aichPartHashSet(other.m_aichPartHashSet)
{
}

bool FileIdentifier::calculateMD4HashByHashSet(bool verifyOnly, bool deleteOnVerifyFail)
{
    if (m_md4HashSet.size() <= 1)
        return false;

    // Concatenate all part hashes and MD4-hash the result
    const auto count = m_md4HashSet.size();
    QByteArray buffer(static_cast<qsizetype>(count * kMdxDigestSize), Qt::Uninitialized);
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(buffer.data() + i * kMdxDigestSize,
                    m_md4HashSet[i].data(), kMdxDigestSize);
    }

    const QByteArray result = QCryptographicHash::hash(buffer, QCryptographicHash::Md4);
    std::array<uint8, 16> computed{};
    std::memcpy(computed.data(), result.constData(), kMdxDigestSize);

    if (verifyOnly) {
        if (!md4equ(computed.data(), m_md4Hash.data())) {
            if (deleteOnVerifyFail)
                deleteMD4Hashset();
            return false;
        }
    } else {
        md4cpy(m_md4Hash.data(), computed.data());
    }
    return true;
}

bool FileIdentifier::loadMD4HashsetFromFile(FileDataIO& file, bool verifyExistingHash)
{
    std::array<uint8, 16> checkId{};
    file.readHash16(checkId.data());
    deleteMD4Hashset();

    uint16 parts = file.readUInt16();
    if (verifyExistingHash && (!md4equ(m_md4Hash.data(), checkId.data())
                               || parts != getTheoreticalMD4PartHashCount()))
        return false;

    m_md4HashSet.reserve(parts);
    for (uint16 i = 0; i < parts; ++i) {
        std::array<uint8, 16> partHash{};
        file.readHash16(partHash.data());
        m_md4HashSet.push_back(partHash);
    }

    if (!verifyExistingHash)
        md4cpy(m_md4Hash.data(), checkId.data());

    return m_md4HashSet.empty() || calculateMD4HashByHashSet(true, true);
}

void FileIdentifier::writeMD4HashsetToFile(FileDataIO& file) const
{
    file.writeHash16(m_md4Hash.data());
    auto parts = static_cast<uint16>(m_md4HashSet.size());
    file.writeUInt16(parts);
    for (uint16 i = 0; i < parts; ++i)
        file.writeHash16(m_md4HashSet[i].data());
}

bool FileIdentifier::setMD4HashSet(const std::vector<std::array<uint8, 16>>& hashSet)
{
    deleteMD4Hashset();
    m_md4HashSet = hashSet;
    return m_md4HashSet.empty() || calculateMD4HashByHashSet(true, true);
}

const uint8* FileIdentifier::getMD4PartHash(uint32 part) const
{
    return (part < static_cast<uint32>(m_md4HashSet.size()))
         ? m_md4HashSet[part].data()
         : nullptr;
}

void FileIdentifier::deleteMD4Hashset()
{
    m_md4HashSet.clear();
}

uint16 FileIdentifier::getTheoreticalMD4PartHashCount() const
{
    if (!m_fileSize)
        return 0;
    auto result = static_cast<uint16>(m_fileSize / PARTSIZE);
    return result + static_cast<uint16>(result > 0);
}

void FileIdentifier::writeHashSetsToPacket(FileDataIO& file, bool md4, bool aich) const
{
    uint8 byOptions = 0;
    if (md4) {
        if (getTheoreticalMD4PartHashCount() == 0)
            md4 = false;
        else if (hasExpectedMD4HashCount())
            byOptions |= 0x01;
        else
            md4 = false;
    }
    if (aich) {
        if (getTheoreticalAICHPartHashCount() == 0)
            aich = false;
        else if (hasExpectedAICHHashCount() && hasAICHHash())
            byOptions |= 0x02;
        else
            aich = false;
    }
    file.writeUInt8(byOptions);
    if (md4)
        writeMD4HashsetToFile(file);
    if (aich)
        writeAICHHashsetToFile(file);
}

bool FileIdentifier::readHashSetsFromPacket(FileDataIO& file, bool& md4, bool& aich)
{
    uint8 byOptions = file.readUInt8();
    bool md4Present = (byOptions & 0x01) > 0;
    bool aichPresent = (byOptions & 0x02) > 0;

    if (md4Present && !md4) {
        // Skip unwanted MD4 hashset
        std::array<uint8, 16> tmp{};
        file.readHash16(tmp.data());
        for (int i = file.readUInt16(); --i >= 0;)
            file.readHash16(tmp.data());
    } else if (!md4Present) {
        md4 = false;
    } else if (md4) {
        if (!loadMD4HashsetFromFile(file, true)) {
            md4 = false;
            aich = false;
            return false;
        }
    }

    if (aichPresent && !aich) {
        // Skip unwanted AICH hashset
        file.seek(file.readUInt16() * static_cast<qint64>(kAICHHashSize), 1);
    } else if (!aichPresent || !hasAICHHash()) {
        aich = false;
    } else if (aich) {
        if (!loadAICHHashsetFromFile(file, true)) {
            if (md4) {
                deleteMD4Hashset();
                md4 = false;
            }
            aich = false;
            return false;
        }
    }
    return true;
}

uint16 FileIdentifier::getTheoreticalAICHPartHashCount() const
{
    return (m_fileSize <= PARTSIZE)
         ? 0
         : static_cast<uint16>((m_fileSize + PARTSIZE - 1) / PARTSIZE);
}

bool FileIdentifier::setAICHHashSet(const AICHRecoveryHashSet& source)
{
    if (source.getStatus() != EAICHStatus::HashSetComplete
        || source.getMasterHash() != m_aichHash)
        return false;

    if (!source.getPartHashes(m_aichPartHashSet))
        return false;
    return hasExpectedAICHHashCount();
}

bool FileIdentifier::setAICHHashSet(const FileIdentifier& source)
{
    if (!source.hasAICHHash() || !source.hasExpectedAICHHashCount())
        return false;

    m_aichPartHashSet = source.m_aichPartHashSet;
    return hasExpectedAICHHashCount();
}

bool FileIdentifier::loadAICHHashsetFromFile(FileDataIO& file, bool verify)
{
    m_aichPartHashSet.clear();
    AICHHash masterHash(file);
    if (hasAICHHash() && masterHash != m_aichHash)
        return false;

    uint16 count = file.readUInt16();
    m_aichPartHashSet.reserve(count);
    for (uint16 i = 0; i < count; ++i)
        m_aichPartHashSet.emplace_back(file);

    // Single-part files don't need part hashes — clear if loaded from
    // a .part.met created by a client that stored them anyway
    if (getTheoreticalAICHPartHashCount() == 0 && !m_aichPartHashSet.empty())
        m_aichPartHashSet.clear();

    if (verify)
        return verifyAICHHashSet();
    return true;
}

void FileIdentifier::writeAICHHashsetToFile(FileDataIO& file) const
{
    m_aichHash.write(file);
    auto parts = static_cast<uint16>(m_aichPartHashSet.size());
    file.writeUInt16(parts);
    for (uint16 i = 0; i < parts; ++i)
        m_aichPartHashSet[i].write(file);
}

bool FileIdentifier::verifyAICHHashSet()
{
    if (m_fileSize == 0 || !m_hasValidAICHHash)
        return false;
    if (!hasExpectedAICHHashCount())
        return false;

    AICHRecoveryHashSet tmpSet(m_fileSize);
    tmpSet.setMasterHash(m_aichHash, EAICHStatus::HashSetComplete);

    auto partCount = static_cast<uint32>((m_fileSize + PARTSIZE - 1) / PARTSIZE);
    if (partCount <= 1)
        return true; // No AICH part hashes needed

    for (uint32 part = 0; part < partCount; ++part) {
        uint64 partStartPos = static_cast<uint64>(part) * PARTSIZE;
        auto partSize = static_cast<uint32>(
            std::min<uint64>(PARTSIZE, m_fileSize - partStartPos));
        AICHHashTree* node = tmpSet.m_hashTree.findHash(partStartPos, partSize);
        if (!node)
            return false;
        node->m_hash = m_aichPartHashSet[part];
        node->m_hashValid = true;
    }

    if (!tmpSet.verifyHashTree(false)) {
        m_aichPartHashSet.clear();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// FileIdentifierSA
// ---------------------------------------------------------------------------

FileIdentifierSA::FileIdentifierSA() = default;

FileIdentifierSA::FileIdentifierSA(const uint8* md4Hash, EMFileSize fileSize,
                                   const AICHHash& aichHash, bool aichValid)
    : m_fileSize(fileSize)
{
    setMD4Hash(md4Hash);
    if (aichValid)
        setAICHHash(aichHash);
}

bool FileIdentifierSA::readIdentifier(FileDataIO& file, bool kadValidWithoutMd4)
{
    uint8 desc = file.readUInt8();
    bool hasMD4  = ((desc >> 0) & 0x01) > 0;
    bool hasSize = ((desc >> 1) & 0x01) > 0;
    bool hasAICH = ((desc >> 2) & 0x01) > 0;
    uint8 mandOpt = ((desc >> 3) & 0x03);
    // uint8 opts = ((desc >> 5) & 0x07); // reserved

    if (mandOpt > 0)
        return false;
    if (!hasMD4 && !kadValidWithoutMd4)
        return false;

    if (hasMD4)
        file.readHash16(m_md4Hash.data());
    if (hasSize)
        m_fileSize = file.readUInt64();
    if (hasAICH) {
        m_aichHash.read(file);
        m_hasValidAICHHash = true;
    }
    return true;
}

} // namespace eMule
