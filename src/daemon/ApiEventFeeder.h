#pragma once

/// @file ApiEventFeeder.h
/// @brief Turns the pushes the GUI gets into events for GET /api/v1/events.

#include "IpcMessage.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace eMule {

class ApiEventHub;

class ApiEventFeeder : public QObject {
    Q_OBJECT

public:
    ApiEventFeeder(ApiEventHub* hub, QObject* parent);

    /// Every broadcast passes through here; the ones worth an event become one.
    void onPush(const Ipc::IpcMessage& msg);

    /// "This list changed": sent at once, then at most once a second, and the
    /// last one of a burst is never lost.
    void hint(const QString& type);

    /// A state snapshot: sent only when it differs from the last one.
    void state(const QString& type, const QJsonObject& data);

    /// Something that happened once.
    void event(const QString& type, const QJsonObject& data);

private:
    void flush();

    ApiEventHub* m_hub;
    QHash<QString, qint64> m_lastHintMs;
    QSet<QString> m_pendingHints;
    QHash<QString, QJsonObject> m_lastState;
    QTimer m_flushTimer;
};

} // namespace eMule
