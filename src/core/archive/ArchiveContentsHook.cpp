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

void readArchiveContents(const QStringList& volumes, const QStringList& passwords)
{
    ArchiveContentsHook hook;
    {
        const QMutexLocker lock(&hookMutex());
        hook = hookSlot();
    }
    if (hook && !volumes.isEmpty())
        hook(volumes, passwords);
}

} // namespace eMule
