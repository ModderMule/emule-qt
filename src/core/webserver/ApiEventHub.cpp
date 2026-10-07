#include "pch.h"
/// @file ApiEventHub.cpp
/// @brief Event ring — implementation.

#include "webserver/ApiEventHub.h"

#include <QJsonDocument>

namespace eMule {

ApiEventHub::ApiEventHub(QObject* parent)
    : QObject(parent)
{
}

void ApiEventHub::publish(const QString& type, const QJsonObject& data)
{
    Event event{m_nextId++, type, QJsonDocument(data).toJson(QJsonDocument::Compact)};
    m_ring.push_back(event);
    while (static_cast<qsizetype>(m_ring.size()) > kCapacity)
        m_ring.pop_front();
    emit published(event);
}

QList<ApiEventHub::Event> ApiEventHub::since(qint64 lastId, bool& reset) const
{
    reset = false;
    QList<Event> out;
    if (lastId >= m_nextId) {
        // an id from another daemon run
        reset = true;
        return out;
    }
    if (m_ring.empty())
        return out;
    if (lastId + 1 < m_ring.front().id) {
        // the events in between fell out of the ring
        reset = true;
        return out;
    }
    for (const Event& event : m_ring) {
        if (event.id > lastId)
            out.append(event);
    }
    return out;
}

} // namespace eMule
