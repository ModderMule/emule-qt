#include "pch.h"
/// @file IndexerFeedStore.cpp
/// @brief What a feed has already seen — implementation.

#include "IndexerFeedStore.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>

namespace eMule::indexer {

QString feedSeenStateToString(FeedSeenState state)
{
    switch (state) {
    case FeedSeenState::Added:   return QStringLiteral("added");
    case FeedSeenState::Have:    return QStringLiteral("have");
    case FeedSeenState::Failed:  return QStringLiteral("failed");
    case FeedSeenState::Pending: return QStringLiteral("pending");
    case FeedSeenState::Seeded:  break;
    }
    return QStringLiteral("seeded");
}

FeedSeenState feedSeenStateFromString(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("added"))
        return FeedSeenState::Added;
    if (t == QLatin1String("have"))
        return FeedSeenState::Have;
    if (t == QLatin1String("failed"))
        return FeedSeenState::Failed;
    if (t == QLatin1String("pending"))
        return FeedSeenState::Pending;

    // Anything unrecognised reads as seeded, which is the state that makes the
    // feed leave the row alone. An unknown marker from a newer build must not
    // become a reason to download something twice.
    return FeedSeenState::Seeded;
}

QString FeedState::urlSeedKey()
{
    // A leading colon cannot appear in an account name the user typed, and
    // IndexerConfig::key() case-folds rather than stripping punctuation, so this
    // can never collide with a real one.
    return QStringLiteral(":url");
}

void FeedState::recordSeen(const FeedSeenEntry& entry)
{
    for (auto& existing : seen) {
        if (existing.guid == entry.guid) {
            existing = entry;
            return;
        }
    }
    seen.append(entry);
}

const FeedSeenEntry* FeedState::find(const QString& guid) const
{
    for (const auto& entry : seen)
        if (entry.guid == guid)
            return &entry;
    return nullptr;
}

bool FeedState::hasSeeded(const QString& account) const
{
    return seededAccounts.contains(account, Qt::CaseInsensitive);
}

void FeedState::evict(int cap)
{
    if (cap <= 0 || seen.size() <= cap)
        return;

    // Oldest first. A missing timestamp sorts oldest, so a hand-edited entry is
    // the first to go rather than immortal.
    std::stable_sort(seen.begin(), seen.end(),
                     [](const FeedSeenEntry& a, const FeedSeenEntry& b) {
                         if (a.at.isValid() != b.at.isValid())
                             return !a.at.isValid();
                         return a.at < b.at;
                     });
    seen.erase(seen.begin(), seen.begin() + (seen.size() - cap));
}

QString IndexerFeedStore::directory()
{
    const QString dir = QDir(thePrefs.configDir()).filePath(QStringLiteral("Feeds"));
    QDir().mkpath(dir);
    return dir;
}

QString IndexerFeedStore::pathFor(const IndexerFeed& feed)
{
    return QDir(directory()).filePath(feed.slug() + QStringLiteral(".yml"));
}

bool IndexerFeedStore::load(const IndexerFeed& feed, FeedState& out)
{
    const QString path = pathFor(feed);
    if (!QFile::exists(path))
        return false;

    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root.IsMap())
            return false;

        FeedState state;
        state.name = QString::fromStdString(root["name"].as<std::string>(""));
        state.lastError = QString::fromStdString(root["lastError"].as<std::string>(""));
        state.lastMatched = root["lastMatched"].as<int>(0);
        if (const auto polled = root["lastPolled"]; polled) {
            state.lastPolled =
                QDateTime::fromSecsSinceEpoch(polled.as<qint64>(0), Qt::UTC);
        }

        if (const auto accounts = root["seededAccounts"]; accounts && accounts.IsSequence()) {
            for (const auto& node : accounts)
                state.seededAccounts.append(QString::fromStdString(node.as<std::string>("")));
        }

        if (const auto entries = root["seen"]; entries && entries.IsSequence()) {
            for (const auto& node : entries) {
                if (!node.IsMap())
                    continue;
                FeedSeenEntry entry;
                entry.guid = QString::fromStdString(node["guid"].as<std::string>(""));
                if (entry.guid.isEmpty())
                    continue;
                entry.at = QDateTime::fromSecsSinceEpoch(node["at"].as<qint64>(0), Qt::UTC);
                entry.state = feedSeenStateFromString(
                    QString::fromStdString(node["state"].as<std::string>("")));
                entry.attempts = node["attempts"].as<int>(0);
                entry.title = QString::fromStdString(node["title"].as<std::string>(""));
                state.seen.append(entry);
            }
        }

        out = state;
        return true;
    } catch (const YAML::Exception& ex) {
        // Deliberately not fatal, and deliberately not treated as "no history":
        // the caller gets false and seeds, which costs one skipped poll rather
        // than a queue full of a year's backlog.
        logWarning(QStringLiteral("Feeds: could not read the history for %1: %2")
                       .arg(feed.name, QString::fromStdString(ex.what())));
        return false;
    }
}

bool IndexerFeedStore::save(const IndexerFeed& feed, const FeedState& state)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << feed.name.toStdString();
    out << YAML::Key << "lastPolled" << YAML::Value
        << (state.lastPolled.isValid() ? state.lastPolled.toSecsSinceEpoch() : qint64(0));
    out << YAML::Key << "lastError" << YAML::Value << state.lastError.toStdString();
    out << YAML::Key << "lastMatched" << YAML::Value << state.lastMatched;

    out << YAML::Key << "seededAccounts" << YAML::Value << YAML::BeginSeq;
    for (const auto& account : state.seededAccounts)
        out << account.toStdString();
    out << YAML::EndSeq;

    out << YAML::Key << "seen" << YAML::Value << YAML::BeginSeq;
    for (const auto& entry : state.seen) {
        out << YAML::BeginMap;
        out << YAML::Key << "guid" << YAML::Value << entry.guid.toStdString();
        out << YAML::Key << "at" << YAML::Value
            << (entry.at.isValid() ? entry.at.toSecsSinceEpoch() : qint64(0));
        out << YAML::Key << "state" << YAML::Value
            << feedSeenStateToString(entry.state).toStdString();
        if (entry.attempts > 0)
            out << YAML::Key << "attempts" << YAML::Value << entry.attempts;
        // Only for rows a person might want to read back. On a busy feed the
        // seeded entries outnumber the rest many times over.
        if (!entry.title.isEmpty() && entry.state != FeedSeenState::Seeded)
            out << YAML::Key << "title" << YAML::Value << entry.title.toStdString();
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    const QString path = pathFor(feed);
    std::ofstream file(path.toStdString());
    if (!file) {
        logWarning(QStringLiteral("Feeds: could not write %1").arg(path));
        return false;
    }
    file << out.c_str();
    return file.good();
}

void IndexerFeedStore::remove(const IndexerFeed& feed)
{
    QFile::remove(pathFor(feed));
}

void IndexerFeedStore::removeOrphans(const QList<IndexerFeed>& feeds)
{
    QSet<QString> keep;
    for (const auto& feed : feeds)
        keep.insert(QFileInfo(pathFor(feed)).fileName());

    QDir dir(directory());
    const auto files = dir.entryList({QStringLiteral("*.yml")}, QDir::Files);
    for (const QString& name : files) {
        if (!keep.contains(name))
            QFile::remove(dir.filePath(name));
    }
}

} // namespace eMule::indexer
