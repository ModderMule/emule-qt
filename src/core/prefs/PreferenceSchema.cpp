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
    t << readOnly(restart(number("port", 1, 65535, "", "TCP port for client connections. Must be "
                                 "reachable from outside for a HighID.")));
    t << readOnly(restart(number("udpPort", 0, 65535, "", "UDP port for Kad and client UDP. 0 "
                                 "disables UDP.")));
    t << text("bindAddress", 255, "Network interface, IP address or subnet all P2P traffic is bound "
              "to. Empty = any. While the selected interface is missing nothing connects.");

    // --- Speed and connection limits ---
    t << number("maxUpload", 0, kMaxU32, "KiB/s", "Upload limit. 0 = unlimited.");
    t << number("maxDownload", 0, kMaxU32, "KiB/s", "Download limit. 0 = unlimited.");
    t << number("maxConnections", 1, 65535, "", "Maximum simultaneous connections.");
    t << number("maxSourcesPerFile", 1, 5000, "", "Hard limit of sources kept per download.");
    t << advanced(number("maxConsPerFive", 1, 50, "", "New connections allowed per 5 seconds."));
    t << advanced(number("maxHalfConnections", 1, 100, "", "Half-open connections allowed at once."));
    t << number("queueSize", 1, kMaxU32, "", "Upload queue length in clients.");

    // --- Networks ---
    t << flag("networkED2K", "Use the eD2K server network. Gates the automatic connect at start only.");
    t << flag("kadEnabled", "Use the Kad network. Gates the automatic start only.");
    t << flag("autoConnect", "Connect to the enabled networks when the core starts.");
    t << flag("reconnect", "Reconnect after the server connection is lost.");
    t << flag("enableUPnP", "Ask the router to forward the ports (UPnP, NAT-PMP, PCP).");
    t << advanced(flag("filterLANIPs", "Ignore servers and clients with private (LAN) addresses."));

    // --- Servers ---
    t << flag("safeServerConnect", "Try one server at a time instead of several in parallel.");
    t << flag("autoConnectStaticOnly", "Connect automatically to static servers only.");
    t << flag("useServerPriorities", "Prefer servers with a higher priority when connecting.");
    t << flag("addServersFromServer", "Add servers announced by the connected server.");
    t << flag("addServersFromClients", "Add servers announced by other clients.");
    t << number("deadServerRetries", 1, 100, "", "Failed connection attempts before a server is "
                "disabled.");
    t << flag("autoUpdateServerList", "Fetch the server list from serverListURL at start.");
    t << text("serverListURL", 2048, "URL of a server.met file. Empty = built-in default.");
    t << text("nodesDatURL", 2048, "URL of a Kad nodes.dat file. Empty = built-in default.");
    t << flag("smartLowIdCheck", "Retry another server when one assigns a LowID.");
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
    t << flag("useSecureIdent", "Use secure user identification.");
    t << flag("filterServerByIP", "Apply the IP filter to servers too.");
    t << number("ipFilterLevel", 0, 255, "", "IP filter entries with a level below this are blocked.");
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
