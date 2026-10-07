#include "pch.h"
/// @file InterfacePin.cpp
/// @brief Confine a socket to the bound network interface.

#include "net/InterfacePin.h"
#include "utils/Log.h"

#include <QAbstractSocket>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#endif

namespace eMule::InterfacePin {

namespace {

// AF_INET / AF_INET6 of the descriptor, 0 when unknown.
int socketFamily(qintptr fd)
{
    sockaddr_storage ss{};
#ifdef Q_OS_WIN
    int len = sizeof(ss);
    if (::getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return 0;
#else
    socklen_t len = sizeof(ss);
    if (::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return 0;
#endif
    return ss.ss_family;
}

// The interface @p fd is confined to, 0 when none or unknown.
int pinnedIndex(qintptr fd)
{
#if defined(Q_OS_LINUX) && defined(SO_BINDTOIFINDEX)
    int idx = 0;
    socklen_t len = sizeof(idx);
    if (::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTOIFINDEX, &idx, &len) == 0)
        return idx;
#elif defined(Q_OS_DARWIN)
    unsigned idx = 0;
    socklen_t len = sizeof(idx);
    const int family = socketFamily(fd);
    if (family == AF_INET6
        && ::getsockopt(static_cast<int>(fd), IPPROTO_IPV6, IPV6_BOUND_IF, &idx, &len) == 0)
        return static_cast<int>(idx);
    if (family == AF_INET
        && ::getsockopt(static_cast<int>(fd), IPPROTO_IP, IP_BOUND_IF, &idx, &len) == 0)
        return static_cast<int>(idx);
#else
    Q_UNUSED(fd);
#endif
    return 0;
}

} // namespace

bool pinToInterface(qintptr fd, int index, const QString& name)
{
    if (fd < 0 || index <= 0)
        return false;
    // An accepted socket inherits the listener's pin, and a connected socket may
    // refuse the option: already there is as good as set.
    if (pinnedIndex(fd) == index)
        return true;

#if defined(Q_OS_LINUX)
    // Family-independent; a dual-stack socket is confined for both.
    const int idx = index;
#ifdef SO_BINDTOIFINDEX
    if (::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTOIFINDEX, &idx, sizeof(idx)) == 0)
        return true;
#endif
    const QByteArray dev = name.toUtf8();
    return ::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTODEVICE, dev.constData(),
                        static_cast<socklen_t>(dev.size() + 1)) == 0;

#elif defined(Q_OS_DARWIN)
    Q_UNUSED(name);
    // An AF_INET6 socket takes only the v6 option, which also confines v4-mapped traffic.
    const unsigned idx = static_cast<unsigned>(index);
    const int family = socketFamily(fd);
    if (family == AF_INET6)
        return ::setsockopt(static_cast<int>(fd), IPPROTO_IPV6, IPV6_BOUND_IF, &idx, sizeof(idx)) == 0;
    if (family == AF_INET)
        return ::setsockopt(static_cast<int>(fd), IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx)) == 0;
    return false;

#elif defined(Q_OS_WIN)
    Q_UNUSED(name);
    // Steers outgoing packets only. IP_UNICAST_IF wants network byte order,
    // IPV6_UNICAST_IF host byte order.
    const SOCKET s = static_cast<SOCKET>(fd);
    const int family = socketFamily(fd);
    const DWORD v6idx = static_cast<DWORD>(index);
    const DWORD v4idx = htonl(static_cast<DWORD>(index));
    if (family == AF_INET6) {
        if (::setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<const char*>(&v6idx),
                         sizeof(v6idx)) != 0)
            return false;
        // Dual-stack: the v4 half has its own setting; absent on a v6-only socket.
        ::setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&v4idx), sizeof(v4idx));
        return true;
    }
    if (family == AF_INET)
        return ::setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&v4idx),
                            sizeof(v4idx)) == 0;
    return false;

#else
    Q_UNUSED(name);
    return false;   // no pin on this platform: fail closed
#endif
}

bool pin(qintptr fd)
{
    const BindAddress::Resolution r = BindAddress::current();
    if (r.state == BindAddress::State::Unrestricted)
        return true;
    if (r.state == BindAddress::State::Blocked)
        return false;
    if (pinToInterface(fd, r.index, r.name))
        return true;
    logWarning(QStringLiteral("Cannot pin a socket to interface %1 — socket not used").arg(r.name));
    return false;
}

bool prepareOutgoing(QAbstractSocket& socket, bool proxied)
{
    const BindAddress::Resolution r = BindAddress::current();
    if (r.state == BindAddress::State::Unrestricted)
        return true;
    if (r.state == BindAddress::State::Blocked)
        return false;
    if (proxied)
        return true;   // the proxy is the route the user chose; Qt cannot bind one
    if (socket.state() == QAbstractSocket::UnconnectedState) {
        // The destination family is unknown before the lookup: dual-stack wildcard.
        const QHostAddress from = r.literal.isNull() ? QHostAddress(QHostAddress::Any) : r.literal;
        if (!socket.bind(from, 0))
            return false;
    }
    return pin(socket.socketDescriptor());
}

} // namespace eMule::InterfacePin
