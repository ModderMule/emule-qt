#include "pch.h"
/// @file UPnPWorker.cpp
/// @brief Blocking miniupnpc calls, isolated on a worker thread.

#include "portmap/UPnPWorker.h"
#include "net/LocalIPv6.h"
#include "utils/Log.h"

#include <QElapsedTimer>

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <cstdio>
#include <cstring>

namespace eMule {

namespace {

constexpr int kDiscoverTimeoutMs = 2000;
constexpr unsigned char kDiscoverTtl = 2;

/// IGD1 rejects a finite lease with this error and wants a permanent mapping.
constexpr int kOnlyPermanentLeasesSupported = 725;

[[nodiscard]] const char* protocolName(PortMapProtocol protocol)
{
    return protocol == PortMapProtocol::Udp ? "UDP" : "TCP";
}

/// AddPinhole wants the numeric IANA protocol, not "TCP"/"UDP" — passing the
/// name yields a 402 Invalid Args that looks like a firewall refusal.
[[nodiscard]] const char* protocolNumber(PortMapProtocol protocol)
{
    return protocol == PortMapProtocol::Udp ? "17" : "6";
}

[[nodiscard]] QString describeError(int code)
{
    const char* text = strupnperror(code);
    return text != nullptr ? QString::fromLatin1(text)
                           : QStringLiteral("UPnP error %1").arg(code);
}

} // namespace

// ---------------------------------------------------------------------------
// IGDState — PIMPL keeping miniupnpc types out of the header
// ---------------------------------------------------------------------------

struct UPnPWorker::IGDState {
    UPNPUrls urls{};
    IGDdatas data{};
    char lanIP[64]{};
    char wanIP[64]{};
    bool valid = false;

    ~IGDState()
    {
        if (valid)
            FreeUPNPUrls(&urls);
    }
};

UPnPWorker::UPnPWorker(QObject* parent)
    : QObject(parent)
{
}

UPnPWorker::~UPnPWorker() = default;

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void UPnPWorker::discover(const QString& bindAddress)
{
    releaseIGD();
    m_pinholesAvailable = false;

    // Every outcome below reports how long it took. Discovery is the one backend
    // that can overrun PortMapper's probe round — SSDP's fixed wait plus one HTTP
    // description fetch per SSDP responder, whether or not it is a router — so the
    // number is what tells a bug report whether the budget or the router was at fault.
    QElapsedTimer elapsed;
    elapsed.start();
    const auto took = [&elapsed](const QString& text) {
        return QStringLiteral("%1 after %2 ms").arg(text).arg(elapsed.elapsed());
    };

    const QByteArray bindLatin = bindAddress.toLatin1();
    const char* bindPtr = bindLatin.isEmpty() ? nullptr : bindLatin.constData();

    int error = 0;
    UPNPDev* devices = upnpDiscover(kDiscoverTimeoutMs, bindPtr, nullptr, 0,
                                    /*ipv6=*/0, kDiscoverTtl, &error);
    if (aborted()) {
        if (devices != nullptr)
            freeUPNPDevlist(devices);
        return;
    }
    if (devices == nullptr) {
        emit discovered(false, took(QStringLiteral("no IGD answered SSDP (error %1)").arg(error)),
                        Address{}, false);
        return;
    }

    auto igd = std::make_unique<IGDState>();
    const int result = UPNP_GetValidIGD(devices, &igd->urls, &igd->data,
                                        igd->lanIP, sizeof(igd->lanIP),
                                        igd->wanIP, sizeof(igd->wanIP));
    freeUPNPDevlist(devices);

    if (aborted())
        return;

    if (result == UPNP_NO_IGD || result == UPNP_UNKNOWN_DEVICE
        || result == UPNP_DISCONNECTED_IGD) {
        if (result != UPNP_NO_IGD)
            FreeUPNPUrls(&igd->urls);
        emit discovered(false, took(QStringLiteral("no usable IGD (code %1)").arg(result)),
                        Address{}, false);
        return;
    }
    igd->valid = true;

    // UPNP_PRIVATEIP_IGD means the router's own WAN address is non-routable,
    // i.e. we are behind a second NAT. Mapping still helps outbound, but the
    // address must never be published as ours.
    const bool privateWan = result == UPNP_PRIVATEIP_IGD;

    char externalIp[64]{};
    Address externalAddress;
    if (UPNP_GetExternalIPAddress(igd->urls.controlURL, igd->data.first.servicetype,
                                  externalIp)
        == UPNPCOMMAND_SUCCESS) {
        externalAddress = Address::fromString(QString::fromLatin1(externalIp));
    } else if (igd->wanIP[0] != '\0') {
        externalAddress = Address::fromString(QString::fromLatin1(igd->wanIP));
    }

    // IGD2 IPv6 pinholes. Advertising the service is not enough — the dev
    // FRITZ!Box reports FirewallEnabled=1 InboundPinholeAllowed=1, but plenty of
    // devices advertise it and then refuse, so both flags are required.
    if (igd->urls.controlURL_6FC != nullptr && igd->urls.controlURL_6FC[0] != '\0') {
        int firewallEnabled = 0;
        int inboundPinholeAllowed = 0;
        if (UPNP_GetFirewallStatus(igd->urls.controlURL_6FC, igd->data.IPv6FC.servicetype,
                                   &firewallEnabled, &inboundPinholeAllowed)
            == UPNPCOMMAND_SUCCESS) {
            m_pinholesAvailable = firewallEnabled != 0 && inboundPinholeAllowed != 0;
            logDebug(QStringLiteral("UPnP: IGD2 firewall control — enabled=%1 pinholes=%2")
                         .arg(firewallEnabled)
                         .arg(inboundPinholeAllowed));
        }
    }

    m_igd = std::move(igd);
    emit discovered(true,
                    took(privateWan ? QStringLiteral("IGD behind a second NAT")
                                    : QStringLiteral("IGD found")),
                    privateWan ? Address{} : externalAddress,
                    m_pinholesAvailable);
}

void UPnPWorker::addMapping(const PortMapRequest& request, uint32 lifetimeSecs)
{
    PortMapping mapping;
    mapping.request = request;
    mapping.method = PortMapMethod::UPnP;

    if (aborted())
        return;
    if (!m_igd || !m_igd->valid) {
        emit mapped(mapping, false, QStringLiteral("no IGD"));
        return;
    }

    QString error;
    const bool ok = request.family == PortMapFamily::IPv6
                        ? addPinhole(request, lifetimeSecs, mapping, error)
                        : addPortMapping(request, lifetimeSecs, mapping, error);
    emit mapped(mapping, ok, error);
}

void UPnPWorker::deleteMapping(const PortMapping& mapping)
{
    if (!m_igd || !m_igd->valid)
        return;

    if (mapping.request.family == PortMapFamily::IPv6) {
        if (!mapping.opaqueId.isEmpty()) {
            UPNP_DeletePinhole(m_igd->urls.controlURL_6FC, m_igd->data.IPv6FC.servicetype,
                               mapping.opaqueId.constData());
        }
        return;
    }

    const QByteArray port = QByteArray::number(mapping.externalPort);
    const int result = UPNP_DeletePortMapping(m_igd->urls.controlURL,
                                              m_igd->data.first.servicetype,
                                              port.constData(),
                                              protocolName(mapping.request.protocol),
                                              nullptr);
    if (result != UPNPCOMMAND_SUCCESS) {
        logDebug(QStringLiteral("UPnP: DeletePortMapping(%1) returned %2")
                     .arg(mapping.externalPort)
                     .arg(describeError(result)));
    }
}

void UPnPWorker::releaseIGD()
{
    m_igd.reset();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool UPnPWorker::addPortMapping(const PortMapRequest& request, uint32 lifetimeSecs,
                                PortMapping& out, QString& error)
{
    const QByteArray internalPort = QByteArray::number(request.internalPort);
    const QByteArray description = request.description.isEmpty()
                                       ? QByteArray("eMule")
                                       : request.description.toLatin1();
    const char* protocol = protocolName(request.protocol);
    QByteArray lease = QByteArray::number(lifetimeSecs);

    // AddAnyPortMapping first: it reports the port actually reserved instead of
    // silently stealing another LAN host's mapping the way IGD1 AddPortMapping
    // does when the external port is already taken.
    char reservedPort[8]{};
    int result = UPNP_AddAnyPortMapping(m_igd->urls.controlURL, m_igd->data.first.servicetype,
                                        internalPort.constData(), internalPort.constData(),
                                        m_igd->lanIP, description.constData(), protocol,
                                        nullptr, lease.constData(), reservedPort);

    if (result == kOnlyPermanentLeasesSupported) {
        // IGD1 devices that refuse a finite lease. Fall back to a permanent
        // mapping; lifetimeSecs 0 makes PortMapper poll instead of renew.
        lease = QByteArray("0");
        lifetimeSecs = 0;
        result = UPNP_AddAnyPortMapping(m_igd->urls.controlURL, m_igd->data.first.servicetype,
                                        internalPort.constData(), internalPort.constData(),
                                        m_igd->lanIP, description.constData(), protocol,
                                        nullptr, lease.constData(), reservedPort);
    }

    if (result != UPNPCOMMAND_SUCCESS) {
        // Older devices implement only AddPortMapping.
        result = UPNP_AddPortMapping(m_igd->urls.controlURL, m_igd->data.first.servicetype,
                                     internalPort.constData(), internalPort.constData(),
                                     m_igd->lanIP, description.constData(), protocol,
                                     nullptr, lease.constData());
        std::snprintf(reservedPort, sizeof(reservedPort), "%u", request.internalPort);
    }

    if (result != UPNPCOMMAND_SUCCESS) {
        error = describeError(result);
        return false;
    }

    const uint16 granted = static_cast<uint16>(QByteArray(reservedPort).toUInt());
    out.externalPort = granted != 0 ? granted : request.internalPort;
    out.lifetimeSecs = lifetimeSecs;
    out.method = PortMapMethod::UPnP;
    return true;
}

bool UPnPWorker::addPinhole(const PortMapRequest& request, uint32 lifetimeSecs,
                            PortMapping& out, QString& error)
{
    if (!m_pinholesAvailable) {
        error = QStringLiteral("router does not allow inbound IPv6 pinholes");
        return false;
    }

    // The pinhole is for our own global address; a link-local or ULA would be
    // accepted by some devices and then never receive anything.
    Address client = request.internalClient;
    if (!client.isIPv6())
        client = selectPreferredIPv6(scanLocalIPv6());
    if (!client.isIPv6()) {
        error = QStringLiteral("no global IPv6 address to pinhole");
        return false;
    }

    const QByteArray internalClient = client.toString().toLatin1();
    const QByteArray internalPort = QByteArray::number(request.internalPort);
    const QByteArray lease = QByteArray::number(lifetimeSecs);

    char uniqueId[32]{};
    const int result = UPNP_AddPinhole(m_igd->urls.controlURL_6FC,
                                       m_igd->data.IPv6FC.servicetype,
                                       /*remoteHost=*/"", /*remotePort=*/"0",
                                       internalClient.constData(), internalPort.constData(),
                                       protocolNumber(request.protocol),
                                       lease.constData(), uniqueId);
    if (result != UPNPCOMMAND_SUCCESS) {
        error = describeError(result);
        return false;
    }

    // IPv6 has no translation: the pinhole is a firewall rule, so the external
    // port and address are always the internal ones.
    out.externalPort = request.internalPort;
    out.externalAddress = client;
    out.lifetimeSecs = lifetimeSecs;
    out.method = PortMapMethod::UPnP;
    out.opaqueId = QByteArray(uniqueId);
    return true;
}

} // namespace eMule
