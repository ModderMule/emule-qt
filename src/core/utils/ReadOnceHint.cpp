#include "ReadOnceHint.h"

#include <QFileDevice>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#endif

namespace eMule {

ReadOnceHint::ReadOnceHint(const QFileDevice& file)
{
#if defined(Q_OS_UNIX)
    m_fd = file.handle();
    if (m_fd < 0)
        return;
#if defined(Q_OS_LINUX)
    (void)::posix_fadvise(m_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#elif defined(Q_OS_DARWIN)
    (void)::fcntl(m_fd, F_NOCACHE, 1);
#endif
#else
    Q_UNUSED(file);
#endif
}

ReadOnceHint::~ReadOnceHint()
{
    drop(m_dropped, 0);   // 0: to the end
}

void ReadOnceHint::readUpTo(qint64 pos)
{
    if (pos - m_dropped < kDropStep)
        return;
    drop(m_dropped, pos - m_dropped);
    m_dropped = pos;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void ReadOnceHint::drop(qint64 from, qint64 len)
{
#if defined(Q_OS_LINUX)
    if (m_fd >= 0)
        (void)::posix_fadvise(m_fd, from, len, POSIX_FADV_DONTNEED);
#else
    Q_UNUSED(from);
    Q_UNUSED(len);
#endif
}

} // namespace eMule
