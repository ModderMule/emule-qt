#include "UsenetSession.h"

#include "nntp/NntpServerPool.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

namespace eMule::usenet {

UsenetSession::UsenetSession(QObject* parent)
    : QObject(parent)
    , m_pool(std::make_unique<NntpServerPool>())
{
    applyPreferences();
}

UsenetSession::~UsenetSession()
{
    stop();
}

void UsenetSession::start()
{
    if (m_running)
        return;

    m_running = true;
    applyPreferences();

    const int configured = m_pool->servers().size();
    logInfo(QStringLiteral("Usenet: engine started, %1 server(s) configured")
                .arg(configured));
    if (configured == 0) {
        logInfo(QStringLiteral("Usenet: no news servers configured — "
                               "add one under Options > Usenet"));
    }
}

void UsenetSession::stop()
{
    if (!m_running)
        return;

    m_running = false;
    m_pool->closeIdleConnections();
    logInfo(QStringLiteral("Usenet: engine stopped"));
}

void UsenetSession::applyPreferences()
{
    m_pool->setRetryInterval(thePrefs.usenetRetryIntervalSeconds());
    m_pool->setServers(thePrefs.usenetServers());
}

} // namespace eMule::usenet
