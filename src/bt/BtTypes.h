#pragma once

/// @file BtTypes.h
/// @brief What eMule::Bt hands out: plain Qt values, no libtorrent type (research doc §4).

#include <QByteArray>
#include <QList>
#include <QString>

#include <array>

namespace eMule::bt {

/// The session's settings, resolved by the daemon from Preferences.
struct BtSettings {
    quint16 listenPort = 6881;
    /// where new torrents are saved; never empty
    QString downloadDir;
    /// <config>/BT: session.dat and resume/
    QString stateDir;
    /// 0 = unlimited
    quint32 maxDownloadKb = 0;
    quint32 maxUploadKb = 0;
    bool dht = true;
    bool lsd = true;
    /// applied when the session starts
    bool pex = true;
    /// 0 enabled (prefer), 1 forced, 2 disabled
    int encryption = 0;
    int maxActiveDownloads = 3;
    int maxActiveSeeds = 5;
    /// peer connections, session-wide and the default per torrent
    int maxConnections = 200;
    int maxConnectionsPerTorrent = 50;
    /// stop seeding at this ratio, percent; 0 = no limit
    int seedRatioPercent = 100;
    /// stop seeding after this long; 0 = no limit
    int seedTimeLimitMinutes = 0;
    /// "eMuleQt/0.6.26", and its numbers for the peer ID
    QString userAgent;
    int versionMajor = 0;
    int versionMinor = 0;
    int versionRevision = 0;
};

/// State of a torrent as the GUI shows it. Values are IPC: append only.
enum class BtState : quint8 {
    Queued = 0,
    CheckingFiles = 1,
    DownloadingMetadata = 2,
    Downloading = 3,
    Finished = 4,
    Seeding = 5,
    CheckingResume = 6,
    Paused = 7,
    Error = 8,
    Moving = 9,
};

struct BtTorrentRow {
    /// "bt:v1:<40 hex>" or "bt:v2:<64 hex>", upper case (BtIds)
    QString id;
    QString name;
    QString savePath;
    BtState state = BtState::Queued;
    /// wanted bytes and those done
    qint64 size = 0;
    qint64 done = 0;
    /// 0..1
    double progress = 0;
    qint64 rateDown = 0;
    qint64 rateUp = 0;
    int peers = 0;
    int seeds = 0;
    /// from the trackers' scrape: -1 = unknown
    int peersTotal = -1;
    int seedsTotal = -1;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    /// bytes of pieces that failed their hash, and were fetched again
    qint64 failedBytes = 0;
    QString error;
    /// lower-case hex; empty when the torrent has none of that version
    QString hashV1;
    QString hashV2;
    bool paused = false;
    bool hasMetadata = false;
    bool isPrivate = false;
    qint64 addedAt = 0;      ///< Unix seconds
    qint64 completedAt = 0;  ///< Unix seconds, 0 = not yet
    qint64 seedingSeconds = 0;
    int queuePosition = -1;
    /// part map: up to 256 buckets, each 0..255 = share of its pieces done
    QByteArray pieceMap;
    int pieces = 0;
    int pieceLength = 0;
    int numFiles = 0;
    /// seconds left, -1 = unknown
    qint64 eta = -1;
    /// from the .torrent; empty / 0 when absent or no metadata yet
    QString comment;
    QString creator;
    qint64 createdAt = 0;    ///< Unix seconds
    /// last payload up or down, Unix seconds; 0 = never
    qint64 lastActivity = 0;
    /// time spent active (downloading or seeding)
    qint64 activeSeconds = 0;
    /// verified bytes of the whole torrent, wanted or not
    qint64 haveBytes = 0;
    /// the whole torrent, wanted or not
    qint64 totalSize = 0;
    /// copies in the swarm, from the connected peers (distributed_copies); -1 = unknown
    double availability = -1;
};

struct BtFileRow {
    int index = 0;
    /// relative to the save path, '/' separated
    QString path;
    qint64 size = 0;
    qint64 done = 0;
    /// libtorrent priority 0 (skip) .. 7 (top)
    int priority = 4;
    bool pad = false;
};

struct BtPeerRow {
    QString address;  ///< "ip:port"
    QString client;
    double progress = 0;
    qint64 rateDown = 0;
    qint64 rateUp = 0;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    bool seed = false;
    bool encrypted = false;
    bool utp = false;
    /// "tracker", "dht", "pex", "lsd", "resume", "incoming"
    QString source;
    /// status letters as in Transmission: T O D d U u K ? E h H X I
    QString flags;
};

struct BtTrackerRow {
    QString url;
    int tier = 0;
    bool working = false;
    QString message;
    int seeds = -1;
    int peers = -1;
    /// completed downloads from the scrape; -1 = unknown
    int downloaded = -1;
    /// seconds until the next announce; -1 = none scheduled
    qint64 nextAnnounce = -1;
    /// announcing now
    bool updating = false;
    /// failed announces in a row
    int fails = 0;
    bool hasError = false;
};

/// How a per-torrent seed limit applies. Values are IPC: append only.
enum class BtLimitMode : quint8 {
    Global = 0,     ///< the BitTorrent options
    Unlimited = 1,
    Custom = 2,
};

/// Per-torrent options. Speeds and peers live in libtorrent's resume data, the seed
/// limits in BT/torrent-options.json.
struct BtTorrentOptions {
    /// KiB/s, 0 = no own limit
    int downloadLimitKb = 0;
    int uploadLimitKb = 0;
    /// 0 = the session default
    int maxPeers = 0;
    BtLimitMode ratioMode = BtLimitMode::Global;
    int ratioPercent = 100;
    BtLimitMode seedTimeMode = BtLimitMode::Global;
    int seedTimeMinutes = 0;

    bool operator==(const BtTorrentOptions&) const = default;
};

/// Outcome of an add. Values are IPC: append only.
enum class BtAddOutcome : quint8 {
    Added = 0,
    Duplicate = 1,
    Invalid = 2,
    NotRunning = 3,
    Failed = 4,
};

struct BtAddResult {
    BtAddOutcome outcome = BtAddOutcome::Failed;
    QString id;
    /// English, for the log; the GUI maps the outcome
    QString message;
};

/// A blocked IPv4 range from the app's IP filter, both ends included, host byte order.
struct BtIpRange4 {
    quint32 first = 0;
    quint32 last = 0;
};

/// A blocked IPv6 range, both ends included, network byte order.
struct BtIpRange6 {
    std::array<quint8, 16> first{};
    std::array<quint8, 16> last{};
};

/// What the engine asks of the shared lines (daemon's BandwidthCoordinator).
/// Rates are wire bytes, protocol included, as Usenet counts them.
struct BtDemand {
    /// a torrent downloading or fetching metadata
    bool downActive = false;
    /// a torrent with peers connected that may upload to them
    bool upActive = false;
    qint64 downRate = 0;        ///< bytes/s over ~2 s
    qint64 downRecent = 0;      ///< over ~0.5 s
    qint64 upRate = 0;
    qint64 upRecent = 0;
};

struct BtStatus {
    bool running = false;
    quint16 listenPort = 0;
    bool listening = false;
    int dhtNodes = 0;
    int torrents = 0;
    qint64 rateDown = 0;
    qint64 rateUp = 0;
};

} // namespace eMule::bt
