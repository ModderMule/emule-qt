#include "pch.h"
/// @file KadIncomingLog.cpp
/// @brief Recent incoming Kad requests.

#include "kademlia/KadIncomingLog.h"

#include "kademlia/KadContact.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/Kademlia.h"

#include "utils/TimeUtils.h"

#include <QMutexLocker>

#include <algorithm>

namespace eMule::kad {

void IncomingLog::add(IncomingRequest request)
{
    const QMutexLocker lock(&m_mutex);
    request.id = ++m_nextId;
    const bool lookup = request.type == IncomingType::NodeLookup;
    auto& ring = lookup ? m_lookups : m_other;
    ring.push_back(std::move(request));
    if (ring.size() > (lookup ? kMaxLookups : kMaxOther))
        ring.pop_front();
}

std::vector<IncomingRequest> IncomingLog::snapshot() const
{
    const QMutexLocker lock(&m_mutex);
    std::vector<IncomingRequest> all;
    all.reserve(m_lookups.size() + m_other.size());
    all.insert(all.end(), m_lookups.begin(), m_lookups.end());
    all.insert(all.end(), m_other.begin(), m_other.end());
    std::ranges::stable_sort(all, std::ranges::greater{}, &IncomingRequest::at);
    return all;
}

size_t IncomingLog::size() const
{
    const QMutexLocker lock(&m_mutex);
    return m_lookups.size() + m_other.size();
}

std::optional<IncomingRequest> IncomingLog::find(uint64 id) const
{
    const QMutexLocker lock(&m_mutex);
    for (const auto* ring : {&m_lookups, &m_other})
        if (auto it = std::ranges::find(*ring, id, &IncomingRequest::id); it != ring->end())
            return *it;
    return std::nullopt;
}

void IncomingLog::clear()
{
    const QMutexLocker lock(&m_mutex);
    m_lookups.clear();
    m_other.clear();
}

IncomingNote::IncomingNote(IncomingLog& log, IncomingType type, uint32 ip, uint16 port)
    : m_log(log)
{
    m_request.at = getTickCount();
    m_request.type = type;
    m_request.ip = ip;
    m_request.port = port;
    m_request.own = RoutingZone::localKadId();
}

IncomingNote::~IncomingNote()
{
    // REQs carry no sender ID: the routing table may know it by address
    if (auto* rz = Kademlia::getInstanceRoutingZone(); rz && m_request.port != 0) {
        if (const Contact* c = rz->getContact(m_request.ip, m_request.port, false)) {
            m_request.asker = c->getClientID();
            m_request.askerVersion = c->getVersion();
            m_request.hasAsker = true;
        }
    }
    m_log.add(std::move(m_request));
}

void IncomingNote::setTarget(const UInt128& target)
{
    m_request.target = target;
    m_request.hasTarget = true;
}

} // namespace eMule::kad
