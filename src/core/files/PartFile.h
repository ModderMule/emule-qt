#pragma once

/// @file PartFile.h
/// @brief In-progress download file — port of MFC CPartFile.
///
/// Core download file with gap management, buffered I/O, status machine,
/// priority, block selection, persistence (.part.met), and source tracking.
/// Inherits KnownFile (non-QObject); uses PartFileNotifier for signals.

#include "files/KnownFile.h"
#include "files/SourceIndex.h"
#include "files/SourceSaver.h"
#include "search/FakeFileDetector.h"
#include "crypto/AICHHashSet.h"
#include "client/ClientStructs.h"
#include "client/CorruptionBlackBox.h"
#include "client/DeadSourceList.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <QFile>
#include <QObject>
#include <QThread>

#include <QPointer>

#include <array>
#include <ctime>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <vector>

namespace eMule {

class UpDownClient;
class SafeMemFile;
class FileMoveThread;
struct PartDigest;
struct PartDigestRequest;
struct PartFileWriteResult;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/// Values match MFC's EPartFileStatus exactly (srchybrid/PartFile.h:21-33).
///
/// Two of these are never *stored*: Paused and Insufficient are synthesised at read
/// time by status(), because pause is an overlay on the real state rather than a
/// state of its own — see the comment on status(). The stored set is
/// {Ready, Empty, WaitingForHash, Hashing, Error, Completing, Complete}.
///
/// Ready and Empty are not "active" and "idle": Empty means nothing has been verified
/// complete yet, so there is nothing to offer, and Ready means at least one part has
/// verified, so the file is shareable. That is the shareability latch.
enum class PartFileStatus : uint8 {
    Ready           = 0,
    Empty           = 1,
    WaitingForHash  = 2,
    Hashing         = 3,
    Error           = 4,
    Insufficient    = 5,
    Unknown         = 6,
    Paused          = 7,
    Completing      = 8,
    Complete        = 9
};

enum class PartFileFormat : uint8 {
    Unknown  = 0,
    DefaultOld,
    Splitted,
    NewOld,
    Shareaza,
    BadFormat
};

enum class PartFileLoadResult {
    FailedNoAccess = -2,
    FailedCorrupt  = -1,
    FailedOther    = 0,
    LoadSuccess    = 1,
    CheckSuccess   = 2
};

enum class PartFileOp : uint8 {
    None          = 0,
    Hashing,
    Copying,
    Uncompressing,
    ImportParts
};

// ---------------------------------------------------------------------------
// Gap — represents an unfilled byte range [start, end] (inclusive)
// ---------------------------------------------------------------------------

struct Gap {
    uint64 start = 0;
    uint64 end   = 0;
};

// ---------------------------------------------------------------------------
// BufferedData — data waiting to be flushed to disk
// ---------------------------------------------------------------------------

struct BufferedData {
    uint64 start = 0;
    uint64 end   = 0;
    std::vector<uint8> data;
    Requested_Block_Struct* block = nullptr;
};

// ---------------------------------------------------------------------------
// PartFileNotifier — QObject signal emitter owned by PartFile
// ---------------------------------------------------------------------------

class PartFileNotifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void statusChanged(eMule::PartFileStatus newStatus);
    void progressUpdated(float percent);
    void sourceAdded(eMule::UpDownClient* client);
    void sourceRemoved(eMule::UpDownClient* client);
    void downloadCompleted();
    void fileMoveFinished(bool success);
};

// ---------------------------------------------------------------------------
// FileMoveThread — async file move for completed downloads
// ---------------------------------------------------------------------------

class FileMoveThread : public QThread {
    Q_OBJECT
public:
    /// What the worker needs to re-check the data before it delivers it. Copied on the
    /// main thread: the worker never touches the PartFile.
    struct Verify {
        QByteArray fileHash;
        uint64 fileSize = 0;
        std::vector<std::array<uint8, 16>> partHashes;
    };

    /// @p aichFileSize non-zero: also build the AICH recovery set and store it in known2
    /// (MFC does it in the completion hash, srchybrid/PartFile.cpp:1543-1550).
    FileMoveThread(const QString& srcPath, const QString& destPath,
                   std::optional<Verify> verify = std::nullopt, uint64 aichFileSize = 0,
                   QObject* parent = nullptr);
    void run() override;

    /// Deliver across volumes: copy to a staged sibling of @p finalDest, put it on the
    /// disk, rename it in place, then remove @p srcPath. On failure, or when
    /// @p keepGoing says stop, nothing is left at the destination and the source stays.
    static bool copyThenRename(const QString& srcPath, const QString& finalDest,
                               const std::function<bool()>& keepGoing);
signals:
    /// The data does not match its hashes, or could not be read. One
    /// PartFile::PartVerdict per part; nothing was moved.
    void verifyFailed(const QByteArray& partOk);
    void moveStarted();
    /// The recovery set is in known2; @p masterHash is its 20-byte root. Before moveFinished.
    void aichHashSetStored(const QByteArray& masterHash);
    void moveFinished(bool success, const QString& destPath);
private:
    QString m_srcPath;
    QString m_destPath;
    std::optional<Verify> m_verify;
    uint64 m_aichFileSize = 0;
};

// ---------------------------------------------------------------------------
// PartFile — in-progress download file
// ---------------------------------------------------------------------------

class PartFile : public KnownFile {
public:
    explicit PartFile(uint32 category = 0);
    ~PartFile() override;

    // Non-copyable (owns file handles and source lists)
    PartFile(const PartFile&) = delete;
    PartFile& operator=(const PartFile&) = delete;

    // Signal emitter for download-specific events
    [[nodiscard]] PartFileNotifier* partNotifier() { return &m_partNotifier; }

    // -- Identity -------------------------------------------------------------

    [[nodiscard]] bool isPartFile() const override;

    /// May this part file be offered to servers and published to Kad yet?
    ///
    /// MFC's gate, spelled out: the full MD4 hashset must be known and at least one
    /// part must be verifiably complete (srchybrid/PartFile.cpp:4507-4516,
    /// CPartFile::AddToSharedFiles). Without the hashset we cannot prove any part we
    /// hand out is the file we claim it is; without a complete part there is nothing
    /// to hand out.
    ///
    /// This is the predicate; PartFileStatus carries the resulting latch. Once it
    /// holds, addToSharedFiles() promotes Empty -> Ready, and Ready is thereafter the
    /// answer to "is this shared" — exactly as in MFC.
    [[nodiscard]] bool canBeShared() const;

    /// Register with SharedFileList once canBeShared() holds. Idempotent — safeAddKFile()
    /// dedups, and every caller sits in a loop that re-runs.
    void addToSharedFiles();

    [[nodiscard]] const QString& partMetFileName() const { return m_partMetFilename; }
    [[nodiscard]] const QString& fullName() const { return m_fullName; }
    void setFullName(const QString& name) { m_fullName = name; }
    [[nodiscard]] const QString& tmpPath() const { return m_tmpPath; }
    void setTmpPath(const QString& path) { m_tmpPath = path; }

    // -- File size override ---------------------------------------------------

    void setFileSize(EMFileSize size) override;

    // -- Gap management -------------------------------------------------------

    void addGap(uint64 start, uint64 end);
    void fillGap(uint64 start, uint64 end);
    [[nodiscard]] bool isComplete(uint64 start, uint64 end) const;
    [[nodiscard]] bool isComplete(uint32 part) const;
    /// Complete and already on disk: a filled gap may still sit in the write buffer.
    /// MFC IsCompleteBDSafe — the test an upload read must pass. @p end inclusive, clamped.
    [[nodiscard]] bool isCompleteBDSafe(uint64 start, uint64 end) const;
    /// KnownFile::isPartComplete — a partfile answers from its gap list.
    [[nodiscard]] bool isPartComplete(uint32 part) const override { return isComplete(part); }

    /// KnownFile::dataFilePath — the `.part` data file, i.e. fullName() (which is
    /// the `.part.met`) with the `.met` suffix removed.
    [[nodiscard]] QString dataFilePath() const override
    {
        QString path = m_fullName;
        if (path.endsWith(QStringLiteral(".met")))
            path.chop(4);
        return path;
    }
    [[nodiscard]] bool isPureGap(uint64 start, uint64 end) const;
    [[nodiscard]] bool isAlreadyRequested(uint64 start, uint64 end) const;
    [[nodiscard]] uint64 totalGapSizeInRange(uint64 start, uint64 end) const;
    [[nodiscard]] uint64 totalGapSizeInPart(uint32 part) const;
    [[nodiscard]] uint64 totalGapSize() const;

    /// How close the download is to its end, for block selection.
    ///  - Late: 90 % there. A source much slower than another gets short reservations,
    ///    and a block whose holder has gone quiet may be given to a second source.
    ///  - Endgame: 99.9 %, or what is left would take the current rate under 30 s, or
    ///    it fits in ENDGAME_BLOCK_THRESHOLD blocks. A much faster source may also
    ///    double up on a slow holder's block.
    /// Never more than two holders per block; a healthy holder keeps it to itself.
    enum class EndPhase { Normal, Late, Endgame };
    [[nodiscard]] EndPhase endPhase() const;
    static constexpr int ENDGAME_BLOCK_THRESHOLD = 3;
    static constexpr uint32 kStalledBlockMs = 15'000;   // holder delivered nothing for this long
    static constexpr uint32 kSlowSourceFactor = 5;      // "much slower / faster"
    static constexpr uint32 kSlowReservationSecs = 10;  // a slow source reserves this much time
    static constexpr uint64 kMinSlowReservation = 16 * 1024;
    [[nodiscard]] EMFileSize completedSize() const { return m_completedSize; }
    [[nodiscard]] float percentCompleted() const { return m_percentCompleted; }
    [[nodiscard]] uint64 compressionGain() const { return m_compressionGain; }
    [[nodiscard]] uint64 corruptionLoss() const { return m_corruptionLoss; }
    void updateCompletedInfos();
    [[nodiscard]] const std::list<Gap>& gapList() const { return m_gapList; }

    // -- Buffered I/O ---------------------------------------------------------

    ///  sender  who supplied these bytes, for corruption attribution. MFC passes
    ///                the CUpDownClient here; an Address is enough and lets a caller name
    ///                somebody other than itself (see HttpCacheClient). A null Address means
    ///                "nobody to blame" and records nothing.
    /// Returns the bytes accepted: 0 for a duplicate or for data touching a complete part.
    uint32 writeToBuffer(uint64 transize, const uint8* data,
                         uint64 start, uint64 end,
                         Requested_Block_Struct* block,
                         const Address& sender = {});
    /// @param forceICH  re-hash without asking AICH first (the ICH pass).
    /// @param noAICH    never start an AICH recovery request from this flush — the
    ///                  destructor flushes this way, since a request would outlive us.
    ///                  MFC srchybrid/PartFile.h:232.
    /// Writes the buffer and checks the parts it completed, inline. Waits for a
    /// write handed to the worker first, so on return nothing is in flight.
    void flushBuffer(bool forceICH = false, bool noAICH = false);
    /// The same on the disk worker: returns at once, the outcome arrives through
    /// applyFlushResult(). Until then the data counts as buffered — not servable,
    /// and saved as gaps. Inline when there is no worker.
    void flushBufferAsync();
    void applyFlushResult(PartFileWriteResult& result, bool forceICH = false, bool noAICH = false);
    [[nodiscard]] bool isFlushPending() const { return m_flushToken != 0; }
    /// No gap left, but the disk worker has not said yet whether the last parts
    /// are good. A source with nothing to ask for stays put until it has.
    [[nodiscard]] bool awaitsFinalVerdict() const { return m_flushToken != 0 && m_gapList.empty(); }
    /// A hashset arrived: verify the parts that completed while we had none.
    void hashsetReceived();

    // -- Block selection ------------------------------------------------------

    bool getNextRequestedBlock(UpDownClient* sender,
                               Requested_Block_Struct** newblocks,
                               int& count);
    bool getNextEmptyBlockInPart(uint32 partNumber,
                                Requested_Block_Struct* reqBlock,
                                uint64 searchFrom = 0) const;
    bool removeBlockFromList(uint64 start, uint64 end);
    /// Exactly this reservation. With two holders of one range, the range alone
    /// does not say whose entry to drop.
    bool removeBlockFromList(const Requested_Block_Struct* block);
    void removeAllRequestedBlocks();

    /// Claim every still-missing block of one part for a non-ed2k transfer.
    ///
    /// HTTP Cache fetches a whole part in one HTTP request, so it has to stop
    /// ed2k sources from pulling the same bytes at the same time. Registering the
    /// part's gaps in m_requestedBlocks does exactly that for free:
    /// getNextRequestedBlock() already skips anything isAlreadyRequested().
    ///
    /// The caller owns the returned blocks and MUST hand them back to
    /// releaseReservedBlocks(), success or failure — they are not attached to any
    /// client's pending list, so nothing else will ever free them.
    ///
    /// @return bytes covered; 0 when the part is already complete or fully claimed
    uint64 reservePartForExternalTransfer(uint32 partNumber,
                                          std::vector<Requested_Block_Struct*>& out);

    /// Undo reservePartForExternalTransfer(): unregister and delete. Clears @p blocks.
    void releaseReservedBlocks(std::vector<Requested_Block_Struct*>& blocks);

    // -- Status machine -------------------------------------------------------

    /// The file's status, with pause applied as an overlay — MFC
    /// CPartFile::GetStatus (srchybrid/PartFile.cpp:2152-2157).
    ///
    /// A paused or space-starved file reads back as Paused/Insufficient even though
    /// the stored status still records what it really is. Pass @p ignorePause to see
    /// through that, which is what the sharing paths do: a paused part file with
    /// verified parts is still shared and still offered to the server.
    [[nodiscard]] PartFileStatus status(bool ignorePause = false) const
    {
        if ((!m_paused && !m_insufficient)
            || m_status == PartFileStatus::Error
            || m_status == PartFileStatus::Completing
            || m_status == PartFileStatus::Complete
            || ignorePause)
            return m_status;
        return m_paused ? PartFileStatus::Paused : PartFileStatus::Insufficient;
    }

    /// Set the stored status. Paused/Insufficient are rejected — they are overlays,
    /// not stored states (MFC asserts the same in _SetStatus).
    void setStatus(PartFileStatus s);

    /// Full path of the .part data file (the .part.met path minus its ".met").
    [[nodiscard]] QString partDataPath() const;

    /// Verdict of a part check. Unread = the bytes could not be read, so no verdict.
    enum class PartVerdict : char { Bad = 0, Ok = 1, Unread = 2 };

    /// Apply a completed rehash: one PartVerdict per part. Re-gaps complete parts that
    /// hashed bad, leaves everything else alone, and re-latches the status.
    /// MFC CPartFile::PartFileHashFinished (srchybrid/PartFile.cpp:1479-1573).
    void applyRehashResult(const QByteArray& partOk);

    /// Check a .part file's parts against their MD4 hashes (a file below PARTSIZE against
    /// @p fileHash). Runs on a worker and touches no PartFile. One PartVerdict per part;
    /// a part that could not be read, and every part after it, stays Unread.
    /// @p keepGoing is asked after each part and stops the pass by returning false.
    /// @p aichOut, if given, is fed the same bytes; the caller finishes the tree.
    [[nodiscard]] static QByteArray verifyPartData(
        const QString& partPath, uint64 fileSize, const QByteArray& fileHash,
        const std::vector<std::array<uint8, 16>>& partHashes,
        const std::function<bool(uint32 partsDone, uint32 partCount)>& keepGoing = {},
        AICHRecoveryHashSet* aichOut = nullptr);

    /// A file loaded with nothing left to download: finish what the last run began.
    void finishLoadedDownload();
    /// New token for a rehash about to be queued; a result carrying another one is stale.
    uint64 beginRehash() { return m_rehashToken = ++s_rehashSerial; }
    [[nodiscard]] uint64 rehashToken() const { return m_rehashToken; }
    [[nodiscard]] bool isStopped() const { return m_stopped; }
    [[nodiscard]] bool isPaused() const { return m_paused; }
    [[nodiscard]] bool isInsufficient() const { return m_insufficient; }
    /// Bytes the .part still has to take on disk (it is created sparse).
    [[nodiscard]] uint64 neededSpace() const
    {
        const uint64 size = static_cast<uint64>(fileSize());
        const uint64 done = static_cast<uint64>(m_completedSize);
        return size > done ? size - done : 0;
    }
    void pauseFile(bool insufficient = false);
    void resumeFile();
    void stopFile(bool cancel = false);
    /// Paused, out of space or errored for an hour: let the sources go. A paused
    /// file becomes stopped; the other two keep their state.
    void stopPausedFile();
    /// Drop every source, offering each to another file it is wanted for first.
    void removeAllSources(bool tryToSwap);
    /// Start of the idle hour stopPausedFile() measures. Tests.
    void setLastPausePurge(time_t t) { m_lastPausePurge = t; }
    [[nodiscard]] bool completionError() const { return m_completionError; }
    /// The long-running operation in progress, if any. Relabels the displayed status —
    /// "Completing (Hashing)" and so on, as MFC's getPartfileStatus does.
    [[nodiscard]] PartFileOp fileOp() const { return m_fileOp; }
    /// Mark or clear an operation that runs outside this class (importing parts).
    void setFileOp(PartFileOp op) { m_fileOp = op; }
    /// Seconds this download has been running while connected (MFC GetDlActiveTime).
    [[nodiscard]] uint32 dlActiveTime() const;
    /// Seconds until done, -1 = unknown (MFC getTimeRemaining).
    [[nodiscard]] int64 timeRemaining() const;
    /// The rule behind timeRemaining(): size/rate, or with @p advanced the smaller of
    /// that and the average over @p activeSecs; -1 past 15 days or without data.
    [[nodiscard]] static int64 estimateTimeRemaining(uint64 left, uint64 done, uint32 rate,
                                                     uint32 activeSecs, bool advanced);
    /// Start or stop the active-time clock (MFC SetActive). Starts only while connected.
    void setActive(bool active);
    /// Fetch the first and last part early for this file (MFC GetPreviewPrio).
    [[nodiscard]] bool previewPrio() const { return m_previewPrio; }
    void setPreviewPrio(bool on) { m_previewPrio = on; }
    /// Pause once a preview is possible; cleared when it fires (MFC
    /// IsPausingOnPreview: only for a previewable file that can be paused).
    [[nodiscard]] bool isPausingOnPreview() const;
    void setPauseOnPreview(bool on) { m_pauseOnPreview = on; }
    /// A movie or an archive (MFC IsPreviewableFileType).
    [[nodiscard]] bool isPreviewableFileType() const;
    /// When every part last had a source at the same time; 0 = never.
    [[nodiscard]] time_t lastSeenComplete() const { return m_lastSeenComplete; }

    // -- Priority -------------------------------------------------------------

    [[nodiscard]] uint8 downPriority() const { return m_downPriority; }
    void setDownPriority(uint8 priority);
    [[nodiscard]] bool isAutoDownPriority() const { return m_autoDownPriority; }
    void setAutoDownPriority(bool flag) { m_autoDownPriority = flag; }
    void updateAutoDownPriority();

    static bool rightFileHasHigherPrio(const PartFile* left, const PartFile* right);

    // -- Source tracking ------------------------------------------------------

    [[nodiscard]] int sourceCount() const { return static_cast<int>(m_srcList.size()); }
    [[nodiscard]] int a4afSourceCount() const { return static_cast<int>(m_a4afSrcList.size()); }

    // MFC: m_ClientSrcAnswered — file-level timestamp for source exchange throttling
    [[nodiscard]] uint64 lastAnsweredTime() const { return m_clientSrcAnswered; }
    void setLastAnsweredTime() { m_clientSrcAnswered = getTickCount(); }
    void setLastAnsweredTimeTimeout() { m_clientSrcAnswered = getTickCount() + 2 * CONNECTION_LATENCY - SOURCECLIENTREASKS; }
    [[nodiscard]] const std::vector<UpDownClient*>& srcList() const { return m_srcList; }
    [[nodiscard]] bool hasSource(const UpDownClient* client) const { return m_srcIndex.contains(client); }
    /// The source that is @p candidate itself or the same peer as it (isSamePeer), or null.
    [[nodiscard]] UpDownClient* findSourceLike(const UpDownClient* candidate) const;
    /// The same, by walking the whole list. Only to check the index against.
    [[nodiscard]] UpDownClient* findSourceLikeByScan(const UpDownClient* candidate) const;
    /// The duplicate test of DownloadQueue::checkAndAddSource — MFC DownloadQueue.cpp:488-505.
    [[nodiscard]] static bool isSamePeer(const UpDownClient* source, const UpDownClient* candidate);
    /// @p client's hash, address, ID or port changed: file it under the new ones.
    void rekeySource(UpDownClient* client) { m_srcIndex.rekey(client); }
    /// Empty the source list without touching the clients or telling anyone. Tests.
    void forgetAllSources();
    /// isSamePeer() calls made by findSourceLike() so far, all files. Tests.
    [[nodiscard]] static uint64 sourceCompareCount() { return s_sourceCompares; }
    [[nodiscard]] const std::vector<UpDownClient*>& a4afSrcList() const { return m_a4afSrcList; }
    [[nodiscard]] std::vector<UpDownClient*>& a4afSrcList() { return m_a4afSrcList; }

    void addSource(UpDownClient* client);
    void removeSource(UpDownClient* client);
    void addDownloadingSource(UpDownClient* client);
    void removeDownloadingSource(UpDownClient* client);
    [[nodiscard]] int transferringSrcCount() const { return static_cast<int>(m_downloadingSources.size()); }
    [[nodiscard]] const std::vector<UpDownClient*>& downloadingSources() const { return m_downloadingSources; }

    /// Sources currently usable for this download — OnQueue + Downloading.
    /// MFC CPartFile::GetAvailableSrcCount() (PartFile.cpp:3179); MorphXT's Save/Load Sources
    /// uses it to decide whether a file is rare enough to be worth remembering.
    [[nodiscard]] int availableSourceCount() const;

    /// Sources that answered us at all — OnQueue, Downloading, Connected or
    /// RemoteQueueFull. MFC CPartFile::GetValidSourcesCount() (PartFile.cpp:2116);
    /// isSourceRequestAllowed() weighs it against the raw source count.
    [[nodiscard]] int validSourcesCount() const;
    /// Would start a Kad source search now, were it this file's turn.
    [[nodiscard]] bool wantsKadSourceSearch(uint64 curTick) const;

    /// The file's own source limit, 0 = the global one (MFC m_uMaxSources).
    [[nodiscard]] uint32 privateMaxSources() const { return m_privateMaxSources; }
    void setPrivateMaxSources(uint32 limit) { m_privateMaxSources = limit; }
    /// The limit in force: the file's own in advanced mode, else the pref
    /// (MFC CPartFile::GetMaxSources).
    [[nodiscard]] uint32 maxSources() const;
    /// Source caps derived from maxSources().
    /// MFC CPartFile::GetMaxSourcePerFileSoft/UDP (PartFile.cpp:5349-5359).
    [[nodiscard]] uint32 maxSourcePerFileSoft() const;
    [[nodiscard]] uint32 maxSourcePerFileUDP() const;

    /// Waiting in DownloadQueue for the next OP_GETSOURCES frame to our server.
    [[nodiscard]] bool isLocalSrcReqQueued() const { return m_localSrcReqQueued; }
    void setLocalSrcReqQueued(bool queued) { m_localSrcReqQueued = queued; }
    /// Tick our server was last asked for sources; 0 = ask as soon as possible.
    [[nodiscard]] uint64 lastSearchTimeServer() const { return m_lastSearchTimeServer; }
    void setLastSearchTimeServer(uint64 tick) { m_lastSearchTimeServer = tick; }

    /// Save/Load Sources driver for this file. Held per file so its resave/reload timers and
    /// their jitter stay independent — mirrors MorphXT CPartFile::m_sourcesaver.
    [[nodiscard]] SourceSaver& sourceSaver() { return m_sourceSaver; }

    [[nodiscard]] uint32 datarate() const { return m_datarate; }
    [[nodiscard]] uint64 transferred() const { return m_transferred; }

    // -- Persistence ----------------------------------------------------------

    bool createPartFile(const QString& tempDir);
    PartFileLoadResult loadPartFile(const QString& directory, const QString& filename);
    bool savePartFile();

    // -- Process (periodic tick) ----------------------------------------------

    uint32 process(uint32 reduceDownload, uint32 counter);

    // -- Protocol helpers -----------------------------------------------------

    void writePartStatus(SafeMemFile& file) const;
    void writeCompleteSourcesCount(SafeMemFile& file) const;
    void getFilledArray(std::vector<Gap>& filled) const;

    // -- Source exchange (SX2) ------------------------------------------------

    std::unique_ptr<Packet> createSrcInfoPacket(const UpDownClient* forClient,
                                                 uint8 version, uint16 options) const override;

    /// Build the OP_GETSOURCES(_OBFU) packet asking the connected server for this
    /// file's sources. For a large file (>4 GiB) the size is sent as a zero uint32
    /// marker followed by a uint64, matching the ed2k protocol (and eNode's parser);
    /// otherwise a plain uint32. Extracted from process() so it can be unit-tested.
    [[nodiscard]] std::unique_ptr<Packet> createServerSourceRequestPacket(bool obfuscated) const;

    /// Parse an incoming source answer. `isSX2` selects the wire dialect: SX1 has no
    /// version byte and its real version is inferred from the record size, whereas SX2
    /// states its version up front and must match it exactly.
    void addClientSources(SafeMemFile& data, uint8 clientSXVersion, bool isSX2,
                          const UpDownClient* sender);

    // -- Category -------------------------------------------------------------

    [[nodiscard]] uint32 category() const { return m_category; }
    void setCategory(uint32 cat) { m_category = cat; }

    // -- Misc -----------------------------------------------------------------

    /// 0 while nothing was ever received (MFC GetLastReceptionDate). completedSize()
    /// covers part files written before m_transferred was kept.
    [[nodiscard]] time_t lastReceptionDate() const
    {
        return (m_transferred > 0 || completedSize() > 0) ? m_tLastModified : 0;
    }
    [[nodiscard]] time_t createdDate() const { return m_tCreated; }
    [[nodiscard]] const std::vector<uint16>& srcPartFrequency() const { return m_srcPartFrequency; }
    std::vector<uint16>& srcPartFrequency() { return m_srcPartFrequency; }
    [[nodiscard]] const std::list<Requested_Block_Struct*>& requestedBlockList() const { return m_requestedBlocks; }
    [[nodiscard]] const std::vector<uint16>& corruptedParts() const { return m_corruptedParts; }
    /// True while @p partNumber has failed a hash check and has not been recovered.
    [[nodiscard]] bool isCorruptedPart(uint32 partNumber) const;

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;

    /// Rebuild source part frequencies and the complete-source estimate from our
    /// download sources. MFC CPartFile::UpdatePartsInfo (PartFile.cpp:2558).
    void updatePartsInfo() override;

    // AICH recovery
    /// Sources that said they do not have this file (MFC m_DeadSourceList).
    [[nodiscard]] DeadSourceList& deadSourceList() { return m_deadSourceList; }

    [[nodiscard]] AICHRecoveryHashSet& aichRecoveryHashSet() { return m_aichRecoveryHashSet; }
    [[nodiscard]] const AICHRecoveryHashSet& aichRecoveryHashSet() const { return m_aichRecoveryHashSet; }
    [[nodiscard]] bool isMD4HashsetNeeded() const { return m_md4HashsetNeeded; }
    void setMD4HashsetNeeded(bool val) { m_md4HashsetNeeded = val; }
    [[nodiscard]] bool isAICHPartHashsetNeeded() const { return m_aichPartHashsetNeeded; }
    void setAICHPartHashsetNeeded(bool val) { m_aichPartHashsetNeeded = val; }
    /// Adopt the identifier's AICH root hash as a Verified recovery master hash.
    /// Call whenever an AICH hash arrives with a link or a search result.
    void seedAICHRecoveryMasterHash();
    /// One signer's word for the AICH root. A root that becomes Trusted by it is
    /// written to the identifier and the .part.met, so it survives a restart.
    void voteAICHRoot(const AICHHash& root, const Address& from);
    void requestAICHRecovery(uint32 partNumber);
    void aichRecoveryDataAvailable(uint32 partNumber);

    // Preview
    [[nodiscard]] bool isPreviewPossible() const;

    /// Fake-file verdict from the first bytes, the names the sources and earlier
    /// searches gave the file, comments and ratings. Cached for a few seconds: the
    /// download list asks on every poll.
    [[nodiscard]] const FakeFileVerdict& fakeVerdict() const;
    /// Names the search result this download came from was seen under.
    void addObservedNames(const QStringList& names);

    // Archive recovery state
    [[nodiscard]] bool isRecoveringArchive() const { return m_recoveringArchive; }
    void setRecoveringArchive(bool val) { m_recoveringArchive = val; }

protected:
    /// A download is judged as soon as its first bytes land, not at completion:
    /// the sooner a fake is named the less bandwidth it costs. Reads the `.part`
    /// file, which is laid out at the same offsets as the finished one.
    [[nodiscard]] bool readContainerHead(QByteArray& head) const override;

private:
    void initPartFile();
    void unlinkA4AFSources();
    /// @p alreadyVerified skips the re-read of the data (a rehash has just done it).
    void completeFile(bool alreadyVerified = false);
    /// Take over the AICH recovery set the move thread stored in known2.
    void adoptCompletedAICHHashSet();
    void performFileMove(const QString& srcPath, const QString& destPath, bool verify);
    /// `given`: the part's digest when a worker already read it; else read here.
    PartVerdict hashSinglePart(uint32 partNumber, bool* aichAgreed = nullptr,
                               const PartDigest* given = nullptr);
    [[nodiscard]] PartDigestRequest digestRequest(uint32 partNumber);
    [[nodiscard]] QString partFilePath() const;
    void finishPendingFlush(bool forceICH, bool noAICH);
    void resumeIdleSources();
    /// completeFile(), unless a changed part could not be read back for its check.
    void completeIfVerified();
    /// Flag the parts [start, end] touches for the next verification pass.
    void markChangedParts(uint64 start, uint64 end);
    /// MD4/AICH/ICH check of the changed parts only (MFC FlushBuffer's part loop).
    void verifyChangedParts(bool forceICH, bool noAICH,
                            const std::map<uint32, PartDigest>* digests = nullptr);
    /// Take @p partNumber off the corrupted list; true if it was on it.
    bool dropCorruptedPart(uint32 partNumber);
    /// Report — at most once per kKadSkipLogInterval — why process() wanted a Kad
    /// source search for this file but did not start one.
    void logKadSourceSearchSkipped(uint64 curTick, const QString& reason);
    /// Ask the blackbox who ruined  part and ban whoever is over the threshold.
    void punishCorruptionSenders(uint16 part);
    /// Condemn a whole part in EMBLOCKSIZE steps, which is the only granularity
    /// CorruptionBlackBox::corruptedData() accepts.
    void markPartCorrupted(uint32 partNumber);
    /// Trim [start, end] to a range nobody has requested or buffered; false if none is left.
    [[nodiscard]] bool shrinkToAvoidAlreadyRequested(uint64& start, uint64& end) const;
    /// May @p sender take [start, end] although another source holds it? See EndPhase.
    [[nodiscard]] bool maySecondSourceTake(const UpDownClient* sender, uint64 start, uint64 end,
                                           EndPhase phase) const;

    // -- Private members ------------------------------------------------------

    PartFileNotifier m_partNotifier;

    // File identity
    QString m_partMetFilename;
    QString m_fullName;
    QString m_tmpPath;

    // Gap management
    std::list<Gap> m_gapList;

    // Buffered write data
    std::list<BufferedData> m_bufferedData;
    std::list<BufferedData> m_flushingData;   // handed to the worker; data moved out
    uint64 m_flushingBytes = 0;
    quint64 m_flushToken = 0;                 // 0: nothing in flight
    uint64 m_totalBufferData = 0;

    // Requested blocks
    std::list<Requested_Block_Struct*> m_requestedBlocks;

    // Source lists
    std::vector<UpDownClient*> m_srcList;
    SourceIndex m_srcIndex;   // over m_srcList, kept by addSource / removeSource
    static inline uint64 s_sourceCompares = 0;
    std::vector<UpDownClient*> m_a4afSrcList;
    std::vector<UpDownClient*> m_downloadingSources;

    // Part frequency and corruption
    std::vector<uint16> m_srcPartFrequency;
    std::vector<uint16> m_corruptedParts;
    CorruptionBlackBox m_corruptionBlackBox;
    DeadSourceList m_deadSourceList;
    // Parts written since the last verification (MFC m_aChangedPart)
    uint64 m_rehashToken = 0;
    static inline uint64 s_rehashSerial = 0;
    std::vector<bool> m_changedParts;
    // Complete parts checked without a hashset; re-checked once one arrives
    std::vector<bool> m_partsAwaitingHashset;

    // Open file handle for .part file
    QFile m_partFileHandle;

    // Progress tracking
    EMFileSize m_completedSize = 0;
    float m_percentCompleted = 0.0f;

    // Transfer stats
    uint64 m_transferred = 0;
    uint64 m_corruptionLoss = 0;
    uint64 m_compressionGain = 0;
    uint32 m_datarate = 0;

    // Status
    PartFileStatus m_status = PartFileStatus::Empty;
    PartFileOp m_fileOp = PartFileOp::None;
    uint32 m_category = 0;

    // Priority
    uint8 m_downPriority = kPrNormal;
    bool m_autoDownPriority = true;

    // State flags
    bool m_paused = false;
    bool m_destroying = false;   // set first thing in the destructor
    bool m_stopped = false;
    bool m_insufficient = false;
    bool m_completionError = false;
    bool m_completionRunning = false;   // verify + move in flight
    QPointer<FileMoveThread> m_moveThread;
    QByteArray m_completedAICHMaster;   // root of the set the move thread stored
    bool m_recoveringArchive = false;

    // Fake-file verdict cache
    QStringList m_observedNames;
    mutable FakeFileVerdict m_fakeVerdict;
    mutable qint64 m_fakeVerdictAt = 0;          // unix secs; 0 = never
    mutable bool m_fakeVerdictHadHead = false;

    // Timestamps
    time_t m_tLastModified = 0;
    time_t m_tCreated = 0;
    time_t m_lastPausePurge = 0;
    uint64 m_lastBufferFlushTime = 0;
    uint64 m_nextMetSaveTime = 0;    // Next scheduled .part.met save (matches MFC m_nNextMetFlushTime)
    uint64 m_lastPurgeTime = 0;
    uint32 m_dlActiveTime = 0;
    time_t m_activated = 0;          // MFC m_tActivated: 0 = clock stopped
    time_t m_lastSeenComplete = 0;
    uint32 m_privateMaxSources = 0;
    bool m_previewPrio = false;
    bool m_pauseOnPreview = false;
    uint64 m_clientSrcAnswered = 0;  // MFC: m_ClientSrcAnswered

    // Save/Load Sources (MorphXT CPartFile::m_sourcesaver)
    SourceSaver m_sourceSaver;

    // Hashset
    bool m_md4HashsetNeeded = true;
    bool m_aichPartHashsetNeeded = true;

    // AICH recovery hashset
    AICHRecoveryHashSet m_aichRecoveryHashSet;

    // Server source search state
    uint64 m_lastSearchTimeServer = 0;   // last asked, not next due
    bool m_localSrcReqQueued = false;

    // Kad source search state
    uint64 m_lastSearchTimeKad = 0;
    uint8  m_totalSearchesKad = 0;
    uint64 m_lastKadSkipLogTime = 0;   // throttle for logKadSourceSearchSkipped()

    // Per-download-state source counts
    std::array<uint32, 17> m_anStates{};
};

} // namespace eMule
