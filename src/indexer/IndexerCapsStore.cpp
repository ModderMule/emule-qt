#include "IndexerCapsStore.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>

#include <yaml-cpp/yaml.h>

#include <fstream>

namespace eMule::indexer {

namespace {

void emitCategories(YAML::Emitter& out, const QList<IndexerCategory>& categories)
{
    out << YAML::Key << "categories" << YAML::Value << YAML::BeginSeq;
    for (const auto& cat : categories) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << cat.id;
        out << YAML::Key << "name" << YAML::Value << cat.name.toStdString();
        if (!cat.subcategories.isEmpty()) {
            out << YAML::Key << "subcats" << YAML::Value << YAML::BeginSeq;
            for (const auto& sub : cat.subcategories) {
                out << YAML::BeginMap;
                out << YAML::Key << "id" << YAML::Value << sub.id;
                out << YAML::Key << "name" << YAML::Value << sub.name.toStdString();
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
}

} // namespace

QString IndexerCapsStore::directory()
{
    const QString dir = QDir(thePrefs.configDir()).filePath(QStringLiteral("Indexers"));
    QDir().mkpath(dir);
    return dir;
}

QString IndexerCapsStore::pathFor(const IndexerConfig& config)
{
    return QDir(directory()).filePath(config.slug() + QStringLiteral(".caps.yml"));
}

bool IndexerCapsStore::load(const IndexerConfig& config, IndexerCaps& out)
{
    const QString path = pathFor(config);
    if (!QFile::exists(path))
        return false;

    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root.IsMap())
            return false;

        IndexerCaps caps;
        caps.serverTitle = QString::fromStdString(root["serverTitle"].as<std::string>(""));
        caps.limitMax = root["limitMax"].as<int>(100);
        caps.limitDefault = root["limitDefault"].as<int>(100);

        if (const auto probed = root["probedAt"]; probed) {
            caps.probedAt = QDateTime::fromSecsSinceEpoch(probed.as<qint64>(0), Qt::UTC);
        }

        if (const auto modes = root["modes"]; modes && modes.IsSequence()) {
            for (const auto& node : modes) {
                if (!node.IsMap())
                    continue;
                IndexerSearchMode mode;
                mode.available = node["available"].as<bool>(false);
                if (const auto params = node["params"]; params && params.IsSequence()) {
                    for (const auto& p : params) {
                        mode.supportedParams.append(
                            QString::fromStdString(p.as<std::string>("")));
                    }
                }
                caps.modes.insert(QString::fromStdString(node["name"].as<std::string>("")),
                                  mode);
            }
        }

        if (const auto cats = root["categories"]; cats && cats.IsSequence()) {
            for (const auto& node : cats) {
                if (!node.IsMap())
                    continue;
                IndexerCategory cat;
                cat.id = node["id"].as<int>(0);
                cat.name = QString::fromStdString(node["name"].as<std::string>(""));
                if (const auto subs = node["subcats"]; subs && subs.IsSequence()) {
                    for (const auto& subNode : subs) {
                        IndexerCategory sub;
                        sub.id = subNode["id"].as<int>(0);
                        sub.name = QString::fromStdString(subNode["name"].as<std::string>(""));
                        cat.subcategories.append(sub);
                    }
                }
                caps.categories.append(cat);
            }
        }

        out = caps;
        return true;
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Indexers: could not read the cached capabilities for %1: %2")
                       .arg(config.displayName(), QString::fromStdString(ex.what())));
        return false;
    }
}

bool IndexerCapsStore::save(const IndexerConfig& config, const IndexerCaps& caps)
{
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "serverTitle" << YAML::Value << caps.serverTitle.toStdString();
    out << YAML::Key << "limitMax" << YAML::Value << caps.limitMax;
    out << YAML::Key << "limitDefault" << YAML::Value << caps.limitDefault;
    out << YAML::Key << "probedAt" << YAML::Value
        << (caps.probedAt.isValid() ? caps.probedAt.toSecsSinceEpoch() : qint64(0));

    out << YAML::Key << "modes" << YAML::Value << YAML::BeginSeq;
    for (auto it = caps.modes.constBegin(); it != caps.modes.constEnd(); ++it) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << it.key().toStdString();
        out << YAML::Key << "available" << YAML::Value << it->available;
        out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
        for (const auto& param : it->supportedParams)
            out << param.toStdString();
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    emitCategories(out, caps.categories);
    out << YAML::EndMap;

    const QString path = pathFor(config);
    std::ofstream file(path.toStdString());
    if (!file) {
        logWarning(QStringLiteral("Indexers: could not write %1").arg(path));
        return false;
    }
    file << out.c_str();
    return file.good();
}

void IndexerCapsStore::remove(const IndexerConfig& config)
{
    QFile::remove(pathFor(config));
}

bool IndexerCapsStore::isStale(const IndexerCaps& caps, int refreshDays)
{
    if (!caps.probedAt.isValid())
        return true;
    return caps.probedAt.daysTo(QDateTime::currentDateTimeUtc()) >= qint64(refreshDays);
}

} // namespace eMule::indexer
