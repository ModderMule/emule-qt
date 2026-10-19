/// @file DaemonApiBackend.cpp
/// @brief The daemon-side half of the REST API — implementation.

#include "DaemonApiBackend.h"

#include "DaemonApp.h"
#include "IpcClientHandler.h"
#include "IpcServer.h"
#include "app/AppContext.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QTimer>

namespace eMule {

DaemonApiBackend::DaemonApiBackend(IpcServer* ipcServer)
    : m_ipcServer(ipcServer)
{
}

QList<ApiBackend::LogRecord> DaemonApiBackend::logs(qint64 sinceId) const
{
    QList<LogRecord> out;
    for (const auto& entry : DaemonApp::logsSince(sinceId)) {
        out.append({static_cast<qint64>(entry.id), entry.category, static_cast<int>(entry.severity),
                    entry.message, entry.timestamp});
    }
    return out;
}

QJsonArray DaemonApiBackend::categories() const
{
    return IpcClientHandler::categoryList().toJsonArray();
}

ops::Status DaemonApiBackend::setCategories(const QJsonArray& categories)
{
    const ops::Status st = IpcClientHandler::storeCategories(QCborArray::fromJsonArray(categories));
    if (st.ok() && m_ipcServer)
        m_ipcServer->notifyCategoriesChanged();
    return st;
}

ApiBackend::PrefApplyReport DaemonApiBackend::applyPreferences(const PrefChanges& changes)
{
    const IpcClientHandler::PrefApplyOutcome outcome =
        IpcClientHandler::applyPreferenceChanges(changes);
    PrefApplyReport report;
    report.ports = outcome.ports;
    report.network = outcome.network;
    // Same as the SetPreferences reply: where the sockets are, not the prefs
    report.tcpPort = theApp.listenSocket && theApp.listenSocket->isListening()
        ? theApp.listenSocket->connectedPort() : uint16{0};
    report.udpPort = theApp.clientUDP && theApp.clientUDP->isBound()
        ? theApp.clientUDP->connectedPort() : uint16{0};
    if (!outcome.saved)
        report.status = ops::Status::fail(500, QStringLiteral("Could not write preferences.yml"));
    if (!m_ipcServer)
        return report;

    if (outcome.categoriesChanged)
        m_ipcServer->notifyCategoriesChanged();
    // Queued: the web server answering this request may be the thing that restarts.
    QTimer::singleShot(0, m_ipcServer, [server = m_ipcServer, standby = outcome.standbyChanged,
                                        usenet = outcome.usenetChanged] {
        emit server->webServerConfigChanged();
        if (standby)
            emit server->standbyConfigChanged();
        if (usenet)
            emit server->usenetConfigChanged();
    });
    return report;
}

void DaemonApiBackend::requestShutdown()
{
    // Let the reply out first.
    QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

} // namespace eMule
