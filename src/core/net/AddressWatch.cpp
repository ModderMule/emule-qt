#include "pch.h"
/// @file AddressWatch.cpp
/// @brief Tells when a local interface address comes or goes, as the OS reports it.

#include "net/AddressWatch.h"
#include "utils/Log.h"

#include <QMetaObject>
#include <QSocketNotifier>

#if defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#if defined(Q_OS_LINUX)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#elif defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
#include <net/if.h>
#include <net/route.h>
#define EMULE_ROUTING_SOCKET 1
#endif
#endif

namespace eMule {

#if defined(Q_OS_WIN)
namespace {

// a system thread: hand over to the owner's
void WINAPI onAddressChange(PVOID context, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE)
{
    static_cast<AddressWatch*>(context)->noteEvent();
}

} // namespace
#endif

AddressWatch::AddressWatch(QObject* parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(kSettleMs);
    connect(&m_settle, &QTimer::timeout, this, &AddressWatch::changed);
    openSource();
}

AddressWatch::~AddressWatch()
{
    closeSource();
}

void AddressWatch::noteEvent()
{
    // the timer belongs to this thread; Windows calls from its own
    QMetaObject::invokeMethod(this, [this] { m_settle.start(); }, Qt::QueuedConnection);
}

void AddressWatch::openSource()
{
#if defined(Q_OS_WIN)
    HANDLE handle = nullptr;
    if (const DWORD rc = ::NotifyUnicastIpAddressChange(AF_UNSPEC, &onAddressChange, this, FALSE, &handle);
        rc != NO_ERROR) {
        logWarning(QStringLiteral("AddressWatch: no address notifications (error %1); polling only").arg(rc));
        return;
    }
    m_handle = handle;
    m_active = true;
#elif defined(Q_OS_LINUX)
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (fd < 0) {
        logWarning(QStringLiteral("AddressWatch: no netlink socket (errno %1); polling only").arg(errno));
        return;
    }
    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        logWarning(QStringLiteral("AddressWatch: netlink bind failed (errno %1); polling only").arg(errno));
        ::close(fd);
        return;
    }
    m_fd = fd;
#elif defined(EMULE_ROUTING_SOCKET)
    const int fd = ::socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
    if (fd < 0) {
        logWarning(QStringLiteral("AddressWatch: no routing socket (errno %1); polling only").arg(errno));
        return;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    m_fd = fd;
#endif
#if !defined(Q_OS_WIN)
    if (m_fd < 0)
        return;
    m_notifier = std::make_unique<QSocketNotifier>(m_fd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &AddressWatch::drain);
    m_active = true;
#endif
}

void AddressWatch::closeSource()
{
#if defined(Q_OS_WIN)
    // waits for a callback in progress
    if (m_handle)
        ::CancelMibChangeNotify2(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
#else
    m_notifier.reset();
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
#endif
    m_active = false;
}

void AddressWatch::drain()
{
#if !defined(Q_OS_WIN)
    // read all, note once: the group filter (Linux) and the types below say enough
    alignas(8) char buf[8192];
    bool relevant = false;
    for (;;) {
        const ssize_t n = ::recv(m_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0)
            break;
#if defined(Q_OS_LINUX)
        int left = static_cast<int>(n);
        for (auto* nh = reinterpret_cast<const nlmsghdr*>(buf); NLMSG_OK(nh, left); nh = NLMSG_NEXT(nh, left)) {
            switch (nh->nlmsg_type) {
            case RTM_NEWADDR:
            case RTM_DELADDR:
            case RTM_NEWLINK:
            case RTM_DELLINK:
                relevant = true;
                break;
            default:
                break;
            }
        }
#elif defined(EMULE_ROUTING_SOCKET)
        // routes come through too (ARP, cloned): only address and link messages count
        if (static_cast<size_t>(n) >= sizeof(rt_msghdr)) {
            const auto* rtm = reinterpret_cast<const rt_msghdr*>(buf);
            if (rtm->rtm_type == RTM_NEWADDR || rtm->rtm_type == RTM_DELADDR || rtm->rtm_type == RTM_IFINFO)
                relevant = true;
        }
#endif
    }
    if (relevant)
        m_settle.start();
#endif
}

} // namespace eMule
