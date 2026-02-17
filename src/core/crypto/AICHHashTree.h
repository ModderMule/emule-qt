#pragma once

/// @file AICHHashTree.h
/// @brief Binary hash tree for AICH verification.
///
/// Replaces the original CAICHHashTree class. Uses std::unique_ptr for
/// child ownership instead of raw new/delete.

#include "AICHData.h"

#include <cstdint>
#include <memory>

namespace eMule {

class FileDataIO;

class AICHHashTree {
    friend class AICHRecoveryHashSet;

public:
    AICHHashTree(uint64 dataSize, bool isLeftBranch, uint64 baseSize);

    void setBlockHash(uint64 size, uint64 startPos, AICHHashAlgo* hashAlg);
    bool reCalculateHash(AICHHashAlgo* hashAlg, bool dontReplace);
    bool verifyHashTree(AICHHashAlgo* hashAlg, bool deleteBadTrees);

    AICHHashTree* findHash(uint64 startPos, uint64 size)
    {
        uint8 level = 0;
        return findHash(startPos, size, &level);
    }

    const AICHHashTree* findExistingHash(uint64 startPos, uint64 size) const
    {
        uint8 level = 0;
        return findExistingHash(startPos, size, &level);
    }

    [[nodiscard]] uint64 getBaseSize() const;
    void setBaseSize(uint64 value);

    // Public data (matching original design — internal implementation class)
    uint64 m_dataSize;
    AICHHash m_hash;
    bool m_isLeftBranch;
    bool m_hashValid;
    std::unique_ptr<AICHHashTree> m_left;
    std::unique_ptr<AICHHashTree> m_right;

protected:
    AICHHashTree* findHash(uint64 startPos, uint64 size, uint8* level);
    const AICHHashTree* findExistingHash(uint64 startPos, uint64 size, uint8* level) const;

    bool createPartRecoveryData(uint64 startPos, uint64 size,
                                FileDataIO& out, uint32 hashIdent, bool use32Bit);
    void writeHash(FileDataIO& out, uint32 hashIdent, bool use32Bit) const;
    bool writeLowestLevelHashes(FileDataIO& out, uint32 hashIdent,
                                bool noIdent, bool use32Bit) const;
    bool loadLowestLevelHashes(FileDataIO& input);
    bool setHash(FileDataIO& input, uint32 hashIdent,
                 sint8 level = -1, bool allowOverwrite = true);
    bool reduceToBaseSize(uint64 baseSize);

private:
    // BaseSize stored as bool: true = PARTSIZE, false = EMBLOCKSIZE
    bool m_baseSize;
};

} // namespace eMule
