#include "pch.h"
/// @file StatsSnapshot.cpp
/// @brief Statistics collection and serialisation — implementation.

#include "stats/StatsSnapshot.h"

#include "app/AppContext.h"
#include "httpcache/HttpCacheManager.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "server/ServerList.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"

#include <QCborArray>
#include <QCborMap>
#include <QMap>
#include <QStorageInfo>

#include <algorithm>
#include <ctime>
#include <utility>

namespace eMule {

namespace {

/// Format a client version word the way the statistics tree labels it.
QString formatClientVersion(uint32 ver)
{
    const uint32 maj = ver / (100 * 10 * 100);
    const uint32 mn = (ver - maj * 100 * 10 * 100) / (100 * 10);
    const uint32 upd = (ver - maj * 100 * 10 * 100 - mn * 100 * 10) / 100;
    if (upd == 0 && ver < makeClientVersion(0, 40, 0))
        return QStringLiteral("v%1.%2").arg(maj).arg(mn);
    if (upd < 26)
        return QStringLiteral("v%1.%2%3").arg(maj).arg(mn).arg(QChar('a' + upd));
    return QStringLiteral("v%1.%2.%3").arg(maj).arg(mn).arg(upd);
}

/// Display name for a client software type. eMule/OldEMule and the compat family
/// each collapse onto one label, which is what merges their counts.
QString clientSoftName(int sw)
{
    switch (static_cast<ClientSoftware>(sw)) {
    case ClientSoftware::eMule:
    case ClientSoftware::OldEMule:      return QStringLiteral("eMule");
    case ClientSoftware::eDonkeyHybrid: return QStringLiteral("eD Hybrid");
    case ClientSoftware::eDonkey:       return QStringLiteral("eDonkey");
    case ClientSoftware::aMule:         return QStringLiteral("aMule");
    case ClientSoftware::MLDonkey:      return QStringLiteral("MLdonkey");
    case ClientSoftware::Shareaza:      return QStringLiteral("Shareaza");
    case ClientSoftware::cDonkey:
    case ClientSoftware::xMule:
    case ClientSoftware::lphant:        return QStringLiteral("eM Compat");
    case ClientSoftware::URL:           return QStringLiteral("URL");
    default:                            return QStringLiteral("Unknown");
    }
}

/// Walk the known-client list into the software -> version -> mod breakdown,
/// each level sorted by count descending. @p lowIDOut receives the LowID tally from the
/// same pass — MFC counts it in the one GetStatistics loop too (ClientList.cpp:78,
/// stats[14]) rather than walking the list a second time.
std::vector<ClientSoftStat> collectClientSoftwareStats(const ClientList& clients,
                                                       qint64& lowIDOut)
{
    lowIDOut = 0;
    struct ModInfo {
        QMap<QString, int> mods;          // mod string -> count
        int count = 0;
    };
    struct SoftInfo {
        QMap<uint32, ModInfo> versions;   // version word -> mod breakdown
        int count = 0;
    };

    QMap<int, SoftInfo> softMap;          // ClientSoftware enum -> info
    clients.forEachClient([&](UpDownClient* c) {
        if (c->hasLowID())
            ++lowIDOut;

        const int sw = static_cast<int>(c->clientSoft());
        auto& si = softMap[sw];
        ++si.count;

        auto& vi = si.versions[c->clientVersion()];
        ++vi.count;

        // Mod strings only for eMule-family clients
        const auto cs = c->clientSoft();
        if (cs == ClientSoftware::eMule || cs == ClientSoftware::OldEMule
            || cs == ClientSoftware::cDonkey || cs == ClientSoftware::xMule
            || cs == ClientSoftware::lphant || cs == ClientSoftware::aMule) {
            const QString& mod = c->modVersion();
            ++vi.mods[mod.isEmpty() ? QStringLiteral("Official") : mod];
        }
    });

    // Merge the enum values that share a display name
    QMap<QString, SoftInfo> mergedSoft;
    for (auto it = softMap.cbegin(); it != softMap.cend(); ++it) {
        auto& merged = mergedSoft[clientSoftName(it.key())];
        merged.count += it->count;
        for (auto vit = it->versions.cbegin(); vit != it->versions.cend(); ++vit) {
            auto& mv = merged.versions[vit.key()];
            mv.count += vit->count;
            for (auto mit = vit->mods.cbegin(); mit != vit->mods.cend(); ++mit)
                mv.mods[mit.key()] += mit.value();
        }
    }

    std::vector<ClientSoftStat> out;
    out.reserve(static_cast<size_t>(mergedSoft.size()));
    for (auto it = mergedSoft.cbegin(); it != mergedSoft.cend(); ++it) {
        ClientSoftStat soft;
        soft.name = it.key();
        soft.count = it->count;

        soft.versions.reserve(static_cast<size_t>(it->versions.size()));
        for (auto vit = it->versions.cbegin(); vit != it->versions.cend(); ++vit) {
            ClientVersionStat ver;
            ver.label = formatClientVersion(vit.key());
            ver.count = vit->count;

            ver.mods.reserve(static_cast<size_t>(vit->mods.size()));
            for (auto mit = vit->mods.cbegin(); mit != vit->mods.cend(); ++mit)
                ver.mods.push_back({mit.key(), mit.value()});
            std::sort(ver.mods.begin(), ver.mods.end(),
                      [](const auto& a, const auto& b) { return a.count > b.count; });

            soft.versions.push_back(std::move(ver));
        }
        std::sort(soft.versions.begin(), soft.versions.end(),
                  [](const auto& a, const auto& b) { return a.count > b.count; });

        out.push_back(std::move(soft));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.count > b.count; });

    return out;
}

/// Raise the stored records that this session has beaten. Called before the
/// snapshot reads them back, so a new high shows up in the same reading.
void updateRecords()
{
    if (auto* sl = theApp.serverList) {
        const auto srvStats = sl->stats();
        const uint32 working = srvStats.total - srvStats.failed;
        if (working > thePrefs.recMaxWorkingServers())
            thePrefs.setRecMaxWorkingServers(working);
        if (srvStats.users > thePrefs.recMaxUsersOnline())
            thePrefs.setRecMaxUsersOnline(static_cast<uint32>(srvStats.users));
        if (srvStats.files > thePrefs.recMaxFilesAvail())
            thePrefs.setRecMaxFilesAvail(static_cast<uint32>(srvStats.files));
    }

    if (auto* sf = theApp.sharedFileList) {
        uint64 largest = 0;
        const auto totalSize = static_cast<uint64>(sf->getDataSize(largest));
        const auto count = static_cast<uint64>(sf->getCount());
        if (count > thePrefs.recMaxSharedFiles())
            thePrefs.setRecMaxSharedFiles(count);
        if (totalSize > thePrefs.recMaxSharedSize())
            thePrefs.setRecMaxSharedSize(totalSize);
        if (count > 0 && totalSize / count > thePrefs.recMaxAvgFileSize())
            thePrefs.setRecMaxAvgFileSize(totalSize / count);
        if (largest > thePrefs.recMaxLargestFile())
            thePrefs.setRecMaxLargestFile(largest);
    }
}

// Wire key names for the per-client breakdown arrays.
constexpr const char* kSesUpClientKeys[] = {
    "sesUpEmule", "sesUpEDHybrid", "sesUpEDonkey", "sesUpAMule",
    "sesUpMLdonkey", "sesUpShareaza", "sesUpEMCompat"
};
constexpr const char* kSesDownClientKeys[] = {
    "sesDownEmule", "sesDownEDHybrid", "sesDownEDonkey", "sesDownAMule",
    "sesDownMLdonkey", "sesDownShareaza", "sesDownEMCompat", "sesDownURL"
};
constexpr const char* kCumUpClientKeys[] = {
    "cumUpEmule", "cumUpEDHybrid", "cumUpEDonkey", "cumUpAMule",
    "cumUpMLdonkey", "cumUpShareaza", "cumUpEMCompat"
};
constexpr const char* kCumDownClientKeys[] = {
    "cumDownEmule", "cumDownEDHybrid", "cumDownEDonkey", "cumDownAMule",
    "cumDownMLdonkey", "cumDownShareaza", "cumDownEMCompat", "cumDownURL"
};

static_assert(std::size(kSesUpClientKeys) == Statistics::kUpClientCount);
static_assert(std::size(kSesDownClientKeys) == Statistics::kDownClientCount);
static_assert(std::size(kCumUpClientKeys) == Statistics::kUpClientCount);
static_assert(std::size(kCumDownClientKeys) == Statistics::kDownClientCount);

} // namespace

// ---------------------------------------------------------------------------
// Collection
// ---------------------------------------------------------------------------

Statistics::ExternalSessionCounters collectExternalSessionCounters()
{
    Statistics::ExternalSessionCounters ext;

    if (const auto* uq = theApp.uploadQueue) {
        ext.upSuccessfulSessions = uq->successfulUploadCount();
        ext.upFailedSessions = uq->failedUploadCount();
    }
    if (const auto* dq = theApp.downloadQueue) {
        ext.downSuccessfulSessions = dq->successfulDownloadCount();
        ext.downFailedSessions = dq->failedDownloadCount();
        // A completed download is a successful one; MFC counts them the same way.
        ext.downCompletedFiles = dq->successfulDownloadCount();
    }
    if (const auto* ls = theApp.listenSocket) {
        ext.connPeak = ls->peakConnections();
        ext.connMaxLimitReached = ls->maxConnectionReached();
    }
    if (const auto* hc = theApp.httpCache) {
        ext.httpCacheBytesPublished = hc->sessionBytesPublished();
        ext.httpCacheBytesFetched = hc->sessionBytesFetched();
        ext.httpCacheBytesSaved = hc->sessionBytesSaved();
        ext.httpCacheChunksPublished = hc->sessionChunksPublished();
        ext.httpCacheChunksFetched = hc->sessionChunksFetched();
    }

    return ext;
}

void flushCumulativeStats(Preferences& prefs)
{
    if (auto* s = theApp.statistics)
        s->flushCumulativeToPrefs(prefs, collectExternalSessionCounters());
}

StatsSnapshot collectStatsSnapshot()
{
    StatsSnapshot out;

    updateRecords();

    const auto now = static_cast<uint32>(std::time(nullptr));

    if (const auto* s = theApp.statistics) {
        out.sessionSentBytes = static_cast<qint64>(s->sessionSentBytes());
        out.sessionReceivedBytes = static_cast<qint64>(s->sessionReceivedBytes());
        out.sessionSentBytesToFriend = static_cast<qint64>(s->sessionSentBytesToFriend());
        // uptime comes off the monotonic session tick; startTime is then the wall
        // clock the session began at, which is what a remote viewer can use.
        out.uptime = static_cast<qint64>(s->uptimeSecs());
        out.startTime = static_cast<qint64>(now) - out.uptime;

        out.rateDown = s->rateDown();
        out.rateUp = s->rateUp();
        out.upOverheadRate = static_cast<double>(s->upDatarateOverhead()) / 1024.0;
        out.downOverheadRate = static_cast<double>(s->downDatarateOverhead()) / 1024.0;
        out.maxDown = s->maxDown();
        out.maxUp = s->maxUp();
        out.maxDownAvg = s->maxDownAvg();
        out.maxUpAvg = s->maxUpAvg();

        out.avgDownSession = s->avgDownloadRate(AverageType::Session);
        out.avgUpSession = s->avgUploadRate(AverageType::Session);
        out.avgDownTime = s->avgDownloadRate(AverageType::Time);
        out.avgUpTime = s->avgUploadRate(AverageType::Time);

        out.cumDownAvg = s->cumDownAvg();
        out.cumUpAvg = s->cumUpAvg();
        out.maxCumDown = s->maxCumDown();
        out.maxCumUp = s->maxCumUp();
        out.maxCumDownAvg = s->maxCumDownAvg();
        out.maxCumUpAvg = s->maxCumUpAvg();

        out.transferTime = static_cast<qint64>(s->transferTime());
        out.uploadTime = static_cast<qint64>(s->uploadTime());
        out.downloadTime = static_cast<qint64>(s->downloadTime());
        out.serverDuration = static_cast<qint64>(s->serverDuration());

        out.reconnects = static_cast<qint64>(s->reconnects());
        out.filteredClients = static_cast<qint64>(s->filteredClients());

        // Download overhead
        const auto downOhFR = s->downDataOverheadFileRequest();
        const auto downOhSE = s->downDataOverheadSourceExchange();
        const auto downOhSv = s->downDataOverheadServer();
        const auto downOhKd = s->downDataOverheadKad();
        const auto downOhOt = s->downDataOverheadOther();
        const auto downOhFRp = s->downDataOverheadFileRequestPackets();
        const auto downOhSEp = s->downDataOverheadSourceExchangePackets();
        const auto downOhSvp = s->downDataOverheadServerPackets();
        const auto downOhKdp = s->downDataOverheadKadPackets();
        const auto downOhOtp = s->downDataOverheadOtherPackets();

        out.downOverheadTotal = static_cast<qint64>(downOhFR + downOhSE + downOhSv + downOhKd + downOhOt);
        out.downOverheadTotalPackets = static_cast<qint64>(downOhFRp + downOhSEp + downOhSvp + downOhKdp + downOhOtp);
        out.downOverheadFileReq = static_cast<qint64>(downOhFR);
        out.downOverheadFileReqPkt = static_cast<qint64>(downOhFRp);
        out.downOverheadSrcExch = static_cast<qint64>(downOhSE);
        out.downOverheadSrcExchPkt = static_cast<qint64>(downOhSEp);
        out.downOverheadServer = static_cast<qint64>(downOhSv);
        out.downOverheadServerPkt = static_cast<qint64>(downOhSvp);
        out.downOverheadKad = static_cast<qint64>(downOhKd);
        out.downOverheadKadPkt = static_cast<qint64>(downOhKdp);

        // Upload overhead
        const auto upOhFR = s->upDataOverheadFileRequest();
        const auto upOhSE = s->upDataOverheadSourceExchange();
        const auto upOhSv = s->upDataOverheadServer();
        const auto upOhKd = s->upDataOverheadKad();
        const auto upOhOt = s->upDataOverheadOther();
        const auto upOhFRp = s->upDataOverheadFileRequestPackets();
        const auto upOhSEp = s->upDataOverheadSourceExchangePackets();
        const auto upOhSvp = s->upDataOverheadServerPackets();
        const auto upOhKdp = s->upDataOverheadKadPackets();
        const auto upOhOtp = s->upDataOverheadOtherPackets();

        out.upOverheadTotal = static_cast<qint64>(upOhFR + upOhSE + upOhSv + upOhKd + upOhOt);
        out.upOverheadTotalPackets = static_cast<qint64>(upOhFRp + upOhSEp + upOhSvp + upOhKdp + upOhOtp);
        out.upOverheadFileReq = static_cast<qint64>(upOhFR);
        out.upOverheadFileReqPkt = static_cast<qint64>(upOhFRp);
        out.upOverheadSrcExch = static_cast<qint64>(upOhSE);
        out.upOverheadSrcExchPkt = static_cast<qint64>(upOhSEp);
        out.upOverheadServer = static_cast<qint64>(upOhSv);
        out.upOverheadServerPkt = static_cast<qint64>(upOhSvp);
        out.upOverheadKad = static_cast<qint64>(upOhKd);
        out.upOverheadKadPkt = static_cast<qint64>(upOhKdp);

        // Session per-client / per-port / per-source breakdown
        for (int i = 0; i < Statistics::kUpClientCount; ++i)
            out.sesUpByClient[static_cast<size_t>(i)] = static_cast<qint64>(s->sesUpByClient(i));
        for (int i = 0; i < Statistics::kDownClientCount; ++i)
            out.sesDownByClient[static_cast<size_t>(i)] = static_cast<qint64>(s->sesDownByClient(i));

        out.sesUpPort4662 = static_cast<qint64>(s->sesUpPort4662());
        out.sesUpPortOther = static_cast<qint64>(s->sesUpPortOther());
        out.sesDownPort4662 = static_cast<qint64>(s->sesDownPort4662());
        out.sesDownPortOther = static_cast<qint64>(s->sesDownPortOther());
        out.sesUpFromFile = static_cast<qint64>(s->sesUpFromFile());
        out.sesUpFromPartfile = static_cast<qint64>(s->sesUpFromPartfile());

        // HTTP Cache session counters live in the manager, not in Statistics —
        // same reason as the other external counters: Statistics never reaches
        // into theApp, the collector brings the numbers to it.
        if (const auto* hc = theApp.httpCache) {
            out.sesHttpCachePublished = static_cast<qint64>(hc->sessionBytesPublished());
            out.sesHttpCacheFetched = static_cast<qint64>(hc->sessionBytesFetched());
            out.sesHttpCacheSaved = static_cast<qint64>(hc->sessionBytesSaved());
            out.sesHttpCacheChunksUp = static_cast<qint64>(hc->sessionChunksPublished());
            out.sesHttpCacheChunksDown = static_cast<qint64>(hc->sessionChunksFetched());
        }

        out.sesCompressionGain = static_cast<qint64>(s->sesCompressionGain());
        out.sesCorruptionLoss = static_cast<qint64>(s->sesCorruptionLoss());
        out.sesIchPartsSaved = static_cast<qint64>(s->sesIchPartsSaved());

        // Cumulative totals come from Statistics, NOT from thePrefs.cum* + session:
        // the periodic flush banks the session into preferences, so adding the session
        // on top of the pref would count it twice for the rest of the interval.
        const Statistics::CumulativeTotals cum =
            s->cumulativeTotals(collectExternalSessionCounters());

        out.cumTotalUp = static_cast<qint64>(cum.totalUploaded);
        out.cumTotalDown = static_cast<qint64>(cum.totalDownloaded);
        out.cumTotalUpFriend = static_cast<qint64>(cum.totalUploadedToFriend);

        for (int i = 0; i < Statistics::kUpClientCount; ++i)
            out.cumUpByClient[static_cast<size_t>(i)] =
                static_cast<qint64>(cum.upByClient[static_cast<size_t>(i)]);
        for (int i = 0; i < Statistics::kDownClientCount; ++i)
            out.cumDownByClient[static_cast<size_t>(i)] =
                static_cast<qint64>(cum.downByClient[static_cast<size_t>(i)]);

        out.cumUpPort4662 = static_cast<qint64>(cum.upPort4662);
        out.cumUpPortOther = static_cast<qint64>(cum.upPortOther);
        out.cumDownPort4662 = static_cast<qint64>(cum.downPort4662);
        out.cumDownPortOther = static_cast<qint64>(cum.downPortOther);
        out.cumUpFromFile = static_cast<qint64>(cum.upFromFile);
        out.cumUpFromPartfile = static_cast<qint64>(cum.upFromPartfile);

        out.cumHttpCachePublished = static_cast<qint64>(cum.httpCacheBytesPublished);
        out.cumHttpCacheFetched = static_cast<qint64>(cum.httpCacheBytesFetched);
        out.cumHttpCacheSaved = static_cast<qint64>(cum.httpCacheBytesSaved);
        out.cumHttpCacheChunksUp = static_cast<qint64>(cum.httpCacheChunksPublished);
        out.cumHttpCacheChunksDown = static_cast<qint64>(cum.httpCacheChunksFetched);

        out.cumUpSuccessful = static_cast<qint64>(cum.upSuccessfulSessions);
        out.cumUpFailed = static_cast<qint64>(cum.upFailedSessions);
        out.cumUpAvgTime = static_cast<qint64>(
            theApp.uploadQueue ? theApp.uploadQueue->averageUpTime() : 0);
        out.cumDownSuccessful = static_cast<qint64>(cum.downSuccessfulSessions);
        out.cumDownFailed = static_cast<qint64>(cum.downFailedSessions);
        out.cumDownAvgTime = static_cast<qint64>(
            theApp.downloadQueue ? theApp.downloadQueue->averageDownTime() : 0);
        out.cumDownCompletedFiles = static_cast<qint64>(cum.downCompletedFiles);

        out.cumUpOhTotal = static_cast<qint64>(cum.upOverheadTotal);
        out.cumUpOhTotalPkt = static_cast<qint64>(cum.upOverheadTotalPackets);
        out.cumUpOhFileReq = static_cast<qint64>(cum.upOverheadFileReq);
        out.cumUpOhFileReqPkt = static_cast<qint64>(cum.upOverheadFileReqPackets);
        out.cumUpOhSrcExch = static_cast<qint64>(cum.upOverheadSrcExch);
        out.cumUpOhSrcExchPkt = static_cast<qint64>(cum.upOverheadSrcExchPackets);
        out.cumUpOhServer = static_cast<qint64>(cum.upOverheadServer);
        out.cumUpOhServerPkt = static_cast<qint64>(cum.upOverheadServerPackets);
        out.cumUpOhKad = static_cast<qint64>(cum.upOverheadKad);
        out.cumUpOhKadPkt = static_cast<qint64>(cum.upOverheadKadPackets);

        out.cumDownOhTotal = static_cast<qint64>(cum.downOverheadTotal);
        out.cumDownOhTotalPkt = static_cast<qint64>(cum.downOverheadTotalPackets);
        out.cumDownOhFileReq = static_cast<qint64>(cum.downOverheadFileReq);
        out.cumDownOhFileReqPkt = static_cast<qint64>(cum.downOverheadFileReqPackets);
        out.cumDownOhSrcExch = static_cast<qint64>(cum.downOverheadSrcExch);
        out.cumDownOhSrcExchPkt = static_cast<qint64>(cum.downOverheadSrcExchPackets);
        out.cumDownOhServer = static_cast<qint64>(cum.downOverheadServer);
        out.cumDownOhServerPkt = static_cast<qint64>(cum.downOverheadServerPackets);
        out.cumDownOhKad = static_cast<qint64>(cum.downOverheadKad);
        out.cumDownOhKadPkt = static_cast<qint64>(cum.downOverheadKadPackets);

        out.cumConnPeak = static_cast<qint64>(cum.connPeak);
        out.cumConnMaxLimitReached = static_cast<qint64>(cum.connMaxLimitReached);
        out.cumConnReconnects = static_cast<qint64>(cum.connReconnects);

        out.cumRunTime = static_cast<qint64>(cum.runTime);
        out.cumTransferTime = static_cast<qint64>(cum.transferTime);
        out.cumUploadTime = static_cast<qint64>(cum.uploadTime);
        out.cumDownloadTime = static_cast<qint64>(cum.downloadTime);
        out.cumServerDuration = static_cast<qint64>(cum.serverDuration);

        out.cumCompressionGain = static_cast<qint64>(cum.compressionGain);
        out.cumCorruptionLoss = static_cast<qint64>(cum.corruptionLoss);
        out.cumIchPartsSaved = static_cast<qint64>(cum.ichPartsSaved);
    }

    // Upload queue
    if (const auto* uq = theApp.uploadQueue) {
        out.upDatarate = static_cast<qint64>(uq->datarate());
        out.upFriendDatarate = static_cast<qint64>(uq->friendDatarate());
        out.upSuccessful = static_cast<qint64>(uq->successfulUploadCount());
        out.upFailed = static_cast<qint64>(uq->failedUploadCount());
        out.upWaiting = static_cast<qint64>(uq->waitingUserCount());
        out.upQueueLength = static_cast<qint64>(uq->uploadQueueLength());
        out.upAvgTime = static_cast<qint64>(uq->averageUpTime());
    }

    // Download queue. One walk feeds the queue counters, the compression/corruption
    // totals and the Total Downloads section, which used to be three separate passes.
    if (const auto* dq = theApp.downloadQueue) {
        out.downDatarate = static_cast<qint64>(dq->datarate());
        out.downFileCount = static_cast<qint64>(dq->fileCount());
        out.downUdpReasks = static_cast<qint64>(dq->udpFileReasks());
        out.downUdpReasksFailed = static_cast<qint64>(dq->failedUDPFileReasks());

        qint64 completedCount = 0;
        qint64 totalSources = 0;
        qint64 totalCount = 0;
        qint64 totalSize = 0;
        qint64 totalDone = 0;
        for (const auto* f : dq->files()) {
            if (f->status() == PartFileStatus::Complete)
                ++completedCount;
            totalSources += f->sourceCount();

            ++totalCount;
            const auto size = static_cast<qint64>(f->fileSize());
            const auto done = static_cast<qint64>(f->completedSize());
            totalSize += size;
            totalDone += done;
        }
        out.completedDownloads = completedCount;
        out.downFoundSources = totalSources;
        out.totalDownCount = totalCount;
        out.totalDownSize = totalSize;
        out.totalDownDone = totalDone;
        out.totalDownLeft = totalSize - totalDone;
    }

    // Free space on the incoming directory
    if (const QStorageInfo storage(thePrefs.incomingDir()); storage.isValid())
        out.freeTempSpace = storage.bytesAvailable();

    // Connections
    if (const auto* ls = theApp.listenSocket) {
        out.connActive = static_cast<qint64>(ls->activeConnections());
        out.connPeak = static_cast<qint64>(ls->peakConnections());
        out.connMaxReached = static_cast<qint64>(ls->maxConnectionReached());
        out.connAverage = ls->averageConnections();
        out.connOpen = static_cast<qint64>(ls->openSockets());
    }

    // Servers
    if (const auto* sl = theApp.serverList) {
        const auto srvStats = sl->stats();
        out.srvWorking = static_cast<qint64>(srvStats.total - srvStats.failed);
        out.srvFailed = static_cast<qint64>(srvStats.failed);
        out.srvTotal = static_cast<qint64>(srvStats.total);
        out.srvUsers = static_cast<qint64>(srvStats.users);
        out.srvFiles = static_cast<qint64>(srvStats.files);
        out.srvLowIDUsers = static_cast<qint64>(srvStats.lowIDUsers);
    }

    // Clients
    if (const auto* cl = theApp.clientList) {
        out.knownClients = static_cast<qint64>(cl->clientCount());
        out.bannedClients = static_cast<qint64>(cl->bannedCount());
        out.clientSoftwareStats = collectClientSoftwareStats(*cl, out.lowIDClients);
    }

    // Shared files
    if (auto* sf = theApp.sharedFileList) {
        uint64 largest = 0;
        out.sharedCount = static_cast<qint64>(sf->getCount());
        out.sharedSize = static_cast<qint64>(sf->getDataSize(largest));
        out.sharedLargest = static_cast<qint64>(largest);
    }

    // Records (already raised above by updateRecords())
    out.statsLastReset = static_cast<qint64>(thePrefs.statsLastReset());
    if (out.statsLastReset > 0)
        out.timeSinceReset = std::max<qint64>(0, static_cast<qint64>(now) - out.statsLastReset);
    out.statsBackupAvailable = thePrefs.hasCumulativeStatsBackup();

    out.recMaxWorkingServers = static_cast<qint64>(thePrefs.recMaxWorkingServers());
    out.recMaxUsersOnline = static_cast<qint64>(thePrefs.recMaxUsersOnline());
    out.recMaxFilesAvail = static_cast<qint64>(thePrefs.recMaxFilesAvail());
    out.recMaxSharedFiles = static_cast<qint64>(thePrefs.recMaxSharedFiles());
    out.recMaxSharedSize = static_cast<qint64>(thePrefs.recMaxSharedSize());
    out.recMaxAvgFileSize = static_cast<qint64>(thePrefs.recMaxAvgFileSize());
    out.recMaxLargestFile = static_cast<qint64>(thePrefs.recMaxLargestFile());

    return out;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

QCborMap toCborMap(const StatsSnapshot& s)
{
    QCborMap m;
    // QStringLiteral keys: static UTF-16, so serialising a snapshot allocates
    // nothing for the 170-odd key names.
    const auto put = [&m](const QString& key, auto value) { m.insert(key, value); };

    // Session bytes & uptime
    put(QStringLiteral("sessionSentBytes"), s.sessionSentBytes);
    put(QStringLiteral("sessionReceivedBytes"), s.sessionReceivedBytes);
    put(QStringLiteral("sessionSentBytesToFriend"), s.sessionSentBytesToFriend);
    put(QStringLiteral("uptime"), s.uptime);
    put(QStringLiteral("startTime"), s.startTime);

    // Current rates
    put(QStringLiteral("rateDown"), s.rateDown);
    put(QStringLiteral("rateUp"), s.rateUp);
    put(QStringLiteral("upOverheadRate"), s.upOverheadRate);
    put(QStringLiteral("downOverheadRate"), s.downOverheadRate);
    put(QStringLiteral("maxDown"), s.maxDown);
    put(QStringLiteral("maxUp"), s.maxUp);
    put(QStringLiteral("maxDownAvg"), s.maxDownAvg);
    put(QStringLiteral("maxUpAvg"), s.maxUpAvg);

    // Averages
    put(QStringLiteral("avgDownSession"), s.avgDownSession);
    put(QStringLiteral("avgUpSession"), s.avgUpSession);
    put(QStringLiteral("avgDownTime"), s.avgDownTime);
    put(QStringLiteral("avgUpTime"), s.avgUpTime);
    put(QStringLiteral("cumDownAvg"), s.cumDownAvg);
    put(QStringLiteral("cumUpAvg"), s.cumUpAvg);
    put(QStringLiteral("maxCumDown"), s.maxCumDown);
    put(QStringLiteral("maxCumUp"), s.maxCumUp);
    put(QStringLiteral("maxCumDownAvg"), s.maxCumDownAvg);
    put(QStringLiteral("maxCumUpAvg"), s.maxCumUpAvg);

    // Transfer times
    put(QStringLiteral("transferTime"), s.transferTime);
    put(QStringLiteral("uploadTime"), s.uploadTime);
    put(QStringLiteral("downloadTime"), s.downloadTime);
    put(QStringLiteral("serverDuration"), s.serverDuration);

    // Global state
    put(QStringLiteral("reconnects"), s.reconnects);
    put(QStringLiteral("filteredClients"), s.filteredClients);

    // Download overhead
    put(QStringLiteral("downOverheadTotal"), s.downOverheadTotal);
    put(QStringLiteral("downOverheadTotalPackets"), s.downOverheadTotalPackets);
    put(QStringLiteral("downOverheadFileReq"), s.downOverheadFileReq);
    put(QStringLiteral("downOverheadFileReqPkt"), s.downOverheadFileReqPkt);
    put(QStringLiteral("downOverheadSrcExch"), s.downOverheadSrcExch);
    put(QStringLiteral("downOverheadSrcExchPkt"), s.downOverheadSrcExchPkt);
    put(QStringLiteral("downOverheadServer"), s.downOverheadServer);
    put(QStringLiteral("downOverheadServerPkt"), s.downOverheadServerPkt);
    put(QStringLiteral("downOverheadKad"), s.downOverheadKad);
    put(QStringLiteral("downOverheadKadPkt"), s.downOverheadKadPkt);

    // Upload overhead
    put(QStringLiteral("upOverheadTotal"), s.upOverheadTotal);
    put(QStringLiteral("upOverheadTotalPackets"), s.upOverheadTotalPackets);
    put(QStringLiteral("upOverheadFileReq"), s.upOverheadFileReq);
    put(QStringLiteral("upOverheadFileReqPkt"), s.upOverheadFileReqPkt);
    put(QStringLiteral("upOverheadSrcExch"), s.upOverheadSrcExch);
    put(QStringLiteral("upOverheadSrcExchPkt"), s.upOverheadSrcExchPkt);
    put(QStringLiteral("upOverheadServer"), s.upOverheadServer);
    put(QStringLiteral("upOverheadServerPkt"), s.upOverheadServerPkt);
    put(QStringLiteral("upOverheadKad"), s.upOverheadKad);
    put(QStringLiteral("upOverheadKadPkt"), s.upOverheadKadPkt);

    // Upload queue
    put(QStringLiteral("upDatarate"), s.upDatarate);
    put(QStringLiteral("upFriendDatarate"), s.upFriendDatarate);
    put(QStringLiteral("upSuccessful"), s.upSuccessful);
    put(QStringLiteral("upFailed"), s.upFailed);
    put(QStringLiteral("upWaiting"), s.upWaiting);
    put(QStringLiteral("upQueueLength"), s.upQueueLength);
    put(QStringLiteral("upAvgTime"), s.upAvgTime);

    // Download queue
    put(QStringLiteral("downDatarate"), s.downDatarate);
    put(QStringLiteral("downFileCount"), s.downFileCount);
    put(QStringLiteral("completedDownloads"), s.completedDownloads);
    put(QStringLiteral("downFoundSources"), s.downFoundSources);
    put(QStringLiteral("downUdpReasks"), s.downUdpReasks);
    put(QStringLiteral("downUdpReasksFailed"), s.downUdpReasksFailed);
    if (s.freeTempSpace)
        put(QStringLiteral("freeTempSpace"), *s.freeTempSpace);

    // Connections
    put(QStringLiteral("connActive"), s.connActive);
    put(QStringLiteral("connPeak"), s.connPeak);
    put(QStringLiteral("connMaxReached"), s.connMaxReached);
    put(QStringLiteral("connAverage"), s.connAverage);
    put(QStringLiteral("connOpen"), s.connOpen);

    // Servers
    put(QStringLiteral("srvWorking"), s.srvWorking);
    put(QStringLiteral("srvFailed"), s.srvFailed);
    put(QStringLiteral("srvTotal"), s.srvTotal);
    put(QStringLiteral("srvUsers"), s.srvUsers);
    put(QStringLiteral("srvFiles"), s.srvFiles);
    put(QStringLiteral("srvLowIDUsers"), s.srvLowIDUsers);

    // Clients
    put(QStringLiteral("knownClients"), s.knownClients);
    put(QStringLiteral("bannedClients"), s.bannedClients);
    put(QStringLiteral("lowIDClients"), s.lowIDClients);

    QCborArray softArr;
    for (const auto& soft : s.clientSoftwareStats) {
        QCborMap softEntry;
        softEntry.insert(QStringLiteral("n"), soft.name);
        softEntry.insert(QStringLiteral("c"), soft.count);

        QCborArray verArr;
        for (const auto& ver : soft.versions) {
            QCborMap verEntry;
            verEntry.insert(QStringLiteral("l"), ver.label);
            verEntry.insert(QStringLiteral("c"), ver.count);

            if (!ver.mods.empty()) {
                QCborArray modArr;
                for (const auto& mod : ver.mods) {
                    QCborMap modEntry;
                    modEntry.insert(QStringLiteral("n"), mod.name);
                    modEntry.insert(QStringLiteral("c"), mod.count);
                    modArr.append(modEntry);
                }
                verEntry.insert(QStringLiteral("m"), modArr);
            }
            verArr.append(verEntry);
        }
        softEntry.insert(QStringLiteral("v"), verArr);
        softArr.append(softEntry);
    }
    m.insert(QStringLiteral("clientSoftwareStats"), softArr);

    // Shared files
    put(QStringLiteral("sharedCount"), s.sharedCount);
    put(QStringLiteral("sharedSize"), s.sharedSize);
    put(QStringLiteral("sharedLargest"), s.sharedLargest);

    // Cumulative transfer totals
    put(QStringLiteral("cumTotalUp"), s.cumTotalUp);
    put(QStringLiteral("cumTotalDown"), s.cumTotalDown);
    put(QStringLiteral("cumTotalUpFriend"), s.cumTotalUpFriend);

    // Per-client breakdown
    for (int i = 0; i < Statistics::kUpClientCount; ++i) {
        const auto idx = static_cast<size_t>(i);
        m.insert(QString::fromLatin1(kSesUpClientKeys[idx]), s.sesUpByClient[idx]);
        m.insert(QString::fromLatin1(kCumUpClientKeys[idx]), s.cumUpByClient[idx]);
    }
    for (int i = 0; i < Statistics::kDownClientCount; ++i) {
        const auto idx = static_cast<size_t>(i);
        m.insert(QString::fromLatin1(kSesDownClientKeys[idx]), s.sesDownByClient[idx]);
        m.insert(QString::fromLatin1(kCumDownClientKeys[idx]), s.cumDownByClient[idx]);
    }

    // Per-port
    put(QStringLiteral("sesUpPort4662"), s.sesUpPort4662);
    put(QStringLiteral("sesUpPortOther"), s.sesUpPortOther);
    put(QStringLiteral("sesDownPort4662"), s.sesDownPort4662);
    put(QStringLiteral("sesDownPortOther"), s.sesDownPortOther);
    put(QStringLiteral("cumUpPort4662"), s.cumUpPort4662);
    put(QStringLiteral("cumUpPortOther"), s.cumUpPortOther);
    put(QStringLiteral("cumDownPort4662"), s.cumDownPort4662);
    put(QStringLiteral("cumDownPortOther"), s.cumDownPortOther);

    // Per-source
    put(QStringLiteral("sesUpFromFile"), s.sesUpFromFile);
    put(QStringLiteral("sesUpFromPartfile"), s.sesUpFromPartfile);
    put(QStringLiteral("cumUpFromFile"), s.cumUpFromFile);
    put(QStringLiteral("cumUpFromPartfile"), s.cumUpFromPartfile);

    put(QStringLiteral("sesHttpCachePublished"), s.sesHttpCachePublished);
    put(QStringLiteral("sesHttpCacheFetched"), s.sesHttpCacheFetched);
    put(QStringLiteral("sesHttpCacheSaved"), s.sesHttpCacheSaved);
    put(QStringLiteral("sesHttpCacheChunksUp"), s.sesHttpCacheChunksUp);
    put(QStringLiteral("sesHttpCacheChunksDown"), s.sesHttpCacheChunksDown);
    put(QStringLiteral("cumHttpCachePublished"), s.cumHttpCachePublished);
    put(QStringLiteral("cumHttpCacheFetched"), s.cumHttpCacheFetched);
    put(QStringLiteral("cumHttpCacheSaved"), s.cumHttpCacheSaved);
    put(QStringLiteral("cumHttpCacheChunksUp"), s.cumHttpCacheChunksUp);
    put(QStringLiteral("cumHttpCacheChunksDown"), s.cumHttpCacheChunksDown);

    // Cumulative sessions
    put(QStringLiteral("cumUpSuccessful"), s.cumUpSuccessful);
    put(QStringLiteral("cumUpFailed"), s.cumUpFailed);
    put(QStringLiteral("cumUpAvgTime"), s.cumUpAvgTime);
    put(QStringLiteral("cumDownSuccessful"), s.cumDownSuccessful);
    put(QStringLiteral("cumDownFailed"), s.cumDownFailed);
    put(QStringLiteral("cumDownAvgTime"), s.cumDownAvgTime);
    put(QStringLiteral("cumDownCompletedFiles"), s.cumDownCompletedFiles);

    // Cumulative overhead — upload
    put(QStringLiteral("cumUpOhTotal"), s.cumUpOhTotal);
    put(QStringLiteral("cumUpOhTotalPkt"), s.cumUpOhTotalPkt);
    put(QStringLiteral("cumUpOhFileReq"), s.cumUpOhFileReq);
    put(QStringLiteral("cumUpOhFileReqPkt"), s.cumUpOhFileReqPkt);
    put(QStringLiteral("cumUpOhSrcExch"), s.cumUpOhSrcExch);
    put(QStringLiteral("cumUpOhSrcExchPkt"), s.cumUpOhSrcExchPkt);
    put(QStringLiteral("cumUpOhServer"), s.cumUpOhServer);
    put(QStringLiteral("cumUpOhServerPkt"), s.cumUpOhServerPkt);
    put(QStringLiteral("cumUpOhKad"), s.cumUpOhKad);
    put(QStringLiteral("cumUpOhKadPkt"), s.cumUpOhKadPkt);

    // Cumulative overhead — download
    put(QStringLiteral("cumDownOhTotal"), s.cumDownOhTotal);
    put(QStringLiteral("cumDownOhTotalPkt"), s.cumDownOhTotalPkt);
    put(QStringLiteral("cumDownOhFileReq"), s.cumDownOhFileReq);
    put(QStringLiteral("cumDownOhFileReqPkt"), s.cumDownOhFileReqPkt);
    put(QStringLiteral("cumDownOhSrcExch"), s.cumDownOhSrcExch);
    put(QStringLiteral("cumDownOhSrcExchPkt"), s.cumDownOhSrcExchPkt);
    put(QStringLiteral("cumDownOhServer"), s.cumDownOhServer);
    put(QStringLiteral("cumDownOhServerPkt"), s.cumDownOhServerPkt);
    put(QStringLiteral("cumDownOhKad"), s.cumDownOhKad);
    put(QStringLiteral("cumDownOhKadPkt"), s.cumDownOhKadPkt);

    // Cumulative connections
    put(QStringLiteral("cumConnPeak"), s.cumConnPeak);
    put(QStringLiteral("cumConnMaxLimitReached"), s.cumConnMaxLimitReached);
    put(QStringLiteral("cumConnReconnects"), s.cumConnReconnects);

    // Cumulative times
    put(QStringLiteral("cumRunTime"), s.cumRunTime);
    put(QStringLiteral("cumTransferTime"), s.cumTransferTime);
    put(QStringLiteral("cumUploadTime"), s.cumUploadTime);
    put(QStringLiteral("cumDownloadTime"), s.cumDownloadTime);
    put(QStringLiteral("cumServerDuration"), s.cumServerDuration);

    // Compression / corruption / ICH
    put(QStringLiteral("sesCompressionGain"), s.sesCompressionGain);
    put(QStringLiteral("sesCorruptionLoss"), s.sesCorruptionLoss);
    put(QStringLiteral("sesIchPartsSaved"), s.sesIchPartsSaved);
    put(QStringLiteral("cumCompressionGain"), s.cumCompressionGain);
    put(QStringLiteral("cumCorruptionLoss"), s.cumCorruptionLoss);
    put(QStringLiteral("cumIchPartsSaved"), s.cumIchPartsSaved);

    // Total downloads
    put(QStringLiteral("totalDownCount"), s.totalDownCount);
    put(QStringLiteral("totalDownSize"), s.totalDownSize);
    put(QStringLiteral("totalDownDone"), s.totalDownDone);
    put(QStringLiteral("totalDownLeft"), s.totalDownLeft);

    // Records
    put(QStringLiteral("statsLastReset"), s.statsLastReset);
    put(QStringLiteral("timeSinceReset"), s.timeSinceReset);
    put(QStringLiteral("statsBackupAvailable"), s.statsBackupAvailable);

    put(QStringLiteral("recMaxWorkingServers"), s.recMaxWorkingServers);
    put(QStringLiteral("recMaxUsersOnline"), s.recMaxUsersOnline);
    put(QStringLiteral("recMaxFilesAvail"), s.recMaxFilesAvail);
    put(QStringLiteral("recMaxSharedFiles"), s.recMaxSharedFiles);
    put(QStringLiteral("recMaxSharedSize"), s.recMaxSharedSize);
    put(QStringLiteral("recMaxAvgFileSize"), s.recMaxAvgFileSize);
    put(QStringLiteral("recMaxLargestFile"), s.recMaxLargestFile);

    return m;
}

} // namespace eMule
