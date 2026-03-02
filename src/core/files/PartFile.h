#pragma once

/// @file PartFile.h
/// @brief In-progress download file — port of MFC CPartFile.
///
/// Core download file with gap management, buffered I/O, status machine,
/// priority, block selection, persistence (.part.met), and source tracking.
/// Inherits KnownFile (non-QObject); uses PartFileNotifier for signals.

#include "files/KnownFile.h"
#include "crypto/AICHHashSet.h"
#include "client/ClientStructs.h"

#include <QFile>
#include <QObject>
#include <QThread>

#include <ctime>
#include <list>
#include <vector>

namespace eMule {

class UpDownClient;
class SafeMemFile;
class FileMoveThread;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class PartFileStatus : uint8 {
    Ready      = 0,
    Empty      = 1,
    Hashing    = 3,
    Error      = 4,
    Insufficient = 5,
    Unknown    = 6,
    Paused     = 7,
    Completing = 8,
    Complete   = 9
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
    FileMoveThread(const QString& srcPath, const QString& destPath,
                   QObject* parent = nullptr);
    void run() override;
signals:
    void moveFinished(bool success, const QString& destPath);
private:
    QString m_srcPath;
    QString m_destPath;
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
    [[nodiscard]] bool isPureGap(uint64 start, uint64 end) const;
    [[nodiscard]] bool isAlreadyRequested(uint64 start, uint64 end) const;
    [[nodiscard]] uint64 totalGapSizeInRange(uint64 start, uint64 end) const;
    [[nodiscard]] uint64 totalGapSizeInPart(uint32 part) const;
    [[nodiscard]] EMFileSize completedSize() const { return m_completedSize; }
    [[nodiscard]] float percentCompleted() const { return m_percentCompleted; }
    void updateCompletedInfos();
    [[nodiscard]] const std::list<Gap>& gapList() const { return m_gapList; }

    // -- Buffered I/O ---------------------------------------------------------

    void writeToBuffer(uint64 transize, const uint8* data,
                       uint64 start, uint64 end,
                       Requested_Block_Struct* block);
    void flushBuffer(bool forceICH = false);

    // -- Block selection ------------------------------------------------------

    bool getNextRequestedBlock(UpDownClient* sender,
                               Requested_Block_Struct** newblocks,
                               int& count);
    bool getNextEmptyBlockInPart(uint32 partNumber,
                                Requested_Block_Struct* reqBlock,
                                uint64 searchFrom = 0) const;
    bool removeBlockFromList(uint64 start, uint64 end);
    void removeAllRequestedBlocks();

    // -- Status machine -------------------------------------------------------

    [[nodiscard]] PartFileStatus status() const { return m_status; }
    void setStatus(PartFileStatus s);
    [[nodiscard]] bool isStopped() const { return m_stopped; }
    [[nodiscard]] bool isPaused() const { return m_paused; }
    [[nodiscard]] bool isInsufficient() const { return m_insufficient; }
    void pauseFile(bool insufficient = false);
    void resumeFile();
    void stopFile(bool cancel = false);
    [[nodiscard]] bool completionError() const { return m_completionError; }

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
    [[nodiscard]] const std::vector<UpDownClient*>& srcList() const { return m_srcList; }
    [[nodiscard]] std::vector<UpDownClient*>& srcList() { return m_srcList; }
    [[nodiscard]] const std::vector<UpDownClient*>& a4afSrcList() const { return m_a4afSrcList; }
    [[nodiscard]] std::vector<UpDownClient*>& a4afSrcList() { return m_a4afSrcList; }

    void addSource(UpDownClient* client);
    void removeSource(UpDownClient* client);
    void addDownloadingSource(UpDownClient* client);
    void removeDownloadingSource(UpDownClient* client);
    [[nodiscard]] int transferringSrcCount() const { return static_cast<int>(m_downloadingSources.size()); }
    [[nodiscard]] const std::vector<UpDownClient*>& downloadingSources() const { return m_downloadingSources; }

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
    void addClientSources(SafeMemFile& data, uint8 version, const UpDownClient* sender);

    // -- Category -------------------------------------------------------------

    [[nodiscard]] uint32 category() const { return m_category; }
    void setCategory(uint32 cat) { m_category = cat; }

    // -- Misc -----------------------------------------------------------------

    [[nodiscard]] time_t lastReceptionDate() const { return m_tLastModified; }
    [[nodiscard]] time_t createdDate() const { return m_tCreated; }
    [[nodiscard]] const std::vector<uint16>& srcPartFrequency() const { return m_srcPartFrequency; }
    std::vector<uint16>& srcPartFrequency() { return m_srcPartFrequency; }
    [[nodiscard]] const std::vector<uint16>& corruptedParts() const { return m_corruptedParts; }

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;

    // AICH recovery
    [[nodiscard]] AICHRecoveryHashSet& aichRecoveryHashSet() { return m_aichRecoveryHashSet; }
    [[nodiscard]] const AICHRecoveryHashSet& aichRecoveryHashSet() const { return m_aichRecoveryHashSet; }
    [[nodiscard]] bool isAICHPartHashsetNeeded() const { return m_aichPartHashsetNeeded; }
    void setAICHPartHashsetNeeded(bool val) { m_aichPartHashsetNeeded = val; }
    void requestAICHRecovery(uint32 partNumber);
    void aichRecoveryDataAvailable(uint32 partNumber);

    // Archive recovery state
    [[nodiscard]] bool isRecoveringArchive() const { return m_recoveringArchive; }
    void setRecoveringArchive(bool val) { m_recoveringArchive = val; }

private:
    void initPartFile();
    void completeFile();
    void performFileMove(const QString& srcPath, const QString& destPath);
    bool hashSinglePart(uint32 partNumber, bool* aichAgreed = nullptr);

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
    uint64 m_totalBufferData = 0;

    // Requested blocks
    std::list<Requested_Block_Struct*> m_requestedBlocks;

    // Source lists
    std::vector<UpDownClient*> m_srcList;
    std::vector<UpDownClient*> m_a4afSrcList;
    std::vector<UpDownClient*> m_downloadingSources;

    // Part frequency and corruption
    std::vector<uint16> m_srcPartFrequency;
    std::vector<uint16> m_corruptedParts;

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
    bool m_stopped = false;
    bool m_insufficient = false;
    bool m_completionError = false;
    bool m_recoveringArchive = false;

    // Timestamps
    time_t m_tLastModified = 0;
    time_t m_tCreated = 0;
    time_t m_lastPausePurge = 0;
    uint32 m_lastBufferFlushTime = 0;
    uint32 m_lastPurgeTime = 0;
    uint32 m_dlActiveTime = 0;

    // Hashset
    bool m_md4HashsetNeeded = true;
    bool m_aichPartHashsetNeeded = true;

    // AICH recovery hashset
    AICHRecoveryHashSet m_aichRecoveryHashSet;

    // Per-download-state source counts
    std::array<uint32, 17> m_anStates{};
};

} // namespace eMule
