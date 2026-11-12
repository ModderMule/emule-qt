#pragma once

/// @file IpcListenWatch.h
/// @brief Keeps the IPC server on the addresses its listen host name resolves to:
/// looks the name up again when this machine's addresses or host name change, and
/// every ipc.listenRecheckMinutes.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

namespace eMule {

class IpcServer;

class IpcListenWatch : public QObject {
    Q_OBJECT

public:
    /// @p name: the ipc.listenAddress value, a host name.
    IpcListenWatch(IpcServer* server, const QString& name, QObject* parent = nullptr);

    /// Looks the name up now and moves the server to its addresses. Not while one runs.
    void lookUp();

private:
    void onTick();
    /// This machine's addresses and host name, as one comparable string.
    [[nodiscard]] static QString networkState();

    IpcServer* m_server;
    QString m_name;
    QString m_state;              ///< networkState() as last seen
    QTimer m_tick;
    QElapsedTimer m_sinceLookup;
    bool m_looking = false;
};

} // namespace eMule
