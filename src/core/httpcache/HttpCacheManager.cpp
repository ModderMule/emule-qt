#include "pch.h"
/// @file HttpCacheManager.cpp
/// @brief The brain of HTTP Cache — implementation.

#include "httpcache/HttpCacheManager.h"

#include "app/AppContext.h"
#include "client/ClientStructs.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadSearchManager.h"
#include "httpcache/HttpCacheClient.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
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

    // Naming the rotation once at startup is what makes a later "publishing to X"
    // line legible. Hosts and key ids only — a key is never logged.
    for (const auto& server : thePrefs.httpCacheServers()) {
        logInfo(QStringLiteral("HTTP Cache: server %1%2%3")
                    .arg(QUrl(server.baseUrl).host(),
                         server.keyId.isEmpty() ? QString()
                                                : QStringLiteral(" (key %1)").arg(server.keyId),
                         server.enabled ? QString() : QStringLiteral(" [disabled]")));
    }
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
    m_serverHealth.clear();
    m_publishCursor = 0;
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

    // Independent of offersEnabled(): looking for chunks is a download-side activity
    // and does not need us to be able to offer anything.
    lookupKadChunks();

    if (!offersEnabled())
        return;

    // Re-offer anything already published and still good before spending a new
    // upload: the whole point is that one chunk serves many peers over time.
    // Only peers that have not already been told are targeted (see Entry::offeredTo).
    //
    // Gated on offersEnabled(), not uploadEnabled(): handing out a chunk needs no base
    // url and no API key, so a node that only relays reaches this loop while skipping
    // everything below it.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->offerable)
            offerToQueue(*it);
    }

    if (!uploadEnabled())
        return;

    if (m_activePublishes >= static_cast<int>(thePrefs.httpCacheMaxConcurrentPublishes()))
        return;

    // The rotation is recomputed per candidate rather than once for the tick, so
    // two chunks published in the same pass go to different servers.
    //
    // Deliberately after the re-offer loop: a POST endpoint that is refusing says
    // nothing about the chunks already sitting on that server, and peers should
    // keep being pointed at them.
    const QList<HttpCacheServerConfig> servers = thePrefs.httpCacheServers();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (const auto& candidate : findCandidates()) {
        if (m_activePublishes >= static_cast<int>(thePrefs.httpCacheMaxConcurrentPublishes()))
            break;

        const int index = chooseServer(servers, m_serverHealth, m_publishCursor, now);
        if (index < 0)
            break;   // every configured server is sick, disabled or unconfigured

        publish(candidate, servers.at(index));
    }
}

// ---------------------------------------------------------------------------
// Uploader
// ---------------------------------------------------------------------------

bool HttpCacheManager::uploadEnabled() const
{
    if (!thePrefs.httpCacheEnabled() || !thePrefs.httpCacheAllowUpload())
        return false;

    // One usable account is enough. Whether any of them is *healthy* is a
    // different question, asked per publish by chooseServer(): a node whose only
    // server is in backoff still re-offers the chunks already up there.
    const auto servers = thePrefs.httpCacheServers();
    return std::ranges::any_of(servers, [](const HttpCacheServerConfig& server) {
        return server.enabled && !server.baseUrl.isEmpty() && !server.apiKey.isEmpty();
    });
}

QString HttpCacheManager::serverFingerprint(const HttpCacheServerConfig& server)
{
    // A newline separator so the two halves cannot be confused for one another.
    return server.baseUrl + QLatin1Char('\n') + server.apiKey;
}

bool HttpCacheManager::serverIsUsable(const HttpCacheServerConfig& server,
                                      const QHash<QString, ServerHealth>& health, qint64 now)
{
    if (!server.enabled || server.baseUrl.isEmpty() || server.apiKey.isEmpty())
        return false;

    const auto it = health.constFind(server.baseUrl);
    if (it == health.constEnd() || it->backoffUntil == 0)
        return true;

    // A pause taken against a different credential is an answer to a question
    // nobody is asking any more: re-applying the link is the operator saying they
    // have fixed it, so it must not have to be waited out.
    if (it->fingerprint != serverFingerprint(server))
        return true;

    return now >= it->backoffUntil;
}

int HttpCacheManager::chooseServer(const QList<HttpCacheServerConfig>& servers,
                                   const QHash<QString, ServerHealth>& health, quint64 cursor,
                                   qint64 now)
{
    const auto count = static_cast<quint64>(servers.size());
    if (count == 0)
        return -1;

    for (quint64 step = 0; step < count; ++step) {
        const int index = static_cast<int>((cursor + step) % count);
        if (serverIsUsable(servers.at(index), health, now))
            return index;
    }

    return -1;
}

std::vector<HttpCacheOffer>
HttpCacheManager::kadChunksForFile(const std::array<uint8, 16>& fileHash) const
{
    std::vector<HttpCacheOffer> result;

    if (!thePrefs.httpCachePublishToKad() || !uploadEnabled())
        return result;

    const auto now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    for (const auto& entry : m_entries) {
        if (entry.relayed || !entry.offerable)
            continue;
        if (entry.offer.fileHash != fileHash)
            continue;
        // A record sits on a storing node for KADEMLIAREPUBLISHTIMES, so a chunk that
        // is nearly out of time would be advertised long past its own death.
        if (entry.offer.expiresAt == 0 || entry.offer.expiresAt < now + kExpiryMarginSeconds)
            continue;
        if (entry.offer.url.size() > KADHC_MAX_URL_LEN)
            continue;

        result.push_back(entry.offer);
    }

    // Longest-lived last, then keep the longest-lived: with room for only a few, the
    // ones that will still be there when somebody looks are worth more.
    std::ranges::sort(result, [](const HttpCacheOffer& a, const HttpCacheOffer& b) {
        return a.expiresAt > b.expiresAt;
    });
    if (result.size() > KADHC_MAX_CHUNKS)
        result.resize(KADHC_MAX_CHUNKS);

    return result;
}

bool HttpCacheManager::relayEnabled() const
{
    // Deliberately no base url and no API key: passing on somebody else's chunk needs
    // no cache account, because we never talk to the server at all — we hand the peer
    // a URL and it fetches for itself.
    return thePrefs.httpCacheEnabled() && thePrefs.httpCacheAllowRelay();
}

bool HttpCacheManager::offersEnabled() const
{
    return uploadEnabled() || relayEnabled();
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

void HttpCacheManager::publish(const Candidate& candidate, const HttpCacheServerConfig& server)
{
    if (!candidate.file || server.baseUrl.isEmpty())
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
    request.baseUrl = server.baseUrl;
    request.apiKey = server.apiKey;
    request.ttlSeconds = thePrefs.httpCacheChunkTtlSeconds();
    request.rateBytesPerSecond = publishRateBytesPerSecond();
    request.fileHash = fileHash;
    request.partIndex = candidate.partIndex;
    request.dataFilePath = candidate.file->dataFilePath();
    request.partOffset = static_cast<uint64>(candidate.partIndex) * PARTSIZE;
    request.partLength = PARTSIZE;

    logInfo(QStringLiteral("HTTP Cache: publishing part %1 of %2 to %3 for %4 peers")
                .arg(candidate.partIndex)
                .arg(candidate.file->fileName(), QUrl(server.baseUrl).host())
                .arg(candidate.peers.size()));

    m_publishing.insert(key, true);
    ++m_activePublishes;

    // Advanced here rather than on success: a server that fails still spends its
    // turn, so the next chunk moves on instead of retrying the same one.
    ++m_publishCursor;

    const QString serverBaseUrl = server.baseUrl;

    auto* publisher = new HttpCachePublisher(this);
    connect(publisher, &HttpCachePublisher::finished, this,
            [this, key, serverBaseUrl](const HttpCachePublishResult& result,
                                       const HttpCacheOffer& offer) {
                m_publishing.remove(key);
                --m_activePublishes;
                onPublishFinished(key, serverBaseUrl, result, offer);
            });

    publisher->start(request);
}

void HttpCacheManager::onPublishFinished(const QString& key, const QString& serverBaseUrl,
                                         const HttpCachePublishResult& result,
                                         const HttpCacheOffer& offer)
{
    if (!result.ok) {
        notePublishFailure(key, serverBaseUrl, result);
        return;
    }

    // A chunk got through, so whatever *this* server was doing, it has stopped.
    // The others keep their own verdicts.
    m_publishCooldown.remove(key);
    m_serverHealth.remove(serverBaseUrl);

    Entry entry;
    entry.offer = offer;
    entry.chunkId = result.chunkId;
    entry.serverBaseUrl = serverBaseUrl;

    m_entries.insert(key, entry);

    // The Kad source record for this file now advertises one more chunk than it did.
    nudgeKadRepublish(offer.fileHash);

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

void HttpCacheManager::notePublishFailure(const QString& key, const QString& serverBaseUrl,
                                          const HttpCachePublishResult& result)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString host = QUrl(serverBaseUrl).host();

    // Escalation is per server: a run of failures on one says nothing about the
    // next one's, so each keeps its own place in the table.
    ServerHealth& health = m_serverHealth[serverBaseUrl];
    const PublishBackoff backoff = backoffFor(result, health.failures);

    if (!backoff.serverWide) {
        // The part stands down, not the server — and deliberately for every
        // server, because an unreadable part file is not going to read any better
        // for the next one in the rotation.
        m_publishCooldown.insert(key, now + backoff.seconds);
        if (health.backoffUntil == 0)
            m_serverHealth.remove(serverBaseUrl);
        logWarning(QStringLiteral("HTTP Cache: publish failed: %1 — retrying that part in %2 min")
                       .arg(result.error)
                       .arg(backoff.seconds / 60));
        return;
    }

    const auto servers = thePrefs.httpCacheServers();
    const auto configured = std::ranges::find_if(servers, [&serverBaseUrl](const auto& server) {
        return server.baseUrl == serverBaseUrl;
    });

    ++health.failures;
    health.backoffUntil = now + backoff.seconds;
    // Empty when the server was removed from the configuration mid-publish, which
    // makes the pause moot: chooseServer() never looks at a server that is gone.
    health.fingerprint =
        configured == servers.end() ? QString() : serverFingerprint(*configured);

    // How many servers are left is the difference between a hiccup and the
    // offload going quiet, so the line says which one this is.
    const auto left = std::ranges::count_if(servers, [this, now](const auto& server) {
        return serverIsUsable(server, m_serverHealth, now);
    });

    logWarning(QStringLiteral("HTTP Cache: publish to %1 failed: %2 — pausing that server for "
                              "%3 min (failure %4); %5")
                   .arg(host, result.error)
                   .arg(static_cast<double>(backoff.seconds) / 60.0, 0, 'g', 2)
                   .arg(health.failures)
                   .arg(left == 0 ? QStringLiteral("no cache server is usable right now")
                                  : QStringLiteral("%1 still in the rotation").arg(left)));
}

bool HttpCacheManager::serverIsCoolingDown(const QString& baseUrl) const
{
    const auto it = m_serverHealth.constFind(baseUrl);
    if (it == m_serverHealth.constEnd() || it->backoffUntil == 0)
        return false;

    return QDateTime::currentSecsSinceEpoch() < it->backoffUntil;
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

        // The first peer offered a chunk we published is the one whose demand paid for
        // the upload; every peer after that gets a part we never had to send again.
        // That difference is the entire economic argument for the feature, so it is
        // what "Upload Saved" counts. A relayed chunk cost us no upload at all, so
        // every one of its offers is a saving, the first included.
        if (entry.relayed || entry.offersSent > 1)
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

void HttpCacheManager::nudgeKadRepublish(const std::array<uint8, 16>& fileHash) const
{
    if (!thePrefs.httpCachePublishToKad() || !theApp.sharedFileList)
        return;

    KnownFile* file = theApp.sharedFileList->getFileByID(fileHash.data());
    if (!file)
        return;

    // Source publishing is on a five-hour cycle (KADEMLIAREPUBLISHTIMES) and a storing
    // node expires the record on the same clock, but chunks come and go on a TTL of
    // hours. Without this a chunk published just after a republish would go unadvertised
    // for most of its life. Zeroing the timestamp is the existing "publish this one
    // next" signal — SharedFileList::publish() uses it as its retry path — and the
    // one-file-per-tick round robin plus KADEMLIATOTALSTORESRC keep the cost bounded.
    file->setLastPublishTimeKadSrc(0, 0);
}

void HttpCacheManager::lookupKadChunks()
{
    if (!downloadEnabled() || !thePrefs.httpCacheFetchFromKad())
        return;

    auto* kadInst = kad::Kademlia::instance();
    if (!kadInst || !kadInst->isConnected())
        return;

    DownloadQueue* queue = theApp.downloadQueue;
    if (!queue)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (PartFile* file : queue->files()) {
        if (!file || file->isStopped() || file->isPaused())
            continue;
        if (file->status() != PartFileStatus::Ready && file->status() != PartFileStatus::Empty)
            continue;
        if (file->gapList().empty())
            continue;

        const QString hashKey = QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16).toHex());
        const auto last = m_kadLookupAt.constFind(hashKey);
        if (last != m_kadLookupAt.constEnd() && now - last.value() < kKadLookupIntervalSeconds)
            continue;

        // PartFile::process() runs this same lookup type, but only while the file is
        // short of sources — so a popular file, which is exactly where a cache chunk is
        // worth most, never fires one. prepareLookup() dedups by target, so when the
        // download queue already has one running for this file ours is simply refused
        // and the results reach the same parser anyway.
        kad::UInt128 target;
        target.setValueBE(file->fileHash());
        if (kad::SearchManager::prepareLookup(kad::SearchType::File, true, target))
            m_kadLookupAt.insert(hashKey, now);
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

void HttpCacheManager::addKadChunks(const std::vector<HttpCacheOffer>& chunks)
{
    if (!downloadEnabled() || !thePrefs.httpCacheFetchFromKad())
        return;

    DownloadQueue* queue = theApp.downloadQueue;
    if (!queue)
        return;

    for (const auto& offer : chunks) {
        if (activeFetchCount() >= static_cast<int>(thePrefs.httpCacheMaxConcurrentFetches()))
            return;

        if (!offer.isWellFormed())
            continue;

        // An ed2k offer is validated against "the peer sending it is already a source
        // for this file" (spec §6, rule 4), which is what stops an arbitrary client
        // pointing us at an arbitrary URL. There is no sender here, so the replacement
        // is that *we* chose the file hash: the record only reaches us because we
        // looked that hash up ourselves. Everything else is the same structural
        // validation an offer gets.
        PartFile* file = queue->fileByID(offer.fileHash.data());
        if (!file)
            continue;

        if (offer.partIndex >= file->partCount() || file->isComplete(offer.partIndex))
            continue;

        // Whole parts only, which is what let the lengths be derived rather than sent.
        // A record naming the short tail part is either a bug or a lie; either way the
        // derived plainLength would be wrong for it.
        const uint64 partEnd = static_cast<uint64>(offer.partIndex + 1) * PARTSIZE;
        if (partEnd > static_cast<uint64>(file->fileSize()))
            continue;

        if (offer.expiresAt == 0
            || offer.expiresAt < QDateTime::currentSecsSinceEpoch() + kExpiryMarginSeconds) {
            continue;
        }

        if (m_kadBadUrls.contains(offer.url))
            continue;

        if (!urlIsAcceptable(offer.url)) {
            logWarning(QStringLiteral("HTTP Cache: refusing a Kad chunk record pointing at %1")
                           .arg(offer.url));
            continue;
        }

        const QString key = entryKey(offer.fileHash, offer.partIndex);
        if (m_fetches.contains(key))
            continue;

        auto* client = new HttpCacheClient();
        connect(client, &HttpCacheClient::fetchFinished, this, &HttpCacheManager::onFetchFinished);

        // A null peer hash and — the part that matters — a null Address. PartFile::
        // writeToBuffer() skips the corruption black box entirely for a null sender,
        // which is exactly right: nobody vouched for these bytes, so nobody may be
        // blamed for them. Passing anything else would hand soleSenderOfWholePart() an
        // address that did not send the data — the cache server's — and ban it.
        const std::array<uint8, 16> noPeer{};
        if (!client->beginFetch(offer, file, noPeer, Address())) {
            client->deleteLater();
            continue;
        }

        m_fetches.insert(key, client);
        m_kadFetches.insert(key);

        logInfo(QStringLiteral("HTTP Cache: fetching part %1 of '%2' from a chunk found in Kad")
                    .arg(offer.partIndex).arg(file->fileName()));
    }

    emit statsChanged();
}

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

    const bool fromKad = m_kadFetches.remove(key);

    if (result == HttpCacheResult::Ok) {
        m_sessionBytesFetched += bytesFetched;
        ++m_sessionChunksFetched;

        // Ok here means the ciphertext matched the digest the peer pinned, not that
        // the plaintext is right — MD4 has not run yet and will not until the part
        // file flushes. Remember who to hold responsible until it does.
        if (m_fetchedFrom.size() >= kFetchLedgerMax)
            m_fetchedFrom.clear();
        m_fetchedFrom.insert(key, FetchedFrom{client->offeringPeerHash(),
                                              QDateTime::currentSecsSinceEpoch(),
                                              fromKad,
                                              client->offer()});
    } else {
        logDebug(QStringLiteral("HTTP Cache: fetch of part %1 ended with code %2")
                     .arg(client->partIndex())
                     .arg(static_cast<int>(result)));
    }

    if (result != HttpCacheResult::Ok && fromKad) {
        // No peer to tell and no three-strike counter to run, so the only thing that
        // stops the next lookup walking back into a bad record is remembering the URL.
        if (m_kadBadUrls.size() >= kKadBadUrlMax)
            m_kadBadUrls.clear();
        m_kadBadUrls.insert(client->offer().url);
    }

    // Tell the uploader how it went, so a genuinely bad chunk stops being handed
    // around. Located by userhash: the peer may have reconnected since. Skipped for a
    // Kad-discovered chunk, whose peer hash is all zeroes — there is nobody to address,
    // and looking one up by that hash would find a peer at random.
    if (theApp.clientList && !fromKad) {
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

bool HttpCacheManager::hasRelayEntryForTest(const std::array<uint8, 16>& fileHash,
                                            uint32 partIndex) const
{
    const auto it = m_entries.constFind(entryKey(fileHash, partIndex));
    return it != m_entries.constEnd() && it->relayed;
}

bool HttpCacheManager::wasOfferedToForTest(const std::array<uint8, 16>& fileHash,
                                           uint32 partIndex,
                                           const std::array<uint8, 16>& peerHash) const
{
    const auto it = m_entries.constFind(entryKey(fileHash, partIndex));
    if (it == m_entries.constEnd())
        return false;
    return it->offeredTo.contains(
        QByteArray(reinterpret_cast<const char*>(peerHash.data()), 16));
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

    // If an earlier flush already promoted this chunk for relay, stop handing it out.
    // Reachable when a part verifies and later fails — a disk fault, or an AICH
    // recovery that rewrote it — and the alternative is passing bad bytes around under
    // our own name.
    if (const auto entry = m_entries.constFind(key);
        entry != m_entries.constEnd() && entry->relayed) {
        m_entries.remove(key);
        logWarning(QStringLiteral("HTTP Cache: withdrawing relayed chunk for part %1 — "
                                  "the part failed its hash here")
                       .arg(partIndex));
    }

    if (record.fromKad) {
        // Nobody offered this part — it came out of a Kad record. Blame is already
        // withheld at the source (writeToBuffer got a null sender), so all that is
        // left is to stop trusting the URL.
        logWarning(QStringLiteral("HTTP Cache: part %1 failed its hash after a Kad chunk "
                                  "fetch — no peer to blame, dropping the URL")
                       .arg(partIndex));
        if (m_kadBadUrls.size() >= kKadBadUrlMax)
            m_kadBadUrls.clear();
        m_kadBadUrls.insert(record.offer.url);
        return;
    }

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

void HttpCacheManager::reportPartVerified(const std::array<uint8, 16>& fileHash,
                                          uint32 partIndex)
{
    if (!relayEnabled())
        return;

    const QString key = entryKey(fileHash, partIndex);

    // Already offering this chunk — either our own publish or a promotion from an
    // earlier flush. flushBuffer() re-verifies every complete part on every flush, so
    // this is the ordinary case once a part is done, not an edge case.
    if (m_entries.contains(key))
        return;

    // Not a part we fetched over the cache. Also the ordinary case: every part of every
    // download reaches here, and only the handful we pulled from a chunk are in the
    // ledger.
    const auto it = m_fetchedFrom.constFind(key);
    if (it == m_fetchedFrom.constEnd())
        return;

    const HttpCacheOffer& offer = it->offer;
    if (!offer.isWellFormed())
        return;

    // A chunk with no stated expiry is one we would hold forever: expireEntries()
    // reads 0 as "never lapses", which is correct for a blob we own and wrong for one
    // we are only borrowing, since the owner can DELETE it at any moment.
    if (offer.expiresAt == 0)
        return;

    if (offer.expiresAt < QDateTime::currentSecsSinceEpoch() + kExpiryMarginSeconds)
        return;

    Entry entry;
    entry.offer = offer;
    entry.relayed = true;          // chunkId stays empty — the blob is not ours to delete
    entry.offerable = true;

    // Never offer a chunk back to the peer that gave it to us. Nothing to exclude for a
    // Kad-discovered chunk — no peer handed it over — and seeding the all-zero hash
    // would silently exclude any peer whose userhash happened to be zero.
    if (!it->fromKad)
        entry.offeredTo.insert(QByteArray(reinterpret_cast<const char*>(it->peerHash.data()), 16));

    m_entries.insert(key, std::move(entry));

    logInfo(QStringLiteral("HTTP Cache: part %1 verified — relaying its chunk to peers "
                           "that still need it")
                .arg(partIndex));

    emit statsChanged();
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

bool HttpCacheManager::urlIsAcceptable(const QString& url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;

    const QString scheme = parsed.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;

    // A hostname is vetted after resolution, in URLClient; a literal can be checked
    // right now, before we spend a connection on it.
    //
    // QUrl::host() hands back an IPv6 literal with its brackets already stripped,
    // which is exactly the form setAddress() wants — nothing to unwrap.
    QHostAddress parsedHost;
    if (!parsedHost.setAddress(parsed.host()))
        return true;   // a name, not a literal

    // Anything that will not convert is the wildcard (0.0.0.0 or ::), which Address
    // has no family for and which is never a cache host. It has to be rejected here
    // rather than fall through, or it reads as "not a literal" and gets waved past.
    const Address literal = Address::fromQHostAddress(parsedHost);
    if (literal.isNull())
        return false;

    // isGoodIP screens both families, and fromQHostAddress() has already folded
    // ::ffff:a.b.c.d back to plain IPv4 — QHostAddress::protocol() still calls that
    // IPv6, so a v4-mapped literal used to walk straight past the IPv4-only test
    // that lived here, loopback and all.
    if (!isGoodIP(literal))
        return false;

    return !(theApp.ipFilter && theApp.ipFilter->isFiltered(literal));
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

        // A chunk of ours that downloaders gave up on, sitting on a server that is
        // itself in backoff: the server is the thing implicated, so let the part go
        // and let the candidate scan publish it again — the rotation will pick a
        // different server, because this one is being skipped.
        //
        // Deliberately narrow. An entry retired while its server is healthy points
        // at a blob nobody could use, and re-uploading it elsewhere would only move
        // the problem.
        const bool retiredOnASickServer =
            !it->offerable && !it->relayed && serverIsCoolingDown(it->serverBaseUrl);

        if ((expires != 0 && expires < now + kExpiryMarginSeconds) || retiredOnASickServer) {
            const bool wasOurs = !it->relayed;
            const auto fileHash = it->offer.fileHash;
            it = m_entries.erase(it);
            // Only our own chunks are ever in the record, so only their loss changes it.
            if (wasOurs)
                nudgeKadRepublish(fileHash);
        } else {
            ++it;
        }
    }

    // Health for servers that are no longer configured is an answer to a question
    // nobody can ask any more.
    if (!m_serverHealth.isEmpty()) {
        const auto servers = thePrefs.httpCacheServers();
        for (auto it = m_serverHealth.begin(); it != m_serverHealth.end();) {
            const bool stillConfigured =
                std::ranges::any_of(servers, [&it](const HttpCacheServerConfig& server) {
                    return server.baseUrl == it.key();
                });
            it = stillConfigured ? std::next(it) : m_serverHealth.erase(it);
        }
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
