#pragma once

/// @file StatsSnapshot.h
/// @brief One reading of every number the statistics tree shows.
///
/// This used to be assembled inline in the daemon's GetStats handler, which meant
/// core could not reach it and nothing else could reuse it. Collection lives here
/// now; the daemon only serialises.
///
/// Field names are the wire keys: `toCborMap()` maps each one to a key of the same
/// name, and the GUI's StatisticsPanel::updateTree reads them by that name.
///
/// Every field is emitted, even when the subsystem behind it is absent — the sole
/// exception is freeTempSpace, whose storage path can genuinely be unreadable at
/// runtime. A reader that finds a key missing treats it as 0.

#include "stats/Statistics.h"
#include "utils/Types.h"

#include <QString>

#include <array>
#include <optional>
#include <vector>

class QCborMap;

namespace eMule {

/// One mod string within a client version, e.g. "MorphXT" x 12.
struct ClientModStat {
    QString name;
    int count = 0;
};

/// One client version within a software family, e.g. "v0.50a" x 340.
struct ClientVersionStat {
    QString label;
    int count = 0;
    std::vector<ClientModStat> mods;   ///< eMule-family clients only
};

/// One software family in the known-clients breakdown, e.g. "eMule" x 1200.
struct ClientSoftStat {
    QString name;
    int count = 0;
    std::vector<ClientVersionStat> versions;
};

/// Every value the statistics tree displays, read at one instant.
struct StatsSnapshot {
    // --- Session bytes & uptime ---
    qint64 sessionSentBytes = 0;
    qint64 sessionReceivedBytes = 0;
    qint64 sessionSentBytesToFriend = 0;
    qint64 uptime = 0;
    qint64 startTime = 0;

    // --- Current rates (KB/s) ---
    double rateDown = 0.0;
    double rateUp = 0.0;
    double upOverheadRate = 0.0;
    double downOverheadRate = 0.0;
    double maxDown = 0.0;
    double maxUp = 0.0;
    double maxDownAvg = 0.0;
    double maxUpAvg = 0.0;

    // --- Averages ---
    double avgDownSession = 0.0;
    double avgUpSession = 0.0;
    double avgDownTime = 0.0;
    double avgUpTime = 0.0;
    double cumDownAvg = 0.0;
    double cumUpAvg = 0.0;
    double maxCumDown = 0.0;
    double maxCumUp = 0.0;
    double maxCumDownAvg = 0.0;
    double maxCumUpAvg = 0.0;

    // --- Transfer times (seconds) ---
    qint64 transferTime = 0;
    qint64 uploadTime = 0;
    qint64 downloadTime = 0;
    qint64 serverDuration = 0;

    // --- Global state ---
    qint64 reconnects = 0;
    qint64 filteredClients = 0;

    // --- Download overhead (bytes + packets) ---
    qint64 downOverheadTotal = 0;
    qint64 downOverheadTotalPackets = 0;
    qint64 downOverheadFileReq = 0;
    qint64 downOverheadFileReqPkt = 0;
    qint64 downOverheadSrcExch = 0;
    qint64 downOverheadSrcExchPkt = 0;
    qint64 downOverheadServer = 0;
    qint64 downOverheadServerPkt = 0;
    qint64 downOverheadKad = 0;
    qint64 downOverheadKadPkt = 0;

    // --- Upload overhead (bytes + packets) ---
    qint64 upOverheadTotal = 0;
    qint64 upOverheadTotalPackets = 0;
    qint64 upOverheadFileReq = 0;
    qint64 upOverheadFileReqPkt = 0;
    qint64 upOverheadSrcExch = 0;
    qint64 upOverheadSrcExchPkt = 0;
    qint64 upOverheadServer = 0;
    qint64 upOverheadServerPkt = 0;
    qint64 upOverheadKad = 0;
    qint64 upOverheadKadPkt = 0;

    // --- Upload queue ---
    qint64 upDatarate = 0;
    qint64 upFriendDatarate = 0;
    qint64 upSuccessful = 0;
    qint64 upFailed = 0;
    qint64 upWaiting = 0;
    qint64 upQueueLength = 0;
    qint64 upAvgTime = 0;

    // --- Download queue ---
    qint64 downDatarate = 0;
    qint64 downFileCount = 0;
    qint64 completedDownloads = 0;
    qint64 downFoundSources = 0;
    qint64 downUdpReasks = 0;         ///< MFC CDownloadQueue::GetUDPFileReasks()
    qint64 downUdpReasksFailed = 0;   ///< MFC CDownloadQueue::GetFailedUDPFileReasks()

    /// Free space on the incoming directory; absent when the path is unreadable.
    std::optional<qint64> freeTempSpace;

    // --- Connections ---
    qint64 connActive = 0;
    qint64 connPeak = 0;
    qint64 connMaxReached = 0;
    double connAverage = 0.0;
    qint64 connOpen = 0;

    // --- Servers ---
    qint64 srvWorking = 0;
    qint64 srvFailed = 0;
    qint64 srvTotal = 0;
    qint64 srvUsers = 0;
    qint64 srvFiles = 0;
    qint64 srvLowIDUsers = 0;

    // --- Clients ---
    qint64 knownClients = 0;
    qint64 bannedClients = 0;
    qint64 lowIDClients = 0;    ///< MFC CClientList::GetStatistics stats[14]
    std::vector<ClientSoftStat> clientSoftwareStats;   ///< sorted by count, descending

    // --- Shared files ---
    qint64 sharedCount = 0;
    qint64 sharedSize = 0;
    qint64 sharedLargest = 0;

    // --- Cumulative transfer totals ---
    qint64 cumTotalUp = 0;
    qint64 cumTotalDown = 0;
    qint64 cumTotalUpFriend = 0;

    // --- Per-client breakdown (session and cumulative) ---
    std::array<qint64, Statistics::kUpClientCount> sesUpByClient{};
    std::array<qint64, Statistics::kDownClientCount> sesDownByClient{};
    std::array<qint64, Statistics::kUpClientCount> cumUpByClient{};
    std::array<qint64, Statistics::kDownClientCount> cumDownByClient{};

    // --- Per-port breakdown ---
    qint64 sesUpPort4662 = 0;
    qint64 sesUpPortOther = 0;
    qint64 sesDownPort4662 = 0;
    qint64 sesDownPortOther = 0;
    qint64 cumUpPort4662 = 0;
    qint64 cumUpPortOther = 0;
    qint64 cumDownPort4662 = 0;
    qint64 cumDownPortOther = 0;

    // --- Per-source breakdown (upload only) ---
    qint64 sesUpFromFile = 0;
    qint64 sesUpFromPartfile = 0;
    qint64 cumUpFromFile = 0;
    qint64 cumUpFromPartfile = 0;

    // --- HTTP Cache ---
    qint64 sesHttpCachePublished = 0;   ///< ciphertext bytes pushed to the cache
    qint64 sesHttpCacheFetched = 0;     ///< plaintext bytes pulled back out
    qint64 sesHttpCacheSaved = 0;       ///< upstream not spent thanks to it
    qint64 sesHttpCacheChunksUp = 0;
    qint64 sesHttpCacheChunksDown = 0;
    qint64 cumHttpCachePublished = 0;
    qint64 cumHttpCacheFetched = 0;
    qint64 cumHttpCacheSaved = 0;
    qint64 cumHttpCacheChunksUp = 0;
    qint64 cumHttpCacheChunksDown = 0;

    // --- Cumulative sessions ---
    qint64 cumUpSuccessful = 0;
    qint64 cumUpFailed = 0;
    qint64 cumUpAvgTime = 0;
    qint64 cumDownSuccessful = 0;
    qint64 cumDownFailed = 0;
    qint64 cumDownAvgTime = 0;
    qint64 cumDownCompletedFiles = 0;

    // --- Cumulative overhead — upload ---
    qint64 cumUpOhTotal = 0;
    qint64 cumUpOhTotalPkt = 0;
    qint64 cumUpOhFileReq = 0;
    qint64 cumUpOhFileReqPkt = 0;
    qint64 cumUpOhSrcExch = 0;
    qint64 cumUpOhSrcExchPkt = 0;
    qint64 cumUpOhServer = 0;
    qint64 cumUpOhServerPkt = 0;
    qint64 cumUpOhKad = 0;
    qint64 cumUpOhKadPkt = 0;

    // --- Cumulative overhead — download ---
    qint64 cumDownOhTotal = 0;
    qint64 cumDownOhTotalPkt = 0;
    qint64 cumDownOhFileReq = 0;
    qint64 cumDownOhFileReqPkt = 0;
    qint64 cumDownOhSrcExch = 0;
    qint64 cumDownOhSrcExchPkt = 0;
    qint64 cumDownOhServer = 0;
    qint64 cumDownOhServerPkt = 0;
    qint64 cumDownOhKad = 0;
    qint64 cumDownOhKadPkt = 0;

    // --- Cumulative connections ---
    qint64 cumConnPeak = 0;
    qint64 cumConnMaxLimitReached = 0;
    qint64 cumConnReconnects = 0;

    // --- Cumulative times ---
    qint64 cumRunTime = 0;
    qint64 cumTransferTime = 0;
    qint64 cumUploadTime = 0;
    qint64 cumDownloadTime = 0;
    qint64 cumServerDuration = 0;

    // --- Compression / corruption / ICH ---
    qint64 sesCompressionGain = 0;
    qint64 sesCorruptionLoss = 0;
    qint64 sesIchPartsSaved = 0;
    qint64 cumCompressionGain = 0;
    qint64 cumCorruptionLoss = 0;
    qint64 cumIchPartsSaved = 0;

    // --- Total downloads ---
    qint64 totalDownCount = 0;
    qint64 totalDownSize = 0;
    qint64 totalDownDone = 0;
    qint64 totalDownLeft = 0;

    /// Wall clock of the last statistics reset; 0 = never reset.
    qint64 statsLastReset = 0;
    /// Seconds since that reset, measured on the daemon's clock so a GUI on
    /// another machine does not report the skew between the two.
    qint64 timeSinceReset = 0;
    /// Whether a reset can still be undone. It rides along with the stats a GUI
    /// already polls, so the Restore Statistics menu item can grey itself without
    /// a round trip of its own.
    bool statsBackupAvailable = false;

    // --- Records ---
    qint64 recMaxWorkingServers = 0;
    qint64 recMaxUsersOnline = 0;
    qint64 recMaxFilesAvail = 0;
    qint64 recMaxSharedFiles = 0;
    qint64 recMaxSharedSize = 0;
    qint64 recMaxAvgFileSize = 0;
    qint64 recMaxLargestFile = 0;
};

/// Read every statistics value from the running core.
///
/// Not purely a read: the rec* records in Preferences are raised first when the
/// current session has beaten them, because the snapshot then reports them. That is
/// the same order the daemon handler used before this moved.
[[nodiscard]] StatsSnapshot collectStatsSnapshot();

/// Gather the session counters that feed the cumulative totals but live outside
/// Statistics (the upload/download queues and the listen socket).
[[nodiscard]] Statistics::ExternalSessionCounters collectExternalSessionCounters();

/// Bank this session's contribution into the cumulative counters in @p prefs.
/// Idempotent — see Statistics::flushCumulativeToPrefs — so it is safe to call on
/// a timer as well as at shutdown. Does not write the file.
void flushCumulativeStats(Preferences& prefs);

/// Serialise a snapshot to the wire map. One key per field, named after the field.
[[nodiscard]] QCborMap toCborMap(const StatsSnapshot& s);

} // namespace eMule
