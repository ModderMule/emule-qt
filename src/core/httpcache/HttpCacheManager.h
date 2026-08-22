#pragma once

/// @file HttpCacheManager.h
/// @brief The brain of HTTP Cache — decides what to publish, to whom, and when.
///
/// PeerCache's successor. The idea it keeps: when several peers all want the same
/// bytes, spend the upstream once and let them fetch it over HTTP. The parts it
/// drops: ISP-specific cache discovery, plaintext on the wire, and one round trip
/// per 180 KB block.
///
/// Uploader flow, once every kTickIntervalMs:
///
///   1. group the upload queue by (file, part), keeping only HTTP-Cache-capable
///      peers that are missing that part;
///   2. seed the grouping from a part somebody is actively requesting, so nothing
///      speculative is ever published;
///   3. if the group is at least minClients and we hold a full part, encrypt it
///      under a fresh random key and POST it to the cache server;
///   4. only once that succeeds, hand every capable peer the URL and the key —
///      and release the ed2k slot of any peer that held one, so the freed
///      upstream goes to somebody the cache cannot help.
///
/// Downloader flow: validate the offer hard, fetch it with an HttpCacheClient,
/// let the ordinary MD4 part hash decide whether the bytes were good, and report
/// the outcome back so the uploader learns something.

#include "httpcache/HttpCacheOffer.h"
#include "httpcache/HttpCachePublisher.h"
#include "utils/Types.h"

#include <QHash>
#include <QSet>
#include <QObject>
#include <QString>

#include <array>
#include <vector>

class QTimer;

namespace eMule {

class HttpCacheClient;
class KnownFile;
class PartFile;
class UpDownClient;

class HttpCacheManager : public QObject {
    Q_OBJECT

public:
    /// How often the upload queue is scanned. Long enough that the scan is free,
    /// short enough that a chunk is offered while the peers still want it.
    static constexpr int kTickIntervalMs = 5000;

    /// Consecutive failed fetches after which an entry stops being offered. It is
    /// not deleted from the server: a failure says as much about the downloader
    /// or the network as about the blob, and other peers may be using it happily.
    static constexpr uint32 kMaxEntryFailures = 3;

    /// How long a finished fetch stays attributable, and how many at once. The MD4
    /// verdict arrives whenever the part file next flushes its buffer, which is a
    /// matter of seconds on a live download but unbounded on a stalled one.
    static constexpr qint64 kFetchLedgerSeconds = 3600;
    static constexpr int kFetchLedgerMax = 256;

    /// How long one part sits out after a publish failure that was about that
    /// part rather than about the server — an unreadable block, a body the
    /// server said was short. Long enough that a stuck part is not re-encrypted
    /// and re-POSTed every tick.
    static constexpr qint64 kChunkCooldownSeconds = 600;

    /// Escalating pause after a server-side publish failure, indexed by the run
    /// of consecutive failures. Without this a server answering 500 collects a
    /// fresh 9.28 MB POST every kTickIntervalMs, for as long as it stays broken.
    static constexpr qint64 kServerBackoffSeconds[] = {60, 300, 900, 1800};

    /// Pause after a refusal that a retry cannot fix on its own: a rejected API
    /// key, a chunk size this server will never accept, an exhausted quota. The
    /// operator has to change something, so the retry is a slow re-probe rather
    /// than a backoff.
    static constexpr qint64 kServerRefusalBackoffSeconds = 1800;

    /// What a failed publish costs: how long to wait, and whether the wait
    /// applies to the whole server or only to the part that just failed.
    struct PublishBackoff {
        bool serverWide = false;
        qint64 seconds = 0;
    };

    /// The retry policy, as a pure function of the failure and the run of
    /// server-side failures before it (0 for the first).
    ///
    /// Public and static so the table above can be pinned by a test without
    /// standing up an upload queue, a listen socket and a set of peers.
    [[nodiscard]] static PublishBackoff backoffFor(const HttpCachePublishResult& result,
                                                   int priorServerFailures);

    explicit HttpCacheManager(QObject* parent = nullptr);
    ~HttpCacheManager() override;

    void start();
    void stop();

    /// One OP_HTTPCACHE packet, routed here by UpDownClient.
    void handlePacket(UpDownClient* sender, const uint8* data, uint32 size);

    /// A part we filled from a cache chunk has failed its MD4 check.
    ///
    /// Called by PartFile long after the fetch ended (see the ledger note on
    /// m_fetchedFrom), and silently ignored for a part we did not fetch that way.
    /// The ban is PartFile's business; this only tells the uploader, so its own
    /// three-strike counter retires the chunk rather than handing it to the next
    /// downloader.
    void reportPartCorrupt(const std::array<uint8, 16>& fileHash, uint32 partIndex);

    /// Is this part still attributable to the peer that offered it? The ledger is
    /// otherwise invisible, and "the late report went nowhere" and "the ledger was
    /// never filled" look identical from outside.
    [[nodiscard]] bool hasFetchAttributionForTest(const std::array<uint8, 16>& fileHash,
                                                  uint32 partIndex) const;

    // -- Session counters (Statistics mirrors these into its cumulative totals) --

    /// Ciphertext bytes we pushed to the cache server.
    [[nodiscard]] uint64 sessionBytesPublished() const { return m_sessionBytesPublished; }
    /// Plaintext bytes we pulled back out of it.
    [[nodiscard]] uint64 sessionBytesFetched() const { return m_sessionBytesFetched; }
    /// Upstream we did not have to spend: every extra peer served from one upload.
    [[nodiscard]] uint64 sessionBytesSaved() const { return m_sessionBytesSaved; }
    /// Distinct parts published.
    [[nodiscard]] uint32 sessionChunksPublished() const { return m_sessionChunksPublished; }
    /// Offers accepted and successfully fetched.
    [[nodiscard]] uint32 sessionChunksFetched() const { return m_sessionChunksFetched; }

    /// Live counts, for the log and the statistics tree.
    [[nodiscard]] int activePublishCount() const { return m_activePublishes; }
    [[nodiscard]] int activeFetchCount() const;
    [[nodiscard]] int liveEntryCount() const { return static_cast<int>(m_entries.size()); }

signals:
    void statsChanged();

private slots:
    void process();

private:
    // Private helpers after the public interface, per the house style.

    /// A chunk we published and may still hand out.
    struct Entry {
        HttpCacheOffer offer;
        QString chunkId;
        uint32 failures = 0;
        uint32 offersSent = 0;
        bool offerable = true;
        /// Userhashes already told about this chunk. offerToQueue() runs every
        /// tick for the entry's whole lifetime, so without this a long-lived
        /// chunk would re-offer itself to the same peers forever.
        QSet<QByteArray> offeredTo;
    };

    /// One (file, part) the queue says is worth publishing.
    struct Candidate {
        KnownFile* file = nullptr;
        uint32 partIndex = 0;
        std::vector<UpDownClient*> peers;
    };

    // -- Uploader ------------------------------------------------------------

    [[nodiscard]] bool uploadEnabled() const;
    [[nodiscard]] std::vector<Candidate> findCandidates() const;
    void publish(const Candidate& candidate);
    void onPublishFinished(const QString& key, const HttpCachePublishResult& result,
                           const HttpCacheOffer& offer);

    /// Turn a failed publish into a cooldown, so the next tick does not simply
    /// repeat it. Scoped to the chunk or to the whole server depending on what
    /// the failure actually says.
    void notePublishFailure(const QString& key, const HttpCachePublishResult& result);

    /// Would publishing anything at all right now just repeat a known failure?
    [[nodiscard]] bool serverIsCoolingDown() const;
    void offerToQueue(Entry& entry);
    [[nodiscard]] bool sendOffer(UpDownClient* peer, Entry& entry);
    void handleReport(UpDownClient* sender, const HttpCacheReport& report);

    /// Bytes per second the publisher may use, from prefs or the upload limit.
    [[nodiscard]] uint64 publishRateBytesPerSecond() const;

    // -- Downloader ----------------------------------------------------------

    [[nodiscard]] bool downloadEnabled() const;
    void handleOffer(UpDownClient* sender, const HttpCacheOffer& offer);
    void handleCancel(UpDownClient* sender, const HttpCacheReport& report);
    void onFetchFinished(HttpCacheClient* client, HttpCacheResult result, uint64 bytesFetched);
    void reply(UpDownClient* peer, const HttpCacheReport& report, bool declined);

    /// Would fetching this URL send us somewhere we should not go?
    [[nodiscard]] bool urlIsAcceptable(const QString& url) const;

    // -- Shared --------------------------------------------------------------

    [[nodiscard]] static QString entryKey(const std::array<uint8, 16>& hash, uint32 part);
    void expireEntries();
    void rollDailyBudget();

    QTimer* m_timer = nullptr;

    /// Published chunks, keyed by fileHash+part.
    QHash<QString, Entry> m_entries;

    /// In-flight fetches, keyed the same way, so two peers offering the same part
    /// cannot start two downloads of it.
    QHash<QString, HttpCacheClient*> m_fetches;

    /// Who gave us a part, kept past the end of the fetch.
    ///
    /// A fetch reports Ok the moment the ciphertext digest matches, which is well
    /// before PartFile buffers the plaintext and runs MD4 over the finished part.
    /// By then the HttpCacheClient is deleted and its m_fetches entry erased, so
    /// the late verdict has nothing left to address. Bounded and swept by age —
    /// a part file that stalls must not let this grow without limit.
    struct FetchedFrom {
        std::array<uint8, 16> peerHash{};
        qint64 at = 0;
    };
    QHash<QString, FetchedFrom> m_fetchedFrom;

    int m_activePublishes = 0;

    /// Publishes in progress, keyed by entry key, so the scan does not queue the
    /// same part twice while its POST is still running.
    QHash<QString, bool> m_publishing;

    /// Entry key -> unix time before which that part must not be published again.
    QHash<QString, qint64> m_publishCooldown;

    /// Unix time before which nothing is published at all, and the run of
    /// server-side failures that set it. Offering chunks that are already up
    /// there continues regardless — a broken POST endpoint says nothing about
    /// the blobs the server is already serving.
    qint64 m_serverBackoffUntil = 0;
    int m_serverFailures = 0;

    /// The base url and api key the backoff was set against. Changing either in
    /// the options is the user answering the "check your configuration" warning,
    /// so it clears the pause instead of leaving them to wait it out.
    QString m_backoffConfig;

    uint64 m_publishedToday = 0;
    qint64 m_budgetDay = 0;    ///< days since epoch the counter belongs to

    uint64 m_sessionBytesPublished = 0;
    uint64 m_sessionBytesFetched = 0;
    uint64 m_sessionBytesSaved = 0;
    uint32 m_sessionChunksPublished = 0;
    uint32 m_sessionChunksFetched = 0;
};

} // namespace eMule
