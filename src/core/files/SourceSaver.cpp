#include "pch.h"
/// @file SourceSaver.cpp
/// @brief Save/Load Sources implementation — see SourceSaver.h for the format contract.

#include "files/SourceSaver.h"
#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <QtEndian>

namespace eMule {

namespace {

/// Value inet_addr() returns for an unparsable address; MorphXT skips those records
/// (SourceSaver.cpp:105-106). It is also the broadcast address, which is never a source.
constexpr uint32 kInvalidAddress = 0xFFFFFFFFu;

/// Length of the "yymmddhhmm" stamp MorphXT v12.7 writes. The pre-Morph khaos format used a
/// 6-char "yymmdd"; those records are simply skipped rather than half-understood.
constexpr qsizetype kExpirationLength = 10;

/// Split "host:port" at the last colon so a bracketed IPv6 literal survives.
[[nodiscard]] bool splitHostPort(QStringView text, QStringView& host, uint16& port)
{
    const qsizetype colon = text.lastIndexOf(u':');
    if (colon <= 0)
        return false;

    bool ok = false;
    const uint port32 = text.sliced(colon + 1).toUInt(&ok);
    if (!ok || port32 > 0xFFFFu)
        return false;

    host = text.sliced(0, colon);
    port = static_cast<uint16>(port32);
    return true;
}

/// Parse a bracketed or bare IPv6 literal. Returns a null Address when it is not IPv6.
[[nodiscard]] Address parseIPv6(QStringView text)
{
    if (text.size() >= 2 && text.front() == u'[' && text.back() == u']')
        text = text.sliced(1, text.size() - 2);

    const Address addr = Address::fromString(text.toString());
    return addr.isIPv6() ? addr : Address();
}

/// inet_addr() equivalent: a dotted quad as a network-order uint32.
///
/// Not routed through Address, which collapses 0.0.0.0 to a null address — the server
/// endpoint of a Kad-only source is legitimately 0.0.0.0:0 and must survive the round trip.
[[nodiscard]] std::optional<uint32> parseDottedQuad(QStringView text)
{
    QHostAddress host;
    if (!host.setAddress(text.toString()))
        return std::nullopt;
    if (host.protocol() != QAbstractSocket::IPv4Protocol)
        return std::nullopt;
    return qToBigEndian(static_cast<uint32>(host.toIPv4Address()));
}

} // namespace

// ---------------------------------------------------------------------------
// SavedSource
// ---------------------------------------------------------------------------

uint32 SavedSource::hybridId() const
{
    // ver >= 3 already stores the hybrid (host-order) ID, exactly as MorphXT wrote it
    // (SourceSaver.cpp:28-31). Below that the field is a network-order IPv4.
    return (srcExchangeVer >= 3) ? legacyId
                                 : Address::fromNetworkOrder(legacyId).toUint32();
}

bool SavedSource::sameSourceAs(const SavedSource& other) const
{
    // A source that moved to a new address is still the same peer.
    if (hasUserHash && other.hasUserHash)
        return md4equ(userHash.data(), other.userHash.data());

    // IPv6-only records all carry legacyId == 0, so the IPv4 endpoint cannot tell them apart.
    if (legacyId == 0 || other.legacyId == 0)
        return !ipv6.isNull() && ipv6 == other.ipv6 && port == other.port;

    return hybridId() == other.hybridId() && port == other.port;
}

// ---------------------------------------------------------------------------
// SourceListFile — writing
// ---------------------------------------------------------------------------

QString SourceListFile::formatRecord(const SavedSource& src)
{
    // A record with no usable IPv4 puts its IPv6 in the host field, which MorphXT could not
    // parse — so such a line is always private, whatever the caller asked for.
    const bool ipv6Host = (src.legacyId == 0 && src.ipv6.isIPv6());
    const bool isPrivate = src.privateLine || ipv6Host;

    // ipstr() renders the uint32's bytes in memory order, matching MFC's ipstr()
    // (OtherFunctions.cpp:2872) byte for byte. That is what makes a ver>=3 hybrid ID print
    // as a reversed dotted quad — see the SavedSource docs.
    const QString host = ipv6Host ? QStringLiteral("[%1]").arg(src.ipv6.toString())
                                  : ipstr(src.legacyId);

    QString line = QStringLiteral("%1:%2,%3,%4,%5:%6;")
                       .arg(host)
                       .arg(src.port)
                       .arg(src.expiration)
                       .arg(static_cast<int>(src.srcExchangeVer))
                       .arg(ipstr(src.serverIP))
                       .arg(src.serverPort);

    QStringList ext;
    if (!ipv6Host && src.ipv6.isIPv6())
        ext << QStringLiteral("v6=[%1]").arg(src.ipv6.toString());
    if (src.hasUserHash)
        ext << QStringLiteral("h=%1").arg(encodeBase16(src.userHash));
    if (src.connectOptions != 0)
        ext << QStringLiteral("co=%1").arg(src.connectOptions);
    if (src.kadPort != 0)
        ext << QStringLiteral("kp=%1").arg(src.kadPort);
    if (src.udpPort != 0)
        ext << QStringLiteral("up=%1").arg(src.udpPort);

    if (!ext.isEmpty())
        line += ext.join(u',');

    return isPrivate ? (kPrivatePrefix + line) : line;
}

bool SourceListFile::write(const QString& path, const std::vector<SavedSource>& records,
                           const QString& ed2kLink)
{
    if (path.isEmpty())
        return false;

    // A category can point a download at a temp dir that startup never saw, so the directory
    // is created here as well as at startup.
    const QString dirPath = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        logError(QStringLiteral("SourceSaver: cannot create %1").arg(dirPath));
        return false;
    }

    QByteArray out;
    // The first two lines are byte-identical to MorphXT (SourceSaver.cpp:286-287); the third
    // is ours and is a comment to both readers.
    out += "#format: a.b.c.d:port,expirationdate(yymmddhhmm);\r\n";
    if (!ed2kLink.isEmpty())
        out += '#' + ed2kLink.toUtf8() + "\r\n";
    out += "#emuleqt-sls: 1\r\n";

    // Public records first so a MorphXT reader hits them before the private tail.
    QStringList publicLines;
    QStringList privateLines;
    for (const auto& rec : records) {
        const QString line = formatRecord(rec);
        // Never emit a blank line: MorphXT's reader calls CString::GetAt(0) unconditionally
        // (SourceSaver.cpp:97), which asserts on an empty string in a debug build.
        if (line.isEmpty())
            continue;
        (line.startsWith(kPrivatePrefix) ? privateLines : publicLines) << line;
    }
    for (const QString& line : publicLines)
        out += line.toUtf8() + "\r\n";
    for (const QString& line : privateLines)
        out += line.toUtf8() + "\r\n";

    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile::remove(tmpPath);

    {
        QFile file(tmpPath);
        // No QIODevice::Text — it would rewrite the literal CRLF the format requires.
        if (!file.open(QIODevice::WriteOnly)) {
            logError(QStringLiteral("SourceSaver: cannot write %1").arg(tmpPath));
            return false;
        }
        if (file.write(out) != out.size()) {
            logError(QStringLiteral("SourceSaver: short write on %1").arg(tmpPath));
            file.close();
            QFile::remove(tmpPath);
            return false;
        }
    } // closed before the rename

    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        logError(QStringLiteral("SourceSaver: failed to rename tmp → %1").arg(path));
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SourceListFile — reading
// ---------------------------------------------------------------------------

std::optional<SavedSource> SourceListFile::parseRecord(QStringView line)
{
    while (!line.isEmpty() && (line.back() == u'\r' || line.back() == u'\n'
                               || line.back() == u' ' || line.back() == u'\t'))
        line.chop(1);
    if (line.isEmpty())
        return std::nullopt;

    SavedSource rec;
    if (line.startsWith(kPrivatePrefix)) {
        rec.privateLine = true;
        line = line.sliced(kPrivatePrefix.size());
    } else if (line.front() == u'#') {
        return std::nullopt;  // comment — including MorphXT's own header lines
    }

    const qsizetype semi = line.indexOf(u';');
    if (semi < 0)
        return std::nullopt;

    // Everything MorphXT reads lives before the ';', in four comma-separated fields. An IPv6
    // literal contains colons but never a comma, so splitting here is safe.
    const QList<QStringView> parts = line.sliced(0, semi).split(u',');
    if (parts.size() != 4)
        return std::nullopt;

    // 1. host:port
    QStringView hostText;
    if (!splitHostPort(parts[0], hostText, rec.port) || rec.port == 0)
        return std::nullopt;  // MorphXT skips a zero port (SourceSaver.cpp:113-114)

    if (const Address v6 = parseIPv6(hostText); !v6.isNull()) {
        rec.ipv6 = v6;
        rec.legacyId = 0;
    } else {
        const auto v4 = parseDottedQuad(hostText);
        // 0 is MorphXT's marker for "no address" and 0xFFFFFFFF is inet_addr()'s failure
        // value (SourceSaver.cpp:105-106); neither can be a source.
        if (!v4 || *v4 == 0 || *v4 == kInvalidAddress)
            return std::nullopt;
        rec.legacyId = *v4;
    }

    // 2. expiration — kept verbatim; expiry itself is read()'s call
    if (parts[1].size() != kExpirationLength)
        return std::nullopt;
    rec.expiration = parts[1].toString();

    // 3. source-exchange version
    bool ok = false;
    const uint ver = parts[2].toUInt(&ok);
    if (!ok || ver > 0xFFu)
        return std::nullopt;
    rec.srcExchangeVer = static_cast<uint8>(ver);

    // 4. serverIp:serverPort. MorphXT rejects 0.0.0.0 and port 0 here; we accept both,
    //    because that is exactly what a Kad-only source looks like — MorphXT writes such
    //    records itself and then discards them on its own next read.
    QStringView serverText;
    if (!splitHostPort(parts[3], serverText, rec.serverPort))
        return std::nullopt;
    const auto serverIp = parseDottedQuad(serverText);
    if (!serverIp)
        return std::nullopt;
    rec.serverIP = (*serverIp == kInvalidAddress) ? 0 : *serverIp;

    // -- extension fields: never fatal, per-field failure drops only that field --
    for (QStringView field : line.sliced(semi + 1).split(u',', Qt::SkipEmptyParts)) {
        const qsizetype eq = field.indexOf(u'=');
        if (eq <= 0)
            continue;
        const QStringView key = field.sliced(0, eq);
        const QStringView val = field.sliced(eq + 1);

        if (key == QLatin1StringView("v6")) {
            if (const Address v6 = parseIPv6(val); !v6.isNull())
                rec.ipv6 = v6;
        } else if (key == QLatin1StringView("h")) {
            if (decodeBase16(val.toString(), rec.userHash.data(), rec.userHash.size())
                == rec.userHash.size())
                rec.hasUserHash = true;
            else
                rec.userHash.fill(0);
        } else if (key == QLatin1StringView("co")) {
            bool fieldOk = false;
            if (const uint co = val.toUInt(&fieldOk); fieldOk && co <= 0xFFu)
                rec.connectOptions = static_cast<uint8>(co);
        } else if (key == QLatin1StringView("kp")) {
            bool fieldOk = false;
            if (const uint kp = val.toUInt(&fieldOk); fieldOk && kp <= 0xFFFFu)
                rec.kadPort = static_cast<uint16>(kp);
        } else if (key == QLatin1StringView("up")) {
            bool fieldOk = false;
            if (const uint up = val.toUInt(&fieldOk); fieldOk && up <= 0xFFFFu)
                rec.udpPort = static_cast<uint16>(up);
        }
        // Anything else is a newer client's field — ignore it and keep the record.
    }

    return rec;
}

std::vector<SavedSource> SourceListFile::read(const QString& path, bool dropExpired)
{
    std::vector<SavedSource> records;
    if (path.isEmpty())
        return records;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return records;  // no list yet — the normal case on a fresh download

    const QString text = QString::fromUtf8(file.readAll());
    for (QStringView line : QStringView(text).split(u'\n')) {
        auto rec = parseRecord(line);
        if (!rec)
            continue;
        if (dropExpired && isExpired(rec->expiration))
            continue;
        records.push_back(std::move(*rec));
    }
    return records;
}

// ---------------------------------------------------------------------------
// SourceListFile — expiration
// ---------------------------------------------------------------------------

QString SourceListFile::calcExpiration(int minutesFromNow)
{
    const QDateTime expiry =
        QDateTime::currentDateTime().addSecs(static_cast<qint64>(minutesFromNow) * 60);
    const QDate date = expiry.date();
    const QTime time = expiry.time();

    return QStringLiteral("%1%2%3%4%5")
        .arg(date.year() % 100, 2, 10, QChar(u'0'))
        .arg(date.month(),      2, 10, QChar(u'0'))
        .arg(date.day(),        2, 10, QChar(u'0'))
        .arg(time.hour(),       2, 10, QChar(u'0'))
        .arg(time.minute(),     2, 10, QChar(u'0'));
}

bool SourceListFile::isExpired(QStringView expiration)
{
    // Parsed by hand rather than through QDateTime::fromString("yyMMddhhmm"): Qt's
    // two-digit-year century rule and its hh/HH handling are both easy to get subtly wrong,
    // and MFC simply builds CTime(year, month, day, hour, minute, 0) in local time.
    if (expiration.size() != kExpirationLength)
        return true;

    int fields[5] = {};
    for (int i = 0; i < 5; ++i) {
        bool ok = false;
        fields[i] = expiration.sliced(i * 2, 2).toInt(&ok);
        if (!ok)
            return true;
    }

    const QDateTime expiry(QDate(2000 + fields[0], fields[1], fields[2]),
                           QTime(fields[3], fields[4]), QTimeZone::LocalTime);
    if (!expiry.isValid())
        return true;

    return expiry < QDateTime::currentDateTime();
}

// ---------------------------------------------------------------------------
// SourceSaver — public
// ---------------------------------------------------------------------------

SourceSaver::SourceSaver()
{
    // MorphXT pre-ages both timers (SourceSaver.cpp:16-21) so the first tick after startup
    // already loads, injects and saves instead of waiting out the 10-minute window. The
    // extra jitter span is ours: MorphXT lands exactly on the threshold, so whether its
    // first tick fires depends on the sign of the random offset and on how long startup
    // took. The arithmetic is deliberately allowed to wrap; every comparison is a signed
    // delta.
    const auto now = static_cast<uint32>(getTickCount());
    m_lastLoaded = now - kReloadTimeMs - kTimerJitterMs;
    m_lastSaved  = (now + static_cast<uint32>(jitter())) - kResaveTimeMs - kTimerJitterMs;
}

bool SourceSaver::process(PartFile* file)
{
    if (!file)
        return false;

    const auto now = static_cast<uint32>(getTickCount());
    if (static_cast<int32>(now - m_lastSaved) <= static_cast<int32>(kResaveTimeMs))
        return false;

    const QString path = filePath(file->tmpPath(), file->partMetFileName());
    if (path.isEmpty())
        return false;

    // MorphXT keeps SLS for rare files only: above the limit it deletes the list and saves
    // nothing (SourceSaver.cpp:57-62), deliberately without stamping the timer so the check
    // repeats every tick. eMuleQt disables the gate and remembers sources for every download;
    // the branch stays so re-enabling it is a one-constant change.
    if (kRareFileSourceLimit >= 0 && file->availableSourceCount() > kRareFileSourceLimit) {
        QFile::remove(path);
        return false;
    }

    m_lastSaved = (now + static_cast<uint32>(jitter()));

    // Read before overwriting: the previous set both tops up this save and is what gets
    // re-injected, exactly as MorphXT does (SourceSaver.cpp:64-71).
    const std::vector<SavedSource> previous = SourceListFile::read(path);
    const bool written = saveList(file, path, previous);

    if (static_cast<int32>(now - m_lastLoaded) > static_cast<int32>(kReloadTimeMs)) {
        m_lastLoaded = (now + static_cast<uint32>(jitter()));
        static_cast<void>(injectRecords(file, previous));
    }

    return written;
}

bool SourceSaver::saveNow(PartFile* file)
{
    if (!file)
        return false;

    const QString path = filePath(file->tmpPath(), file->partMetFileName());
    if (path.isEmpty())
        return false;

    if (kRareFileSourceLimit >= 0 && file->availableSourceCount() > kRareFileSourceLimit) {
        QFile::remove(path);
        return false;
    }

    m_lastSaved = static_cast<uint32>(getTickCount()) + static_cast<uint32>(jitter());
    return saveList(file, path, SourceListFile::read(path));
}

int SourceSaver::loadAndInject(PartFile* file)
{
    if (!file)
        return 0;

    const QString path = filePath(file->tmpPath(), file->partMetFileName());
    if (path.isEmpty())
        return 0;

    return injectRecords(file, SourceListFile::read(path));
}

void SourceSaver::removeFile(const PartFile* file)
{
    if (file)
        removeFile(file->tmpPath(), file->partMetFileName());
}

void SourceSaver::removeFile(const QString& tmpPath, const QString& partMetFilename)
{
    const QString path = filePath(tmpPath, partMetFilename);
    if (path.isEmpty())
        return;

    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".tmp"));  // a save interrupted mid-write
}

QString SourceSaver::filePath(const QString& tmpPath, const QString& partMetFilename)
{
    if (tmpPath.isEmpty() || partMetFilename.isEmpty())
        return {};

    const QString listDir = QDir(tmpPath).filePath(QString::fromLatin1(kSourceListsDirName));
    return QDir(listDir).filePath(partMetFilename + QString::fromLatin1(kSourceListSuffix));
}

void SourceSaver::ensureDirectories(const QStringList& tempDirs)
{
    for (const QString& tempDir : tempDirs) {
        if (tempDir.isEmpty())
            continue;
        const QString listDir = QDir(tempDir).filePath(QString::fromLatin1(kSourceListsDirName));
        if (!QDir().mkpath(listDir))
            logWarning(QStringLiteral("SourceSaver: cannot create %1").arg(listDir));
    }
}

// ---------------------------------------------------------------------------
// SourceSaver — private
// ---------------------------------------------------------------------------

bool SourceSaver::saveList(PartFile* file, const QString& path,
                           const std::vector<SavedSource>& previous)
{
    std::vector<SavedSource> best = collectBestSources(file, kMaxSavedSourcesPerFile);
    mergePrevious(best, previous, kMaxSavedSourcesPerFile);

    // Nothing worth remembering and nothing remembered before — leave the disk alone rather
    // than churning an empty file every 10 minutes.
    if (best.empty()) {
        QFile::remove(path);
        return false;
    }

    // MorphXT stores the eD2K link as a met-recovery aid (SourceSaver.cpp:287). Neither
    // parser reads it back, so the canonical three-field form is enough.
    const QString ed2kLink = QStringLiteral("ed2k://|file|%1|%2|%3|/")
                                 .arg(urlEncode(stripInvalidFilenameChars(file->fileName())))
                                 .arg(static_cast<uint64>(file->fileSize()))
                                 .arg(md4str(file->fileHash()));

    return SourceListFile::write(path, best, ed2kLink);
}

std::vector<SavedSource> SourceSaver::collectBestSources(const PartFile* file, int maxToSave)
{
    const QString expiration = SourceListFile::calcExpiration(kSourceExpiryMinutes);

    std::vector<SavedSource> candidates;
    candidates.reserve(file->srcList().size());

    for (const UpDownClient* client : file->srcList()) {
        if (auto rec = makeRecord(client, file, expiration))
            candidates.push_back(std::move(*rec));
    }

    // MorphXT ranks with a hand-rolled insertion loop (SourceSaver.cpp:205-257) that has two
    // bugs: bInserted is never reset between iterations, and the "holds a part we need" test
    // inserts at whatever tail-ward position the walk has reached — placing the best
    // candidates last, the inverse of the intent. Its first candidate also bypasses the crypt
    // filter entirely. This comparator implements the documented policy instead; the on-disk
    // format is untouched, so compatibility is unaffected.
    const bool manySources = file->availableSourceCount() > 2 * maxToSave;

    std::ranges::stable_sort(candidates, [manySources](const SavedSource& a, const SavedSource& b) {
        if (a.holdsNeededPart != b.holdsNeededPart)
            return a.holdsNeededPart;
        if (manySources) {
            if (a.srcExchangeVer != b.srcExchangeVer)
                return a.srcExchangeVer > b.srcExchangeVer;
            if (a.availableParts != b.availableParts)
                return a.availableParts > b.availableParts;
        } else {
            if (a.availableParts != b.availableParts)
                return a.availableParts > b.availableParts;
            if (a.srcExchangeVer != b.srcExchangeVer)
                return a.srcExchangeVer > b.srcExchangeVer;
        }
        return a.hybridId() < b.hybridId();   // deterministic tie-break
    });

    if (candidates.size() > static_cast<std::size_t>(maxToSave))
        candidates.resize(static_cast<std::size_t>(maxToSave));

    return candidates;
}

std::optional<SavedSource> SourceSaver::makeRecord(const UpDownClient* client,
                                                   const PartFile* file,
                                                   const QString& expiration)
{
    // MorphXT saves only sources that are actually usable for this file and speak eD2K
    // (SourceSaver.cpp:191-196). A URL source has nothing to reconnect to.
    const DownloadState state = client->downloadState();
    if (state != DownloadState::OnQueue && state != DownloadState::Downloading
        && state != DownloadState::NoNeededParts)
        return std::nullopt;
    if (!client->isEd2kClient())
        return std::nullopt;

    const bool hasHash = client->hasValidHash();

    // MorphXT drops every crypt-required source (SourceSaver.cpp:202-204) purely because it
    // cannot store the user hash. We can, so we keep them — but only when we actually have
    // the hash, and they go on a private line a MorphXT reader will skip.
    const bool needsObfuscation = client->requiresCryptLayer() || thePrefs.cryptLayerRequired();
    if (needsObfuscation && !hasHash)
        return std::nullopt;

    const Address v4 = client->userAddress().isIPv4() ? client->userAddress() : Address();
    const Address v6 = (client->openIPv6() && client->userIPv6().isIPv6()
                        && client->userIPv6().isPublicIP())
                           ? client->userIPv6()
                           : Address();
    if (v4.isNull() && v6.isNull())
        return std::nullopt;

    SavedSource rec;
    rec.srcExchangeVer = client->sourceExchange1Ver();

    if (v4.isNull()) {
        rec.legacyId = 0;             // IPv6-only — the address goes in the host field
    } else if (rec.srcExchangeVer > 2) {
        rec.legacyId = client->userIDHybrid();      // host order, as MorphXT writes it
    } else {
        rec.legacyId = v4.toNetworkUint32();        // MFC GetIP()
    }

    // A LowID peer is only reachable through a server callback, and only the ver>=3 encoding
    // preserves its ID: the ver<3 branch would store the peer's own IP and reload it as a
    // HighID we can never reach. MorphXT saves those anyway; we skip them.
    if (client->hasLowID()) {
        if (v6.isNull()
            && (rec.srcExchangeVer < 3 || client->serverAddress().isNull()
                || client->serverPort() == 0))
            return std::nullopt;
        if (v6.isNull())
            rec.legacyId = client->userIDHybrid();
    }

    rec.port       = client->userPort();
    rec.serverIP   = client->serverAddress().toNetworkUint32();
    rec.serverPort = client->serverPort();
    rec.expiration = expiration;
    rec.ipv6       = v6;
    rec.kadPort    = client->kadPort();
    rec.udpPort    = client->udpPort();

    if (hasHash) {
        md4cpy(rec.userHash.data(), client->userHash());
        rec.hasUserHash = true;
    }

    // Same bit layout UpDownClient::setConnectOptions() decodes (UpDownClient.cpp:446-452).
    if (client->supportsCryptLayer())        rec.connectOptions |= 0x01;
    if (client->requestsCryptLayer())        rec.connectOptions |= 0x02;
    if (client->requiresCryptLayer())        rec.connectOptions |= 0x04;
    if (client->supportsDirectUDPCallback()) rec.connectOptions |= 0x08;

    // Anything a MorphXT reader could not use correctly is hidden behind the "#x=" prefix:
    // an IPv6-only source it cannot address, and an obfuscated one it would try to reach
    // without the user hash the handshake needs.
    rec.privateLine = v4.isNull() || needsObfuscation;

    // Ranking inputs — never persisted.
    rec.availableParts = client->availablePartCount();
    const auto& status = client->partStatus();
    if (client->partCount() == file->partCount() && status.size() == file->partCount()) {
        for (uint16 part = 0; part < file->partCount(); ++part) {
            if (status[part] != 0 && !file->isComplete(part)) {
                rec.holdsNeededPart = true;   // MorphXT's !IsPartShareable(x) test
                break;
            }
        }
    }

    return rec;
}

void SourceSaver::mergePrevious(std::vector<SavedSource>& out,
                                const std::vector<SavedSource>& previous, int maxToSave)
{
    // MorphXT tops the list up from the previously saved set (SourceSaver.cpp:260-278) so a
    // source that has just gone quiet is not forgotten the moment it stops answering.
    for (const SavedSource& old : previous) {
        if (out.size() >= static_cast<std::size_t>(maxToSave))
            break;
        const bool known = std::ranges::any_of(out, [&old](const SavedSource& kept) {
            return kept.sameSourceAs(old);
        });
        if (!known)
            out.push_back(old);
    }
}

int SourceSaver::injectRecords(PartFile* file, const std::vector<SavedSource>& records)
{
    auto* queue = theApp.downloadQueue;
    if (!queue || records.empty())
        return 0;

    // Saved sources are a cold-start aid, nothing more. Once a file has found this many live
    // sources by itself, day-old entries are far likelier to be dead than useful, so skip the
    // injection entirely — we keep saving, we just stop dialling stale peers. MorphXT has no
    // such gate: its records expire after 30 minutes, so it injects until GetMaxSources() is
    // reached (SourceSaver.cpp:155-156).
    if (file->sourceCount() > kSkipInjectAboveSources)
        return 0;

    int added = 0;

    for (const SavedSource& rec : records) {
        if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
            break;

        // The list is untrusted input — it may have been written by another client, or by us
        // before the IP filter was updated — so both families are re-vetted here.
        const Address v6 = queue->vetPeerAddress(rec.ipv6);

        uint32 ed2kUserId = 0;
        if (rec.legacyId == 0) {
            if (v6.isNull())
                continue;                   // IPv6-only record whose address we just rejected
            ed2kUserId = kNoIPv4SourceId;
        } else {
            const uint32 hybrid = rec.hybridId();
            if (isLowID(hybrid)) {
                // A LowID peer is only reachable by asking its server to call back.
                if (rec.serverIP == 0 || rec.serverPort == 0)
                    continue;
                ed2kUserId = hybrid;
            } else if (const Address v4 = queue->vetPeerAddress(Address::fromHostOrder(hybrid));
                       !v4.isNull()) {
                ed2kUserId = v4.toNetworkUint32();
            } else if (!v6.isNull()) {
                ed2kUserId = kNoIPv4SourceId;   // IPv4 rejected, but IPv6 still reaches it
            } else {
                continue;
            }
        }

        const SourceHints hints{
            .serverIP       = rec.serverIP,
            .serverPort     = rec.serverPort,
            .userHash       = rec.hasUserHash ? rec.userHash.data() : nullptr,
            .connectOptions = rec.connectOptions,
            .kadPort        = rec.kadPort,
            .udpPort        = rec.udpPort,
        };

        if (queue->addVettedSource(file, ed2kUserId, v6, rec.port, SourceFrom::SLS, hints))
            ++added;
    }

    if (added > 0) {
        logInfo(QStringLiteral("Restored %1 saved source(s) for %2")
                    .arg(added).arg(file->fileName()));
    }
    return added;
}

int32 SourceSaver::jitter()
{
    // MorphXT: rand() * 30000 / RAND_MAX - 15000 (SourceSaver.cpp:19).
    constexpr auto span = static_cast<int32>(kTimerJitterMs);
    return static_cast<int32>(getRandomUInt16()) * span / 0xFFFF - span / 2;
}

} // namespace eMule
