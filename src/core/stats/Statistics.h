#pragma once

/// @file Statistics.h
/// @brief Global transfer statistics — replaces MFC CStatistics.
///
/// QObject-based manager for transfer rates, overhead tracking,
/// session counters, and transfer time accumulation.  Provides
/// getters and Qt signals for GUI consumption.

#include "utils/Types.h"

#include <QObject>

#include <atomic>
#include <list>

namespace eMule {

class Preferences;

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

    // --- Global state ---

    [[nodiscard]] uint16 reconnects() const { return m_reconnects; }
    void setReconnects(uint16 val) { m_reconnects = val; }
    void addReconnect() { ++m_reconnects; }

    [[nodiscard]] uint32 filteredClients() const { return m_filteredClients; }
    void setFilteredClients(uint32 val) { m_filteredClients = val; }
    void addFilteredClient() { ++m_filteredClients; }

    [[nodiscard]] uint32 startTime() const { return m_startTime; }
    void setStartTime(uint32 val) { m_startTime = val; }

    [[nodiscard]] uint32 transferStartTime() const { return m_transferStartTime; }
    void setTransferStartTime(uint32 val) { m_transferStartTime = val; }

    [[nodiscard]] uint32 serverConnectTime() const { return m_serverConnectTime; }
    void setServerConnectTime(uint32 val) { m_serverConnectTime = val; }

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
    uint32 m_startTime = 0;
    uint32 m_transferStartTime = 0;
    uint32 m_serverConnectTime = 0;

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
};

} // namespace eMule
