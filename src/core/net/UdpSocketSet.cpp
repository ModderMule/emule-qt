#include "pch.h"
/// @file UdpSocketSet.cpp
/// @brief The UDP sockets of one port.

#include "net/UdpSocketSet.h"
#include "net/BindAddress.h"
#include "net/InterfacePin.h"

#include <QUdpSocket>

namespace eMule {

namespace {

bool isWildcard(const QHostAddress& a)
{
    return a == QHostAddress(QHostAddress::Any) || a == QHostAddress(QHostAddress::AnyIPv4)
        || a == QHostAddress(QHostAddress::AnyIPv6);
}

// IPv4 for a plain or v4-mapped address
bool isV4(const QHostAddress& a)
{
    if (a.protocol() == QAbstractSocket::IPv4Protocol)
        return true;
    bool mapped = false;
    a.toIPv4Address(&mapped);
    return mapped;
}

// The family a socket bound to @p bound can send to @p dest.
bool carries(const QHostAddress& bound, const QHostAddress& dest)
{
    if (bound == QHostAddress(QHostAddress::Any))
        return true;   // dual stack
    return (bound.protocol() == QAbstractSocket::IPv4Protocol) == isV4(dest);
}

} // namespace

UdpSocketSet::UdpSocketSet(QString name, QObject* parent)
    : QObject(parent)
    , m_name(std::move(name))
{
}

UdpSocketSet::~UdpSocketSet()
{
    std::lock_guard guard(m_lock);
    m_slots.clear();
}

bool UdpSocketSet::bind(uint16 port)
{
    m_follow = true;
    const QList<QHostAddress> wanted = BindAddress::listenAddresses();
    if (wanted.isEmpty() && !BindAddress::outboundAllowed()) {
        closeAll();
        m_error = QStringLiteral("the selected network interface is not available");
        return false;
    }
    return openAll(wanted, port, m_error);
}

bool UdpSocketSet::bindTo(const QList<QHostAddress>& addresses, uint16 port)
{
    m_follow = false;
    return openAll(addresses, port, m_error);
}

bool UdpSocketSet::sync(const QList<QHostAddress>& addresses)
{
    if (!m_follow || !syncTo(addresses))
        return false;
    // the listen order: the first of a family sends
    const QList<QHostAddress>& order = boundAddresses();
    std::lock_guard guard(m_lock);
    std::ranges::stable_sort(m_slots, {}, [&order](const Slot& s) { return order.indexOf(s.address); });
    return true;
}

void UdpSocketSet::close()
{
    closeAll();
}

bool UdpSocketSet::isBound() const
{
    std::lock_guard guard(m_lock);
    return !m_slots.empty();
}

int UdpSocketSet::socketCount() const
{
    std::lock_guard guard(m_lock);
    return static_cast<int>(m_slots.size());
}

QList<qintptr> UdpSocketSet::socketDescriptors() const
{
    std::lock_guard guard(m_lock);
    QList<qintptr> out;
    for (const Slot& s : m_slots)
        out.append(s.socket->socketDescriptor());
    return out;
}

bool UdpSocketSet::hasPendingDatagrams() const
{
    std::lock_guard guard(m_lock);
    for (const Slot& s : m_slots)
        if (s.socket->hasPendingDatagrams())
            return true;
    return false;
}

QNetworkDatagram UdpSocketSet::receiveDatagram(qint64 maxSize)
{
    std::lock_guard guard(m_lock);
    for (const Slot& s : m_slots)
        if (s.socket->hasPendingDatagrams())
            return s.socket->receiveDatagram(maxSize);
    return {};
}

qint64 UdpSocketSet::writeDatagram(const QNetworkDatagram& datagram)
{
    std::lock_guard guard(m_lock);
    bool keepSender = false;
    QUdpSocket* socket = pickLocked(datagram.destinationAddress(), datagram.senderAddress(), keepSender);
    if (!socket) {
        m_sendError = QStringLiteral("no socket bound for the family of %1").arg(datagram.destinationAddress().toString());
        return -1;
    }
    qint64 sent = 0;
    if (keepSender || datagram.senderAddress().isNull()) {
        sent = socket->writeDatagram(datagram);
    } else {
        // bound to the address it leaves from: no sender of its own
        const QByteArray data = datagram.data();
        sent = socket->writeDatagram(data, datagram.destinationAddress(), static_cast<quint16>(datagram.destinationPort()));
    }
    if (sent < 0)
        m_sendError = socket->errorString();
    return sent;
}

qint64 UdpSocketSet::writeDatagram(const QByteArray& data, const QHostAddress& host, quint16 port)
{
    return writeDatagram(data.constData(), data.size(), host, port);
}

qint64 UdpSocketSet::writeDatagram(const char* data, qint64 len, const QHostAddress& host, quint16 port)
{
    std::lock_guard guard(m_lock);
    bool keepSender = false;
    QUdpSocket* socket = pickLocked(host, {}, keepSender);
    if (!socket) {
        m_sendError = QStringLiteral("no socket bound for the family of %1").arg(host.toString());
        return -1;
    }
    const qint64 sent = socket->writeDatagram(data, len, host, port);
    if (sent < 0)
        m_sendError = socket->errorString();
    return sent;
}

QString UdpSocketSet::sendErrorString() const
{
    std::lock_guard guard(m_lock);
    return m_sendError;
}

void UdpSocketSet::setSocketOption(QAbstractSocket::SocketOption option, const QVariant& value)
{
    m_options.insert(int(option), value);
    std::lock_guard guard(m_lock);
    for (const Slot& s : m_slots)
        s.socket->setSocketOption(option, value);
}

QVariant UdpSocketSet::socketOption(QAbstractSocket::SocketOption option) const
{
    std::lock_guard guard(m_lock);
    if (m_slots.empty())
        return {};
    return m_slots.front().socket->socketOption(option);
}

UdpSocketSet::BindResult UdpSocketSet::bindOne(const QHostAddress& address, uint16 port, QString& error)
{
    auto socket = std::make_unique<QUdpSocket>();
    if (!socket->bind(address, port)) {
        error = socket->errorString();
        // IPv6 DAD unfinished: EADDRNOTAVAIL until it is
        return socket->error() == QAbstractSocket::SocketAddressNotAvailableError ? BindResult::Later
                                                                                   : BindResult::Failed;
    }
    if (!InterfacePin::pin(socket->socketDescriptor())) {
        error = QStringLiteral("cannot pin the socket to the interface");
        return BindResult::Failed;
    }
    for (auto it = m_options.cbegin(); it != m_options.cend(); ++it)
        socket->setSocketOption(QAbstractSocket::SocketOption(it.key()), it.value());
    connect(socket.get(), &QUdpSocket::readyRead, this, &UdpSocketSet::readyRead);
    std::lock_guard guard(m_lock);
    m_slots.push_back({address, std::move(socket)});
    return BindResult::Ok;
}

void UdpSocketSet::closeOne(const QHostAddress& address)
{
    std::unique_ptr<QUdpSocket> gone;
    {
        std::lock_guard guard(m_lock);
        const auto it = std::ranges::find_if(m_slots, [&address](const Slot& s) { return s.address == address; });
        if (it == m_slots.end())
            return;
        gone = std::move(it->socket);
        m_slots.erase(it);
    }
    gone->disconnect(this);
    gone->close();
    // may be in its own readyRead up the stack
    gone.release()->deleteLater();
}

uint16 UdpSocketSet::portOf(const QHostAddress& address) const
{
    std::lock_guard guard(m_lock);
    for (const Slot& s : m_slots)
        if (s.address == address)
            return s.socket->localPort();
    return 0;
}

QUdpSocket* UdpSocketSet::pickLocked(const QHostAddress& dest, const QHostAddress& sender, bool& keepSender) const
{
    keepSender = false;
    if (m_slots.empty())
        return nullptr;
    // one wildcard socket: as a plain QUdpSocket, the sender picks the source address
    if (m_slots.size() == 1 && isWildcard(m_slots.front().address)) {
        keepSender = true;
        return carries(m_slots.front().address, dest) ? m_slots.front().socket.get() : nullptr;
    }
    if (!sender.isNull())
        for (const Slot& s : m_slots)
            if (s.address == sender && carries(s.address, dest))
                return s.socket.get();
    // in listen order: the first of the family is its sender
    for (const Slot& s : m_slots)
        if (carries(s.address, dest))
            return s.socket.get();
    return nullptr;
}

} // namespace eMule
