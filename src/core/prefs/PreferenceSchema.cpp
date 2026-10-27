#include "pch.h"
/// @file PreferenceSchema.cpp
/// @brief Preference metadata table — implementation.

#include "prefs/PreferenceSchema.h"

#include "prefs/Preferences.h"

#include <QCborMap>

#include <cmath>

namespace eMule {

namespace {

using Kind = PrefSpec::Kind;

[[nodiscard]] PrefSpec flag(const char* key, const char* description)
{
    PrefSpec s;
    s.key = QString::fromLatin1(key);
    s.kind = Kind::Bool;
    s.description = QString::fromUtf8(description);
    return s;
}

[[nodiscard]] PrefSpec number(const char* key, double lo, double hi, const char* unit,
                              const char* description)
{
    PrefSpec s;
    s.key = QString::fromLatin1(key);
    s.kind = Kind::Int;
    s.min = lo;
    s.max = hi;
    s.unit = QString::fromLatin1(unit);
    s.description = QString::fromUtf8(description);
    return s;
}

[[nodiscard]] PrefSpec text(const char* key, double maxLength, const char* description)
{
    PrefSpec s;
    s.key = QString::fromLatin1(key);
    s.kind = Kind::String;
    s.max = maxLength;
    s.description = QString::fromUtf8(description);
    return s;
}

[[nodiscard]] PrefSpec restart(PrefSpec s) { s.restartRequired = true; return s; }
[[nodiscard]] PrefSpec advanced(PrefSpec s) { s.advanced = true; return s; }
[[nodiscard]] PrefSpec readOnly(PrefSpec s) { s.writable = false; return s; }

[[nodiscard]] QList<PrefSpec> buildSchema()
{
    constexpr double kMaxU32 = 4294967295.0;
    QList<PrefSpec> t;

    // --- Identity and ports ---
    PrefSpec nick = text("nick", 50, "User name shown to other clients.");
    nick.min = 1;
    t << nick;
    // Rebound at once when nothing is connected, else at the next start
    t << restart(number("port", 1, 65535, "", "TCP port for client connections. Must be "
                        "reachable from outside for a HighID. Moves at once while nothing is "
                        "connected, otherwise at the next start."));
    t << restart(number("udpPort", 0, 65535, "", "UDP port for Kad and client UDP. 0 disables "
                        "UDP. Moves at once while nothing is connected, otherwise at the next "
                        "start; switching UDP on or off always needs a restart."));
    t << restart(flag("randomizePortsAtStart", "Pick new listen ports from randomPortMin..randomPortMax "
                      "at every start: TCP, UDP unless it is off, and BitTorrent when it is on."));
    t << restart(number("randomPortMin", 1024, 65535, "", "Lowest port randomizePortsAtStart picks."));
    t << restart(number("randomPortMax", 1024, 65535, "", "Highest port randomizePortsAtStart picks."));
    t << text("bindAddress", 255, "Network interface, IP address or subnet all P2P traffic is bound "
              "to. Empty = any. A change closes every connection and reconnects on the new "
              "interface at once, no restart. While the selected interface is missing nothing "
              "connects.");

    // --- Speed and connection limits ---
    t << number("maxUpload", 0, kMaxU32, "KiB/s", "Upload limit. 0 = unlimited.");
    t << number("maxDownload", 0, kMaxU32, "KiB/s", "Download limit. 0 = unlimited.");
    t << number("maxConnections", 1, 65535, "", "Maximum simultaneous connections.");
    t << number("maxSourcesPerFile", 1, 5000, "", "Hard limit of sources kept per download.");
    t << advanced(number("maxConsPerFive", 1, 50, "", "New connections allowed per 5 seconds."));
    t << advanced(number("maxHalfConnections", 1, 100, "", "Half-open connections allowed at once."));
    t << advanced(number("maxServedBuddies", 1, 32, "", "Firewalled Kad nodes served as buddy at "
                         "once while this node is reachable. In effect capped at a tenth of "
                         "maxConnections."));
    t << number("queueSize", 2000, 50000, "", "Upload queue length in clients.");
    t << number("uploadSlotTargetRate", 0, 256, "KiB/s",
                "Target rate per upload slot; fewer slots at a higher rate. 0 = automatic, else 12-256.");

    // --- Networks ---
    t << flag("networkED2K", "Use the eD2K server network. Gates the automatic connect at start only.");
    t << flag("kadEnabled", "Use the Kad network. Gates the automatic start only.");
    t << flag("autoConnect", "Connect to the enabled networks when the core starts.");
    t << flag("reconnect", "Reconnect after the server connection is lost.");
    t << flag("enableUPnP", "Ask the router to forward the ports (UPnP, NAT-PMP, PCP).");
    t << advanced(flag("filterLANIPs", "Ignore servers and clients with private (LAN) addresses."));

    // --- Servers ---
    t << flag("safeServerConnect", "Try one server at a time instead of several in parallel.");
    t << flag("natTraversal", "Let two Low ID clients find each other through a server or Kad buddy.");
    t << flag("natDialUnlistedServers", "NAT callback: also use a source's server that is not in the server list (pinged once first, never added).");
    t << flag("autoConnectStaticOnly", "Connect automatically to static servers only.");
    t << flag("useServerPriorities", "Prefer servers with a higher priority when connecting.");
    t << flag("addServersFromServer", "Add servers announced by the connected server.");
    t << flag("addServersFromClients", "Add servers announced by other clients.");
    t << number("deadServerRetries", 1, 99, "", "Failed connection attempts before a server is "
                "disabled.");
    t << flag("autoUpdateServerList", "Fetch the server list from serverListURL at start.");
    t << text("serverListURL", 2048, "URL of a server.met file. Empty = built-in default.");
    t << text("nodesDatURL", 2048, "URL of a Kad nodes.dat file the Kad window offers for a "
              "bootstrap. Empty = built-in default.");
    t << flag("smartLowIdCheck", "Retry another server when one assigns a LowID.");
    t << flag("serverPreferIPv6", "Reconnect to a dual-stack server over IPv6 when IPv4 gives a LowID.");
    t << flag("manualServerHighPriority", "Give manually added servers high priority.");

    // --- Downloads ---
    t << flag("addNewFilesPaused", "Add new downloads in the paused state.");
    t << flag("autoDownloadPriority", "New downloads get automatic priority.");
    t << flag("autoSharedFilesPriority", "New shared files get automatic upload priority.");
    t << flag("transferFullChunks", "Upload whole chunks to one client before switching.");
    t << flag("previewPrio", "Download the first and last part first so a preview is possible.");
    t << flag("startNextPausedFile", "Start the next paused download when one completes.");
    t << flag("startNextPausedFileSameCat", "Prefer the same category when starting the next "
              "paused download.");
    t << flag("rememberDownloadedFiles", "Remember completed files so search results can mark them.");
    t << flag("rememberCancelledFiles", "Remember cancelled files so search results can mark them.");
    t << flag("autoRenameToMajorityName", "Rename a download once to the name most of its sources use "
              "(at least 5 sources and 60%; never after a manual rename or for a suspected fake).");
    t << flag("watchSharedDirs", "Pick up files added to shared folders as they appear (OS file watcher).");
    t << flag("seenFileIndex", "Keep a local index of files seen in search results.");
    t << flag("useSaveLoadSources", "Save the sources of rare files and reload them at start.");
    t << readOnly(text("incomingDir", 4096, "Directory completed downloads are moved to."));

    // --- Disk ---
    t << flag("checkDiskspace", "Pause downloads when free disk space runs low.");
    t << number("minFreeDiskSpace", 0, 1e15, "bytes", "Free space to keep on the download volume.");

    // --- Security ---
    t << readOnly(flag("cryptLayerSupported", "Protocol obfuscation is supported. Most peers "
                       "require it."));
    t << flag("cryptLayerRequested", "Ask for obfuscated connections when the peer supports them.");
    t << flag("cryptLayerRequired", "Accept obfuscated connections only.");
    t << flag("natTraversalStrictObfuscation", "NAT traversal: never send unobfuscated QUIC datagrams.");
    // Keys are loaded at start only, as in MFC
    t << restart(flag("useSecureIdent", "Use secure user identification. Takes effect at the next "
                      "start."));
    t << flag("antiLeecher", "Check peers against the specialdlp anti-leecher rules.");
    t << number("antiLeecherLevel", 0, 2, "", "Anti-leecher level: 0 = lite (soft hits only reduce the "
                "queue score), 1 = strict (soft hits are banned too), 2 = strict + Chinese VeryCD/easyMule mods.");
    t << flag("filterServerByIP", "Apply the IP filter to servers too.");
    t << number("ipFilterLevel", 0, 255, "", "IP filter entries with a level below this are blocked.");
    t << flag("ipFilterAutoUpdate", "Download the IP filter from its update URL every ipFilterUpdateDays days.");
    t << number("ipFilterUpdateDays", 1, 365, "days", "How often the IP filter is downloaded automatically.");
    t << number("viewSharedFilesAccess", 0, 2, "", "Who may browse the shared files: 0 = nobody, "
                "1 = friends, 2 = everybody.");
    t << flag("enableSearchResultFilter", "Hide search results rated as spam.");
    t << flag("msgOnlyFriends", "Accept chat messages from friends only.");
    t << flag("enableSpamFilter", "Filter chat spam.");

    // --- Upload ---
    t << flag("useCreditSystem", "Reward clients that uploaded to us with a better queue position.");
    t << flag("rememberUploadQueue", "Keep the upload queue across a restart.");
    t << flag("dynUpEnabled", "Adjust the upload limit to the measured line latency.");

    // --- Usenet ---
    t << flag("usenetEnabled", "Enable the Usenet download engine.");
    t << flag("usenetPaused", "Pause all Usenet downloads.");
    t << flag("usenetPar2Repair", "Repair damaged Usenet downloads with PAR2.");
    t << flag("usenetUnpack", "Unpack archives after a Usenet download.");
    t << flag("usenetCleanupAfterUnpack", "Delete archives and PAR2 files after a good unpack.");
    t << flag("usenetAutoAddPaused", "Add automatically grabbed releases paused.");
    t << number("usenetDownloadSharePercent", 1, 99, "%", "Share of the download limit Usenet may "
                "use while eD2K is downloading too.");

    // --- BitTorrent ---
    t << flag("btEnabled", "Enable the BitTorrent engine.");
    t << number("btListenPort", 1, 65535, "", "TCP and UDP port for BitTorrent peers.");
    t << text("btDownloadDir", 4096, "Folder torrents are saved to. Empty = the incoming folder.");
    t << number("btMaxDownloadKb", 0, kMaxU32, "KiB/s", "BitTorrent download limit. 0 = unlimited.");
    t << number("btMaxUploadKb", 0, kMaxU32, "KiB/s", "BitTorrent upload limit. 0 = unlimited.");
    t << flag("btDht", "Find BitTorrent peers through the DHT.");
    t << flag("btLsd", "Find BitTorrent peers on the local network (local service discovery).");
    t << flag("btPex", "Exchange BitTorrent peers with connected peers (PEX).");
    t << number("btEncryption", 0, 2, "", "BitTorrent encryption: 0 = enabled (preferred), "
                "1 = forced, 2 = disabled.");
    t << number("btMaxActiveDownloads", 1, 100, "", "Torrents downloading at the same time.");
    t << number("btMaxActiveSeeds", 0, 100, "", "Torrents seeding at the same time.");
    t << number("btSeedRatioPercent", 0, Preferences::kMaxBtSeedRatioPercent, "%", "Stop seeding "
                "at this share ratio, in percent (100 = 1.0). 0 = no limit.");
    t << number("btSeedTimeLimitMinutes", 0, Preferences::kMaxBtSeedTimeLimitMinutes, "min",
                "Stop seeding after this many minutes. 0 = no limit.");
    t << number("btDownloadSharePercent", 1, 99, "%", "Share of the download limit BitTorrent "
                "keeps while eD2K and Usenet download too. Idle bandwidth is borrowed in full.");
    t << number("btUploadSharePercent", 1, 99, "%", "Share of the upload limit BitTorrent keeps "
                "while eD2K uploads too. Idle bandwidth is borrowed in full.");
    t << number("btMaxConnections", 2, Preferences::kMaxBtConnections, "",
                "BitTorrent peer connections, all torrents together.");
    t << number("btMaxConnectionsPerTorrent", 2, Preferences::kMaxBtConnections, "",
                "BitTorrent peer connections per torrent.");
    t << flag("btPortMapping", "Map the BitTorrent port with UPnP/NAT-PMP (needs port mapping on).");

    // --- ed3k (packmule) ---
    t << flag("ed3kEnabled", "Enable the ed3k node (packmule).");
    t << number("ed3kUdpPort", 1, 65535, "", "UDP port of the ed3k node, separate from the eD2K UDP port.");
    t << flag("ed3kShareFiles", "Serve the shared files over ed3k too (while the ed3k node runs).");
    t << flag("ed3kHoldBackPosts", "Hold Usenet posts back on ed3k: share the files, but not the article "
                                   "lists, post sets or nzb records (usenet.md §6).");
    t << advanced(text("ed3kCreditRpc", 4096, "JSON-RPC endpoints of the credit ledger (https URLs, separated by "
                                              "spaces; operator n = the n-th). Empty = off."));
    t << advanced(text("ed3kChaintubeRpc", 4096, "JSON-RPC endpoints of the chaintube chain (https URLs, or http to "
                                                 "a node on this machine, separated by spaces). Empty = off."));
    t << advanced(text("ed3kExtOff", 1024, "Optional ed3k extensions turned off (names of their documents, e.g. "
                                           "lan-discovery chat, separated by spaces). Restarts the node."));
    t << flag("ed3kLan", "Find ed3k nodes in the local network (LAN discovery). Restarts the node.");
    t << advanced(number("ed3kSitePort", 1, 65535, "", "TCP port of the local ed3k website server (loopback only). "
                                                      "Part of every site's address: a change loses what sites stored."));
    t << advanced(text("ed3kNodeLists", 8192, "Node list sources for the first ed3k contact (ed3k:nodes?url=...&key=... "
                                              "links, separated by spaces)."));
    // credits (credit.md): off by default, no default chain or contract (§7.1, §9)
    t << flag("ed3kCreditEnabled", "Use ed3k credits (credit.md). The ledger is public: it shows with whom this "
                                   "node traded, how much and when (§9).");
    t << advanced(text("ed3kCreditChainId", 20, "Chain ID of the credit ledger of the network (decimal). 0 = none."));
    t << advanced(text("ed3kCreditContract", 42, "Address of the credit ledger contract (0x + 40 hex digits)."));
    t << advanced(number("ed3kCreditQuorum", 1, 16, "", "Ledger endpoints that must agree on a read they act on (§7.3)."));
    t << advanced(number("ed3kCreditBidMaxPrice", 0, 9.0e15, "", "Highest price (µ credits per MiB) bid for a priority slot per "
                                                                 "download. 0 = sign receipts only, never pay."));
    t << advanced(flag("ed3kCreditSell", "Sell priority upload slots for credits (§5.1)."));
    t << advanced(number("ed3kCreditSellFloor", 0, 9.0e15, "", "Lowest bid (µ credits per MiB) taken for a priority slot."));
    t << advanced(number("ed3kCreditSellSlots", 0, 1000, "", "Upload slots the priority lane counts. 0 = the upload queue's."));

    // --- Logging ---
    t << flag("verbose", "Write verbose (debug) log lines.");
    t << flag("logToDiskCore", "Write the core log to a file.");
    t << advanced(flag("logWebServer", "Log web server requests."));

    // --- Web server (shown; changing the API's own access is left to the GUI) ---
    t << readOnly(flag("webServerEnabled", "The template web interface is enabled."));
    t << readOnly(flag("webServerRestApiEnabled", "The REST API is enabled."));
    t << readOnly(flag("webServerMcpEnabled", "The MCP endpoint is enabled."));
    t << readOnly(flag("webServerMcpReadOnly", "MCP offers read-only tools only."));
    t << readOnly(number("webServerPort", 1, 65535, "", "Port of the web server."));
    t << readOnly(text("webServerListenAddress", 255, "Address the web server listens on. Empty = "
                       "all interfaces."));
    return t;
}

} // namespace

const QList<PrefSpec>& preferenceSchema()
{
    static const QList<PrefSpec> schema = buildSchema();
    return schema;
}

const PrefSpec* findPreferenceSpec(QStringView key)
{
    for (const PrefSpec& spec : preferenceSchema()) {
        if (spec.key == key)
            return &spec;
    }
    return nullptr;
}

QJsonArray preferenceSchemaJson()
{
    const Preferences defaults;
    const QCborMap defaultValues = defaults.toIpcMap();

    QJsonArray rows;
    for (const PrefSpec& spec : preferenceSchema()) {
        QJsonObject row{
            {QStringLiteral("key"), spec.key},
            {QStringLiteral("kind"), spec.kind == Kind::Bool ? QStringLiteral("boolean")
                                   : spec.kind == Kind::Int ? QStringLiteral("integer")
                                                            : QStringLiteral("string")},
            {QStringLiteral("description"), spec.description},
            {QStringLiteral("restartRequired"), spec.restartRequired},
            {QStringLiteral("advanced"), spec.advanced},
            {QStringLiteral("writable"), spec.writable},
        };
        if (spec.min)
            row.insert(spec.kind == Kind::String ? QStringLiteral("minLength") : QStringLiteral("min"),
                       *spec.min);
        if (spec.max)
            row.insert(spec.kind == Kind::String ? QStringLiteral("maxLength") : QStringLiteral("max"),
                       *spec.max);
        if (!spec.unit.isEmpty())
            row.insert(QStringLiteral("unit"), spec.unit);
        // nick defaults to a generated value; not a default worth advertising
        if (spec.key != QLatin1StringView("nick"))
            row.insert(QStringLiteral("default"), defaultValues.value(spec.key).toJsonValue());
        rows.append(row);
    }
    return rows;
}

QJsonObject preferenceValues(const Preferences& prefs)
{
    const QCborMap all = prefs.toIpcMap();
    QJsonObject values;
    for (const PrefSpec& spec : preferenceSchema())
        values.insert(spec.key, all.value(spec.key).toJsonValue());
    return values;
}

QString preferenceProblem(const PrefSpec& spec, const QCborValue& value)
{
    switch (spec.kind) {
    case Kind::Bool:
        if (!value.isBool())
            return QStringLiteral("'%1' must be true or false").arg(spec.key);
        break;
    case Kind::Int: {
        const bool integral = value.isInteger()
            || (value.isDouble() && value.toDouble() == std::floor(value.toDouble()));
        if (!integral)
            return QStringLiteral("'%1' must be an integer").arg(spec.key);
        const double v = value.isInteger() ? double(value.toInteger()) : value.toDouble();
        if ((spec.min && v < *spec.min) || (spec.max && v > *spec.max))
            return QStringLiteral("'%1' must be between %2 and %3")
                .arg(spec.key)
                .arg(spec.min.value_or(0), 0, 'f', 0)
                .arg(spec.max.value_or(0), 0, 'f', 0);
        break;
    }
    case Kind::String: {
        if (!value.isString())
            return QStringLiteral("'%1' must be a string").arg(spec.key);
        const auto length = static_cast<double>(value.toString().trimmed().size());
        if ((spec.min && length < *spec.min) || (spec.max && length > *spec.max))
            return QStringLiteral("'%1' must be %2 to %3 characters long")
                .arg(spec.key)
                .arg(spec.min.value_or(0), 0, 'f', 0)
                .arg(spec.max.value_or(0), 0, 'f', 0);
        break;
    }
    }
    return {};
}

QString checkPreferencePatch(const QJsonObject& body, PrefChanges& out)
{
    out.clear();
    for (auto it = body.begin(); it != body.end(); ++it) {
        const PrefSpec* spec = findPreferenceSpec(it.key());
        if (!spec)
            return QStringLiteral("Unknown preference: %1").arg(it.key());
        if (!spec->writable)
            return QStringLiteral("Read-only preference: %1").arg(it.key());

        QCborValue value = QCborValue::fromJsonValue(it.value());
        if (spec->kind == Kind::String && value.isString())
            value = value.toString().trimmed();
        if (const QString problem = preferenceProblem(*spec, value); !problem.isEmpty())
            return problem;
        // JSON numbers arrive as doubles; the setters read integers
        if (spec->kind == Kind::Int && value.isDouble())
            value = static_cast<qint64>(value.toDouble());
        out.append({it.key(), value});
    }
    return {};
}

} // namespace eMule
