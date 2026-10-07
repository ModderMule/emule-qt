/// @file DaemonApiBackend.cpp
/// @brief The daemon-side half of the REST API — implementation.

#include "DaemonApiBackend.h"

#include "DaemonApp.h"
#include "IpcClientHandler.h"
#include "IpcServer.h"

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

ops::Status DaemonApiBackend::applyPreferences(const PrefChanges& changes)
{
    const IpcClientHandler::PrefApplyOutcome outcome =
        IpcClientHandler::applyPreferenceChanges(changes);
    if (!m_ipcServer)
        return {};

    if (outcome.categoriesChanged)
        m_ipcServer->notifyCategoriesChanged();
    // Queued: the web server answering this request may be the thing that restarts.
    QTimer::singleShot(0, m_ipcServer, [server = m_ipcServer, standby = outcome.standbyChanged] {
        emit server->webServerConfigChanged();
        if (standby)
            emit server->standbyConfigChanged();
    });
    return outcome.saved ? ops::Status{}
                         : ops::Status::fail(500, QStringLiteral("Could not write preferences.yml"));
}

void DaemonApiBackend::requestShutdown()
{
    // Let the reply out first.
    QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

} // namespace eMule
