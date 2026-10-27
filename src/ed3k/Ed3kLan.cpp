/// @file Ed3kLan.cpp
/// @brief LAN discovery sockets of mode A — implementation.

#include "Ed3kLan.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"

#include "net/BindAddress.h"
#include "utils/Log.h"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QVariant>

#include <array>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QHostAddress kGroup4(QStringLiteral("239.255.101.51"));
const QHostAddress kGroup6(QStringLiteral("ff02::6533:6b"));
constexpr int kRescanMs = 60 * 1000;
constexpr std::size_t kMaxNets = 16;   // ed3k_lan_interface

QString errorName(ffi::Error e)
{
    return QString::number(static_cast<int>(e));
}

QHostAddress withoutScope(QHostAddress a)
{
    a.setScopeId(QString());
    return a;
}

} // namespace

Ed3kLan::Ed3kLan(Ed3kSession& session, std::uint16_t nodePort, Hooks hooks)
    : m_session(session)
    , m_nodePort(nodePort)
    , m_hooks(std::move(hooks))
{
    m_rescan.setInterval(kRescanMs);
    connect(&m_rescan, &QTimer::timeout, this, &Ed3kLan::rescan);
}

Ed3kLan::~Ed3kLan()
{
    stop();
}

bool Ed3kLan::start()
{
    auto* e = m_session.engine();
    if (m_started || !e)
        return m_started;
    if (!m_hooks.send) {
        // shared with other nodes of the host: not the exclusive bind of the ed3k port
        const auto mode = QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint;
        const auto open = [&](const QHostAddress& any) -> std::unique_ptr<QUdpSocket> {
            auto s = std::make_unique<QUdpSocket>();
            if (!s->bind(any, kPort, mode)) {
                logEd3kWarning(QStringLiteral("ed3k: LAN discovery: port %1 cannot be bound (%2)").arg(kPort).arg(s->errorString()));
                return nullptr;
            }
            s->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
            s->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
            QUdpSocket* raw = s.get();
            connect(raw, &QUdpSocket::readyRead, this, [this, raw] { onReadyRead(raw); });
            return s;
        };
        m_sock4 = open(QHostAddress::AnyIPv4);
        m_sock6 = open(QHostAddress::AnyIPv6);
        if (!m_sock4 && !m_sock6) {
            logEd3kWarning(QStringLiteral("ed3k: LAN discovery off"));
            return false;
        }
    }
    if (auto ok = e->lanEnable(m_session.now(), true); !ok) {
        logEd3kDebug(QStringLiteral("ed3k: LAN discovery not on (error %1)").arg(errorName(ok.error())));
        m_sock4.reset();
        m_sock6.reset();
        return false;
    }
    m_started = true;
    rescan();
    m_rescan.start();
    return true;
}

void Ed3kLan::stop()
{
    if (!m_started)
        return;
    m_started = false;
    m_rescan.stop();
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        for (const auto& [index, iface] : m_up)
            (void)e->lanInterfaceDown(t, index);
    }
    m_up.clear();
    m_sock4.reset();
    m_sock6.reset();
}

void Ed3kLan::detach()
{
    m_started = false;
    m_rescan.stop();
    m_up.clear();
    for (auto* sock : {&m_sock4, &m_sock6})
        if (*sock) {
            (*sock)->disconnect(this);
            sock->release()->deleteLater();
        }
}

void Ed3kLan::drain()
{
    auto* e = m_session.engine();
    if (!m_started || !e)
        return;
    std::array<std::byte, 512> buf{};   // holds any (ed3k_lan_datagram_out)
    for (int guard = 0; guard < 256; ++guard) {
        auto d = e->lanDatagramOut(buf);
        if (!d || !*d)
            break;
        const auto& dg = **d;
        const QByteArray data(reinterpret_cast<const char*>(buf.data()), qsizetype(std::min(dg.len, buf.size())));
        sendOne(dg.iface, toQHostAddress(dg.to), dg.to.port, data);
    }
}

void Ed3kLan::datagramIn(std::uint32_t iface, const QHostAddress& from, std::uint16_t port, const QByteArray& data)
{
    auto* e = m_session.engine();
    if (!m_started || !e)
        return;
    const auto ok = e->lanDatagramIn(m_session.now(), iface, toAddress(withoutScope(from), port),
                                      {reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    if (!ok)
        logEd3kDebug(QStringLiteral("ed3k: LAN datagram of %1 dropped (error %2)").arg(from.toString(), errorName(ok.error())));
    m_session.pump();
}

std::vector<std::uint32_t> Ed3kLan::interfacesUp() const
{
    std::vector<std::uint32_t> out;
    for (const auto& [index, iface] : m_up)
        out.push_back(index);
    return out;
}

std::vector<Ed3kLan::Iface> Ed3kLan::systemInterfaces()
{
    std::vector<Iface> out;
    for (const QNetworkInterface& ni : QNetworkInterface::allInterfaces()) {
        if (!eligible(ni) || ni.index() <= 0)
            continue;
        // the bindAddress pref: only the selected interface (all when none is selected)
        if (!BindAddress::isBoundInterface(ni.name()))
            continue;
        Iface iface{std::uint32_t(ni.index()), ni.name(), {}};
        bool local = false;
        for (const QNetworkAddressEntry& entry : ni.addressEntries()) {
            const QHostAddress ip = withoutScope(entry.ip());
            if (ip.isNull() || entry.prefixLength() < 0 || iface.nets.size() >= kMaxNets)
                continue;
            local = local || isLocalNet(ip);
            iface.nets.emplace_back(ip, entry.prefixLength());
        }
        if (local)
            out.push_back(std::move(iface));
    }
    return out;
}

bool Ed3kLan::isLocalNet(const QHostAddress& ip)
{
    static const std::array<std::pair<QHostAddress, int>, 6> kNets{{
        {QHostAddress(QStringLiteral("10.0.0.0")), 8},
        {QHostAddress(QStringLiteral("172.16.0.0")), 12},
        {QHostAddress(QStringLiteral("192.168.0.0")), 16},
        {QHostAddress(QStringLiteral("169.254.0.0")), 16},
        {QHostAddress(QStringLiteral("fe80::")), 10},
        {QHostAddress(QStringLiteral("fc00::")), 7},
    }};
    for (const auto& [net, prefix] : kNets)
        if (ip.isInSubnet(net, prefix))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kLan::onReadyRead(QUdpSocket* socket)
{
    while (m_started && socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = socket->receiveDatagram(1500);
        if (!d.isValid())
            break;
        if (d.interfaceIndex() == 0 || !m_up.contains(d.interfaceIndex()))
            continue;   // not an interface it runs on
        datagramIn(d.interfaceIndex(), d.senderAddress(), std::uint16_t(d.senderPort()), d.data());
    }
}

void Ed3kLan::rescan()
{
    auto* e = m_session.engine();
    if (!m_started || !e)
        return;
    const std::vector<Iface> now = m_hooks.interfaces ? m_hooks.interfaces() : systemInterfaces();
    const auto t = m_session.now();
    std::map<std::uint32_t, Iface> next;
    for (const Iface& iface : now)
        next.emplace(iface.index, iface);
    for (const auto& [index, iface] : m_up) {
        if (next.contains(index))
            continue;
        (void)e->lanInterfaceDown(t, index);
        const QNetworkInterface ni = QNetworkInterface::interfaceFromIndex(int(index));
        if (m_sock4 && ni.isValid())
            m_sock4->leaveMulticastGroup(kGroup4, ni);
        if (m_sock6 && ni.isValid())
            m_sock6->leaveMulticastGroup(kGroup6, ni);
        logEd3kDebug(QStringLiteral("ed3k: LAN discovery off on %1").arg(iface.name));
    }
    for (const auto& [index, iface] : next) {
        const auto old = m_up.find(index);
        if (old != m_up.end() && old->second == iface)
            continue;
        if (old == m_up.end()) {
            const QNetworkInterface ni = QNetworkInterface::interfaceFromIndex(int(index));
            if (m_sock4 && ni.isValid() && !m_sock4->joinMulticastGroup(kGroup4, ni))
                logEd3kDebug(QStringLiteral("ed3k: LAN group not joined on %1 (IPv4): %2").arg(iface.name, m_sock4->errorString()));
            if (m_sock6 && ni.isValid() && !m_sock6->joinMulticastGroup(kGroup6, ni))
                logEd3kDebug(QStringLiteral("ed3k: LAN group not joined on %1 (IPv6): %2").arg(iface.name, m_sock6->errorString()));
        }
        std::vector<ffi::LanNet> nets;
        for (const auto& [ip, prefix] : iface.nets)
            nets.push_back(ffi::LanNet{toAddress(ip, 0), std::uint8_t(prefix)});
        if (auto ok = e->lanInterface(t, index, m_nodePort, nets); !ok) {
            logEd3kDebug(QStringLiteral("ed3k: LAN interface %1 refused (error %2)").arg(iface.name, errorName(ok.error())));
            continue;
        }
        if (old == m_up.end())
            logEd3k(QStringLiteral("ed3k: LAN discovery on %1").arg(iface.name));
    }
    m_up = std::move(next);
    m_session.pump();
}

void Ed3kLan::sendOne(std::uint32_t iface, const QHostAddress& to, std::uint16_t port, const QByteArray& data)
{
    if (m_hooks.send) {
        m_hooks.send(iface, to, port, data);
        return;
    }
    const bool v4 = to.protocol() == QAbstractSocket::IPv4Protocol;
    QUdpSocket* s = v4 ? m_sock4.get() : m_sock6.get();
    if (!s)
        return;
    QNetworkDatagram dg(data, to, port);
    dg.setInterfaceIndex(iface);
    dg.setHopLimit(1);
    if (v4) {
        // IPv4 multicast leaves on the socket's interface
        const QNetworkInterface ni = QNetworkInterface::interfaceFromIndex(int(iface));
        if (ni.isValid())
            s->setMulticastInterface(ni);
    }
    if (s->writeDatagram(dg) < 0)
        logEd3kDebug(QStringLiteral("ed3k: LAN datagram not sent on %1: %2").arg(iface).arg(s->errorString()));
}

bool Ed3kLan::eligible(const QNetworkInterface& ni)
{
    const auto f = ni.flags();
    if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
        || !f.testFlag(QNetworkInterface::CanMulticast) || f.testFlag(QNetworkInterface::IsLoopBack)
        || f.testFlag(QNetworkInterface::IsPointToPoint))
        return false;
    // Apple's device-to-device links
    const QString name = ni.name();
    return !name.startsWith(QLatin1StringView("awdl")) && !name.startsWith(QLatin1StringView("llw"));
}

} // namespace eMule::ed3k
