#pragma once

/// @file FileIdentifier.h
/// @brief File identification with MD4 + AICH hashes.
///
/// Replaces the original CFileIdentifierBase, CFileIdentifier, CFileIdentifierSA.

#include "AICHData.h"
#include "AICHHashSet.h"
#include "MD4Hash.h"
#include "utils/OtherFunctions.h"

#include <array>
#include <cstring>
#include <vector>

namespace eMule {

class FileDataIO;

// ---------------------------------------------------------------------------
// FileIdentifierBase — MD4 hash + optional AICH
// ---------------------------------------------------------------------------

class FileIdentifierBase {
public:
    virtual ~FileIdentifierBase() = default;

    virtual EMFileSize getFileSize() const;

    void writeIdentifier(FileDataIO& file, bool kadExcludeMD4 = false) const;
    [[nodiscard]] bool compareRelaxed(const FileIdentifierBase& other) const;
    [[nodiscard]] bool compareStrict(const FileIdentifierBase& other) const;

    // MD4
    void setMD4Hash(const uint8* hash) { md4cpy(m_md4Hash.data(), hash); }
    void setMD4Hash(FileDataIO& file);
    [[nodiscard]] const uint8* getMD4Hash() const { return m_md4Hash.data(); }

    // AICH
    [[nodiscard]] const AICHHash& getAICHHash() const { return m_aichHash; }
    void setAICHHash(const AICHHash& hash);
    [[nodiscard]] bool hasAICHHash() const { return m_hasValidAICHHash; }
    void clearAICHHash() { m_hasValidAICHHash = false; }

protected:
    FileIdentifierBase();
    FileIdentifierBase(const FileIdentifierBase& other);
    FileIdentifierBase& operator=(const FileIdentifierBase& other);

    std::array<uint8, 16> m_md4Hash{};
    AICHHash m_aichHash;
    bool m_hasValidAICHHash = false;
};

// ---------------------------------------------------------------------------
// FileIdentifier — full hash set management
// ---------------------------------------------------------------------------

class FileIdentifier : public FileIdentifierBase {
public:
    explicit FileIdentifier(EMFileSize& fileSize);
    FileIdentifier(const FileIdentifier& other, EMFileSize& fileSize);

    // Common
    void writeHashSetsToPacket(FileDataIO& file, bool md4, bool aich) const;
    bool readHashSetsFromPacket(FileDataIO& file, bool& md4, bool& aich);
    EMFileSize getFileSize() const override { return m_fileSize; }

    // MD4
    bool calculateMD4HashByHashSet(bool verifyOnly, bool deleteOnVerifyFail = true);
    bool loadMD4HashsetFromFile(FileDataIO& file, bool verifyExistingHash);
    void writeMD4HashsetToFile(FileDataIO& file) const;

    bool setMD4HashSet(const std::vector<std::array<uint8, 16>>& hashSet);
    [[nodiscard]] const uint8* getMD4PartHash(uint32 part) const;
    void deleteMD4Hashset();

    [[nodiscard]] uint16 getTheoreticalMD4PartHashCount() const;
    [[nodiscard]] uint16 getAvailableMD4PartHashCount() const
    {
        return static_cast<uint16>(m_md4HashSet.size());
    }
    [[nodiscard]] bool hasExpectedMD4HashCount() const
    {
        return getTheoreticalMD4PartHashCount() == getAvailableMD4PartHashCount();
    }
    [[nodiscard]] std::vector<std::array<uint8, 16>>& getRawMD4HashSet() { return m_md4HashSet; }

    // AICH
    bool loadAICHHashsetFromFile(FileDataIO& file, bool verify = true);
    void writeAICHHashsetToFile(FileDataIO& file) const;

    bool setAICHHashSet(const AICHRecoveryHashSet& source);
    bool setAICHHashSet(const FileIdentifier& source);

    bool verifyAICHHashSet();
    [[nodiscard]] uint16 getTheoreticalAICHPartHashCount() const;
    [[nodiscard]] uint16 getAvailableAICHPartHashCount() const
    {
        return static_cast<uint16>(m_aichPartHashSet.size());
    }
    [[nodiscard]] bool hasExpectedAICHHashCount() const
    {
        return getTheoreticalAICHPartHashCount() == getAvailableAICHPartHashCount();
    }
    [[nodiscard]] const std::vector<AICHHash>& getRawAICHHashSet() const { return m_aichPartHashSet; }

private:
    EMFileSize& m_fileSize;
    std::vector<std::array<uint8, 16>> m_md4HashSet;
    std::vector<AICHHash> m_aichPartHashSet;
};

// ---------------------------------------------------------------------------
// FileIdentifierSA — standalone read-only identifier
// ---------------------------------------------------------------------------

class FileIdentifierSA : public FileIdentifierBase {
public:
    FileIdentifierSA();
    FileIdentifierSA(const uint8* md4Hash, EMFileSize fileSize,
                     const AICHHash& aichHash, bool aichValid);

    EMFileSize getFileSize() const override { return m_fileSize; }

    bool readIdentifier(FileDataIO& file, bool kadValidWithoutMd4 = false);

private:
    EMFileSize m_fileSize = 0;
};

} // namespace eMule
