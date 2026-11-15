#include "pch.h"
/// @file KadIncomingLog.cpp
/// @brief Recent incoming Kad requests.

#include "kademlia/KadIncomingLog.h"

#include "utils/TimeUtils.h"

#include <QMutexLocker>

#include <algorithm>

namespace eMule::kad {

void IncomingLog::add(IncomingRequest request)
{
    const QMutexLocker lock(&m_mutex);
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
}

IncomingNote::~IncomingNote()
{
    m_log.add(std::move(m_request));
}

void IncomingNote::setTarget(const UInt128& target)
{
    m_request.target = target;
    m_request.hasTarget = true;
}

} // namespace eMule::kad
