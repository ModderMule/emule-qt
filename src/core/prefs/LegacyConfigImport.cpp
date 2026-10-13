#include "pch.h"
/// @file LegacyConfigImport.cpp
/// @brief One-time import of an MFC eMule config directory, requested by the installer.

#include "prefs/LegacyConfigImport.h"

#include "prefs/DownloadCategory.h"
#include "prefs/LegacyIdentityImport.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace eMule::LegacyConfigImport {

namespace {

/// Read as-is by this port once they sit in the config dir. preferences.dat is
/// consumed by LegacyIdentityImport right after.
constexpr std::array kDataFiles{
    "preferences.dat", "cryptkey.dat", "clients.met", "known.met", "known2_64.met",
    "cancelled.met", "emfriends.met", "server.met", "staticservers.dat", "addresses.dat",
    "nodes.dat", "preferencesKad.dat", "ipfilter.dat", "fileinfo.ini", "webservices.dat",
};

/// Either one means this profile was already used -- never overwrite it.
constexpr std::array kProfileInUseMarkers{"known.met", "clients.met"};

constexpr QLatin1StringView kMfcPrefsIni{"preferences.ini"};
constexpr QLatin1StringView kMfcCategoryIni{"Category.ini"};
constexpr QLatin1StringView kMfcSharedDirs{"shareddir.dat"};
constexpr QLatin1StringView kMainSection{"eMule"};

/// MFC's UNLIMITED was 0xFFFF before it became _UI32_MAX; both mean "no limit".
constexpr qint64 kMfcUnlimitedFloor = 0xFFFF;

/// Reader for the INI files MFC wrote through the Win32 profile API: UTF-16LE
/// with BOM or the ANSI code page, sections and keys case-insensitive, first
/// occurrence wins. Not QSettings -- it cannot read UTF-16 and drops values
/// that contain a comma.
class LegacyIni {
public:
    struct Entry {
        QString key;    ///< as written
        QString value;
    };

    [[nodiscard]] bool load(const QString& path);

    /// Value written with WriteString (wide API).
    [[nodiscard]] QString string(QLatin1StringView section, QLatin1StringView key) const;
    /// Value written with WriteStringUTF8 (UTF-8 bytes through the ANSI API).
    [[nodiscard]] QString utf8String(QLatin1StringView section, QLatin1StringView key) const;
    [[nodiscard]] std::optional<qint64> integer(QLatin1StringView section,
                                                QLatin1StringView key) const;
    [[nodiscard]] std::optional<bool> boolean(QLatin1StringView section,
                                              QLatin1StringView key) const;

    [[nodiscard]] QStringList sections() const { return m_order; }
    [[nodiscard]] QList<Entry> entries(const QString& section) const;

private:
    [[nodiscard]] std::optional<QString> raw(QLatin1StringView section,
                                             QLatin1StringView key) const;

    QHash<QString, QList<Entry>> m_sections;   ///< keyed by lower-case section name
    QStringList m_order;                        ///< section names as written
    bool m_utf16 = false;
};

[[nodiscard]] QString expandEnvironment(QString path);
[[nodiscard]] QString cleanFolder(const QString& mfcPath);
[[nodiscard]] QString readRequestSource(const QString& requestFile);
[[nodiscard]] bool profileInUse(const QDir& config);
bool writeState(const QString& configDir, const QString& sourceDir, const QStringList& pending);
[[nodiscard]] int copyDataFiles(const QDir& source, const QDir& config);
void copySchedules(const LegacyIni& ini, const QDir& config);
[[nodiscard]] int applyPreferences(const LegacyIni& ini, Preferences& prefs);
[[nodiscard]] QStringList readTempDirs(const LegacyIni& ini);
[[nodiscard]] int applyCategories(const QString& path, Preferences& prefs);
[[nodiscard]] int applySharedDirs(const QString& path, Preferences& prefs);

} // namespace

QString requestFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/config/")
           + QLatin1StringView(kRequestFileName);
}

Outcome run(const QString& requestFile, const QString& configDir, Preferences& prefs)
{
    Outcome out;
    if (configDir.isEmpty() || !QFile::exists(requestFile))
        return out;

    const QDir config(configDir);
    if (config.exists(QLatin1StringView(kStateFileName))) {
        out.result = Result::AlreadyDone;
        return out;
    }

    out.sourceDir = readRequestSource(requestFile);
    const QDir source(out.sourceDir);

    if (profileInUse(config)) {
        out.result = Result::ProfileInUse;
        logInfo(QStringLiteral("eMule import from %1 skipped: this profile is already in use")
                    .arg(out.sourceDir));
        writeState(configDir, out.sourceDir, {});
        return out;
    }

    LegacyIni ini;
    if (out.sourceDir.isEmpty() || source.canonicalPath() == config.canonicalPath()
        || !ini.load(source.filePath(kMfcPrefsIni))) {
        out.result = Result::SourceMissing;
        logWarning(QStringLiteral("eMule import skipped: no preferences.ini in '%1'")
                       .arg(out.sourceDir));
        writeState(configDir, out.sourceDir, {});
        return out;
    }

    out.filesCopied = copyDataFiles(source, config);
    copySchedules(ini, config);

    // Incoming dir first: shared dirs and category paths are validated against it
    out.prefsApplied = applyPreferences(ini, prefs);
    out.categories = applyCategories(source.filePath(kMfcCategoryIni), prefs);
    out.sharedDirs = applySharedDirs(source.filePath(kMfcSharedDirs), prefs);
    out.pendingTempDirs = readTempDirs(ini);

    if (!prefs.save())
        logWarning(QStringLiteral("eMule import: preferences could not be saved"));
    writeState(configDir, out.sourceDir, out.pendingTempDirs);

    out.result = Result::Imported;
    logInfo(QStringLiteral("Imported eMule settings from %1: %2 files, %3 preferences, "
                           "%4 categories, %5 shared folders, %6 temp folders queued")
                .arg(out.sourceDir).arg(out.filesCopied).arg(out.prefsApplied)
                .arg(out.categories).arg(out.sharedDirs).arg(out.pendingTempDirs.size()));
    return out;
}

QStringList takePendingTempDirs(const QString& configDir)
{
    const QString statePath = QDir(configDir).filePath(QLatin1StringView(kStateFileName));
    if (configDir.isEmpty() || !QFile::exists(statePath))
        return {};

    QStringList pending;
    QString sourceDir;
    try {
        const YAML::Node root = YAML::LoadFile(statePath.toStdString());
        sourceDir = QString::fromStdString(root["source"].as<std::string>(std::string{}));
        if (root["pendingTempDirs"] && root["pendingTempDirs"].IsSequence()) {
            for (const auto& item : root["pendingTempDirs"])
                pending.append(QString::fromStdString(item.as<std::string>(std::string{})));
        }
    } catch (const YAML::Exception&) {
        return {};
    }
    pending.removeAll(QString());

    // Cleared before the scan: a converter crash must not replay it at every start
    if (!pending.isEmpty())
        writeState(configDir, sourceDir, {});
    return pending;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

namespace {

bool LegacyIni::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();

    QString text;
    m_utf16 = data.startsWith("\xFF\xFE");
    if (m_utf16) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2),
                                  (data.size() - 2) / 2);
    } else {
        // Latin-1 keeps every byte; the accessors decode per value
        text = QString::fromLatin1(data);
    }

    QList<Entry>* current = nullptr;
    for (const QString& rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('['))) {
            const qsizetype end = line.indexOf(QLatin1Char(']'));
            if (end < 0) {
                current = nullptr;
                continue;
            }
            const QString name = line.mid(1, end - 1).trimmed();
            const QString lower = name.toLower();
            if (!m_sections.contains(lower))
                m_order.append(name);
            current = &m_sections[lower];
            continue;
        }

        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || !current)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        // GetPrivateProfileString drops one pair of surrounding quotes
        if (value.size() >= 2
            && ((value.startsWith(u'"') && value.endsWith(u'"'))
                || (value.startsWith(u'\'') && value.endsWith(u'\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        current->append({line.left(eq).trimmed(), value});
    }
    return true;
}

QString LegacyIni::string(QLatin1StringView section, QLatin1StringView key) const
{
    const auto value = raw(section, key);
    if (!value)
        return {};
    return m_utf16 ? *value : QString::fromLocal8Bit(value->toLatin1());
}

QString LegacyIni::utf8String(QLatin1StringView section, QLatin1StringView key) const
{
    const auto value = raw(section, key);
    if (!value)
        return {};

    // In a UTF-16 file the ANSI API widened the UTF-8 bytes through the code page
    const QByteArray bytes = m_utf16 ? value->toLocal8Bit() : value->toLatin1();
    // MFC OptUtf8ToStr: UTF-8 when it decodes cleanly, else the plain string
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = decoder.decode(bytes);
    if (!decoder.hasError())
        return decoded;
    return m_utf16 ? *value : QString::fromLocal8Bit(bytes);
}

std::optional<qint64> LegacyIni::integer(QLatin1StringView section, QLatin1StringView key) const
{
    const auto value = raw(section, key);
    if (!value)
        return std::nullopt;
    bool ok = false;
    const qint64 number = value->toLongLong(&ok);
    return ok ? std::optional(number) : std::nullopt;
}

std::optional<bool> LegacyIni::boolean(QLatin1StringView section, QLatin1StringView key) const
{
    const auto number = integer(section, key);
    return number ? std::optional(*number != 0) : std::nullopt;
}

QList<LegacyIni::Entry> LegacyIni::entries(const QString& section) const
{
    return m_sections.value(section.toLower());
}

std::optional<QString> LegacyIni::raw(QLatin1StringView section, QLatin1StringView key) const
{
    const auto it = m_sections.constFind(QString(section).toLower());
    if (it == m_sections.cend())
        return std::nullopt;
    for (const Entry& entry : *it) {
        if (entry.key.compare(key, Qt::CaseInsensitive) == 0)
            return entry.value;
    }
    return std::nullopt;
}

/// %NAME% -> environment value. The installer keeps %LOCALAPPDATA% unexpanded
/// so the path resolves for the user who runs the daemon, not the elevated one.
QString expandEnvironment(QString path)
{
    static const QRegularExpression pattern(QStringLiteral("%([A-Za-z0-9_()]+)%"));
    qsizetype from = 0;
    for (auto m = pattern.match(path, from); m.hasMatch(); m = pattern.match(path, from)) {
        const QString name = m.captured(1);
        if (!qEnvironmentVariableIsSet(qPrintable(name))) {
            from = m.capturedEnd();
            continue;
        }
        const QString value = qEnvironmentVariable(qPrintable(name));
        path.replace(m.capturedStart(), m.capturedLength(), value);
        from = m.capturedStart() + value.size();
    }
    return path;
}

/// MFC folder names carry a trailing backslash.
QString cleanFolder(const QString& mfcPath)
{
    const QString trimmed = mfcPath.trimmed();
    return trimmed.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

QString readRequestSource(const QString& requestFile)
{
    try {
        const YAML::Node root = YAML::LoadFile(requestFile.toStdString());
        const QString source = QString::fromStdString(root["source"].as<std::string>(std::string{}));
        return cleanFolder(expandEnvironment(source));
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("eMule import request %1 unreadable: %2")
                       .arg(requestFile, QString::fromStdString(ex.what())));
    }
    return {};
}

bool profileInUse(const QDir& config)
{
    return std::ranges::any_of(kProfileInUseMarkers, [&config](const char* name) {
        return config.exists(QLatin1StringView(name));
    });
}

bool writeState(const QString& configDir, const QString& sourceDir, const QStringList& pending)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "source" << YAML::Value << YAML::DoubleQuoted << sourceDir.toStdString();
    out << YAML::Key << "pendingTempDirs" << YAML::Value << YAML::BeginSeq;
    for (const QString& dir : pending)
        out << YAML::DoubleQuoted << dir.toStdString();
    out << YAML::EndSeq << YAML::EndMap;

    QDir().mkpath(configDir);
    const QString path = QDir(configDir).filePath(QLatin1StringView(kStateFileName));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logWarning(QStringLiteral("Cannot write %1").arg(path));
        return false;
    }
    file.write(out.c_str());
    return true;
}

int copyDataFiles(const QDir& source, const QDir& config)
{
    // The user hash is worthless without its key and the other way round
    const bool identity = source.exists(QLatin1StringView(LegacyIdentityImport::kLegacyPrefsFileName))
                          && source.exists(QLatin1StringView(LegacyIdentityImport::kCryptKeyFileName));

    int copied = 0;
    for (const char* name : kDataFiles) {
        const QLatin1StringView file(name);
        if (!source.exists(file))
            continue;
        if (!identity && (file == QLatin1StringView(LegacyIdentityImport::kLegacyPrefsFileName)
                          || file == QLatin1StringView(LegacyIdentityImport::kCryptKeyFileName))) {
            continue;
        }

        // Fresh profile (see profileInUse), so what is there is seeded or generated
        const QString dest = config.filePath(file);
        QFile::remove(dest);
        if (QFile::copy(source.filePath(file), dest)) {
            ++copied;
        } else {
            logWarning(QStringLiteral("eMule import: could not copy %1")
                           .arg(source.filePath(file)));
        }
    }
    return copied;
}

/// Scheduler::loadFromFile reads [Scheduler] / [Schedule#N] from preferences.ini
/// in the config dir. Only those sections are carried over, as plain text.
void copySchedules(const LegacyIni& ini, const QDir& config)
{
    const QString dest = config.filePath(kMfcPrefsIni);
    if (QFile::exists(dest))
        return;

    QString text;
    for (const QString& section : ini.sections()) {
        if (section.compare(QLatin1StringView("Scheduler"), Qt::CaseInsensitive) != 0
            && !section.startsWith(QLatin1StringView("Schedule#"), Qt::CaseInsensitive)) {
            continue;
        }
        text += u'[' + section + QStringLiteral("]\n");
        for (const auto& entry : ini.entries(section))
            text += entry.key + u'=' + entry.value + u'\n';
        text += u'\n';
    }
    if (text.isEmpty())
        return;

    QFile file(dest);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text.toUtf8());
}

/// Encryption keys are left out on purpose: obfuscation must never be turned off.
int applyPreferences(const LegacyIni& ini, Preferences& prefs)
{
    int applied = 0;

    // MFC resets its default nick (the homepage URL) at load; ours stays then
    if (const QString nick = ini.utf8String(kMainSection, QLatin1StringView("Nick"));
        !nick.isEmpty() && !nick.contains(QLatin1StringView("emule-project.net"))) {
        prefs.setNick(nick);
        ++applied;
    }

    if (const auto port = ini.integer(kMainSection, QLatin1StringView("Port"));
        port && *port > 0 && *port <= std::numeric_limits<uint16>::max()) {
        prefs.setPort(static_cast<uint16>(*port));
        ++applied;
    }
    // 0 is valid: UDP off
    if (const auto port = ini.integer(kMainSection, QLatin1StringView("UDPPort"));
        port && *port >= 0 && *port <= std::numeric_limits<uint16>::max()) {
        prefs.setUdpPort(static_cast<uint16>(*port));
        ++applied;
    }

    // Capacities before limits, as MFC loads them
    const auto rate = [&ini](QLatin1StringView key) -> std::optional<uint32> {
        const auto value = ini.integer(kMainSection, key);
        if (!value)
            return std::nullopt;
        // 0 = no limit here; MFC writes UNLIMITED as -1, 0xFFFF or _UI32_MAX
        return (*value <= 0 || *value >= kMfcUnlimitedFloor) ? 0u : static_cast<uint32>(*value);
    };
    if (const auto value = rate(QLatin1StringView("DownloadCapacity")); value && *value != 0) {
        prefs.setMaxGraphDownloadRate(*value);
        ++applied;
    }
    if (const auto value = rate(QLatin1StringView("UploadCapacityNew")); value && *value != 0) {
        prefs.setMaxGraphUploadRate(*value);
        ++applied;
    }
    if (const auto value = rate(QLatin1StringView("MaxUpload"))) {
        prefs.setMaxUpload(*value);
        ++applied;
    }
    if (const auto value = rate(QLatin1StringView("MaxDownload"))) {
        prefs.setMaxDownload(*value);
        ++applied;
    }

    const auto count = [&ini](QLatin1StringView key) -> std::optional<uint16> {
        const auto value = ini.integer(kMainSection, key);
        if (!value || *value <= 0)
            return std::nullopt;
        return static_cast<uint16>(std::min<qint64>(*value, std::numeric_limits<uint16>::max()));
    };
    if (const auto value = count(QLatin1StringView("MaxConnections"))) {
        prefs.setMaxConnections(*value);
        ++applied;
    }
    if (const auto value = count(QLatin1StringView("MaxSourcesPerFile"))) {
        prefs.setMaxSourcesPerFile(*value);
        ++applied;
    }

    if (const auto value = ini.boolean(kMainSection, QLatin1StringView("Autoconnect"))) {
        prefs.setAutoConnect(*value);
        ++applied;
    }
    if (const auto value = ini.boolean(kMainSection, QLatin1StringView("Reconnect"))) {
        prefs.setReconnect(*value);
        ++applied;
    }
    if (const auto value = ini.boolean(kMainSection, QLatin1StringView("NetworkKademlia"))) {
        prefs.setKadEnabled(*value);
        ++applied;
    }
    if (const auto value = ini.boolean(kMainSection, QLatin1StringView("NetworkED2K"))) {
        prefs.setNetworkED2K(*value);
        ++applied;
    }

    // Finished files stay where they are, so known.met still matches them
    if (const QString incoming = cleanFolder(ini.string(kMainSection, QLatin1StringView("IncomingDir")));
        !incoming.isEmpty() && QDir(incoming).exists()) {
        prefs.setIncomingDir(incoming);
        ++applied;
    }
    return applied;
}

/// MFC: TempDir, then '|'-separated TempDirs. Never adopted as our temp dir --
/// two clients on the same part files corrupt them; the converter copies instead.
QStringList readTempDirs(const LegacyIni& ini)
{
    QStringList raw{ini.string(kMainSection, QLatin1StringView("TempDir"))};
    raw += ini.string(kMainSection, QLatin1StringView("TempDirs")).split(u'|', Qt::SkipEmptyParts);

    QStringList dirs;
    for (const QString& entry : std::as_const(raw)) {
        const QString dir = cleanFolder(entry);
        if (!dir.isEmpty() && QDir(dir).exists() && !dirs.contains(dir, Qt::CaseInsensitive))
            dirs.append(dir);
    }
    return dirs;
}

/// Category.ini: [General] Count=N, then [Cat#0]..[Cat#N]; Cat#0 is "All".
int applyCategories(const QString& path, Preferences& prefs)
{
    LegacyIni ini;
    if (!ini.load(path))
        return 0;
    // Downloads store their category by index: only fill an untouched list
    if (prefs.categoryCount() > 1)
        return 0;

    const qint64 count = ini.integer(QLatin1StringView("General"), QLatin1StringView("Count"))
                             .value_or(0);
    if (count <= 0)
        return 0;

    QList<DownloadCategory> list;
    for (qint64 i = 0; i <= std::min<qint64>(count, kMaxCategories - 1); ++i) {
        const QByteArray name = QByteArrayLiteral("Cat#") + QByteArray::number(i);
        const QLatin1StringView section(name);

        DownloadCategory cat;
        cat.title = ini.utf8String(section, QLatin1StringView("Title"));
        if (i != 0)
            cat.incomingPath = cleanFolder(ini.utf8String(section, QLatin1StringView("Incoming")));
        cat.comment = ini.utf8String(section, QLatin1StringView("Comment"));
        cat.regexp = ini.utf8String(section, QLatin1StringView("RegularExpression"));
        cat.autocat = ini.utf8String(section, QLatin1StringView("AutoCat"));
        // Written as a signed int: -1 is CLR_NONE, the same bits as kCategoryColorAuto
        cat.color = static_cast<quint32>(
            ini.integer(section, QLatin1StringView("Color")).value_or(-1));
        cat.prio = static_cast<quint8>(
            ini.integer(section, QLatin1StringView("a4afPriority")).value_or(cat.prio));
        cat.filter = static_cast<int>(ini.integer(section, QLatin1StringView("Filter")).value_or(0));
        cat.filterNeg = ini.boolean(section, QLatin1StringView("FilterNegator")).value_or(false);
        cat.autocatIsRegexp =
            ini.boolean(section, QLatin1StringView("AutoCatAsRegularExpression")).value_or(false);
        cat.care4all = ini.boolean(section, QLatin1StringView("Care4All")).value_or(false);
        cat.downloadInAlphabeticalOrder =
            ini.boolean(section, QLatin1StringView("downloadInAlphabeticalOrder")).value_or(false);
        list.append(cat);
    }

    prefs.setCategories(list);
    return prefs.categoryCount() - 1;
}

/// shareddir.dat: one folder per line, UTF-16LE with BOM or ANSI.
int applySharedDirs(const QString& path, Preferences& prefs)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray data = file.readAll();
    const QString text = data.startsWith("\xFF\xFE")
        ? QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2),
                             (data.size() - 2) / 2)
        : QString::fromLocal8Bit(data);

    QStringList dirs = prefs.sharedDirs();
    int added = 0;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString dir = cleanFolder(line);
        if (dir.isEmpty() || dirs.contains(dir, Qt::CaseInsensitive))
            continue;
        if (!QDir(dir).exists() || !prefs.isShareableDirectory(dir))
            continue;
        dirs.append(dir);
        ++added;
    }
    if (added > 0)
        prefs.setSharedDirs(dirs);
    return added;
}

} // namespace

} // namespace eMule::LegacyConfigImport
