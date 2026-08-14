#pragma once

/// @file Statistics.h
/// @brief Global transfer statistics — replaces MFC CStatistics.
///
/// QObject-based manager for transfer rates, overhead tracking,
/// session counters, and transfer time accumulation.  Provides
/// getters and Qt signals for GUI consumption.

#include "utils/Types.h"

#include <QObject>

#include <array>
#include <atomic>
#include <list>

namespace eMule {

class Preferences;
enum class ClientSoftware : uint8;

/// Averaging mode for download/upload rate calculation.
enum class AverageType : uint8 {
    Session = 0,  ///< Session average (total bytes / session time)
    Time    = 1,  ///< Time-windowed average (based on history ring buffer)
    Total   = 2   ///< Cumulative average (blend of session and all-time)
};

// ---------------------------------------------------------------------------
// Statistics — QObject-based transfer statistics manager
// ---------------------------------------------------------------------------

class Statistics : public QObject {
    Q_OBJECT

public:
    explicit Statistics(QObject* parent = nullptr);
    ~Statistics() override;

    /// Load cumulative max/avg rates from preferences.
    void init(Preferences& prefs);

    /// Record current transfer history sample for time-windowed averaging.
    void recordRate();

    /// Calculate average download rate (KB/s) using the given averaging mode.
    [[nodiscard]] float avgDownloadRate(AverageType type) const;

    /// Calculate average upload rate (KB/s) using the given averaging mode.
    [[nodiscard]] float avgUploadRate(AverageType type) const;

    /// Update connection statistics with current instantaneous rates.
    /// Called periodically (e.g., every second) by the main timer.
    void updateConnectionStats(float uploadRate, float downloadRate);

    // --- Transfer time getters (seconds) ---

    [[nodiscard]] uint32 transferTime() const;
    [[nodiscard]] uint32 uploadTime() const;
    [[nodiscard]] uint32 downloadTime() const;
    [[nodiscard]] uint32 serverDuration() const;
    void add2TotalServerDuration();

    // --- Current rate getters (KB/s) ---

    [[nodiscard]] float rateDown() const { return m_rateDown; }
    [[nodiscard]] float rateUp() const { return m_rateUp; }
    [[nodiscard]] float maxDown() const { return m_maxDown; }
    [[nodiscard]] float maxUp() const { return m_maxUp; }
    [[nodiscard]] float maxDownAvg() const { return m_maxDownAvg; }
    [[nodiscard]] float maxUpAvg() const { return m_maxUpAvg; }

    // --- Cumulative (cross-session) rate getters ---

    [[nodiscard]] float cumDownAvg() const { return m_cumDownAvg; }
    [[nodiscard]] float cumUpAvg() const { return m_cumUpAvg; }
    [[nodiscard]] float maxCumDown() const { return m_maxCumDown; }
    [[nodiscard]] float maxCumUp() const { return m_maxCumUp; }
    [[nodiscard]] float maxCumDownAvg() const { return m_maxCumDownAvg; }
    [[nodiscard]] float maxCumUpAvg() const { return m_maxCumUpAvg; }

    // --- Download overhead ---

    void compDownDatarateOverhead();
    void resetDownDatarateOverhead();

    void addDownDataOverheadSourceExchange(uint32 data);
    void addDownDataOverheadFileRequest(uint32 data);
    void addDownDataOverheadServer(uint32 data);
    void addDownDataOverheadKad(uint32 data);
    void addDownDataOverheadOther(uint32 data);

    [[nodiscard]] uint64 downDatarateOverhead() const { return m_downDatarateOverhead; }
    [[nodiscard]] uint64 downDataOverheadSourceExchange() const { return m_downOverheadSourceExchange.load(); }
    [[nodiscard]] uint64 downDataOverheadFileRequest() const { return m_downOverheadFileRequest.load(); }
    [[nodiscard]] uint64 downDataOverheadServer() const { return m_downOverheadServer.load(); }
    [[nodiscard]] uint64 downDataOverheadKad() const { return m_downOverheadKad.load(); }
    [[nodiscard]] uint64 downDataOverheadOther() const { return m_downOverheadOther.load(); }
    [[nodiscard]] uint64 downDataOverheadSourceExchangePackets() const { return m_downOverheadSourceExchangePackets.load(); }
    [[nodiscard]] uint64 downDataOverheadFileRequestPackets() const { return m_downOverheadFileRequestPackets.load(); }
    [[nodiscard]] uint64 downDataOverheadServerPackets() const { return m_downOverheadServerPackets.load(); }
    [[nodiscard]] uint64 downDataOverheadKadPackets() const { return m_downOverheadKadPackets.load(); }
    [[nodiscard]] uint64 downDataOverheadOtherPackets() const { return m_downOverheadOtherPackets.load(); }

    // --- Upload overhead ---

    void compUpDatarateOverhead();
    void resetUpDatarateOverhead();

    void addUpDataOverheadSourceExchange(uint32 data);
    void addUpDataOverheadFileRequest(uint32 data);
    void addUpDataOverheadServer(uint32 data);
    void addUpDataOverheadKad(uint32 data);
    void addUpDataOverheadOther(uint32 data);

    [[nodiscard]] uint64 upDatarateOverhead() const { return m_upDatarateOverhead; }
    [[nodiscard]] uint64 upDataOverheadSourceExchange() const { return m_upOverheadSourceExchange.load(); }
    [[nodiscard]] uint64 upDataOverheadFileRequest() const { return m_upOverheadFileRequest.load(); }
    [[nodiscard]] uint64 upDataOverheadServer() const { return m_upOverheadServer.load(); }
    [[nodiscard]] uint64 upDataOverheadKad() const { return m_upOverheadKad.load(); }
    [[nodiscard]] uint64 upDataOverheadOther() const { return m_upOverheadOther.load(); }
    [[nodiscard]] uint64 upDataOverheadSourceExchangePackets() const { return m_upOverheadSourceExchangePackets.load(); }
    [[nodiscard]] uint64 upDataOverheadFileRequestPackets() const { return m_upOverheadFileRequestPackets.load(); }
    [[nodiscard]] uint64 upDataOverheadServerPackets() const { return m_upOverheadServerPackets.load(); }
    [[nodiscard]] uint64 upDataOverheadKadPackets() const { return m_upOverheadKadPackets.load(); }
    [[nodiscard]] uint64 upDataOverheadOtherPackets() const { return m_upOverheadOtherPackets.load(); }

    // --- Session counters ---

    [[nodiscard]] uint64 sessionReceivedBytes() const { return m_sessionReceivedBytes.load(); }
    [[nodiscard]] uint64 sessionSentBytes() const { return m_sessionSentBytes.load(); }
    [[nodiscard]] uint64 sessionSentBytesToFriend() const { return m_sessionSentBytesToFriend.load(); }

    void addSessionReceivedBytes(uint64 bytes);
    void addSessionSentBytes(uint64 bytes);
    void addSessionSentBytesToFriend(uint64 bytes);

    // --- Download quality counters ---
    //
    // MFC keeps these as session counters in Preferences
    // (srchybrid/Preferences.h:750-751) and bumps them where the event happens.
    // They cannot be derived by summing the download queue: a file that leaves
    // the queue would take its contribution with it, and a session total that
    // can go down is useless as the basis for a cumulative one.  The per-file
    // totals stay on PartFile, which persists them in .part.met.

    [[nodiscard]] uint64 sesCompressionGain() const { return m_sesCompressionGain.load(); }
    [[nodiscard]] uint64 sesCorruptionLoss() const { return m_sesCorruptionLoss.load(); }
    [[nodiscard]] uint32 sesIchPartsSaved() const { return m_sesIchPartsSaved.load(); }

    void addCompressionGain(uint64 bytes);
    void addCorruptionLoss(uint64 bytes);
    /// Credit recovered bytes back to the session loss, saturating at 0
    /// (MFC: srchybrid/PartFile.cpp:4222).
    void subCorruptionLoss(uint64 bytes);
    void addIchPartSaved();

    // --- Global state ---

    [[nodiscard]] uint16 reconnects() const { return m_reconnects; }
    void setReconnects(uint16 val) { m_reconnects = val; }
    void addReconnect() { ++m_reconnects; }

    [[nodiscard]] uint32 filteredClients() const { return m_filteredClients; }
    void setFilteredClients(uint32 val) { m_filteredClients = val; }
    void addFilteredClient() { ++m_filteredClients; }

    /// Monotonic getTickCount() value at which this session started; 0 = not
    /// started yet.  MFC stamps the same thing in CemuleDlg::OnInitDialog
    /// (srchybrid/EmuleDlg.cpp:375) — ours is stamped by init().  It is a tick,
    /// not a wall clock: never subtract it from time(nullptr).
    [[nodiscard]] uint64 startTick() const { return m_startTick; }
    void setStartTick(uint64 val) { m_startTick = val; }

    /// Seconds since the session started; 0 until it has been stamped.
    [[nodiscard]] uint32 uptimeSecs() const;

    [[nodiscard]] uint32 transferStartTime() const { return m_transferStartTime; }
    void setTransferStartTime(uint32 val) { m_transferStartTime = val; }

    [[nodiscard]] uint32 serverConnectTime() const { return m_serverConnectTime; }
    void setServerConnectTime(uint32 val) { m_serverConnectTime = val; }

    // --- Per-client/port/source transfer breakdown ---

    /// Record a data transfer for per-client-type, per-port, per-source tracking.
    /// Called from upload/download client code after each block transfer.
    void addTransferData(ClientSoftware clientType, uint16 port,
                         bool isPartFile, bool isUpload, uint64 bytes);

    /// Map ClientSoftware enum to compact array index 0-7.
    [[nodiscard]] static int clientIndex(ClientSoftware cs);

    /// Number of client types tracked for uploads (eMule..eMCompat).
    static constexpr int kUpClientCount = 7;
    /// Number of client types tracked for downloads (eMule..URL).
    static constexpr int kDownClientCount = 8;

    /// Per-client session bytes — upload
    [[nodiscard]] uint64 sesUpByClient(int idx) const { return (idx >= 0 && idx < kUpClientCount) ? m_sesUpByClient[static_cast<size_t>(idx)].load() : 0; }
    /// Per-client session bytes — download
    [[nodiscard]] uint64 sesDownByClient(int idx) const { return (idx >= 0 && idx < kDownClientCount) ? m_sesDownByClient[static_cast<size_t>(idx)].load() : 0; }

    /// Per-port session bytes
    [[nodiscard]] uint64 sesUpPort4662() const { return m_sesUpPort4662.load(); }
    [[nodiscard]] uint64 sesUpPortOther() const { return m_sesUpPortOther.load(); }
    [[nodiscard]] uint64 sesDownPort4662() const { return m_sesDownPort4662.load(); }
    [[nodiscard]] uint64 sesDownPortOther() const { return m_sesDownPortOther.load(); }

    /// Per-source session bytes (upload only)
    [[nodiscard]] uint64 sesUpFromFile() const { return m_sesUpFromFile.load(); }
    [[nodiscard]] uint64 sesUpFromPartfile() const { return m_sesUpFromPartfile.load(); }

    // --- Cumulative totals ---

    /// Session counters that live outside Statistics but still feed the
    /// cumulative totals.  The caller collects them, so Statistics never has to
    /// reach into theApp.
    struct ExternalSessionCounters {
        uint32 upSuccessfulSessions = 0;
        uint32 upFailedSessions = 0;
        uint32 downSuccessfulSessions = 0;
        uint32 downFailedSessions = 0;
        uint32 downCompletedFiles = 0;
        uint32 connPeak = 0;
        uint32 connMaxLimitReached = 0;
    };

    /// Every cumulative counter the Statistics tree shows.  Each field is the
    /// value read out of Preferences when the session started plus what this
    /// session has added since — so display and flush cannot drift, and writing
    /// it back is idempotent.  Field names mirror the Preferences::cum* getters.
    struct CumulativeTotals {
        // Transfer totals
        uint64 totalUploaded = 0;
        uint64 totalDownloaded = 0;
        uint64 totalUploadedToFriend = 0;

        // Sessions
        uint32 upSuccessfulSessions = 0;
        uint32 upFailedSessions = 0;
        uint32 downSuccessfulSessions = 0;
        uint32 downFailedSessions = 0;
        uint32 downCompletedFiles = 0;

        // Connections
        uint32 connPeak = 0;              ///< a maximum, not a sum
        uint32 connMaxLimitReached = 0;
        uint32 connReconnects = 0;

        // Times (seconds)
        uint64 runTime = 0;
        uint64 transferTime = 0;
        uint64 uploadTime = 0;
        uint64 downloadTime = 0;
        uint64 serverDuration = 0;

        // Download quality
        uint64 compressionGain = 0;
        uint64 corruptionLoss = 0;
        uint32 ichPartsSaved = 0;

        // Upload overhead
        uint64 upOverheadTotal = 0;
        uint64 upOverheadTotalPackets = 0;
        uint64 upOverheadFileReq = 0;
        uint64 upOverheadFileReqPackets = 0;
        uint64 upOverheadSrcExch = 0;
        uint64 upOverheadSrcExchPackets = 0;
        uint64 upOverheadServer = 0;
        uint64 upOverheadServerPackets = 0;
        uint64 upOverheadKad = 0;
        uint64 upOverheadKadPackets = 0;

        // Download overhead
        uint64 downOverheadTotal = 0;
        uint64 downOverheadTotalPackets = 0;
        uint64 downOverheadFileReq = 0;
        uint64 downOverheadFileReqPackets = 0;
        uint64 downOverheadSrcExch = 0;
        uint64 downOverheadSrcExchPackets = 0;
        uint64 downOverheadServer = 0;
        uint64 downOverheadServerPackets = 0;
        uint64 downOverheadKad = 0;
        uint64 downOverheadKadPackets = 0;

        // Per-client / per-port / per-source
        std::array<uint64, kUpClientCount> upByClient{};
        std::array<uint64, kDownClientCount> downByClient{};
        uint64 upPort4662 = 0;
        uint64 upPortOther = 0;
        uint64 downPort4662 = 0;
        uint64 downPortOther = 0;
        uint64 upFromFile = 0;
        uint64 upFromPartfile = 0;
    };

    /// Cumulative totals as of right now — what both the Statistics tree and the
    /// flush must use.  Reading Preferences::cum* directly and adding the session
    /// on top double-counts once the periodic flush has banked it.
    [[nodiscard]] CumulativeTotals cumulativeTotals(const ExternalSessionCounters& ext) const;

    /// Write cumulativeTotals() into @p prefs.  Absolute, not additive: running
    /// it twice changes nothing, which is what lets it run on a timer instead of
    /// only at shutdown.  Does not save the file — the caller decides when.
    void flushCumulativeToPrefs(Preferences& prefs,
                                const ExternalSessionCounters& ext) const;

    /// Re-read the cumulative baseline from @p prefs.  Needed after the totals in
    /// preferences are changed behind our back — i.e. by a statistics reset.
    void rebaseCumulative(const Preferences& prefs);

    // --- Global progress (for taskbar / status) ---

    [[nodiscard]] float globalDone() const { return m_globalDone; }
    void setGlobalDone(float val) { m_globalDone = val; }

    [[nodiscard]] float globalSize() const { return m_globalSize; }
    void setGlobalSize(float val) { m_globalSize = val; }

    [[nodiscard]] uint32 overallStatus() const { return m_overallStatus; }
    void setOverallStatus(uint32 val) { m_overallStatus = val; }

signals:
    /// Emitted after updateConnectionStats() — rates and times updated.
    void statsUpdated();

    /// Emitted after compDown/UpDatarateOverhead() — overhead rates recomputed.
    void overheadStatsUpdated();

    /// Emitted when session byte counters change.
    void sessionBytesChanged();

private:
    /// Internal rate history entry for time-windowed averaging.
    struct RateEntry {
        uint64 dataLen = 0;
        uint32 timestamp = 0;
    };

    Preferences* m_prefs = nullptr;

    // Current rates
    float m_rateDown = 0.0f;
    float m_rateUp = 0.0f;

    // Session max rates
    float m_maxDown = 0.0f;
    float m_maxUp = 0.0f;
    float m_maxDownAvg = 0.0f;
    float m_maxUpAvg = 0.0f;

    // Cumulative (cross-session) rates — loaded from / saved to preferences
    float m_cumDownAvg = 0.0f;
    float m_cumUpAvg = 0.0f;
    float m_maxCumDown = 0.0f;
    float m_maxCumUp = 0.0f;
    float m_maxCumDownAvg = 0.0f;
    float m_maxCumUpAvg = 0.0f;

    // Transfer time tracking (seconds / tick values)
    uint32 m_timeTransfers = 0;
    uint32 m_timeDownloads = 0;
    uint32 m_timeUploads = 0;
    uint32 m_startTimeTransfers = 0;
    uint32 m_startTimeDownloads = 0;
    uint32 m_startTimeUploads = 0;
    uint32 m_timeThisTransfer = 0;
    uint32 m_timeThisDownload = 0;
    uint32 m_timeThisUpload = 0;
    uint32 m_timeServerDuration = 0;
    uint32 m_timeThisServerDuration = 0;

    // Session counters (atomic for thread safety)
    std::atomic<uint64> m_sessionReceivedBytes{0};
    std::atomic<uint64> m_sessionSentBytes{0};
    std::atomic<uint64> m_sessionSentBytesToFriend{0};

    // Global state
    uint16 m_reconnects = 0;
    uint32 m_filteredClients = 0;
    uint64 m_startTick = 0;
    uint32 m_transferStartTime = 0;
    uint32 m_serverConnectTime = 0;

    // Cumulative values as they stood in Preferences when the session started.
    CumulativeTotals m_cumBase;

    // Global progress
    float m_globalDone = 0.0f;
    float m_globalSize = 0.0f;
    uint32 m_overallStatus = 0;

    // Rate history ring buffers
    std::list<RateEntry> m_downRateHistory;
    std::list<RateEntry> m_upRateHistory;

    // Download overhead (atomics for thread-safe accumulation from network threads)
    uint64 m_downDatarateOverhead = 0;
    std::atomic<uint64> m_downDataRateMSOverhead{0};
    std::atomic<uint64> m_downOverheadSourceExchange{0};
    std::atomic<uint64> m_downOverheadSourceExchangePackets{0};
    std::atomic<uint64> m_downOverheadFileRequest{0};
    std::atomic<uint64> m_downOverheadFileRequestPackets{0};
    std::atomic<uint64> m_downOverheadServer{0};
    std::atomic<uint64> m_downOverheadServerPackets{0};
    std::atomic<uint64> m_downOverheadKad{0};
    std::atomic<uint64> m_downOverheadKadPackets{0};
    std::atomic<uint64> m_downOverheadOther{0};
    std::atomic<uint64> m_downOverheadOtherPackets{0};

    // Upload overhead (atomics for thread-safe accumulation)
    uint64 m_upDatarateOverhead = 0;
    std::atomic<uint64> m_upDataRateMSOverhead{0};
    std::atomic<uint64> m_upOverheadSourceExchange{0};
    std::atomic<uint64> m_upOverheadSourceExchangePackets{0};
    std::atomic<uint64> m_upOverheadFileRequest{0};
    std::atomic<uint64> m_upOverheadFileRequestPackets{0};
    std::atomic<uint64> m_upOverheadServer{0};
    std::atomic<uint64> m_upOverheadServerPackets{0};
    std::atomic<uint64> m_upOverheadKad{0};
    std::atomic<uint64> m_upOverheadKadPackets{0};
    std::atomic<uint64> m_upOverheadOther{0};
    std::atomic<uint64> m_upOverheadOtherPackets{0};

    // Overhead averaging lists and sums
    uint64 m_sumAvgDDRO = 0;
    uint64 m_sumAvgUDRO = 0;
    std::list<RateEntry> m_avgDDROList;
    std::list<RateEntry> m_avgUDROList;

    // Per-client session breakdown (atomic for thread safety)
    std::array<std::atomic<uint64>, kUpClientCount> m_sesUpByClient{};
    std::array<std::atomic<uint64>, kDownClientCount> m_sesDownByClient{};

    // Per-port session breakdown
    std::atomic<uint64> m_sesUpPort4662{0};
    std::atomic<uint64> m_sesUpPortOther{0};
    std::atomic<uint64> m_sesDownPort4662{0};
    std::atomic<uint64> m_sesDownPortOther{0};

    // Per-source session breakdown (upload only)
    std::atomic<uint64> m_sesUpFromFile{0};
    std::atomic<uint64> m_sesUpFromPartfile{0};

    // Download quality session counters
    std::atomic<uint64> m_sesCompressionGain{0};
    std::atomic<uint64> m_sesCorruptionLoss{0};
    std::atomic<uint32> m_sesIchPartsSaved{0};
};

} // namespace eMule
