#include "pch.h"
/// @file MetaAccountStore.cpp
/// @brief Meta API session tokens, per server origin (daemon-side).

#include "enodemeta/MetaAccountStore.h"

#include "prefs/Preferences.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <yaml-cpp/yaml.h>

namespace eMule::enodemeta {

MetaAccountStore::MetaAccountStore(QString path)
    : m_path(path.isEmpty() ? QDir(thePrefs.configDir()).filePath(QStringLiteral("metaaccounts.yml"))
                            : std::move(path))
{
    load();
}

MetaAccount MetaAccountStore::account(const QString& origin) const
{
    MetaAccount acc = m_accounts.value(origin);
    if (acc.tokenExpiresAt > 0 && acc.tokenExpiresAt <= QDateTime::currentSecsSinceEpoch())
        acc.token.clear();
    return acc;
}

void MetaAccountStore::setAccount(const QString& origin, const MetaAccount& account)
{
    m_accounts.insert(origin, account);
    save();
}

void MetaAccountStore::clearToken(const QString& origin)
{
    auto it = m_accounts.find(origin);
    if (it == m_accounts.end() || it->token.isEmpty())
        return;
    it->token.clear();
    it->tokenExpiresAt = 0;
    save();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaAccountStore::load()
{
    m_accounts.clear();
    const auto raw = BackgroundFileWriter::readFile(m_path);   // a queued save is newer
    if (!raw)
        return;
    try {
        const YAML::Node root = YAML::Load(raw->toStdString());
        const YAML::Node servers = root["servers"];
        if (!servers || !servers.IsMap())
            return;
        for (const auto& kv : servers) {
            const QString origin = QString::fromStdString(kv.first.as<std::string>());
            const YAML::Node& n = kv.second;
            MetaAccount acc;
            if (n["username"])
                acc.username = QString::fromStdString(n["username"].as<std::string>());
            if (n["token"])
                acc.token = QString::fromStdString(n["token"].as<std::string>());
            if (n["tokenExpiresAt"])
                acc.tokenExpiresAt = n["tokenExpiresAt"].as<qint64>();
            m_accounts.insert(origin, acc);
        }
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Meta API: cannot read %1: %2").arg(m_path, QString::fromUtf8(ex.what())));
    }
}

void MetaAccountStore::save() const
{
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "servers" << YAML::Value << YAML::BeginMap;
    for (auto it = m_accounts.cbegin(); it != m_accounts.cend(); ++it) {
        out << YAML::Key << it.key().toStdString() << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "username" << YAML::Value << it->username.toStdString();
        out << YAML::Key << "token" << YAML::Value << it->token.toStdString();
        out << YAML::Key << "tokenExpiresAt" << YAML::Value << it->tokenExpiresAt;
        out << YAML::EndMap;
    }
    out << YAML::EndMap << YAML::EndMap;

    // off the main thread (report 43); the writer logs a failed write
    BackgroundFileWriter::instance().write(m_path, QByteArray(out.c_str()), [](const QString& path, const QByteArray& data) {
        constexpr auto kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;   // holds tokens
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.setPermissions(kOwnerOnly);
        if (file.write(data) != data.size() || !file.commit())
            return false;
        QFile::setPermissions(path, kOwnerOnly);
        return true;
    });
}

} // namespace eMule::enodemeta
