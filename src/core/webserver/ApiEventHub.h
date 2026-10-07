#pragma once

/// @file ApiEventHub.h
/// @brief The event ring behind GET /api/v1/events.
///
/// Outlives the web server (which is rebuilt on a config change), so a client
/// that reconnects can resume by id. Fed by the daemon from the same pushes the
/// GUI gets; most events only say "this list changed, fetch it again".

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <deque>

namespace eMule {

class ApiEventHub : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype kCapacity = 1000;

    struct Event {
        qint64 id = 0;
        QString type;       ///< "downloads.changed"; the part before the dot is the topic
        QByteArray data;    ///< compact JSON object

        [[nodiscard]] QString topic() const { return type.section(u'.', 0, 0); }
    };

    explicit ApiEventHub(QObject* parent = nullptr);

    void publish(const QString& type, const QJsonObject& data = {});

    /// Events newer than @p lastId. @p reset is set when that id is not one this
    /// ring can continue from: the client has to refetch everything.
    [[nodiscard]] QList<Event> since(qint64 lastId, bool& reset) const;

    [[nodiscard]] qint64 lastId() const { return m_nextId - 1; }

    /// Open event streams; the feeder may skip work while this is 0.
    [[nodiscard]] int listenerCount() const { return m_listeners; }
    void addListener() { ++m_listeners; }
    void removeListener() { m_listeners = m_listeners > 0 ? m_listeners - 1 : 0; }

signals:
    void published(const eMule::ApiEventHub::Event& event);

private:
    std::deque<Event> m_ring;
    qint64 m_nextId = 1;
    int m_listeners = 0;
};

} // namespace eMule
