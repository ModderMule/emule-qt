#pragma once

/// @file AICHHashSet.h
/// @brief AICH recovery hash set — full implementation.
///
/// Replaces the original CAICHRecoveryHashSet. All methods including
/// file I/O (known2_64.met) and recovery data creation/parsing are
/// implemented.

#include "AICHData.h"
#include "AICHHashTree.h"

#include <QMutex>
#include <QString>

#include <unordered_map>
#include <vector>

namespace eMule {

class SafeMemFile;

// ---------------------------------------------------------------------------
// AICHUntrustedHash — tracks signing IPs for trust evaluation
// ---------------------------------------------------------------------------

class AICHUntrustedHash {
public:
    bool addSigningIP(uint32 ip, bool testOnly);

    AICHHash m_hash;
    std::vector<uint32> m_signingIPs;
};

// ---------------------------------------------------------------------------
// AICHRecoveryHashSet
// ---------------------------------------------------------------------------

class AICHRecoveryHashSet {
public:
    explicit AICHRecoveryHashSet(EMFileSize fileSize = 0);

    bool reCalculateHash(bool dontReplace = false);
    bool verifyHashTree(bool deleteBadTrees);
    void untrustedHashReceived(const AICHHash& hash, uint32 fromIP);
    bool isPartDataAvailable(uint64 partStartPos, EMFileSize fileSize);

    void setStatus(EAICHStatus status) { m_status = status; }
    [[nodiscard]] EAICHStatus getStatus() const { return m_status; }

    void freeHashSet();
    void setFileSize(EMFileSize size);

    [[nodiscard]] const AICHHash& getMasterHash() const { return m_hashTree.m_hash; }
    void setMasterHash(const AICHHash& hash, EAICHStatus newStatus);
    [[nodiscard]] bool hasValidMasterHash() const { return m_hashTree.m_hashValid; }

    [[nodiscard]] static AICHHashAlgo* getNewHashAlgo();

    // --- File persistence (known2_64.met) ---

    /// Save this hashset to known2_64.met. Frees the hashset after saving.
    bool saveHashSet();

    /// Load this hashset from known2_64.met by searching for matching master hash.
    bool loadHashSet();

    // --- Recovery data (used during downloads) ---

    /// Create recovery data for a part (writes sibling hashes to `out`).
    bool createPartRecoveryData(uint64 partStartPos, FileDataIO& out,
                                bool dbgDontLoad = false);

    /// Read recovery data received from another client.
    bool readRecoveryData(uint64 partStartPos, SafeMemFile& in);

    // --- Part hash extraction ---

    /// Extract all part-level hashes from the tree into `result`.
    [[nodiscard]] bool getPartHashes(std::vector<AICHHash>& result) const;

    /// Find the hash tree node for a given part number.
    [[nodiscard]] const AICHHashTree* findPartHash(uint16 part);

    // --- Static configuration ---

    /// Set the path to known2_64.met. Must be called before any save/load.
    static void setKnown2MetPath(const QString& path);

    /// Record a stored AICH hash and its file position. Returns the old
    /// position if a duplicate was replaced, 0 otherwise.
    static uint64 addStoredAICHHash(const AICHHash& hash, uint64 filePos);

    /// Mutex for known2_64.met file access.
    static QMutex s_mutKnown2File;

    // Public tree access (used by FileIdentifier)
    AICHHashTree m_hashTree;

private:
    /// Check if the file is large (>4GB), determining 16-bit vs 32-bit hash identifiers.
    [[nodiscard]] bool isLargeFile() const;

    EAICHStatus m_status = EAICHStatus::Empty;
    std::vector<AICHUntrustedHash> m_untrustedHashes;

    // Static state for known2_64.met hash index
    static QString s_known2MetPath;
    static std::unordered_map<AICHHash, uint64> s_storedHashes;
};

} // namespace eMule
