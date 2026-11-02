#include "kademlia/KadNetworkWatch.h"

#include "kademlia/KadDefines.h"
#include "utils/OtherFunctions.h"

#include <QtEndian>

namespace eMule::kad {

void NetworkWatch::reset(time_t now)
{
    m_lastReceived = now;
    m_askedSites.clear();
    m_offline = false;
}

NetworkWatch::Event NetworkWatch::packetReceived(uint32 ip, time_t now)
{
    if (!counts(ip))
        return Event::None;
    m_lastReceived = now;
    m_askedSites.clear();
    m_evidence = true;
    if (!m_offline)
        return Event::None;
    m_offline = false;
    return Event::BackOnline;
}

void NetworkWatch::requestSent(uint32 ip, time_t now)
{
    if (m_offline || !counts(ip))
        return;
    // only the first ask per site matters; cap so a long silence can't grow it
    if (m_askedSites.size() < 64)
        m_askedSites.try_emplace(siteOf(ip), now);
}

NetworkWatch::Event NetworkWatch::update(time_t now)
{
    if (m_offline || now - m_lastReceived < static_cast<time_t>(kOfflineSilence))
        return Event::None;
    if (failedSites(now) < static_cast<int>(kOfflineSites))
        return Event::None;
    m_offline = true;
    m_askedSites.clear();
    return Event::WentOffline;
}

int NetworkWatch::failedSites(time_t now) const
{
    int n = 0;
    for (const auto& [site, sent] : m_askedSites) {
        if (now - sent >= static_cast<time_t>(kOfflineReplyWait))
            ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool NetworkWatch::counts(uint32 ip) const
{
    const uint32 a = ip >> 24;
    if (ip == 0 || a == 0 || a >= 224)
        return false;
    return m_lanMode || !isLanIP(qToBigEndian(ip));
}

uint32 NetworkWatch::siteOf(uint32 ip) const
{
    // /24 as ed3k sites; LAN peers each their own
    return m_lanMode && isLanIP(qToBigEndian(ip)) ? ip : (ip & 0xFFFFFF00u);
}

} // namespace eMule::kad
