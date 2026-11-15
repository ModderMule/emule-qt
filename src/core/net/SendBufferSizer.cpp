#include "pch.h"
/// @file SendBufferSizer.cpp
/// @brief Grow a socket's SO_SNDBUF with its upload rate.

#include "net/SendBufferSizer.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace eMule {

namespace {

#ifdef Q_OS_WIN
using OptLen = int;
using Native = SOCKET;
#else
using OptLen = socklen_t;
using Native = int;
#endif

int currentSize(Native sock)
{
    int got = 0;
    OptLen len = sizeof(got);
    return getsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&got), &len) == 0 ? got : 0;
}

} // namespace

void SendBufferSizer::noteResult(qint64 want, qint64 got)
{
    m_asked = std::max(m_asked, want);
    m_done = got < want;
}

qint64 SendBufferSizer::grow(qintptr fd, qint64 want)
{
    if (fd == -1)
        return 0;
    m_asked = std::max(m_asked, want);
    const auto sock = static_cast<Native>(fd);
    // Linux autotunes TCP past 1 MiB, and a fixed SO_SNDBUF turns that off: never
    // shrink, and leave an autotuned buffer to the kernel from then on
    // (Linux reports twice what we set, so ours can read as big enough too)
    if (const int now = currentSize(sock); now >= want) {
        m_done = !m_fixed;
        return now;
    }
    // Over the system cap (macOS kern.ipc.maxsockbuf) the call fails: halve until it fits
    int optval = static_cast<int>(want);
    while (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&optval), sizeof(optval)) != 0
           && optval > kStep)
        optval /= 2;
    m_fixed = true;
    // The kernel may give less without saying so (Linux net.core.wmem_max): read it back
    const int got = currentSize(sock);
    m_done = got < want;
    return got;
}

} // namespace eMule
