#include "pch.h"
/// @file ArrWanted.cpp
/// @brief Sonarr/Radarr Wanted list poller — implementation.

#include "webserver/ArrWanted.h"

#include "prefs/Preferences.h"
#include "webserver/ArrSearchBridge.h"
#include "utils/Log.h"

#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace eMule {

namespace {

constexpr int kRequestTimeoutMs = 30 * 1000;

/// http://host:8989/ + api/v3/… ; a URL base path (http://host/sonarr) is kept
[[nodiscard]] QUrl apiUrl(const QString& base, const QString& path, const QUrlQuery& query)
{
    QString root = base.trimmed();
    if (!root.contains(QLatin1StringView("://")))
        root.prepend(QStringLiteral("http://"));
    while (root.endsWith(u'/'))
        root.chop(1);
    QUrl url(root + path);
    url.setQuery(query);
    return url;
}

} // namespace

ArrWanted::ArrWanted(ArrSearchBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
{
    // Sonarr/Radarr sit on the LAN: never through the P2P proxy
    m_nam.setProxy(QNetworkProxy::NoProxy);
    connect(&m_timer, &QTimer::timeout, this, &ArrWanted::poll);
}

void ArrWanted::applyPreferences()
{
    const int minutes = thePrefs.arrWantedPollMinutes();
    const bool anyInstance = std::ranges::any_of(thePrefs.arrInstances(),
                                                 [](const ArrInstance& a) { return a.enabled; });
    const bool on = thePrefs.arrApiEnabled() && minutes > 0 && anyInstance;
    m_bridge->setBackgroundEnabled(on);
    if (!on) {
        m_timer.stop();
        m_on = false;
        return;
    }
    const int interval = minutes * 60 * 1000;
    if (m_timer.interval() != interval || !m_timer.isActive())
        m_timer.start(interval);
    const QList<ArrInstance> instances = thePrefs.arrInstances();
    if (!m_on || instances != m_instances) {
        // first poll a little after start-up (servers and Kad come up first) or an edit
        QTimer::singleShot(m_on ? 2 * 1000 : 30 * 1000, this, &ArrWanted::poll);
        m_on = true;
    }
    m_instances = instances;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void ArrWanted::poll()
{
    if (!m_on || m_pending > 0)
        return;
    m_round.clear();
    for (const ArrInstance& inst : thePrefs.arrInstances()) {
        if (!inst.enabled || inst.url.isEmpty())
            continue;
        QUrlQuery q;
        QString path;
        if (inst.isRadarr()) {
            // /movie: every Radarr v3+; filtered to monitored, missing, available
            path = QStringLiteral("/api/v3/movie");
        } else {
            path = QStringLiteral("/api/v3/wanted/missing");
            q.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
            q.addQueryItem(QStringLiteral("pageSize"), QString::number(kItemsPerInstance));
            q.addQueryItem(QStringLiteral("sortKey"), QStringLiteral("airDateUtc"));
            q.addQueryItem(QStringLiteral("sortDirection"), QStringLiteral("descending"));
            q.addQueryItem(QStringLiteral("includeSeries"), QStringLiteral("true"));
            q.addQueryItem(QStringLiteral("monitored"), QStringLiteral("true"));
        }
        QNetworkRequest req(apiUrl(inst.url, path, q));
        req.setRawHeader("X-Api-Key", inst.apiKey.toUtf8());
        req.setRawHeader("Accept", "application/json");
        req.setTransferTimeout(kRequestTimeoutMs);
        QNetworkReply* reply = m_nam.get(req);
        ++m_pending;
        connect(reply, &QNetworkReply::finished, this, [this, reply, inst] {
            reply->deleteLater();
            --m_pending;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                logWarning(QStringLiteral("Arr: %1 Wanted list failed: %2")
                               .arg(inst.name, status == 401 ? QStringLiteral("wrong API key")
                                                             : reply->errorString()));
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                collect(inst.isRadarr() ? arr::parseRadarrMovies(doc, inst.name, kItemsPerInstance)
                                        : arr::parseSonarrMissing(doc, inst.name));
            }
            if (m_pending == 0) {
                logDebug(QStringLiteral("Arr: %1 wanted titles from the Arr instances").arg(m_round.size()));
                m_bridge->setWanted(m_round);
            }
        });
    }
}

void ArrWanted::collect(const QList<arr::WantedTerm>& terms)
{
    for (const arr::WantedTerm& t : terms) {
        const bool known = std::ranges::any_of(m_round, [&t](const arr::WantedTerm& have) {
            return have.query.compare(t.query, Qt::CaseInsensitive) == 0;
        });
        if (!known)
            m_round.append(t);
    }
}

} // namespace eMule
