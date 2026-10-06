#pragma once

/// @file AICHHashSet.h
/// @brief AICH recovery hash set — full implementation.
///
/// Replaces the original CAICHRecoveryHashSet. All methods including
/// file I/O (known2_64.met) and recovery data creation/parsing are
/// implemented.

#include "AICHData.h"
#include "AICHHashTree.h"
#include "net/Address.h"

#include <QMutex>
#include <QString>

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eMule {

class SafeMemFile;
class PartFile;
class UpDownClient;

// ---------------------------------------------------------------------------
// AICHUntrustedHash — tracks signing IPs for trust evaluation
// ---------------------------------------------------------------------------

class AICHUntrustedHash {
public:
    /// @p voter is a voterKey(): one vote per network, not per address.
    bool addSigningIP(uint64 voter, bool testOnly);

    /// IPv4: the 20 most significant bits (MFC). IPv6: the /48, flagged so the two
    /// families cannot collide.
    [[nodiscard]] static uint64 voterKey(const Address& from);

    AICHHash m_hash;
    std::vector<uint64> m_signingIPs;
};

// ---------------------------------------------------------------------------
// AICHRecoveryHashSet
// ---------------------------------------------------------------------------

class AICHRecoveryHashSet {
public:
    explicit AICHRecoveryHashSet(EMFileSize fileSize = 0);

    bool reCalculateHash(bool dontReplace = false);
    bool verifyHashTree(bool deleteBadTrees);
    void untrustedHashReceived(const AICHHash& hash, const Address& from);
    /// IPv4 in network byte order.
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
    [[nodiscard]] static bool hasKnown2MetPath();

    /// Is a hashset with this master hash in known2_64.met (per the loaded index)?
    [[nodiscard]] static bool isStored(const AICHHash& hash);

    /// Record a stored AICH hash and its file position. Returns the old
    /// position if a duplicate was replaced, 0 otherwise.
    static uint64 addStoredAICHHash(const AICHHash& hash, uint64 filePos);

    /// Master hashes of every set in known2_64.met, per the loaded index.
    [[nodiscard]] static std::vector<AICHHash> storedHashes();

    /// Rewrite known2_64.met without the sets @p keep refuses, older duplicates and
    /// zeroed masters; the index follows. A set saved or asked for again in this
    /// session always stays. Nothing is written when nothing goes.
    /// @return false when the file could not be read or replaced (it is left as it was).
    static bool compactKnown2(const std::function<bool(const AICHHash&)>& keep,
                              uint32& droppedSets, uint64& droppedBytes);

    /// Mutex for known2_64.met file access.
    static QMutex s_mutKnown2File;

    // --- Pending recovery requests (MFC SHAHashSet.h:196-253) ---

    /// One outstanding OP_AICHREQUEST: which part of which file we asked whom for.
    struct RequestedData {
        uint16 part = 0;
        PartFile* file = nullptr;
        UpDownClient* client = nullptr;
    };

    /// Remember an outgoing request so its answer can be matched and, if it never
    /// comes, another source asked. MFC pushes onto m_liRequestedData in SendAICHRequest.
    static void addClientAICHRequest(const RequestedData& request);

    /// This client cannot serve what we asked it: forget the request and ask somebody
    /// else for the same part. MFC ClientAICHRequestFailed (SHAHashSet.cpp:1001).
    static void clientAICHRequestFailed(UpDownClient* client);

    /// Drop the entry without re-asking. MFC RemoveClientAICHRequest.
    static void removeClientAICHRequest(const UpDownClient* client);

    /// Is this part of this file already on its way from somebody? MFC IsClientRequestPending.
    [[nodiscard]] static bool isClientRequestPending(const PartFile* file, uint16 part);

    /// What we asked this client for; a default entry when we asked it nothing.
    [[nodiscard]] static RequestedData aichReqDetails(const UpDownClient* client);

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
    /// Sets saved, or found already stored, since the program started.
    static std::unordered_set<AICHHash> s_savedThisSession;
    static std::vector<RequestedData> s_requestedData;
};

} // namespace eMule
