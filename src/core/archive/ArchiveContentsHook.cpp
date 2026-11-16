#include "pch.h"

#include "archive/ArchiveContentsHook.h"

#include <QMutex>

namespace eMule {

namespace {

QMutex& hookMutex()
{
    static QMutex m;
    return m;
}

ArchiveContentsHook& hookSlot()
{
    static ArchiveContentsHook h;
    return h;
}

} // namespace

void setArchiveContentsHook(ArchiveContentsHook hook)
{
    const QMutexLocker lock(&hookMutex());
    hookSlot() = std::move(hook);
}

std::shared_ptr<ArchiveContentsTap> archiveContentsTap()
{
    ArchiveContentsHook hook;
    {
        const QMutexLocker lock(&hookMutex());
        hook = hookSlot();
    }
    if (!hook)
        return {};
    return std::shared_ptr<ArchiveContentsTap>(hook());
}

} // namespace eMule
