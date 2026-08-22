#pragma once

/// @file UploadQueueStore.h
/// @brief Upload Queue Storage (UQS) — remembers our waiting uploaders across restarts.
///
/// The upload-side counterpart to Save/Load Sources (files/SourceSaver.h). Neither official
/// eMule nor MorphXT ever persisted the upload queue, so unlike SLS there is no on-disk
/// format to stay compatible with and no MFC original to port — the layout below is ours.
///
/// The queue is global rather than per file, so this is one file for the whole client:
/// `<ConfigDir>/uploadqueue.met`, holding the top kMaxSavedQueueClients waiters by score.
///
/// Why it is worth persisting at all: a peer's earned queue position lives in
/// ClientCredits::m_secureWaitTime, which clients.met does not store, so without this a
/// two-hour waiter restarts from zero the moment emulecored does. Restored clients are
/// *not* dialled — they go on the waiting list and normal slot handling takes over. When a
/// restored peer re-asks us on its own, ClientList::attachToAlreadyKnown() re-homes the
/// incoming socket onto the restored object, so it keeps its place instead of being
/// rejected as a duplicate. That merge is what makes the whole feature work.

#include "net/Address.h"
#include "utils/Types.h"

#include <QString>

#include <array>
#include <vector>

namespace eMule {

class UploadQueue;
class UpDownClient;

// ---------------------------------------------------------------------------
// Policy constants
// ---------------------------------------------------------------------------

/// Waiters kept on disk, best score first.
inline constexpr int kMaxSavedQueueClients = 100;

/// Resave cadence, matching SLS's kResaveTimeMs. The real expiry is MAX_PURGEQUEUETIME.
inline constexpr uint32 kQueueResaveTimeMs = 600'000;  // 10 minutes

/// File name inside the config dir.
inline constexpr auto kUploadQueueFileName = "uploadqueue.met";

/// Layout version. Bumped on any field change; an unknown value yields an empty read
/// rather than a partial one. There is no cross-client compatibility to preserve, so this
/// is the entire forward-compatibility story.
inline constexpr uint8 kUploadQueueFileVersion = 1;

// ---------------------------------------------------------------------------
// QueuedClientRecord
// ---------------------------------------------------------------------------

/// One waiting client, as stored.
///
/// Times are **elapsed seconds at save time**, never raw ticks: the wait clock is a
/// steady_clock value truncated to 32 bits, i.e. process-relative and meaningless after a
/// restart. Load rebases them onto the live clock.
struct QueuedClientRecord {
    std::array<uint8, 16> userHash{};      ///< required — see isRestorable()
    std::array<uint8, 16> reqUpFileId{};   ///< the file this peer is queued for

    uint32  userIDHybrid = 0;
    Address userIPv4;                      ///< empty when the peer is IPv6-only or LowID
    Address userIPv6;
    uint16  userPort   = 0;
    uint32  serverIP   = 0;                ///< network byte order; 0 when unknown
    uint16  serverPort = 0;
    uint16  kadPort    = 0;
    uint16  udpPort    = 0;

    /// The byte UpDownClient::setConnectOptions() decodes: 0x01 supports / 0x02 requests /
    /// 0x04 requires crypt, 0x08 direct-UDP-callback. Same encoding SLS persists.
    uint8 connectOptions = 0;

    /// Needed for obfuscation, not display: shouldReceiveCryptUDPPackets() is
    /// `supportsCryptLayer && kadVersion >= KADEMLIA_VERSION8_49b`. Left at 0 we would send
    /// plaintext UDP to a peer expecting obfuscation, and it would drop it — silently
    /// costing us exactly the firewalled peers the direct-callback path exists for.
    uint8 kadVersion = 0;
    /// udpVer > 3 gates the part-status block in the OP_REASKACK reply, i.e. whether a
    /// returning peer can see its queue rank again.
    uint8 udpVer = 0;

    uint32 waitedSeconds            = 0;  ///< how long it had been queued
    uint32 sinceLastRequestSeconds  = 0;  ///< since we last heard from it
    uint32 askedCount               = 0;

    // -- display only, refreshed on the next handshake -------------------------
    QString userName;
    uint32  clientVersion    = 0;
    uint8   emuleVersion     = 0;
    uint8   compatibleClient = 0;

    /// A record is only usable if we can key credits and dedup on a hash, and have some
    /// endpoint to match or dial. Applied on both save and load.
    [[nodiscard]] bool isRestorable() const;
};

// ---------------------------------------------------------------------------
// UploadQueueFile
// ---------------------------------------------------------------------------

/// Binary codec for `uploadqueue.met`, free of UploadQueue/Preferences/AppContext so the
/// format can be tested in isolation and in both directions.
///
/// Layout, little-endian throughout (SafeFile's primitives):
/// @code
///   uint8  version | uint32 savedAtUnix | uint16 recordCount
///   per record:
///     hash16 userHash | hash16 reqUpFileId
///     uint32 userIDHybrid | uint32 userIPv4(net order) | 16 bytes userIPv6
///     uint16 userPort | uint32 serverIP | uint16 serverPort
///     uint16 kadPort  | uint16 udpPort  | uint8 connectOptions
///     uint8  kadVersion | uint8 udpVer
///     uint32 waitedSeconds | uint32 sinceLastRequestSeconds | uint32 askedCount
///     string userName | uint32 clientVersion | uint8 emuleVersion | uint8 compatibleClient
/// @endcode
class UploadQueueFile {
public:
    /// Result of a read: the records plus the wall-clock stamp they were written at.
    struct Contents {
        std::vector<QueuedClientRecord> records;
        uint32 savedAtUnix = 0;
    };

    /// Read the file. A missing file yields empty contents without logging; a truncated,
    /// corrupt or unknown-version file yields empty contents *with* a warning. Never
    /// returns a partially decoded record.
    [[nodiscard]] static Contents read(const QString& path);

    /// Write atomically (tmp + rotate to .bak + rename), as KnownFileList::save() does.
    /// @p savedAtUnix is stamped into the header and drives expiry on the next read.
    static bool write(const QString& path,
                      const std::vector<QueuedClientRecord>& records,
                      uint32 savedAtUnix);
};

// ---------------------------------------------------------------------------
// UploadQueueStore
// ---------------------------------------------------------------------------

/// Load-once / save-periodically driver. Held by value in UploadQueue.
class UploadQueueStore {
public:
    /// Periodic tick from UploadQueue::processStore(), once a second.
    ///
    /// Loads exactly once, and only after theApp.isConnected() — an ED2K server *or* Kad.
    /// Until that load has happened it refuses to save, because writing the current (empty)
    /// queue first would destroy the very records we are about to restore.
    void process(UploadQueue* queue, const QString& path);

    /// Write now, ignoring the resave timer — the clean-shutdown path. Also refuses before
    /// the load, for the same reason: a daemon killed while still offline must not truncate
    /// a good file.
    bool saveNow(UploadQueue* queue, const QString& path);

    /// Read @p path and inject its live records into @p queue. Returns the number added.
    /// Marks the store as loaded — including when the file is missing, expired or corrupt —
    /// which is what unblocks saving. Public so the tests can drive it without standing up
    /// a real ED2K/Kad connection.
    int loadAndInject(UploadQueue* queue, const QString& path);

    /// Whether the one-shot load has already run.
    [[nodiscard]] bool hasLoaded() const { return m_loaded; }

private:
    [[nodiscard]] static std::vector<QueuedClientRecord> collectRecords(const UploadQueue* queue);
    [[nodiscard]] static bool makeRecord(const UpDownClient* client, QueuedClientRecord& out);
    [[nodiscard]] static UpDownClient* buildClient(const QueuedClientRecord& rec,
                                                   const Address& v4, const Address& v6);
    static bool save(UploadQueue* queue, const QString& path);

    bool   m_loaded    = false;
    uint32 m_lastSaved = 0;
};

} // namespace eMule
