#include "pch.h"
/// @file UPnPManager.cpp
/// @brief UPnP port mapping implementation using miniupnpc.

#include "UPnPManager.h"
#include "utils/Log.h"

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <QThread>

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// IGDState — PIMPL hiding miniupnpc types from the header
// ---------------------------------------------------------------------------

struct UPnPManager::IGDState {
    UPNPUrls urls{};
    IGDdatas data{};
    char lanIP[40]{};
    char wanIP[40]{};
    bool valid = false;

    ~IGDState()
    {
        if (valid)
            FreeUPNPUrls(&urls);
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

UPnPManager::UPnPManager(QObject* parent)
    : QObject(parent)
{
}

UPnPManager::~UPnPManager()
{
    stopDiscovery();
    deletePorts();
}

void UPnPManager::startDiscovery(uint16 tcpPort, uint16 udpPort, uint16 webPort)
{
    stopDiscovery();

    {
        std::lock_guard lock(m_mutex);
        m_oldTcpPort = m_tcpPort;
        m_oldUdpPort = m_udpPort;
        m_oldWebPort = m_webPort;
        m_tcpPort = tcpPort;
        m_udpPort = udpPort;
        m_webPort = webPort;
    }

    m_abortDiscovery = false;
    m_consecutiveRefreshFailures = 0;

    m_discoveryThread = QThread::create([this] { runDiscovery(); });
    m_discoveryThread->setObjectName(QStringLiteral("UPnP-Discovery"));
    m_discoveryThread->start();
}

bool UPnPManager::checkAndRefresh()
{
    std::lock_guard lock(m_mutex);

    if (m_consecutiveRefreshFailures >= MaxRefreshRetries)
        return false;

    if (!m_igd || !m_igd->valid) {
        logDebug(QStringLiteral("UPnP: no valid IGD — cannot refresh"));
        return false;
    }

    bool allGood = true;

    if (m_tcpPort != 0) {
        if (!verifyPort(m_tcpPort, "TCP")) {
            logWarning(QStringLiteral("UPnP: TCP port %1 mapping lost, re-adding")
                           .arg(m_tcpPort));
            if (!openPort(m_tcpPort, "TCP", "eMule TCP"))
                allGood = false;
        }
    }

    if (m_udpPort != 0) {
        if (!verifyPort(m_udpPort, "UDP")) {
            logWarning(QStringLiteral("UPnP: UDP port %1 mapping lost, re-adding")
                           .arg(m_udpPort));
            if (!openPort(m_udpPort, "UDP", "eMule UDP"))
                allGood = false;
        }
    }

    if (m_webPort != 0) {
        if (!verifyPort(m_webPort, "TCP")) {
            logWarning(QStringLiteral("UPnP: Web port %1 mapping lost, re-adding")
                           .arg(m_webPort));
            if (!openPort(m_webPort, "TCP", "eMule Web"))
                allGood = false;
        }
    }

    if (allGood) {
        m_consecutiveRefreshFailures = 0;
        setStatus(PortStatus::Forwarded);
    } else {
        ++m_consecutiveRefreshFailures;
        if (m_consecutiveRefreshFailures >= MaxRefreshRetries) {
            logWarning(QStringLiteral("UPnP: port mapping failed %1 times in a row, "
                                      "suspending refresh (UPnP may be disabled on router)")
                           .arg(m_consecutiveRefreshFailures));
        } else {
            logDebug(QStringLiteral("UPnP: refresh failure %1/%2")
                         .arg(m_consecutiveRefreshFailures)
                         .arg(MaxRefreshRetries));
        }
        setStatus(PortStatus::NotForwarded);
    }

    return allGood;
}

void UPnPManager::deletePorts()
{
    std::lock_guard lock(m_mutex);

    if (!m_igd || !m_igd->valid)
        return;

    if (m_tcpPort != 0)
        closePort(m_tcpPort, "TCP");
    if (m_udpPort != 0)
        closePort(m_udpPort, "UDP");
    if (m_webPort != 0)
        closePort(m_webPort, "TCP");

    cleanupIGD();
    m_ready = false;
    setStatus(PortStatus::Unknown);
}

void UPnPManager::stopDiscovery()
{
    m_abortDiscovery = true;

    if (m_discoveryThread) {
        if (m_discoveryThread->isRunning()) {
            if (!m_discoveryThread->wait(7000)) {
                logWarning(QStringLiteral("UPnP: discovery thread did not finish in 7s, terminating"));
                m_discoveryThread->terminate();
                m_discoveryThread->wait(2000);
            }
        }
        delete m_discoveryThread;
        m_discoveryThread = nullptr;
    }
}

void UPnPManager::enableWebServerPort(uint16 port)
{
    std::lock_guard lock(m_mutex);
    m_webPort = port;

    if (m_igd && m_igd->valid && port != 0) {
        if (openPort(port, "TCP", "eMule Web"))
            logDebug(QStringLiteral("UPnP: web server port %1 mapped").arg(port));
    }
}

bool UPnPManager::isReady() const
{
    std::lock_guard lock(m_mutex);
    return m_ready;
}

UPnPManager::PortStatus UPnPManager::portStatus() const
{
    return m_status.load();
}

void UPnPManager::setBindAddress(const QString& addr)
{
    std::lock_guard lock(m_mutex);
    m_bindAddress = addr;
}

// ---------------------------------------------------------------------------
// Private implementation
// ---------------------------------------------------------------------------

void UPnPManager::runDiscovery()
{
    logDebug(QStringLiteral("UPnP: starting device discovery..."));

    const QByteArray bindAddr = m_bindAddress.toLatin1();
    const char* bindAddrPtr = bindAddr.isEmpty() ? nullptr : bindAddr.constData();

    int error = 0;
    UPNPDev* devlist = upnpDiscover(
        2000,           // timeout ms
        bindAddrPtr,    // multicast interface
        nullptr,        // minissdpdsock
        0,              // localport
        0,              // ipv6
        2,              // TTL
        &error
    );

    if (m_abortDiscovery) {
        if (devlist)
            freeUPNPDevlist(devlist);
        logDebug(QStringLiteral("UPnP: discovery aborted"));
        return;
    }

    if (!devlist) {
        logWarning(QStringLiteral("UPnP: no devices found (error=%1)").arg(error));
        setStatus(PortStatus::NotForwarded);
        emit discoveryComplete(false);
        return;
    }

    auto igd = std::make_unique<IGDState>();
    int igdResult = UPNP_GetValidIGD(devlist, &igd->urls, &igd->data,
                                      igd->lanIP, sizeof(igd->lanIP),
                                      igd->wanIP, sizeof(igd->wanIP));
    freeUPNPDevlist(devlist);

    if (m_abortDiscovery) {
        logDebug(QStringLiteral("UPnP: discovery aborted after IGD lookup"));
        return;
    }

    if (igdResult == 0) {
        logWarning(QStringLiteral("UPnP: no valid IGD found"));
        setStatus(PortStatus::NotForwarded);
        emit discoveryComplete(false);
        return;
    }

    igd->valid = true;
    logDebug(QStringLiteral("UPnP: found IGD (type=%1), LAN IP: %2")
                 .arg(igdResult)
                 .arg(QString::fromLatin1(igd->lanIP)));

    if (igd->wanIP[0] != '\0') {
        logDebug(QStringLiteral("UPnP: external IP: %1")
                     .arg(QString::fromLatin1(igd->wanIP)));
    }

    // Store IGD state
    {
        std::lock_guard lock(m_mutex);
        m_igd = std::move(igd);
    }

    if (m_abortDiscovery) {
        logDebug(QStringLiteral("UPnP: discovery aborted before port mapping"));
        return;
    }

    // Open ports
    bool allOk = true;
    {
        std::lock_guard lock(m_mutex);

        if (m_tcpPort != 0) {
            if (m_oldTcpPort != 0 && m_oldTcpPort != m_tcpPort)
                closePort(m_oldTcpPort, "TCP");
            if (!openPort(m_tcpPort, "TCP", "eMule TCP"))
                allOk = false;
        }

        if (m_udpPort != 0) {
            if (m_oldUdpPort != 0 && m_oldUdpPort != m_udpPort)
                closePort(m_oldUdpPort, "UDP");
            if (!openPort(m_udpPort, "UDP", "eMule UDP"))
                allOk = false;
        }

        if (m_webPort != 0) {
            if (m_oldWebPort != 0 && m_oldWebPort != m_webPort)
                closePort(m_oldWebPort, "TCP");
            if (!openPort(m_webPort, "TCP", "eMule Web"))
                allOk = false;
        }

        m_ready = allOk;
        m_succeededOnce = m_succeededOnce || allOk;
    }

    setStatus(allOk ? PortStatus::Forwarded : PortStatus::Failed);
    emit discoveryComplete(allOk);
}

bool UPnPManager::openPort(uint16 port, const char* proto, const char* description)
{
    if (!m_igd || !m_igd->valid)
        return false;

    auto portStr = std::to_string(port);

    int result = UPNP_AddPortMapping(
        m_igd->urls.controlURL,
        m_igd->data.first.servicetype,
        portStr.c_str(),        // external port
        portStr.c_str(),        // internal port
        m_igd->lanIP,           // internal client
        description,            // description
        proto,                  // protocol
        nullptr,                // remote host (any)
        "0"                     // lease duration (indefinite)
    );

    if (result != UPNPCOMMAND_SUCCESS) {
        logWarning(QStringLiteral("UPnP: AddPortMapping(%1/%2) failed: %3")
                       .arg(port)
                       .arg(QString::fromLatin1(proto))
                       .arg(QString::fromLatin1(strupnperror(result))));
        return false;
    }

    // Verify
    if (!verifyPort(port, proto)) {
        logWarning(QStringLiteral("UPnP: port %1/%2 mapped but verification failed")
                       .arg(port)
                       .arg(QString::fromLatin1(proto)));
        return false;
    }

    logDebug(QStringLiteral("UPnP: port %1/%2 mapped successfully (%3)")
                 .arg(port)
                 .arg(QString::fromLatin1(proto))
                 .arg(QString::fromLatin1(description)));
    return true;
}

bool UPnPManager::verifyPort(uint16 port, const char* proto)
{
    if (!m_igd || !m_igd->valid)
        return false;

    auto portStr = std::to_string(port);
    char intClient[40]{};
    char intPort[6]{};
    char duration[16]{};
    char enabled[4]{};
    char desc[80]{};

    int result = UPNP_GetSpecificPortMappingEntry(
        m_igd->urls.controlURL,
        m_igd->data.first.servicetype,
        portStr.c_str(),    // external port
        proto,              // protocol
        nullptr,            // remote host
        intClient,
        intPort,
        desc,
        enabled,
        duration
    );

    return result == UPNPCOMMAND_SUCCESS;
}

void UPnPManager::closePort(uint16 port, const char* proto)
{
    if (!m_igd || !m_igd->valid)
        return;

    auto portStr = std::to_string(port);

    int result = UPNP_DeletePortMapping(
        m_igd->urls.controlURL,
        m_igd->data.first.servicetype,
        portStr.c_str(),
        proto,
        nullptr     // remote host
    );

    if (result != UPNPCOMMAND_SUCCESS) {
        logDebug(QStringLiteral("UPnP: DeletePortMapping(%1/%2) returned: %3")
                     .arg(port)
                     .arg(QString::fromLatin1(proto))
                     .arg(QString::fromLatin1(strupnperror(result))));
    } else {
        logDebug(QStringLiteral("UPnP: port %1/%2 mapping removed")
                     .arg(port)
                     .arg(QString::fromLatin1(proto)));
    }
}

void UPnPManager::cleanupIGD()
{
    m_igd.reset();
}

void UPnPManager::setStatus(PortStatus status)
{
    auto old = m_status.exchange(status);
    if (old != status)
        emit portStatusChanged(status);
}

} // namespace eMule
