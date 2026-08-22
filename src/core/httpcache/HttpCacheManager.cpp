#include "pch.h"
/// @file HttpCacheManager.cpp
/// @brief The brain of HTTP Cache — implementation.

#include "httpcache/HttpCacheManager.h"

#include "app/AppContext.h"
#include "client/ClientStructs.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheClient.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QDateTime>
#include <QHostAddress>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

namespace eMule {

namespace {

/// Hex of a 16-byte hash, for keys and log lines.
QString hashKey(const std::array<uint8, 16>& hash)
{
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(hash.data()), 16).toHex());
}

/// An offer stops being useful shortly before the server drops the chunk; leave
/// enough margin that a peer that accepts it can still finish the fetch.
constexpr uint32 kExpiryMarginSeconds = 120;

/// Fallback publish rate when no explicit cap and no upload limit are configured:
/// a quarter of a notional 1 MB/s, i.e. deliberately unremarkable.
constexpr uint64 kDefaultPublishBytesPerSecond = 256 * 1024;

/// Does this status condemn the server, or only the chunk we just tried to send?
///
/// The distinction is what stops one bad part from silencing the feature and one
/// bad server from being asked the same 9.28 MB question every five seconds.
bool statusCondemnsTheServer(int status)
{
    switch (status) {
    case 400:
        // Empty or short body: the upload was cut off on the way out. That is
        // about this attempt, not about the server.
        return false;
    case 401:
    case 403:
        return true;    // the API key is not accepted
    case 411:
    case 413:
        return true;    // this server will not take a part-sized chunk, ever
    case 429:
        return true;    // upload quota spent
    case 507:
        return true;    // out of storage — every other chunk would bounce too
    default:
        // A 2xx that still reached here is a success we could not use: an
        // unparseable body, a missing url, a size that disagrees with what we
        // sent. Whatever rewrote or mangled that response will do it again for
        // the next chunk, so it counts against the server.
        return status >= 500 || (status >= 200 && status < 300);
    }
}

/// A refusal a retry alone cannot clear: something on one side has to change.
bool statusNeedsOperatorAction(int status)
{
    return status == 401 || status == 403 || status == 411 || status == 413 || status == 429;
}

/// Identifies the server a backoff was taken against. A newline separator so the
/// two halves cannot be confused for one another.
QString cacheServerFingerprint()
{
    return thePrefs.httpCacheBaseUrl() + QLatin1Char('\n') + thePrefs.httpCacheApiKey();
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HttpCacheManager::HttpCacheManager(QObject* parent)
    : QObject(parent)
{
    m_budgetDay = QDateTime::currentSecsSinceEpoch() / 86400;
}

HttpCacheManager::~HttpCacheManager()
{
    stop();
}

void HttpCacheManager::start()
{
    if (m_timer)
        return;

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &HttpCacheManager::process);
    m_timer->start();

    logInfo(QStringLiteral("HTTP Cache: manager started"));
}

void HttpCacheManager::stop()
{
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    // Fetches are UpDownClients owned by their part file's source list; asking
    // them to cancel is enough, and safer than deleting them from under it.
    //
    // Detach the map first: sendCancelTransfer() runs finish(), which emits
    // fetchFinished, which erases from m_fetches — mutating the container we
    // would otherwise still be iterating.
    const auto fetches = std::exchange(m_fetches, {});
    for (auto* client : fetches) {
        if (client)
            client->sendCancelTransfer();
    }

    m_entries.clear();
    m_publishing.clear();
    m_publishCooldown.clear();
    m_serverBackoffUntil = 0;
    m_serverFailures = 0;
    m_backoffConfig.clear();
}

int HttpCacheManager::activeFetchCount() const
{
    return static_cast<int>(m_fetches.size());
}

// ---------------------------------------------------------------------------
// Packet entry point
// ---------------------------------------------------------------------------

void HttpCacheManager::handlePacket(UpDownClient* sender, const uint8* data, uint32 size)
{
    if (!sender)
        return;

    const auto parsed = HttpCacheCodec::parse(data, size);

    switch (parsed.kind) {
    case HttpCacheCodec::Kind::Offer:
        handleOffer(sender, parsed.offer);
        break;

    case HttpCacheCodec::Kind::Report:
        handleReport(sender, parsed.report);
        break;

    case HttpCacheCodec::Kind::Cancel:
        handleCancel(sender, parsed.report);
        break;

    case HttpCacheCodec::Kind::Invalid:
        // Not a ban-worthy offence on its own — a future version of this protocol
        // would look exactly like this to us.
        logDebug(QStringLiteral("HTTP Cache: bad packet from %1: %2")
                     .arg(sender->userName(), parsed.error));
        break;
    }
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void HttpCacheManager::process()
{
    rollDailyBudget();
    expireEntries();

    if (!uploadEnabled())
        return;

    // Re-offer anything already published and still good before spending a new
    // upload: the whole point is that one chunk serves many peers over time.
    // Only peers that have not already been told are targeted (see Entry::offeredTo).
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->offerable)
            offerToQueue(*it);
    }

    // Deliberately after the re-offer loop: a POST endpoint that is refusing says
    // nothing about the chunks already sitting on that server, and peers should
    // keep being pointed at them.
    if (serverIsCoolingDown())
        return;

    if (m_activePublishes >= static_cast<int>(thePrefs.httpCacheMaxConcurrentPublishes()))
        return;

    for (const auto& candidate : findCandidates()) {
        if (m_activePublishes >= static_cast<int>(thePrefs.httpCacheMaxConcurrentPublishes()))
            break;
        publish(candidate);
    }
}

// ---------------------------------------------------------------------------
// Uploader
// ---------------------------------------------------------------------------

bool HttpCacheManager::uploadEnabled() const
{
    return thePrefs.httpCacheEnabled()
        && thePrefs.httpCacheAllowUpload()
        && !thePrefs.httpCacheBaseUrl().isEmpty()
        && !thePrefs.httpCacheApiKey().isEmpty();
}

std::vector<HttpCacheManager::Candidate> HttpCacheManager::findCandidates() const
{
    std::vector<Candidate> result;

    UploadQueue* queue = theApp.uploadQueue;
    if (!queue)
        return result;

    // Step 1: which (file, part) pairs is somebody actually pulling right now?
    //
    // Seeding from live block requests rather than from "what peers are missing"
    // is what keeps this from publishing speculatively: a part nobody has asked
    // for yet may never be asked for at all.
    std::map<std::pair<KnownFile*, uint32>, int> seeds;

    queue->forEachUploading([&seeds](UpDownClient* client) {
        if (!client || !client->supportsHttpCache())
            return;

        KnownFile* file = client->uploadFile();
        if (!file)
            return;

        for (const auto* block : client->blockRequests()) {
            if (!block)
                continue;
            const uint32 part = static_cast<uint32>(block->startOffset / PARTSIZE);
            ++seeds[{file, part}];
        }
    });

    if (seeds.empty())
        return result;

    // Step 2: for each seed, count every capable peer in the queue that is still
    // missing that part — waiting peers included. They are the ones the offer
    // helps most: they get their bytes without ever costing us a slot.
    for (const auto& [key, hits] : seeds) {
        KnownFile* file = key.first;
        const uint32 part = key.second;

        // Only whole parts. The file's short tail part is the least-shared part
        // there is, and carving a special case for it would buy nothing.
        const uint64 partEnd = static_cast<uint64>(part + 1) * PARTSIZE;
        if (partEnd > static_cast<uint64>(file->fileSize()))
            continue;

        if (!file->isPartComplete(part))
            continue;

        Candidate candidate;
        candidate.file = file;
        candidate.partIndex = part;

        const auto collect = [&candidate, file, part](UpDownClient* client) {
            if (!client || !client->supportsHttpCache())
                return;
            if (client->uploadFile() != file)
                return;
            // isUpPartAvailable() is what the peer told us in OP_FILESTATUS.
            if (client->isUpPartAvailable(part))
                return;
            candidate.peers.push_back(client);
        };

        queue->forEachUploading(collect);
        queue->forEachWaiting(collect);

        if (candidate.peers.size() >= thePrefs.httpCacheMinClients())
            result.push_back(std::move(candidate));
    }

    return result;
}

void HttpCacheManager::publish(const Candidate& candidate)
{
    if (!candidate.file)
        return;

    std::array<uint8, 16> fileHash{};
    std::memcpy(fileHash.data(), candidate.file->fileHash(), fileHash.size());

    const QString key = entryKey(fileHash, candidate.partIndex);

    // Already published, or already on its way.
    if (m_entries.contains(key) || m_publishing.contains(key))
        return;

    if (const auto it = m_publishCooldown.constFind(key); it != m_publishCooldown.constEnd()) {
        if (QDateTime::currentSecsSinceEpoch() < *it)
            return;
        m_publishCooldown.remove(key);
    }

    const uint64 budget = thePrefs.httpCacheMaxPublishBytesPerDay();
    if (budget != 0 && m_publishedToday + PARTSIZE > budget) {
        logDebug(QStringLiteral("HTTP Cache: daily publish budget reached (%1 of %2)")
                     .arg(m_publishedToday)
                     .arg(budget));
        return;
    }

    HttpCachePublisher::Request request;
    request.baseUrl = thePrefs.httpCacheBaseUrl();
    request.apiKey = thePrefs.httpCacheApiKey();
    request.ttlSeconds = thePrefs.httpCacheChunkTtlSeconds();
    request.rateBytesPerSecond = publishRateBytesPerSecond();
    request.fileHash = fileHash;
    request.partIndex = candidate.partIndex;
    request.dataFilePath = candidate.file->dataFilePath();
    request.partOffset = static_cast<uint64>(candidate.partIndex) * PARTSIZE;
    request.partLength = PARTSIZE;

    logInfo(QStringLiteral("HTTP Cache: publishing part %1 of %2 for %3 peers")
                .arg(candidate.partIndex)
                .arg(candidate.file->fileName())
                .arg(candidate.peers.size()));

    m_publishing.insert(key, true);
    ++m_activePublishes;

    auto* publisher = new HttpCachePublisher(this);
    connect(publisher, &HttpCachePublisher::finished, this,
            [this, key](const HttpCachePublishResult& result, const HttpCacheOffer& offer) {
                m_publishing.remove(key);
                --m_activePublishes;
                onPublishFinished(key, result, offer);
            });

    publisher->start(request);
}

void HttpCacheManager::onPublishFinished(const QString& key,
                                         const HttpCachePublishResult& result,
                                         const HttpCacheOffer& offer)
{
    if (!result.ok) {
        notePublishFailure(key, result);
        return;
    }

    // A chunk got through, so whatever the server was doing, it has stopped.
    m_publishCooldown.remove(key);
    m_serverFailures = 0;
    m_serverBackoffUntil = 0;
    m_backoffConfig.clear();

    Entry entry;
    entry.offer = offer;
    entry.chunkId = result.chunkId;

    m_entries.insert(key, entry);

    m_publishedToday += offer.cipherLength;
    m_sessionBytesPublished += offer.cipherLength;
    ++m_sessionChunksPublished;

    logInfo(QStringLiteral("HTTP Cache: published part %1 (%2 bytes) -> %3")
                .arg(offer.partIndex)
                .arg(offer.cipherLength)
                .arg(offer.url));

    // The queue has moved on during a multi-megabyte POST, so the group is
    // recomputed here rather than reused from when we decided to publish.
    offerToQueue(m_entries[key]);

    emit statsChanged();
}

HttpCacheManager::PublishBackoff HttpCacheManager::backoffFor(
    const HttpCachePublishResult& result, int priorServerFailures)
{
    PublishBackoff backoff;

    // Local failures — an unreadable part, a bad base url — say nothing about the
    // server, so only the part that failed stands down.
    backoff.serverWide = result.stage == HttpCachePublishStage::Transport
        || (result.stage == HttpCachePublishStage::Server
            && statusCondemnsTheServer(result.httpStatus));

    if (!backoff.serverWide) {
        backoff.seconds = kChunkCooldownSeconds;
        return backoff;
    }

    if (result.stage == HttpCachePublishStage::Server
        && statusNeedsOperatorAction(result.httpStatus)) {
        // Escalating here would be theatre: a rejected key or an oversized chunk
        // is not going to resolve itself on the fourth try.
        backoff.seconds = kServerRefusalBackoffSeconds;
    } else {
        const int steps = static_cast<int>(std::size(kServerBackoffSeconds));
        backoff.seconds = kServerBackoffSeconds[std::clamp(priorServerFailures, 0, steps - 1)];
    }

    // Retry-After is the server telling us what it wants; only let it push the
    // pause out, never pull it in, or a busy server could ask to be hammered.
    if (result.retryAfterSeconds > 0)
        backoff.seconds = std::max<qint64>(backoff.seconds, result.retryAfterSeconds);

    return backoff;
}

void HttpCacheManager::notePublishFailure(const QString& key,
                                          const HttpCachePublishResult& result)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const PublishBackoff backoff = backoffFor(result, m_serverFailures);

    if (!backoff.serverWide) {
        m_publishCooldown.insert(key, now + backoff.seconds);
        logWarning(QStringLiteral("HTTP Cache: publish failed: %1 — retrying that part in %2 min")
                       .arg(result.error)
                       .arg(backoff.seconds / 60));
        return;
    }

    ++m_serverFailures;
    m_serverBackoffUntil = now + backoff.seconds;
    m_backoffConfig = cacheServerFingerprint();

    logWarning(QStringLiteral("HTTP Cache: publish failed: %1 — pausing all uploads to the "
                              "cache server for %2 min (failure %3)")
                   .arg(result.error)
                   .arg(static_cast<double>(backoff.seconds) / 60.0, 0, 'g', 2)
                   .arg(m_serverFailures));
}

bool HttpCacheManager::serverIsCoolingDown() const
{
    if (m_serverBackoffUntil == 0)
        return false;

    // Pointed at a different server, or carrying a new key: that is a different
    // question, so the old answer does not apply.
    if (m_backoffConfig != cacheServerFingerprint())
        return false;

    return QDateTime::currentSecsSinceEpoch() < m_serverBackoffUntil;
}

void HttpCacheManager::offerToQueue(Entry& entry)
{
    UploadQueue* queue = theApp.uploadQueue;
    if (!queue || !entry.offerable)
        return;

    const uint32 part = entry.offer.partIndex;

    std::vector<UpDownClient*> targets;
    const auto collect = [&targets, &entry, part](UpDownClient* client) {
        if (!client || !client->supportsHttpCache())
            return;

        KnownFile* file = client->uploadFile();
        if (!file || std::memcmp(file->fileHash(), entry.offer.fileHash.data(), 16) != 0)
            return;

        if (client->isUpPartAvailable(part))
            return;

        // This runs every tick for the lifetime of the entry, so a peer that has
        // already been told must not be told again — otherwise a long-lived chunk
        // turns into a packet flood and the "saved" counter inflates without any
        // bytes actually being saved.
        if (entry.offeredTo.contains(QByteArray(reinterpret_cast<const char*>(client->userHash()),
                                                16)))
            return;

        targets.push_back(client);
    };

    queue->forEachUploading(collect);
    queue->forEachWaiting(collect);

    if (targets.empty())
        return;

    for (auto* peer : targets) {
        if (!sendOffer(peer, entry))
            continue;

        // The first peer offered a given chunk is the one whose demand paid for
        // the upload; every peer after that gets a part we never had to send
        // again. That difference is the entire economic argument for the feature,
        // so it is what "Upload Saved" counts.
        if (entry.offersSent > 1)
            m_sessionBytesSaved += entry.offer.plainLength;

        // The offer replaces the slot. A peer holding one is sent back to the
        // queue so the freed upstream goes to somebody the cache cannot help;
        // it will re-ask with OP_REQUESTPARTS for a different part when its turn
        // comes round again.
        if (peer->isUploadingToPeer())
            peer->sendOutOfPartReqsAndAddToWaitingQueue();
    }

    emit statsChanged();
}

bool HttpCacheManager::sendOffer(UpDownClient* peer, Entry& entry)
{
    auto packet = HttpCacheCodec::buildOffer(entry.offer);
    if (!packet)
        return false;

    // safeConnectAndSendPacket sends now when there is a socket, and otherwise
    // queues the packet and calls tryToConnect() — which is what reaches a
    // firewalled peer, via a direct UDP callback, the shared server's
    // OP_CALLBACKREQUEST, or a Kad buddy. A LowID peer that dropped its idle
    // queue connection still gets the offer.
    peer->safeConnectAndSendPacket(std::move(packet));

    entry.offeredTo.insert(QByteArray(reinterpret_cast<const char*>(peer->userHash()), 16));
    ++entry.offersSent;

    return true;
}

void HttpCacheManager::handleReport(UpDownClient* sender, const HttpCacheReport& report)
{
    const QString key = entryKey(report.fileHash, report.partIndex);

    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;

    if (report.result == HttpCacheResult::Ok) {
        logDebug(QStringLiteral("HTTP Cache: %1 fetched part %2 (%3 bytes)")
                     .arg(sender ? sender->userName() : QString())
                     .arg(report.partIndex)
                     .arg(report.bytesFetched));
        return;
    }

    // Only a verdict about the blob itself counts against the entry. "I have it
    // switched off" or "I am busy" says nothing about whether the chunk is good.
    const bool blamesTheChunk = report.result == HttpCacheResult::Corrupt
                             || report.result == HttpCacheResult::SizeMismatch
                             || report.result == HttpCacheResult::HttpFailed;

    if (!blamesTheChunk)
        return;

    if (++it->failures >= kMaxEntryFailures) {
        // Stop offering it, but leave it on the server to lapse at its TTL — it
        // may still be serving other peers perfectly well, and a DELETE here
        // would punish the chunk for somebody else's broken network.
        it->offerable = false;
        logWarning(QStringLiteral("HTTP Cache: part %1 failed %2 times, no longer offered")
                       .arg(report.partIndex)
                       .arg(it->failures));
    }
}

uint64 HttpCacheManager::publishRateBytesPerSecond() const
{
    if (const uint32 explicitKBs = thePrefs.httpCachePublishRateKBs(); explicitKBs != 0)
        return static_cast<uint64>(explicitKBs) * 1024;

    // Note the port's sentinel: 0 means *unlimited* here, where MFC used
    // UNLIMITED. Reading it as "no bandwidth" would throttle the publish to a
    // standstill on the most generously configured nodes.
    const uint32 uploadLimit = thePrefs.maxUploadLimit();
    if (uploadLimit == 0)
        return kDefaultPublishBytesPerSecond;

    // A quarter of the upload budget: enough to move a part in a couple of
    // minutes, little enough that ed2k uploads barely notice.
    return std::max<uint64>(16 * 1024, static_cast<uint64>(uploadLimit) * 1024 / 4);
}

// ---------------------------------------------------------------------------
// Downloader
// ---------------------------------------------------------------------------

bool HttpCacheManager::downloadEnabled() const
{
    return thePrefs.httpCacheEnabled() && thePrefs.httpCacheAllowDownload();
}

void HttpCacheManager::handleOffer(UpDownClient* sender, const HttpCacheOffer& offer)
{
    HttpCacheReport report;
    report.fileHash = offer.fileHash;
    report.partIndex = offer.partIndex;

    const auto decline = [&](HttpCacheResult why) {
        report.result = why;
        reply(sender, report, true);
    };

    if (!downloadEnabled()) {
        decline(HttpCacheResult::Disabled);
        return;
    }

    if (activeFetchCount() >= static_cast<int>(thePrefs.httpCacheMaxConcurrentFetches())) {
        decline(HttpCacheResult::Busy);
        return;
    }

    DownloadQueue* queue = theApp.downloadQueue;
    if (!queue) {
        decline(HttpCacheResult::NotWanted);
        return;
    }

    PartFile* file = queue->fileByID(offer.fileHash.data());
    if (!file) {
        decline(HttpCacheResult::NotWanted);
        return;
    }

    // An offer is an instruction to fetch from a URL the peer chose, so the peer
    // has to be someone we were already talking to about this file. Otherwise any
    // client that can reach us could point us at anything.
    if (sender->reqFile() != file) {
        logDebug(QStringLiteral("HTTP Cache: %1 offered a part of a file it is not a source for")
                     .arg(sender->userName()));
        decline(HttpCacheResult::BadOffer);
        return;
    }

    if (offer.partIndex >= file->partCount() || file->isComplete(offer.partIndex)) {
        decline(HttpCacheResult::NotWanted);
        return;
    }

    if (offer.expiresAt != 0
        && offer.expiresAt < QDateTime::currentSecsSinceEpoch() + kExpiryMarginSeconds) {
        decline(HttpCacheResult::BadOffer);
        return;
    }

    if (!urlIsAcceptable(offer.url)) {
        logWarning(QStringLiteral("HTTP Cache: refusing offer from %1 pointing at %2")
                       .arg(sender->userName(), offer.url));
        decline(HttpCacheResult::BadOffer);
        return;
    }

    const QString key = entryKey(offer.fileHash, offer.partIndex);
    if (m_fetches.contains(key)) {
        // Two uploaders offering the same part is the normal case once a chunk is
        // shared; taking both would double the download for nothing.
        decline(HttpCacheResult::Busy);
        return;
    }

    auto* client = new HttpCacheClient();
    connect(client, &HttpCacheClient::fetchFinished, this, &HttpCacheManager::onFetchFinished);

    std::array<uint8, 16> senderHash{};
    std::memcpy(senderHash.data(), sender->userHash(), senderHash.size());

    if (!client->beginFetch(offer, file, senderHash, sender->connectAddress())) {
        client->deleteLater();
        decline(HttpCacheResult::NotWanted);
        return;
    }

    m_fetches.insert(key, client);
    emit statsChanged();
}

void HttpCacheManager::handleCancel(UpDownClient* sender, const HttpCacheReport& report)
{
    Q_UNUSED(sender);

    const QString key = entryKey(report.fileHash, report.partIndex);

    if (auto* client = m_fetches.value(key, nullptr)) {
        logDebug(QStringLiteral("HTTP Cache: offer for part %1 withdrawn")
                     .arg(report.partIndex));
        client->sendCancelTransfer();
    }
}

void HttpCacheManager::onFetchFinished(HttpCacheClient* client, HttpCacheResult result,
                                       uint64 bytesFetched)
{
    if (!client)
        return;

    const QString key = entryKey(client->offer().fileHash, client->partIndex());
    m_fetches.remove(key);

    if (result == HttpCacheResult::Ok) {
        m_sessionBytesFetched += bytesFetched;
        ++m_sessionChunksFetched;

        // Ok here means the ciphertext matched the digest the peer pinned, not that
        // the plaintext is right — MD4 has not run yet and will not until the part
        // file flushes. Remember who to hold responsible until it does.
        if (m_fetchedFrom.size() >= kFetchLedgerMax)
            m_fetchedFrom.clear();
        m_fetchedFrom.insert(key, FetchedFrom{client->offeringPeerHash(),
                                              QDateTime::currentSecsSinceEpoch()});
    } else {
        logDebug(QStringLiteral("HTTP Cache: fetch of part %1 ended with code %2")
                     .arg(client->partIndex())
                     .arg(static_cast<int>(result)));
    }

    // Tell the uploader how it went, so a genuinely bad chunk stops being handed
    // around. Located by userhash: the peer may have reconnected since.
    if (theApp.clientList) {
        const auto& hash = client->offeringPeerHash();
        if (UpDownClient* peer = theApp.clientList->findByUserHash(hash.data(), 0, 0)) {
            HttpCacheReport report;
            report.fileHash = client->offer().fileHash;
            report.partIndex = client->partIndex();
            report.result = result;
            report.bytesFetched = bytesFetched;
            reply(peer, report, false);
        }
    }

    // The client is ours from handleOffer() to here — it joins the part file's
    // source list only for the duration of the fetch, so this is the one place it
    // can be freed. deleteLater(), not delete: we are inside its own
    // fetchFinished emission.
    client->deleteLater();

    emit statsChanged();
}

bool HttpCacheManager::hasFetchAttributionForTest(const std::array<uint8, 16>& fileHash,
                                                  uint32 partIndex) const
{
    return m_fetchedFrom.contains(entryKey(fileHash, partIndex));
}

void HttpCacheManager::reportPartCorrupt(const std::array<uint8, 16>& fileHash, uint32 partIndex)
{
    const QString key = entryKey(fileHash, partIndex);
    const auto it = m_fetchedFrom.constFind(key);
    if (it == m_fetchedFrom.constEnd())
        return;

    const FetchedFrom record = it.value();
    m_fetchedFrom.remove(key);

    logWarning(QStringLiteral("HTTP Cache: part %1 failed its hash after a cache fetch — "
                              "telling the peer that offered it")
                   .arg(partIndex));

    if (!theApp.clientList)
        return;

    // By userhash, not address: the report is worth sending even if the peer has
    // reconnected from somewhere else since the fetch.
    UpDownClient* peer = theApp.clientList->findByUserHash(record.peerHash.data(), 0, 0);
    if (!peer)
        return;

    HttpCacheReport report;
    report.fileHash = fileHash;
    report.partIndex = partIndex;
    report.result = HttpCacheResult::Corrupt;
    report.bytesFetched = 0;
    reply(peer, report, false);
}

void HttpCacheManager::reply(UpDownClient* peer, const HttpCacheReport& report, bool declined)
{
    if (!peer)
        return;

    auto packet = HttpCacheCodec::buildReport(report, declined);
    if (!packet)
        return;

    // Only worth an existing connection: a decline is not important enough to dial
    // a peer for, and by the time a callback completed the offer would be stale.
    if (peer->socket())
        peer->sendPacket(std::move(packet));
}

bool HttpCacheManager::urlIsAcceptable(const QString& url) const
{
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;

    const QString scheme = parsed.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;

    // A hostname is resolved later by URLClient and vetted then; a literal address
    // can be checked right now, before we spend a connection on it.
    const QHostAddress addr(parsed.host());
    if (addr.isNull())
        return true;

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const uint32 ip = qToBigEndian(addr.toIPv4Address());
        if (!isGoodIP(ip))
            return false;
        if (theApp.ipFilter && theApp.ipFilter->isFiltered(ip))
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------

QString HttpCacheManager::entryKey(const std::array<uint8, 16>& hash, uint32 part)
{
    return hashKey(hash) + QLatin1Char(':') + QString::number(part);
}

void HttpCacheManager::expireEntries()
{
    const auto now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        const uint32 expires = it->offer.expiresAt;
        if (expires != 0 && expires < now + kExpiryMarginSeconds)
            it = m_entries.erase(it);
        else
            ++it;
    }

    // A lapsed cooldown is just a key nobody will ever look up again; dropping it
    // here keeps the map the size of the parts currently sitting out.
    for (auto it = m_publishCooldown.begin(); it != m_publishCooldown.end();) {
        if (static_cast<qint64>(now) >= it.value())
            it = m_publishCooldown.erase(it);
        else
            ++it;
    }

    // Same for the attribution ledger. A part whose MD4 verdict never came is a
    // part that was never finished, and nobody is owed a report for it.
    for (auto it = m_fetchedFrom.begin(); it != m_fetchedFrom.end();) {
        if (static_cast<qint64>(now) - it.value().at > kFetchLedgerSeconds)
            it = m_fetchedFrom.erase(it);
        else
            ++it;
    }
}

void HttpCacheManager::rollDailyBudget()
{
    const qint64 today = QDateTime::currentSecsSinceEpoch() / 86400;
    if (today == m_budgetDay)
        return;

    m_budgetDay = today;
    m_publishedToday = 0;
}

} // namespace eMule
