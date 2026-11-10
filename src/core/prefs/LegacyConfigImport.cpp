#include "pch.h"
/// @file LegacyConfigImport.cpp
/// @brief Import of an eMule (MFC) or aMule config dir, asked for in the first start wizard.

#include "prefs/LegacyConfigImport.h"

#include "prefs/DownloadCategory.h"
#include "prefs/LegacyIdentityImport.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <optional>

namespace eMule::LegacyConfigImport {

namespace {

/// Read as-is by this port once they sit in the config dir. preferences.dat is
/// consumed by LegacyIdentityImport right after. aMule writes the same formats.
constexpr std::array kDataFiles{
    "preferences.dat", "cryptkey.dat", "clients.met", "known.met", "known2_64.met",
    "cancelled.met", "emfriends.met", "server.met", "staticservers.dat", "addresses.dat",
    "nodes.dat", "preferencesKad.dat", "ipfilter.dat", "fileinfo.ini", "webservices.dat",
};

constexpr QLatin1StringView kMfcPrefsIni{"preferences.ini"};
constexpr QLatin1StringView kAmuleConf{"amule.conf"};
constexpr QLatin1StringView kMfcCategoryIni{"Category.ini"};
constexpr QLatin1StringView kSharedDirs{"shareddir.dat"};
constexpr QLatin1StringView kMainSection{"eMule"};   ///< both clients
constexpr QLatin1StringView kPartMetFilter{"*.part.met"};

/// MFC's UNLIMITED was 0xFFFF before it became _UI32_MAX; both mean "no limit".
constexpr qint64 kMfcUnlimitedFloor = 0xFFFF;

/// Reader for both INI dialects. Win32 profile API (eMule): UTF-16LE with BOM or
/// the ANSI code page. wxFileConfig (aMule): UTF-8, backslash escapes. Sections and
/// keys case-insensitive, first occurrence wins. Not QSettings -- it cannot read
/// UTF-16 and drops values that contain a comma.
class LegacyIni {
public:
    struct Entry {
        QString key;    ///< as written
        QString value;
    };

    [[nodiscard]] bool load(const QString& path, SourceKind kind = SourceKind::Emule);

    /// Value written with WriteString (wide API), or any aMule value.
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
    bool m_wx = false;
};

[[nodiscard]] QString settingsFile(SourceKind kind);
[[nodiscard]] std::optional<Source> loadSource(const QString& configDir, SourceKind kind,
                                               const QString& winePrefix, LegacyIni* iniOut = nullptr);
[[nodiscard]] QString mapPath(const QString& winePrefix, const QString& path);
[[nodiscard]] QString unescapeWx(const QString& value);
[[nodiscard]] QStringList subdirs(const QString& dir, const QString& filter = {});
[[nodiscard]] QStringList winePrefixes(const SearchRoots& roots);
void discoverWine(const QString& prefix, const std::function<void(const QString&, SourceKind,
                                                                  const QString&)>& tryDir);
[[nodiscard]] QString expandEnvironment(QString path);
bool writeState(const QString& configDir, const QString& sourceDir, const QStringList& pending);
[[nodiscard]] bool hasIdentity(const QDir& source);
void applyDerivedUserHash(Preferences& prefs, Outcome& out);
[[nodiscard]] int copyDataFiles(const QDir& source, const QDir& config, Outcome& out);
void copySchedules(const LegacyIni& ini, const QDir& config);
[[nodiscard]] QString nickFrom(const LegacyIni& ini, SourceKind kind);
[[nodiscard]] int applyPreferences(const LegacyIni& ini, const Source& src, Preferences& prefs);
[[nodiscard]] QStringList readTempDirs(const LegacyIni& ini, const Source& src);
[[nodiscard]] int applyMfcCategories(const QString& path, const Source& src, Preferences& prefs);
[[nodiscard]] int applyAmuleCategories(const LegacyIni& ini, const Source& src, Preferences& prefs);
[[nodiscard]] int applySharedDirs(const QString& path, const Source& src, Preferences& prefs);

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SearchRoots hostRoots()
{
    SearchRoots roots;
    roots.home = QDir::homePath();
#ifdef Q_OS_WIN
    roots.windowsLayout = true;
    roots.appData = qEnvironmentVariable("APPDATA");
    roots.localAppData = qEnvironmentVariable("LOCALAPPDATA");
    roots.programData = qEnvironmentVariable("PROGRAMDATA");
    for (const char* var : {"ProgramFiles(x86)", "ProgramW6432", "ProgramFiles"}) {
        const QString dir = qEnvironmentVariable(var);
        if (!dir.isEmpty() && !roots.programFiles.contains(dir, Qt::CaseInsensitive))
            roots.programFiles.append(dir);
    }

    // Program-directory mode: eMule records no install path, only its own exe in
    // the handlers it registers. Same keys the old installer read.
    const auto exeDir = [](QString command) -> QString {
        const qsizetype p = command.indexOf(QLatin1StringView(".exe"), 0, Qt::CaseInsensitive);
        if (p < 0)
            return {};
        command = command.left(p + 4);
        if (command.startsWith(u'"'))
            command.remove(0, 1);
        const QFileInfo exe(command);
        return exe.fileName().compare(QLatin1StringView("emule.exe"), Qt::CaseInsensitive) == 0
                   ? exe.path() : QString();
    };
    const auto fromRegistry = [&](const QString& key, const QString& value) {
        const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\") + key, QSettings::NativeFormat);
        if (const QString dir = exeDir(reg.value(value).toString()); !dir.isEmpty())
            roots.emuleExeDirs.append(dir);
    };
    fromRegistry(QStringLiteral("Software\\Classes\\ed2k\\shell\\open\\command"), QStringLiteral("Default"));
    fromRegistry(QStringLiteral("Software\\Classes\\eMule\\shell\\open\\command"), QStringLiteral("Default"));
    fromRegistry(QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                 QStringLiteral("eMuleAutoStart"));
#else
    roots.unixLayout = true;
#ifdef Q_OS_MACOS
    roots.macLayout = true;
#endif
#endif
    return roots;
}

QList<Source> discover(const SearchRoots& roots)
{
    QList<Source> found;
    QSet<QString> seen;
    const auto tryDir = [&](const QString& dir, SourceKind kind, const QString& wine) {
        if (dir.isEmpty())
            return;
        const QString canonical = QFileInfo(dir).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical.toLower()))
            return;
        if (auto src = loadSource(canonical, kind, wine)) {
            seen.insert(canonical.toLower());
            found.append(*src);
        }
    };
    const auto join = [](const QString& base, const char* rel) {
        return base.isEmpty() ? QString() : base + u'/' + QLatin1StringView(rel);
    };

    if (roots.windowsLayout) {
        // CPreferences::GetDefaultDirectory order: per-user, all-users, program dir
        tryDir(join(roots.localAppData, "eMule/config"), SourceKind::Emule, {});
        tryDir(join(roots.programData, "eMule/config"), SourceKind::Emule, {});
        for (const QString& exe : roots.emuleExeDirs)
            tryDir(join(exe, "config"), SourceKind::Emule, {});
        for (const QString& pf : roots.programFiles)
            tryDir(join(pf, "eMule/config"), SourceKind::Emule, {});
        // Windows XP layout, left behind by an old install
        tryDir(join(roots.appData, "eMule/config"), SourceKind::Emule, {});
        // wxStandardPaths::GetUserDataDir()
        tryDir(join(roots.appData, "aMule"), SourceKind::Amule, {});
    }
    if (roots.unixLayout && !roots.home.isEmpty()) {
        if (roots.macLayout)
            tryDir(join(roots.home, "Library/Application Support/aMule"), SourceKind::Amule, {});
        tryDir(join(roots.home, ".aMule"), SourceKind::Amule, {});
        // Flatpak builds: host home, else the app's own data dir
        tryDir(join(roots.home, ".var/app/org.amule.aMule/data/.aMule"), SourceKind::Amule, {});
        tryDir(join(roots.home, ".var/app/org.amule.aMule/.aMule"), SourceKind::Amule, {});
        for (const QString& prefix : winePrefixes(roots))
            discoverWine(prefix, tryDir);
    }
    return found;
}

std::optional<Source> probe(const QString& picked)
{
    const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(picked.trimmed()));
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return std::nullopt;

    // Inside a Wine prefix: its C:\ paths map there
    QString wine;
#ifndef Q_OS_WIN
    if (const qsizetype at = dir.indexOf(QLatin1StringView("/drive_c")); at > 0)
        wine = dir.left(at);
    else if (QFileInfo(dir + QStringLiteral("/drive_c")).isDir())
        wine = dir;
#endif

    for (const QString& candidate : {dir, QString(dir + QStringLiteral("/config"))}) {
        for (const SourceKind kind : {SourceKind::Emule, SourceKind::Amule}) {
            if (auto src = loadSource(candidate, kind, wine))
                return src;
        }
    }
    if (wine == dir) {
        QList<Source> inPrefix;
        discoverWine(dir, [&inPrefix](const QString& d, SourceKind kind, const QString& w) {
            if (auto src = loadSource(d, kind, w))
                inPrefix.append(*src);
        });
        if (!inPrefix.isEmpty())
            return inPrefix.first();
    }
    return std::nullopt;
}

QString requestFilePath(const QString& configDir)
{
    return QDir(configDir).filePath(QLatin1StringView(kRequestFileName));
}

bool writeRequest(const QString& configDir, const Source& source, bool parts)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "source" << YAML::Value << YAML::DoubleQuoted << source.configDir.toStdString();
    out << YAML::Key << "kind" << YAML::Value
        << (source.kind == SourceKind::Amule ? "amule" : "emule");
    if (!source.winePrefix.isEmpty())
        out << YAML::Key << "wine" << YAML::Value << YAML::DoubleQuoted << source.winePrefix.toStdString();
    out << YAML::Key << "parts" << YAML::Value << parts;
    // As chosen in the wizard, not re-read from the source
    out << YAML::Key << "tempDirs" << YAML::Value << YAML::BeginSeq;
    for (const QString& dir : source.tempDirs)
        out << YAML::DoubleQuoted << dir.toStdString();
    out << YAML::EndSeq;
    out << YAML::EndMap;

    QDir().mkpath(configDir);
    QFile file(requestFilePath(configDir));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logWarning(QStringLiteral("Cannot write %1").arg(file.fileName()));
        return false;
    }
    file.write(out.c_str());
    return true;
}

Outcome run(const QString& configDir, Preferences& prefs)
{
    Outcome out;
    const QString requestFile = requestFilePath(configDir);
    if (configDir.isEmpty() || !QFile::exists(requestFile))
        return out;

    SourceKind kind = SourceKind::Emule;
    QString wine;
    bool parts = true;
    std::optional<QStringList> chosenTemps;
    try {
        const YAML::Node root = YAML::LoadFile(requestFile.toStdString());
        out.sourceDir = QDir::cleanPath(QDir::fromNativeSeparators(expandEnvironment(
            QString::fromStdString(root["source"].as<std::string>(std::string{})))));
        if (root["kind"].as<std::string>(std::string{}) == "amule")
            kind = SourceKind::Amule;
        wine = QString::fromStdString(root["wine"].as<std::string>(std::string{}));
        parts = root["parts"].as<bool>(true);
        if (root["tempDirs"] && root["tempDirs"].IsSequence()) {
            chosenTemps.emplace();
            for (const auto& item : root["tempDirs"]) {
                const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(
                    QString::fromStdString(item.as<std::string>(std::string{}))));
                if (!dir.isEmpty() && QDir(dir).exists() && !chosenTemps->contains(dir))
                    chosenTemps->append(dir);
            }
        }
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Import request %1 unreadable: %2")
                       .arg(requestFile, QString::fromStdString(ex.what())));
    }
    // Gone before the import: a crash in it must not replay at every start
    QFile::remove(requestFile);

    const QDir config(configDir);
    LegacyIni ini;
    const std::optional<Source> src =
        (out.sourceDir.isEmpty() || QDir(out.sourceDir).canonicalPath() == config.canonicalPath())
            ? std::nullopt : loadSource(out.sourceDir, kind, wine, &ini);
    if (!src) {
        out.result = Result::SourceMissing;
        logWarning(QStringLiteral("Import skipped: no %1 in '%2'")
                       .arg(settingsFile(kind), out.sourceDir));
        writeState(configDir, out.sourceDir, {});
        return out;
    }

    const QDir source(src->configDir);
    out.filesCopied = copyDataFiles(source, config, out);
    if (!hasIdentity(source))
        applyDerivedUserHash(prefs, out);
    if (kind == SourceKind::Emule)
        copySchedules(ini, config);

    // Incoming dir first: shared dirs and category paths are validated against it
    out.prefsApplied = applyPreferences(ini, *src, prefs);
    out.categories = kind == SourceKind::Emule
                         ? applyMfcCategories(source.filePath(kMfcCategoryIni), *src, prefs)
                         : applyAmuleCategories(ini, *src, prefs);
    out.sharedDirs = applySharedDirs(source.filePath(kSharedDirs), *src, prefs);
    if (parts)
        out.pendingTempDirs = chosenTemps.value_or(src->tempDirs);

    if (!prefs.save())
        logWarning(QStringLiteral("Import: preferences could not be saved"));
    writeState(configDir, out.sourceDir, out.pendingTempDirs);

    out.result = Result::Imported;
    logInfo(QStringLiteral("Imported %1 settings from %2: %3 files (%4 own ones moved to %5), "
                           "%6 preferences, %7 categories, %8 shared folders, %9 temp folders queued")
                .arg(kind == SourceKind::Amule ? QStringLiteral("aMule") : QStringLiteral("eMule"),
                     out.sourceDir)
                .arg(out.filesCopied).arg(out.filesBackedUp)
                .arg(out.backupDir.isEmpty() ? QStringLiteral("-") : out.backupDir)
                .arg(out.prefsApplied).arg(out.categories).arg(out.sharedDirs)
                .arg(out.pendingTempDirs.size()));
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

bool LegacyIni::load(const QString& path, SourceKind kind)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();

    QString text;
    m_wx = kind == SourceKind::Amule;
    m_utf16 = !m_wx && data.startsWith("\xFF\xFE");
    if (m_utf16) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2),
                                  (data.size() - 2) / 2);
    } else if (m_wx) {
        text = QString::fromUtf8(data);
    } else {
        // Latin-1 keeps every byte; the accessors decode per value
        text = QString::fromLatin1(data);
    }

    QList<Entry>* current = nullptr;
    for (const QString& rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))
            || (m_wx && line.startsWith(QLatin1Char('#')))) {
            continue;
        }

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
        // GetPrivateProfileString drops one pair of surrounding quotes; wxFileConfig
        // quotes values with outer blanks
        if (value.size() >= 2
            && ((value.startsWith(u'"') && value.endsWith(u'"'))
                || (!m_wx && value.startsWith(u'\'') && value.endsWith(u'\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        current->append({line.left(eq).trimmed(), m_wx ? unescapeWx(value) : value});
    }
    return true;
}

QString LegacyIni::string(QLatin1StringView section, QLatin1StringView key) const
{
    const auto value = raw(section, key);
    if (!value)
        return {};
    return (m_utf16 || m_wx) ? *value : QString::fromLocal8Bit(value->toLatin1());
}

QString LegacyIni::utf8String(QLatin1StringView section, QLatin1StringView key) const
{
    const auto value = raw(section, key);
    if (!value)
        return {};
    if (m_wx)
        return *value;

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

QString settingsFile(SourceKind kind)
{
    return kind == SourceKind::Amule ? QString(kAmuleConf) : QString(kMfcPrefsIni);
}

std::optional<Source> loadSource(const QString& configDir, SourceKind kind,
                                 const QString& winePrefix, LegacyIni* iniOut)
{
    LegacyIni local;
    LegacyIni& ini = iniOut ? *iniOut : local;
    if (!ini.load(QDir(configDir).filePath(settingsFile(kind)), kind))
        return std::nullopt;

    Source src;
    src.kind = kind;
    src.configDir = QDir::cleanPath(configDir);
    src.winePrefix = winePrefix;
    src.nick = nickFrom(ini, kind);
    src.tempDirs = readTempDirs(ini, src);
    for (const QString& dir : std::as_const(src.tempDirs))
        src.partFiles += static_cast<int>(
            QDir(dir).entryList({QString(kPartMetFilter)}, QDir::Files).size());
    return src;
}

/// Folder as written by the old client: trailing separator, backslashes, and in a
/// Wine prefix a drive letter that maps through dosdevices.
QString mapPath(const QString& winePrefix, const QString& path)
{
    QString p = path.trimmed();
    if (p.isEmpty())
        return {};
    if (!winePrefix.isEmpty()) {
        static const QRegularExpression drive(QStringLiteral("^([A-Za-z]):(?:[\\\\/](.*))?$"));
        const auto m = drive.match(p);
        if (!m.hasMatch())
            return {};
        const QString letter = m.captured(1).toLower();
        QString root = QFileInfo(winePrefix + QStringLiteral("/dosdevices/") + letter + u':')
                           .canonicalFilePath();
        if (root.isEmpty() && letter == u"c")
            root = winePrefix + QStringLiteral("/drive_c");
        if (root.isEmpty())
            return {};
        p = root + u'/' + m.captured(2).replace(u'\\', u'/');
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

/// wxFileConfig FilterOutValue undone: \\ \" \n \r \t.
QString unescapeWx(const QString& value)
{
    if (!value.contains(u'\\'))
        return value;
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != u'\\' || i + 1 >= value.size()) {
            out.append(c);
            continue;
        }
        const QChar next = value.at(++i);
        switch (next.unicode()) {
        case 'n': out.append(u'\n'); break;
        case 'r': out.append(u'\r'); break;
        case 't': out.append(u'\t'); break;
        default: out.append(next); break;   // \\ and \"
        }
    }
    return out;
}

QStringList subdirs(const QString& dir, const QString& filter)
{
    QStringList out;
    const QDir d(dir);
    const QStringList names = filter.isEmpty()
        ? d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)
        : d.entryList({filter}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : names)
        out.append(d.filePath(name));
    return out;
}

QStringList winePrefixes(const SearchRoots& roots)
{
    QStringList prefixes;
    if (roots.home.isEmpty())
        return prefixes;
    if (const QString env = qEnvironmentVariable("WINEPREFIX"); !env.isEmpty())
        prefixes.append(env);
    prefixes.append(roots.home + QStringLiteral("/.wine"));
    if (roots.macLayout) {
        prefixes += subdirs(roots.home + QStringLiteral("/Library/Application Support/CrossOver/Bottles"));
    } else {
        prefixes += subdirs(roots.home + QStringLiteral("/.cxoffice"));
    }
    prefixes.removeIf([](const QString& p) { return !QFileInfo(p + QStringLiteral("/drive_c")).isDir(); });
    return prefixes;
}

void discoverWine(const QString& prefix,
                  const std::function<void(const QString&, SourceKind, const QString&)>& tryDir)
{
    const QString driveC = prefix + QStringLiteral("/drive_c");
    for (const QString& user : subdirs(driveC + QStringLiteral("/users"))) {
        for (const char* rel : {"AppData/Local/eMule/config", "Local Settings/Application Data/eMule/config",
                                "AppData/Roaming/eMule/config", "Application Data/eMule/config"}) {
            tryDir(user + u'/' + QLatin1StringView(rel), SourceKind::Emule, prefix);
        }
    }
    tryDir(driveC + QStringLiteral("/ProgramData/eMule/config"), SourceKind::Emule, prefix);
    for (const QString& pf : subdirs(driveC, QStringLiteral("Program Files*")))
        tryDir(pf + QStringLiteral("/eMule/config"), SourceKind::Emule, prefix);
}

/// %NAME% -> environment value, for requests written by hand.
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

/// The user hash is worthless without its key and the other way round
bool hasIdentity(const QDir& source)
{
    return source.exists(QLatin1StringView(LegacyIdentityImport::kLegacyPrefsFileName))
           && source.exists(QLatin1StringView(LegacyIdentityImport::kCryptKeyFileName));
}

/// No hash to take over: ours becomes the ed3k-derived one (eD2K compat, §14.2)
void applyDerivedUserHash(Preferences& prefs, Outcome& out)
{
    const auto hash = Preferences::derivedUserHash();
    if (!hash || *hash == prefs.userHash())
        return;
    const auto hex = [](const std::array<uint8, 16>& h) {
        return encodeBase16(std::span<const uint8>(h.data(), h.size()));
    };
    logInfo(QStringLiteral("Import: no user hash in the source; %1 replaced by the ed3k-derived %2")
                .arg(hex(prefs.userHash()), hex(*hash)));
    prefs.setUserHash(*hash);
    out.userHashDerived = true;
}

int copyDataFiles(const QDir& source, const QDir& config, Outcome& out)
{
    const bool identity = hasIdentity(source);

    QStringList names;
    for (const char* name : kDataFiles) {
        const QLatin1StringView file(name);
        if (!source.exists(file))
            continue;
        if (!identity && (file == QLatin1StringView(LegacyIdentityImport::kLegacyPrefsFileName)
                          || file == QLatin1StringView(LegacyIdentityImport::kCryptKeyFileName))) {
            continue;
        }
        names.append(file);
    }

    // This profile's own copies are kept, not overwritten
    for (const QString& name : std::as_const(names)) {
        if (!config.exists(name))
            continue;
        if (out.backupDir.isEmpty()) {
            out.backupDir = config.filePath(QStringLiteral("import-backup-")
                + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
            QDir().mkpath(out.backupDir);
        }
        if (QFile::rename(config.filePath(name), QDir(out.backupDir).filePath(name)))
            ++out.filesBackedUp;
        else
            QFile::remove(config.filePath(name));
    }

    int copied = 0;
    for (const QString& name : std::as_const(names)) {
        if (QFile::copy(source.filePath(name), config.filePath(name)))
            ++copied;
        else
            logWarning(QStringLiteral("Import: could not copy %1").arg(source.filePath(name)));
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

/// Empty for a client's shipped default (its homepage URL): ours stays.
QString nickFrom(const LegacyIni& ini, SourceKind kind)
{
    const QString nick = ini.utf8String(kMainSection, QLatin1StringView("Nick"));
    if (nick.isEmpty() || Preferences::isMfcDefaultNick(nick)
        || nick.contains(QLatin1StringView("emule-project.net"))
        || (kind == SourceKind::Amule && nick.contains(QLatin1StringView("amule"), Qt::CaseInsensitive)
            && nick.contains(QLatin1StringView("://")))) {
        return {};
    }
    return nick;
}

/// Encryption keys are left out on purpose: obfuscation must never be turned off.
/// aMule keeps eMule's [eMule] key names; where it differs, both are read.
int applyPreferences(const LegacyIni& ini, const Source& src, Preferences& prefs)
{
    const bool amule = src.kind == SourceKind::Amule;
    int applied = 0;

    if (!src.nick.isEmpty()) {
        prefs.setNick(src.nick);
        ++applied;
    }

    if (const auto port = ini.integer(kMainSection, QLatin1StringView("Port"));
        port && *port > 0 && *port <= std::numeric_limits<uint16>::max()) {
        prefs.setPort(static_cast<uint16>(*port));
        ++applied;
    }
    // One port for eD2K TCP, eD2K UDP and ed3k (ed3k Protocol.md §14.1): the old
    // UDP port and aMule's UDP switch are dropped
    prefs.setUdpPort(prefs.port());
    prefs.setEd3kUdpPort(prefs.port());

    // Capacities before limits, as MFC loads them
    const auto rate = [&ini](QLatin1StringView key) -> std::optional<uint32> {
        const auto value = ini.integer(kMainSection, key);
        if (!value)
            return std::nullopt;
        // 0 = no limit here; MFC writes UNLIMITED as -1, 0xFFFF or _UI32_MAX, aMule 0
        return (*value <= 0 || *value >= kMfcUnlimitedFloor) ? 0u : static_cast<uint32>(*value);
    };
    if (const auto value = rate(QLatin1StringView("DownloadCapacity")); value && *value != 0) {
        prefs.setMaxGraphDownloadRate(*value);
        ++applied;
    }
    if (const auto value = rate(QLatin1StringView(amule ? "UploadCapacity" : "UploadCapacityNew"));
        value && *value != 0) {
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
    if (const auto value = ini.boolean(kMainSection,
                                       QLatin1StringView(amule ? "ConnectToKad" : "NetworkKademlia"))) {
        prefs.setKadEnabled(*value);
        ++applied;
    }
    if (const auto value = ini.boolean(kMainSection,
                                       QLatin1StringView(amule ? "ConnectToED2K" : "NetworkED2K"))) {
        prefs.setNetworkED2K(*value);
        ++applied;
    }

    // Finished files stay where they are, so known.met still matches them
    if (const QString incoming = mapPath(src.winePrefix,
                                         ini.string(kMainSection, QLatin1StringView("IncomingDir")));
        !incoming.isEmpty() && QDir(incoming).exists()) {
        prefs.setIncomingDir(incoming);
        ++applied;
    }
    return applied;
}

/// MFC: TempDir, then '|'-separated TempDirs, default <config>/../Temp. aMule:
/// TempDir, default <config>/Temp. Never adopted as our temp dir -- two clients on
/// the same part files corrupt them; the converter copies instead.
QStringList readTempDirs(const LegacyIni& ini, const Source& src)
{
    QStringList raw{mapPath(src.winePrefix, ini.string(kMainSection, QLatin1StringView("TempDir")))};
    if (src.kind == SourceKind::Emule) {
        for (const QString& entry :
             ini.string(kMainSection, QLatin1StringView("TempDirs")).split(u'|', Qt::SkipEmptyParts)) {
            raw.append(mapPath(src.winePrefix, entry));
        }
    }
    if (raw.first().isEmpty()) {
        raw.append(QDir::cleanPath(src.configDir + (src.kind == SourceKind::Emule
                                                        ? QStringLiteral("/../Temp")
                                                        : QStringLiteral("/Temp"))));
    }

    QStringList dirs;
    for (const QString& dir : std::as_const(raw)) {
        if (!dir.isEmpty() && QDir(dir).exists() && !dirs.contains(dir, Qt::CaseInsensitive))
            dirs.append(dir);
    }
    return dirs;
}

/// Category.ini: [General] Count=N, then [Cat#0]..[Cat#N]; Cat#0 is "All".
int applyMfcCategories(const QString& path, const Source& src, Preferences& prefs)
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
            cat.incomingPath = mapPath(src.winePrefix, ini.utf8String(section, QLatin1StringView("Incoming")));
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

/// amule.conf: [General] Count=N, then [Cat#1]..[Cat#N]; the default one is not written.
int applyAmuleCategories(const LegacyIni& ini, const Source& src, Preferences& prefs)
{
    if (prefs.categoryCount() > 1)
        return 0;
    const qint64 count = ini.integer(QLatin1StringView("General"), QLatin1StringView("Count"))
                             .value_or(0);
    if (count <= 0)
        return 0;

    QList<DownloadCategory> list = prefs.categories().mid(0, 1);
    if (list.isEmpty())
        list.append(DownloadCategory{});
    for (qint64 i = 1; i <= count && list.size() < kMaxCategories; ++i) {
        const QByteArray name = QByteArrayLiteral("Cat#") + QByteArray::number(i);
        const QLatin1StringView section(name);

        DownloadCategory cat;
        cat.title = ini.string(section, QLatin1StringView("Title"));
        // aMule skips these too
        if (cat.title.isEmpty())
            continue;
        cat.incomingPath = mapPath(src.winePrefix, ini.string(section, QLatin1StringView("Incoming")));
        cat.comment = ini.string(section, QLatin1StringView("Comment"));
        list.append(cat);
    }
    if (list.size() < 2)
        return 0;

    prefs.setCategories(list);
    return prefs.categoryCount() - 1;
}

/// shareddir.dat: one folder per line. eMule: UTF-16LE with BOM or ANSI. aMule: UTF-8.
int applySharedDirs(const QString& path, const Source& src, Preferences& prefs)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray data = file.readAll();
    const QString text = data.startsWith("\xFF\xFE")
        ? QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2),
                             (data.size() - 2) / 2)
        : src.kind == SourceKind::Amule ? QString::fromUtf8(data) : QString::fromLocal8Bit(data);

    QStringList dirs = prefs.sharedDirs();
    int added = 0;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString dir = mapPath(src.winePrefix, line);
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
