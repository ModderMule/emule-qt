#include "queue/UsenetHistory.h"

#include "prefs/Preferences.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <vector>

namespace eMule::usenet {

namespace {

constexpr int kHistoryVersion = 1;

[[nodiscard]] std::string toStd(const QString& s)
{
    return s.toStdString();
}

} // namespace

QString usenetHistoryStateToString(UsenetHistoryState state)
{
    switch (state) {
    case UsenetHistoryState::Cancelled:  return QStringLiteral("cancelled");
    case UsenetHistoryState::Downloaded: break;
    }
    return QStringLiteral("downloaded");
}

UsenetHistoryState usenetHistoryStateFromString(const QString& text)
{
    if (text.trimmed().toLower() == QLatin1String("cancelled"))
        return UsenetHistoryState::Cancelled;

    // Anything unrecognised reads as downloaded. Either value is safe -- both
    // only ever raise a question -- and downloaded is the commoner case.
    return UsenetHistoryState::Downloaded;
}

QString UsenetHistory::historyPath()
{
    return QDir(UsenetQueueStore::stateDir()).filePath(QStringLiteral("history.yml"));
}

void UsenetHistory::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    const QString path = historyPath();
    if (!QFile::exists(path))
        return;

    try {
        YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root || !root.IsMap())
            return;

        // Only a *newer* file is refused, so a key added later needs no bump.
        if (root["version"] && root["version"].as<int>(0) > kHistoryVersion) {
            logWarning(QStringLiteral("Usenet: %1 is version %2, newer than %3 — ignoring")
                           .arg(path)
                           .arg(root["version"].as<int>(0))
                           .arg(kHistoryVersion));
            return;
        }

        const YAML::Node entries = root["entries"];
        if (!entries || !entries.IsSequence())
            return;

        for (const auto& node : entries) {
            if (!node.IsMap())
                continue;

            UsenetHistoryEntry e;
            e.digest = QString::fromStdString(node["digest"].as<std::string>("")).trimmed();
            if (e.digest.isEmpty())
                continue;

            e.name = QString::fromStdString(node["name"].as<std::string>(""));
            e.size = node["size"].as<long long>(0);
            e.when = node["when"].as<long long>(0);
            e.state = usenetHistoryStateFromString(
                QString::fromStdString(node["state"].as<std::string>("")));

            // A preference that is off means forget, so the entries it governs
            // are not read -- and the save below will not write them back.
            if (!isRemembered(e.state))
                continue;

            m_byDigest.insert(e.digest, e);
        }

        reindex();
        if (!m_byDigest.isEmpty()) {
            logInfo(QStringLiteral("Usenet: remembered %1 past download(s)")
                        .arg(m_byDigest.size()));
        }
    } catch (const std::exception& ex) {
        logWarning(QStringLiteral("Usenet: cannot read %1 (%2) — starting with no history")
                       .arg(path, QString::fromUtf8(ex.what())));
        m_byDigest.clear();
        m_digestsByFoldedName.clear();
    }
}

void UsenetHistory::record(const UsenetQueueItem& item, UsenetHistoryState state)
{
    load();

    if (!isRemembered(state))
        return;

    // findDuplicate() already refuses to act on an empty digest; one empty-digest
    // entry here would match every future release with no message-ids.
    const QString digest = item.articleDigest.isEmpty() ? nzbArticleDigest(item.nzb)
                                                        : item.articleDigest;
    if (digest.isEmpty())
        return;

    auto it = m_byDigest.find(digest);
    if (it != m_byDigest.end()) {
        // Downloaded is monotone. Finishing a release and then clearing its row
        // calls this twice, and the second call must not undo the first.
        if (it->state == UsenetHistoryState::Downloaded)
            state = UsenetHistoryState::Downloaded;
        it->state = state;
        it->when = QDateTime::currentSecsSinceEpoch();
        if (!item.name.isEmpty() && it->name != item.name) {
            it->name = item.name;
            reindex();
        }
        return;
    }

    UsenetHistoryEntry e;
    e.digest = digest;
    e.name = item.name;
    e.size = item.nzb.totalEncodedBytes();
    e.when = QDateTime::currentSecsSinceEpoch();
    e.state = state;
    m_byDigest.insert(digest, e);
    m_digestsByFoldedName.insert(usenetFoldedReleaseName(e.name), digest);

    evict();
}

const UsenetHistoryEntry* UsenetHistory::findByDigest(const QString& digest) const
{
    if (digest.isEmpty())
        return nullptr;

    const auto it = m_byDigest.constFind(digest);
    if (it == m_byDigest.constEnd())
        return nullptr;
    return isRemembered(it->state) ? &(*it) : nullptr;
}

const UsenetHistoryEntry* UsenetHistory::findByName(const QString& name) const
{
    const QString folded = usenetFoldedReleaseName(name);
    if (folded.isEmpty())
        return nullptr;

    // Several releases can fold to one name. The newest wins: it is the one the
    // sentence should talk about, and a Downloaded among them outranks a
    // Cancelled for the same reason record() is monotone.
    const UsenetHistoryEntry* best = nullptr;
    const auto digests = m_digestsByFoldedName.values(folded);
    for (const QString& digest : digests) {
        const UsenetHistoryEntry* e = findByDigest(digest);
        if (!e)
            continue;
        if (!best
            || (e->state == UsenetHistoryState::Downloaded
                && best->state != UsenetHistoryState::Downloaded)
            || (e->state == best->state && e->when > best->when))
        {
            best = e;
        }
    }
    return best;
}

int UsenetHistory::count() const
{
    return static_cast<int>(m_byDigest.size());
}

bool UsenetHistory::save()
{
    load();

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << kHistoryVersion;
    out << YAML::Key << "entries" << YAML::Value << YAML::BeginSeq;

    for (auto it = m_byDigest.constBegin(); it != m_byDigest.constEnd(); ++it) {
        const UsenetHistoryEntry& e = *it;
        if (!isRemembered(e.state))
            continue;   // the preference went off: writing it back would keep it
        out << YAML::BeginMap;
        out << YAML::Key << "digest" << YAML::Value << toStd(e.digest);
        out << YAML::Key << "name" << YAML::Value << toStd(e.name);
        out << YAML::Key << "size" << YAML::Value << static_cast<long long>(e.size);
        out << YAML::Key << "when" << YAML::Value << static_cast<long long>(e.when);
        out << YAML::Key << "state" << YAML::Value << toStd(usenetHistoryStateToString(e.state));
        out << YAML::EndMap;
    }

    out << YAML::EndSeq;
    out << YAML::EndMap;

    return writeSidecarAtomically(historyPath(), out.c_str());
}

bool UsenetHistory::clear()
{
    m_byDigest.clear();
    m_digestsByFoldedName.clear();
    m_loaded = true;
    return save();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool UsenetHistory::isRemembered(UsenetHistoryState state) const
{
    return state == UsenetHistoryState::Cancelled ? thePrefs.rememberCancelledFiles()
                                                  : thePrefs.rememberDownloadedFiles();
}

void UsenetHistory::evict()
{
    if (m_byDigest.size() <= kMaxEntries)
        return;

    std::vector<std::pair<qint64, QString>> byAge;
    byAge.reserve(static_cast<size_t>(m_byDigest.size()));
    for (auto it = m_byDigest.constBegin(); it != m_byDigest.constEnd(); ++it)
        byAge.emplace_back(it->when, it.key());

    const size_t excess = static_cast<size_t>(m_byDigest.size() - kMaxEntries);
    std::partial_sort(byAge.begin(), byAge.begin() + static_cast<qsizetype>(excess), byAge.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

    for (size_t i = 0; i < excess; ++i)
        m_byDigest.remove(byAge[i].second);

    reindex();
}

void UsenetHistory::reindex()
{
    m_digestsByFoldedName.clear();
    for (auto it = m_byDigest.constBegin(); it != m_byDigest.constEnd(); ++it)
        m_digestsByFoldedName.insert(usenetFoldedReleaseName(it->name), it.key());
}

} // namespace eMule::usenet
