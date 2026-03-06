#include "pch.h"
#include "AICHHashTree.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <algorithm>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AICHHashTree::AICHHashTree(uint64 dataSize, bool isLeftBranch, uint64 baseSize)
    : m_dataSize(dataSize)
    , m_isLeftBranch(isLeftBranch)
    , m_hashValid(false)
    , m_baseSize(false)
{
    setBaseSize(baseSize);
}

// ---------------------------------------------------------------------------
// BaseSize accessors
// ---------------------------------------------------------------------------

void AICHHashTree::setBaseSize(uint64 value)
{
    m_baseSize = (value >= PARTSIZE);
}

uint64 AICHHashTree::getBaseSize() const
{
    return m_baseSize ? PARTSIZE : EMBLOCKSIZE;
}

// ---------------------------------------------------------------------------
// FindHash (mutable) — recursive
// ---------------------------------------------------------------------------

AICHHashTree* AICHHashTree::findHash(uint64 startPos, uint64 size, uint8* level)
{
    ++(*level);
    if (*level > 22 || startPos + size > m_dataSize || size > m_dataSize)
        return nullptr;

    if (startPos == 0 && size == m_dataSize)
        return this;

    if (m_dataSize <= getBaseSize())
        return nullptr;

    uint64 nBlocks = m_dataSize / getBaseSize()
                   + static_cast<uint64>(m_dataSize % getBaseSize() != 0);
    uint64 nLeft = (nBlocks + static_cast<unsigned>(m_isLeftBranch)) / 2 * getBaseSize();
    uint64 nRight = m_dataSize - nLeft;

    if (startPos < nLeft) {
        if (startPos + size > nLeft)
            return nullptr;
        if (!m_left)
            m_left = std::make_unique<AICHHashTree>(
                nLeft, true, (nLeft <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
        return m_left->findHash(startPos, size, level);
    }

    startPos -= nLeft;
    if (startPos + size > nRight)
        return nullptr;
    if (!m_right)
        m_right = std::make_unique<AICHHashTree>(
            nRight, false, (nRight <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
    return m_right->findHash(startPos, size, level);
}

// ---------------------------------------------------------------------------
// FindExistingHash (const) — recursive
// ---------------------------------------------------------------------------

const AICHHashTree* AICHHashTree::findExistingHash(uint64 startPos, uint64 size, uint8* level) const
{
    ++(*level);
    if (*level > 22 || startPos + size > m_dataSize || size > m_dataSize)
        return nullptr;

    if (startPos == 0 && size == m_dataSize)
        return m_hashValid ? this : nullptr;

    if (m_dataSize <= getBaseSize())
        return nullptr;

    uint64 nBlocks = m_dataSize / getBaseSize()
                   + static_cast<uint64>(m_dataSize % getBaseSize() != 0);
    uint64 nLeft = (nBlocks + static_cast<unsigned>(m_isLeftBranch)) / 2 * getBaseSize();
    uint64 nRight = m_dataSize - nLeft;

    if (startPos < nLeft) {
        if (startPos + size > nLeft)
            return nullptr;
        if (!m_left || !m_left->m_hashValid)
            return nullptr;
        return m_left->findExistingHash(startPos, size, level);
    }

    startPos -= nLeft;
    if (startPos + size > nRight)
        return nullptr;
    if (!m_right || !m_right->m_hashValid)
        return nullptr;
    return m_right->findExistingHash(startPos, size, level);
}

// ---------------------------------------------------------------------------
// ReCalculateHash — recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::reCalculateHash(AICHHashAlgo* hashAlg, bool dontReplace)
{
    if (static_cast<bool>(m_left) != static_cast<bool>(m_right))
        return false; // only one child — invalid

    if (m_left && m_right) {
        if (!m_left->reCalculateHash(hashAlg, dontReplace)
            || !m_right->reCalculateHash(hashAlg, dontReplace))
            return false;

        if (dontReplace && m_hashValid)
            return true;

        if (m_right->m_hashValid && m_left->m_hashValid) {
            hashAlg->reset();
            hashAlg->add(m_left->m_hash.getRawHash(), kAICHHashSize);
            hashAlg->add(m_right->m_hash.getRawHash(), kAICHHashSize);
            hashAlg->finish(m_hash);
            m_hashValid = true;
            return true;
        }
        return m_hashValid;
    }
    return true;
}

// ---------------------------------------------------------------------------
// VerifyHashTree — recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::verifyHashTree(AICHHashAlgo* hashAlg, bool deleteBadTrees)
{
    if (!m_hashValid) {
        if (deleteBadTrees) {
            m_left.reset();
            m_right.reset();
        }
        return false;
    }

    // Calculate missing hashes without overwriting
    if (m_left && !m_left->m_hashValid)
        m_left->reCalculateHash(hashAlg, true);
    if (m_right && !m_right->m_hashValid)
        m_right->reCalculateHash(hashAlg, true);

    if ((m_right && m_right->m_hashValid) != (m_left && m_left->m_hashValid)) {
        // one branch can never be verified
        if (deleteBadTrees) {
            m_left.reset();
            m_right.reset();
        }
        return false;
    }

    if ((m_right && m_right->m_hashValid) && (m_left && m_left->m_hashValid)) {
        AICHHash cmpHash;
        hashAlg->reset();
        hashAlg->add(m_left->m_hash.getRawHash(), kAICHHashSize);
        hashAlg->add(m_right->m_hash.getRawHash(), kAICHHashSize);
        hashAlg->finish(cmpHash);

        if (m_hash != cmpHash) {
            if (deleteBadTrees) {
                m_left.reset();
                m_right.reset();
            }
            return false;
        }
        return m_left->verifyHashTree(hashAlg, deleteBadTrees)
            && m_right->verifyHashTree(hashAlg, deleteBadTrees);
    }

    // Last hash in branch — nothing below to verify
    if (deleteBadTrees) {
        if (m_left && !m_left->m_hashValid)
            m_left.reset();
        if (m_right && !m_right->m_hashValid)
            m_right.reset();
    }
    return true;
}

// ---------------------------------------------------------------------------
// SetBlockHash
// ---------------------------------------------------------------------------

void AICHHashTree::setBlockHash(uint64 size, uint64 startPos, AICHHashAlgo* hashAlg)
{
    AICHHashTree* target = findHash(startPos, size);
    if (!target)
        return;

    if (target->getBaseSize() != EMBLOCKSIZE || target->m_dataSize != size)
        return;

    hashAlg->finish(target->m_hash);
    target->m_hashValid = true;
}

// ---------------------------------------------------------------------------
// CreatePartRecoveryData — recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::createPartRecoveryData(uint64 startPos, uint64 size,
                                          FileDataIO& out, uint32 hashIdent,
                                          bool use32Bit)
{
    if (startPos + size > m_dataSize || size > m_dataSize)
        return false;

    if (startPos == 0 && size == m_dataSize)
        return writeLowestLevelHashes(out, hashIdent, false, use32Bit);

    if (m_dataSize <= getBaseSize())
        return false;

    hashIdent <<= 1;
    hashIdent |= static_cast<uint32>(m_isLeftBranch);

    uint64 nBlocks = m_dataSize / getBaseSize()
                   + static_cast<uint64>(m_dataSize % getBaseSize() != 0);
    uint64 nLeft = (((m_isLeftBranch) ? nBlocks + 1 : nBlocks) / 2) * getBaseSize();
    uint64 nRight = m_dataSize - nLeft;

    if (!m_left || !m_right)
        return false;

    if (startPos < nLeft) {
        if (startPos + size > nLeft || !m_right->m_hashValid)
            return false;
        m_right->writeHash(out, hashIdent, use32Bit);
        return m_left->createPartRecoveryData(startPos, size, out, hashIdent, use32Bit);
    }

    startPos -= nLeft;
    if (startPos + size > nRight || !m_left->m_hashValid)
        return false;
    m_left->writeHash(out, hashIdent, use32Bit);
    return m_right->createPartRecoveryData(startPos, size, out, hashIdent, use32Bit);
}

// ---------------------------------------------------------------------------
// WriteHash
// ---------------------------------------------------------------------------

void AICHHashTree::writeHash(FileDataIO& out, uint32 hashIdent, bool use32Bit) const
{
    hashIdent <<= 1;
    hashIdent |= static_cast<uint32>(m_isLeftBranch);
    if (use32Bit)
        out.writeUInt32(hashIdent);
    else
        out.writeUInt16(static_cast<uint16>(hashIdent));
    m_hash.write(out);
}

// ---------------------------------------------------------------------------
// WriteLowestLevelHashes — recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::writeLowestLevelHashes(FileDataIO& out, uint32 hashIdent,
                                          bool noIdent, bool use32Bit) const
{
    hashIdent <<= 1;
    hashIdent |= static_cast<uint32>(m_isLeftBranch);

    if (!m_left && !m_right) {
        if (m_dataSize <= getBaseSize() && m_hashValid) {
            if (!noIdent) {
                if (use32Bit)
                    out.writeUInt32(hashIdent);
                else
                    out.writeUInt16(static_cast<uint16>(hashIdent));
            }
            m_hash.write(out);
            return true;
        }
        return false;
    }

    if (!m_left || !m_right)
        return false;

    return m_left->writeLowestLevelHashes(out, hashIdent, noIdent, use32Bit)
        && m_right->writeLowestLevelHashes(out, hashIdent, noIdent, use32Bit);
}

// ---------------------------------------------------------------------------
// LoadLowestLevelHashes — recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::loadLowestLevelHashes(FileDataIO& input)
{
    if (m_dataSize <= getBaseSize()) {
        m_hash.read(input);
        m_hashValid = true;
        return true;
    }

    uint64 nBlocks = m_dataSize / getBaseSize()
                   + static_cast<uint64>(m_dataSize % getBaseSize() != 0);
    uint64 nLeft = (nBlocks + static_cast<unsigned>(m_isLeftBranch)) / 2 * getBaseSize();
    uint64 nRight = m_dataSize - nLeft;

    if (!m_left)
        m_left = std::make_unique<AICHHashTree>(
            nLeft, true, (nLeft <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
    if (!m_right)
        m_right = std::make_unique<AICHHashTree>(
            nRight, false, (nRight <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);

    return m_left->loadLowestLevelHashes(input)
        && m_right->loadLowestLevelHashes(input);
}

// ---------------------------------------------------------------------------
// SetHash — write hash by identifier into tree, recursive
// ---------------------------------------------------------------------------

bool AICHHashTree::setHash(FileDataIO& input, uint32 hashIdent,
                           sint8 level, bool allowOverwrite)
{
    if (level == -1) {
        // First call — determine how many levels to go
        for (level = 31; level >= 0 && (hashIdent & 0x80000000u) == 0; --level)
            hashIdent <<= 1;
        if (level < 0)
            return false;
    }

    if (level == 0) {
        if (m_hashValid && !allowOverwrite) {
            AICHHash::skip(kAICHHashSize, input);
            return true;
        }
        m_hash.read(input);
        m_hashValid = true;
        return true;
    }

    if (m_dataSize <= getBaseSize())
        return false;

    hashIdent <<= 1;
    --level;

    uint64 nBlocks = m_dataSize / getBaseSize()
                   + static_cast<uint64>(m_dataSize % getBaseSize() != 0);
    uint64 nLeft = (nBlocks + static_cast<unsigned>(m_isLeftBranch)) / 2 * getBaseSize();
    uint64 nRight = m_dataSize - nLeft;

    if ((hashIdent & 0x80000000u) > 0) {
        if (!m_left)
            m_left = std::make_unique<AICHHashTree>(
                nLeft, true, (nLeft <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
        return m_left->setHash(input, hashIdent, level);
    }

    if (!m_right)
        m_right = std::make_unique<AICHHashTree>(
            nRight, false, (nRight <= PARTSIZE) ? EMBLOCKSIZE : PARTSIZE);
    return m_right->setHash(input, hashIdent, level);
}

// ---------------------------------------------------------------------------
// ReduceToBaseSize — prune smaller base sizes
// ---------------------------------------------------------------------------

bool AICHHashTree::reduceToBaseSize(uint64 baseSize)
{
    bool deleted = false;
    if (m_left) {
        if (m_left->getBaseSize() < baseSize) {
            m_left.reset();
            deleted = true;
        } else {
            deleted = m_left->reduceToBaseSize(baseSize);
        }
    }
    if (m_right) {
        if (m_right->getBaseSize() < baseSize) {
            m_right.reset();
            deleted = true;
        } else {
            deleted |= m_right->reduceToBaseSize(baseSize);
        }
    }
    return deleted;
}

} // namespace eMule
