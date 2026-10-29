#include "pch.h"
/// @file InterfacePin.cpp
/// @brief Confine a socket to the bound network interface.

#include "net/InterfacePin.h"
#include "utils/Log.h"

#include <QAbstractSocket>

#include <mutex>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace eMule::InterfacePin {

namespace {

int lastError()
{
#ifdef Q_OS_WIN
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

#ifdef Q_OS_WIN
constexpr int kNoFamily = WSAEAFNOSUPPORT;
#else
constexpr int kNoFamily = EAFNOSUPPORT;
#endif

// AF_INET / AF_INET6 of the descriptor, 0 when unknown.
int socketFamily(qintptr fd)
{
    sockaddr_storage ss{};
#ifdef Q_OS_WIN
    int len = sizeof(ss);
    if (::getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&ss), &len) == 0)
        return ss.ss_family;
    // Unbound: getsockname fails with WSAEINVAL, the protocol info still knows
    WSAPROTOCOL_INFOW info{};
    int infoLen = sizeof(info);
    if (::getsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_PROTOCOL_INFOW,
                     reinterpret_cast<char*>(&info), &infoLen) == 0)
        return info.iAddressFamily;
    return 0;
#else
    socklen_t len = sizeof(ss);
    if (::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return 0;
    return ss.ss_family;
#endif
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

// Set the pin on @p fd of @p family. 0 on success, else the OS error.
int applyPin(qintptr fd, int family, int index, const QString& name)
{
#if defined(Q_OS_LINUX)
    // Family-independent; a dual-stack socket is confined for both.
    Q_UNUSED(family);
    const int idx = index;
#ifdef SO_BINDTOIFINDEX
    if (::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTOIFINDEX, &idx, sizeof(idx)) == 0)
        return 0;
#endif
    const QByteArray dev = name.toUtf8();
    if (::setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTODEVICE, dev.constData(),
                     static_cast<socklen_t>(dev.size() + 1)) == 0)
        return 0;
    return lastError();

#elif defined(Q_OS_DARWIN)
    Q_UNUSED(name);
    // An AF_INET6 socket takes only the v6 option, which also confines v4-mapped traffic.
    const unsigned idx = static_cast<unsigned>(index);
    int rc = -1;
    if (family == AF_INET6)
        rc = ::setsockopt(static_cast<int>(fd), IPPROTO_IPV6, IPV6_BOUND_IF, &idx, sizeof(idx));
    else if (family == AF_INET)
        rc = ::setsockopt(static_cast<int>(fd), IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx));
    else
        return kNoFamily;
    return rc == 0 ? 0 : lastError();

#elif defined(Q_OS_WIN)
    Q_UNUSED(name);
    // Steers outgoing packets only. IP_UNICAST_IF wants network byte order,
    // IPV6_UNICAST_IF host byte order.
    const SOCKET s = static_cast<SOCKET>(fd);
    const DWORD v6idx = static_cast<DWORD>(index);
    const DWORD v4idx = htonl(static_cast<DWORD>(index));
    if (family == AF_INET6) {
        // Fails on an adapter without IPv6; the probe then keeps sockets IPv4-only
        if (::setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<const char*>(&v6idx),
                         sizeof(v6idx)) != 0)
            return lastError();
        // Dual-stack: the v4 half has its own setting; absent on a v6-only socket.
        ::setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&v4idx), sizeof(v4idx));
        return 0;
    }
    if (family == AF_INET)
        return ::setsockopt(s, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&v4idx),
                            sizeof(v4idx)) == 0 ? 0 : lastError();
    return kNoFamily;

#else
    Q_UNUSED(fd);
    Q_UNUSED(family);
    Q_UNUSED(index);
    Q_UNUSED(name);
    return kNoFamily;   // no pin on this platform: fail closed
#endif
}

// Pin a fresh, unbound UDP socket of @p family. 0 on success, else the OS error.
int probeFamily(int family, int index, const QString& name)
{
#ifdef Q_OS_WIN
    const SOCKET s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return lastError();
    const qintptr fd = static_cast<qintptr>(s);
#else
    const int s = ::socket(family, SOCK_DGRAM, 0);
    if (s < 0)
        return lastError();
    const qintptr fd = s;
#endif
    if (family == AF_INET6) {
        // the v6 option alone, as an IPv6-only socket would carry it
        const int on = 1;
        ::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&on), sizeof(on));
    }
    const int error = applyPin(fd, family, index, name);
#ifdef Q_OS_WIN
    ::closesocket(s);
#else
    ::close(s);
#endif
    return error;
}

// One warning per interface and error, not one per socket.
void warnOnce(const QString& name, int error)
{
    static std::mutex lock;
    static QString warned;
    const QString key = QStringLiteral("%1/%2").arg(name).arg(error);
    {
        std::lock_guard guard(lock);
        if (warned == key)
            return;
        warned = key;
    }
    logWarning(QStringLiteral("Cannot pin a socket to interface %1 (error %2) — socket not used")
                   .arg(name).arg(error));
}

} // namespace

bool pinToInterface(qintptr fd, int index, const QString& name, int* error)
{
    int err = 0;
    if (fd < 0 || index <= 0) {
        err = kNoFamily;
    } else if (pinnedIndex(fd) == index) {
        // An accepted socket inherits the listener's pin, and a connected socket may
        // refuse the option: already there is as good as set.
        return true;
    } else {
        err = applyPin(fd, socketFamily(fd), index, name);
    }
    if (error)
        *error = err;
    return err == 0;
}

BindAddress::PinProbe probe(int index, const QString& name)
{
    BindAddress::PinProbe p;
    p.v4Error = probeFamily(AF_INET, index, name);
    p.v6Error = probeFamily(AF_INET6, index, name);
    p.v4 = p.v4Error == 0;
    p.v6 = p.v6Error == 0;
    return p;
}

bool pin(qintptr fd)
{
    const BindAddress::Resolution r = BindAddress::current();
    if (r.state == BindAddress::State::Unrestricted)
        return true;
    if (r.state == BindAddress::State::Blocked)
        return false;
    int error = 0;
    if (pinToInterface(fd, r.index, r.name, &error))
        return true;
    warnOnce(r.name, error);
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
        // The destination family is unknown before the lookup: the wildcard of the
        // families that can be pinned.
        const auto from = BindAddress::listenAddress();
        if (!from || !socket.bind(*from, 0))
            return false;
    }
    return pin(socket.socketDescriptor());
}

} // namespace eMule::InterfacePin
