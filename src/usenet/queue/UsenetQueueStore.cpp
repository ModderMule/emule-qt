#include "queue/UsenetQueueStore.h"

#include "queue/UsenetQueueItem.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <yaml-cpp/yaml.h>

#include <fstream>

namespace eMule::usenet {

namespace {

// 2 added requestedPar2 (phase 4, on-demand recovery volumes). A version-1
// sidecar still loads: the field is simply absent and the item re-requests what
// it needs on its next verify.
// publishedPaths and the health keys are *optional* additions and deliberately do
// not move the version: load() refuses anything newer than it knows, so a bump
// would make an older daemon drop the whole queue rather than lose one advisory
// list. An older sidecar loads with publishedPaths empty, which is honest — the
// release did complete, nothing recorded what it published.
constexpr int kStateVersion = 2;

/// Bits out as base64. A 10 000-segment release is 1.25 KB packed, which is what
/// makes storing per-segment completion affordable at all.
[[nodiscard]] QString bitsToBase64(const QBitArray& bits)
{
    if (bits.isEmpty())
        return {};
    QByteArray packed((bits.size() + 7) / 8, '\0');
    for (qsizetype i = 0; i < bits.size(); ++i) {
        if (bits.testBit(i))
            packed[int(i / 8)] = char(packed[int(i / 8)] | (1 << (i % 8)));
    }
    return QString::fromLatin1(packed.toBase64());
}

[[nodiscard]] QBitArray bitsFromBase64(const QString& text, int expectedSize)
{
    QBitArray bits(expectedSize);
    if (text.isEmpty() || expectedSize <= 0)
        return bits;

    const QByteArray packed = QByteArray::fromBase64(text.toLatin1());
    for (int i = 0; i < expectedSize; ++i) {
        const int byte = i / 8;
        if (byte >= packed.size())
            break;
        if (packed.at(byte) & (1 << (i % 8)))
            bits.setBit(i);
    }
    return bits;
}

[[nodiscard]] std::string toStd(const QString& s) { return s.toStdString(); }

[[nodiscard]] QString fromStd(const YAML::Node& node, const char* key)
{
    if (!node[key])
        return {};
    return QString::fromStdString(node[key].as<std::string>(std::string{}));
}

} // namespace

QString UsenetQueueStore::stateDir()
{
    const QString dir = QDir(thePrefs.configDir()).filePath(QStringLiteral("Usenet"));
    QDir().mkpath(dir);
    return dir;
}

QString UsenetQueueStore::statePath(const QString& itemId)
{
    return QDir(stateDir()).filePath(itemId + QStringLiteral(".nzbstate"));
}

bool UsenetQueueStore::save(const UsenetQueueItem& item)
{
    if (item.id.isEmpty())
        return false;

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << kStateVersion;
    out << YAML::Key << "id" << YAML::Value << toStd(item.id);
    out << YAML::Key << "name" << YAML::Value << toStd(item.name);
    out << YAML::Key << "status" << YAML::Value << int(item.status);
    out << YAML::Key << "priority" << YAML::Value << item.priority;
    // Written unconditionally: an absent key reads as 0, which is the implicit
    // "All" category, which is what every sidecar written before categories
    // existed already means. That is why kStateVersion does not move for it.
    out << YAML::Key << "category" << YAML::Value << item.category;
    // Optional keys, and kStateVersion deliberately does not move for them:
    // load() *refuses* a sidecar newer than it knows, so a bump would make an
    // older daemon drop the whole queue rather than lose one advisory figure.
    // Same call already made for requestedPar2 and partLength.
    if (item.healthPercent >= 0) {
        out << YAML::Key << "healthPercent" << YAML::Value << item.healthPercent;
        out << YAML::Key << "healthMissingBytes" << YAML::Value
            << static_cast<long long>(item.healthMissingBytes);
        out << YAML::Key << "healthRecoveryBytes" << YAML::Value
            << static_cast<long long>(item.healthRecoveryBytes);
        out << YAML::Key << "healthProbed" << YAML::Value << item.healthProbed;
    }
    if (!item.publishedPaths.isEmpty()) {
        out << YAML::Key << "publishedPaths" << YAML::Value << YAML::BeginSeq;
        for (const QString& path : item.publishedPaths)
            out << toStd(path);
        out << YAML::EndSeq;
    }
    if (!item.requestedPar2.isEmpty()) {
        QList<int> requested(item.requestedPar2.cbegin(), item.requestedPar2.cend());
        std::sort(requested.begin(), requested.end());   // stable file, readable diffs
        out << YAML::Key << "requestedPar2" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (int index : requested)
            out << index;
        out << YAML::EndSeq;
    }
    if (!item.error.isEmpty())
        out << YAML::Key << "error" << YAML::Value << toStd(item.error);
    if (!item.nzb.password.isEmpty())
        out << YAML::Key << "password" << YAML::Value << toStd(item.nzb.password);
    // Written only when true, like every other optional key here, so an existing
    // sidecar gains nothing and kStateVersion stays where it is.
    if (item.passwordRequired)
        out << YAML::Key << "passwordRequired" << YAML::Value << true;
    // The accounts that could not supply it, as they were when it failed. Absent
    // means "never failed under this version", and Resume then re-asks nothing —
    // which is what keeps the first run after an upgrade quiet.
    if (!item.failedLadder.isEmpty())
        out << YAML::Key << "failedLadder" << YAML::Value << toStd(item.failedLadder);
    // Which check stopped it and what it found: a paused item has to be able to
    // say why after a restart, and an overruled check must stay overruled.
    if (item.stopReason != UsenetStopReason::None) {
        out << YAML::Key << "stopReason" << YAML::Value << int(item.stopReason);
        out << YAML::Key << "stopDetail" << YAML::Value << toStd(item.stopDetail);
    }
    if (item.checksOverridden != 0)
        out << YAML::Key << "checksOverridden" << YAML::Value << item.checksOverridden;

    out << YAML::Key << "files" << YAML::Value << YAML::BeginSeq;
    for (int i = 0; i < item.nzb.files.size(); ++i) {
        const NzbFileInfo& info = item.nzb.files.at(i);
        const UsenetFileState& st = i < item.files.size() ? item.files.at(i)
                                                         : UsenetFileState{};

        out << YAML::BeginMap;
        out << YAML::Key << "subject" << YAML::Value << toStd(info.subject);
        out << YAML::Key << "poster" << YAML::Value << toStd(info.poster);
        out << YAML::Key << "date" << YAML::Value << static_cast<long long>(info.date);
        out << YAML::Key << "fileName" << YAML::Value << toStd(info.fileName);
        out << YAML::Key << "partsTotal" << YAML::Value << info.partsTotal;

        out << YAML::Key << "groups" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (const QString& g : info.groups)
            out << toStd(g);
        out << YAML::EndSeq;

        out << YAML::Key << "segments" << YAML::Value << YAML::BeginSeq;
        for (const NzbSegment& seg : info.segments) {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << toStd(seg.messageId);
            out << YAML::Key << "bytes" << YAML::Value << static_cast<long long>(seg.bytes);
            out << YAML::Key << "number" << YAML::Value << seg.number;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "tempPath" << YAML::Value << toStd(st.tempPath);
        out << YAML::Key << "finalPath" << YAML::Value << toStd(st.finalPath);
        out << YAML::Key << "articleFileName" << YAML::Value << toStd(st.articleFileName);
        // Optional, and kStateVersion deliberately does not move for it: absent
        // means "no PAR2 name known", which is exactly what every sidecar
        // written before this existed meant. load() refuses a version it does
        // not know, so bumping would make an older daemon drop the whole queue
        // rather than lose one name.
        if (!st.par2FileName.isEmpty())
            out << YAML::Key << "par2FileName" << YAML::Value << toStd(st.par2FileName);
        out << YAML::Key << "declaredSize" << YAML::Value << static_cast<long long>(st.declaredSize);
        out << YAML::Key << "decodedBytes" << YAML::Value << static_cast<long long>(st.decodedBytes);
        out << YAML::Key << "finalized" << YAML::Value << st.finalized;
        out << YAML::Key << "missingSegments" << YAML::Value << st.missingSegments;
        out << YAML::Key << "done" << YAML::Value << toStd(bitsToBase64(st.done));

        // Which of those resolved bits are holes rather than arrivals. Optional,
        // and kStateVersion stays where it is for the reason above: absent means
        // "this sidecar predates the map", which retryMissingArticles() refuses
        // rather than guessing which articles the count stood for. Written only
        // when there is a hole, so an unblemished release gains no key.
        if (st.missingSegments > 0 && st.missing.count(true) > 0)
            out << YAML::Key << "missing" << YAML::Value << toStd(bitsToBase64(st.missing));

        // Optional, written only when true: absent means "downloaded", which is
        // what every older sidecar meant.
        if (st.skipped)
            out << YAML::Key << "skipped" << YAML::Value << true;
        if (st.neededForRepair)
            out << YAML::Key << "neededForRepair" << YAML::Value << true;

        // Byte ranges on disk, "start-end" per entry, half-open. Separate from
        // `done` because the two answer different questions: a done bit means
        // *resolved*, and a segment missing everywhere sets it having written
        // nothing. Without this a restart could not tell which bytes of a
        // half-finished file are readable, so preview would be dead until the
        // file completed.
        //
        // Same reasoning as `written`, and the same optional-key treatment: it
        // re-derives from the first article after a restart, so an old sidecar
        // costs at most one article's delay before a seek works.
        if (st.partLength > 0)
            out << YAML::Key << "partLength" << YAML::Value << (long long)st.partLength;

        // Written in plan order, so this is one or two entries in practice.
        if (!st.written.isEmpty()) {
            out << YAML::Key << "written" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (const auto& r : st.written) {
                out << toStd(QStringLiteral("%1-%2").arg(r.first).arg(r.second));
            }
            out << YAML::EndSeq;
        }

        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    return writeSidecarAtomically(statePath(item.id), out.c_str());
}

bool writeSidecarAtomically(const QString& path, const char* text)
{
    const QString tempPath = path + QStringLiteral(".backup");
    const QString bakPath = path + QStringLiteral(".bak");

    {
        std::ofstream file(tempPath.toStdString(), std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            logError(QStringLiteral("Usenet: cannot write %1").arg(tempPath));
            return false;
        }
        file << text;
        if (!file.good()) {
            logError(QStringLiteral("Usenet: write failed for %1").arg(tempPath));
            return false;
        }
    }

    QFile::remove(bakPath);
    if (QFile::exists(path)) {
        if (!QFile::rename(path, bakPath))
            QFile::remove(path);
    }
    if (!QFile::rename(tempPath, path)) {
        logError(QStringLiteral("Usenet: rename failed %1 -> %2").arg(tempPath, path));
        if (QFile::exists(bakPath))
            QFile::rename(bakPath, path);
        return false;
    }

    return true;
}

bool UsenetQueueStore::load(const QString& path, UsenetQueueItem& out, QString& error)
{
    try {
        YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root || !root.IsMap()) {
            error = QStringLiteral("not a YAML map");
            return false;
        }

        const int version = root["version"] ? root["version"].as<int>(0) : 0;
        if (version > kStateVersion) {
            // Refuse rather than misparse: a newer daemon may have added fields
            // whose absence here would silently reset progress.
            error = QStringLiteral("state version %1 is newer than %2")
                        .arg(version)
                        .arg(kStateVersion);
            return false;
        }

        out.id = fromStd(root, "id");
        if (out.id.isEmpty()) {
            error = QStringLiteral("missing id");
            return false;
        }
        out.name = fromStd(root, "name");
        out.status = static_cast<UsenetItemStatus>(root["status"] ? root["status"].as<int>(0) : 0);
        out.priority = root["priority"] ? root["priority"].as<int>(0) : 0;
        out.category = root["category"] ? root["category"].as<int>(0) : 0;
        // Absent in a sidecar written before the health check existed, and
        // absent is harmless: -1 means "not assessed", which is what an item
        // that was never probed should say.
        out.healthPercent = root["healthPercent"] ? root["healthPercent"].as<int>(-1) : -1;
        out.healthMissingBytes =
            root["healthMissingBytes"] ? root["healthMissingBytes"].as<long long>(0) : 0;
        out.healthRecoveryBytes =
            root["healthRecoveryBytes"] ? root["healthRecoveryBytes"].as<long long>(0) : 0;
        out.healthProbed = root["healthProbed"] ? root["healthProbed"].as<bool>(false) : false;
        out.error = fromStd(root, "error");
        out.nzb.name = out.name;
        out.nzb.password = fromStd(root, "password");
        out.passwordRequired =
            root["passwordRequired"] ? root["passwordRequired"].as<bool>(false) : false;
        out.failedLadder = fromStd(root, "failedLadder");
        const int stopReason = root["stopReason"] ? root["stopReason"].as<int>(0) : 0;
        if (stopReason == int(UsenetStopReason::Unrepairable)
            || stopReason == int(UsenetStopReason::Unwanted)) {
            out.stopReason = static_cast<UsenetStopReason>(stopReason);
            out.stopDetail = fromStd(root, "stopDetail");
            if (out.status == UsenetItemStatus::Paused)
                out.stalledReason = out.stopDetail;
        }
        out.checksOverridden =
            root["checksOverridden"] ? root["checksOverridden"].as<int>(0) : 0;

        out.nzb.files.clear();
        out.files.clear();

        if (root["files"] && root["files"].IsSequence()) {
            for (const auto& fnode : root["files"]) {
                NzbFileInfo info;
                info.subject = fromStd(fnode, "subject");
                info.poster = fromStd(fnode, "poster");
                info.date = fnode["date"] ? fnode["date"].as<long long>(0) : 0;
                info.fileName = fromStd(fnode, "fileName");
                info.partsTotal = fnode["partsTotal"] ? fnode["partsTotal"].as<int>(0) : 0;

                if (fnode["groups"] && fnode["groups"].IsSequence()) {
                    for (const auto& g : fnode["groups"])
                        info.groups << QString::fromStdString(g.as<std::string>(std::string{}));
                }
                if (fnode["segments"] && fnode["segments"].IsSequence()) {
                    for (const auto& s : fnode["segments"]) {
                        NzbSegment seg;
                        seg.messageId = fromStd(s, "id");
                        seg.bytes = s["bytes"] ? s["bytes"].as<long long>(0) : 0;
                        seg.number = s["number"] ? s["number"].as<int>(0) : 0;
                        info.segments.append(seg);
                    }
                }

                UsenetFileState st;
                st.tempPath = fromStd(fnode, "tempPath");
                st.finalPath = fromStd(fnode, "finalPath");
                st.articleFileName = fromStd(fnode, "articleFileName");
                st.par2FileName = fromStd(fnode, "par2FileName");
                st.declaredSize = fnode["declaredSize"] ? fnode["declaredSize"].as<long long>(0) : 0;
                st.decodedBytes = fnode["decodedBytes"] ? fnode["decodedBytes"].as<long long>(0) : 0;
                st.finalized = fnode["finalized"] ? fnode["finalized"].as<bool>(false) : false;
                st.missingSegments = fnode["missingSegments"]
                                         ? fnode["missingSegments"].as<int>(0) : 0;
                st.done = bitsFromBase64(fromStd(fnode, "done"), int(info.segments.size()));
                st.missing = bitsFromBase64(fromStd(fnode, "missing"),
                                            int(info.segments.size()));
                // Read as well as written: remapCategories() round-trips sidecars
                // through load() and save(), and would drop a key load() ignores.
                st.skipped = fnode["skipped"] ? fnode["skipped"].as<bool>(false) : false;
                st.neededForRepair =
                    fnode["neededForRepair"] ? fnode["neededForRepair"].as<bool>(false) : false;

                // Absent in a sidecar written before streaming existed, and
                // absent is harmless: the file simply offers no preview until it
                // finishes. That is why kStateVersion did not move for this —
                // load() refuses anything newer than it knows, so a bump would
                // make an older daemon drop the whole queue rather than lose one
                // optional field.
                st.partLength = fnode["partLength"] ? fnode["partLength"].as<long long>(0) : 0;

                if (const auto written = fnode["written"]; written && written.IsSequence()) {
                    for (const auto& entry : written) {
                        const QString text =
                            QString::fromStdString(entry.as<std::string>(std::string{}));
                        const qsizetype dash = text.indexOf(QLatin1Char('-'));
                        if (dash <= 0)
                            continue;
                        bool okStart = false;
                        bool okEnd = false;
                        const qint64 start = text.left(dash).toLongLong(&okStart);
                        const qint64 end = text.mid(dash + 1).toLongLong(&okEnd);
                        if (okStart && okEnd && end > start)
                            st.addWritten(start, end - start);
                    }
                }

                out.nzb.files.append(info);
                out.files.append(st);
            }
        }

        // Absent in a version-2 sidecar. An empty list is the honest answer
        // there: the release completed, but nothing recorded what it published.
        if (const auto pub = root["publishedPaths"]; pub && pub.IsSequence()) {
            for (const auto& entry : pub) {
                const QString published =
                    QString::fromStdString(entry.as<std::string>(std::string{}));
                if (!published.isEmpty())
                    out.publishedPaths.append(published);
            }
        }

        if (const auto requested = root["requestedPar2"]; requested && requested.IsSequence()) {
            for (const auto& entry : requested)
                out.requestedPar2.insert(entry.as<int>(-1));
            out.requestedPar2.remove(-1);
        }

        // Neither Downloading nor any post-processing state can be resumed
        // *into* — nothing is in flight and no pipeline job survives a restart.
        // Demote them all to Queued: the scheduler finds nothing left to fetch,
        // the tick notices, and post-processing starts again from the top. Every
        // stage is idempotent, and re-verifying costs far less than reasoning
        // about a repair that was halfway through.
        //
        // Checking joins them, for a slightly different reason: no probe survives
        // a restart either, and re-probing on every start would spend round trips
        // re-learning something advisory. The stored healthPercent is kept.
        if (out.status == UsenetItemStatus::Downloading
            || out.status == UsenetItemStatus::Checking || out.isPostProcessing()) {
            out.status = UsenetItemStatus::Queued;
        }

        return true;
    } catch (const YAML::Exception& e) {
        error = QString::fromStdString(e.what());
        return false;
    }
}

void UsenetQueueStore::remove(const QString& itemId)
{
    const QString path = statePath(itemId);
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
    QFile::remove(path + QStringLiteral(".backup"));
}

QStringList UsenetQueueStore::listStateFiles()
{
    QDir dir(stateDir());
    QStringList out;
    const auto entries = dir.entryInfoList({QStringLiteral("*.nzbstate")},
                                           QDir::Files, QDir::Name);
    out.reserve(entries.size());
    for (const QFileInfo& fi : entries)
        out << fi.absoluteFilePath();
    return out;
}

int UsenetQueueStore::remapCategories(const QHash<uint32, uint32>& oldToNew)
{
    int changed = 0;

    for (const QString& path : listStateFiles()) {
        UsenetQueueItem item;
        QString error;
        if (!load(path, item, error)) {
            logWarning(QStringLiteral("Usenet: cannot renumber categories in %1: %2")
                           .arg(path, error));
            continue;
        }

        const int mapped = remapCategoryIndex(item.category, oldToNew);
        if (mapped == item.category)
            continue;

        // Only a sidecar whose category actually moves is rewritten. load()
        // demotes a resumable status on the way in, and writing that back for
        // every item would persist a change nobody asked for.
        item.category = mapped;
        if (save(item))
            ++changed;
    }

    return changed;
}

} // namespace eMule::usenet
