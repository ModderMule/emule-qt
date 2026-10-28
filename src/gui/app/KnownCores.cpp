#include "pch.h"

/// @file KnownCores.cpp
/// @brief Pinned TLS fingerprints of known cores — implementation.

#include "app/KnownCores.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace eMule {

KnownCores::KnownCores(QString path)
    : m_path(std::move(path))
{
    load();
}

KnownCores& KnownCores::instance()
{
    static KnownCores store(thePrefs.configDir() + QStringLiteral("/ipc-known-cores.json"));
    return store;
}

KnownCores::Check KnownCores::check(const QString& hostPort, const QString& fingerprint) const
{
    const QString have = pinned(hostPort);
    if (have.isEmpty())
        return Check::New;
    return have.compare(fingerprint, Qt::CaseInsensitive) == 0 ? Check::Match : Check::Mismatch;
}

QString KnownCores::pinned(const QString& hostPort) const
{
    const auto it = std::ranges::find(m_entries, hostPort, &Entry::hostPort);
    return it != m_entries.end() ? it->fingerprint : QString();
}

void KnownCores::pin(const QString& hostPort, const QString& fingerprint)
{
    auto it = std::ranges::find(m_entries, hostPort, &Entry::hostPort);
    if (it == m_entries.end())
        m_entries.push_back({hostPort, fingerprint, QDateTime::currentDateTimeUtc()});
    else
        *it = {hostPort, fingerprint, QDateTime::currentDateTimeUtc()};
    save();
}

void KnownCores::remove(const QString& hostPort)
{
    if (std::erase_if(m_entries, [&hostPort](const Entry& e) { return e.hostPort == hostPort; }) > 0)
        save();
}

QString KnownCores::key(const QString& host, quint16 port)
{
    const bool v6 = host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['));
    return v6 ? QStringLiteral("[%1]:%2").arg(host).arg(port)
              : QStringLiteral("%1:%2").arg(host).arg(port);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void KnownCores::load()
{
    m_entries.clear();
    if (m_path.isEmpty())
        return;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray rows = QJsonDocument::fromJson(f.readAll()).object()
                                .value(QStringLiteral("cores")).toArray();
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        Entry e{o.value(QStringLiteral("host")).toString(),
                o.value(QStringLiteral("sha256")).toString(),
                QDateTime::fromString(o.value(QStringLiteral("firstSeen")).toString(), Qt::ISODate)};
        if (!e.hostPort.isEmpty() && !e.fingerprint.isEmpty())
            m_entries.push_back(std::move(e));
    }
}

void KnownCores::save() const
{
    if (m_path.isEmpty())
        return;
    QJsonArray rows;
    for (const auto& e : m_entries) {
        rows.append(QJsonObject{{QStringLiteral("host"), e.hostPort},
                                {QStringLiteral("sha256"), e.fingerprint},
                                {QStringLiteral("firstSeen"), e.firstSeen.toString(Qt::ISODate)}});
    }
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        logWarning(QStringLiteral("Cannot write %1").arg(m_path));
        return;
    }
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("cores"), rows}}).toJson());
    if (!f.commit())
        logWarning(QStringLiteral("Cannot write %1").arg(m_path));
}

} // namespace eMule
