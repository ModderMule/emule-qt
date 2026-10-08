#include "pch.h"
/// @file FileDate.cpp
/// @brief Comparing a stored file date with the one on disk.

#include "utils/FileDate.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStorageInfo>

#include <cstdlib>

namespace eMule {

namespace {

constexpr long long kRoundingSecs = 2;
constexpr long long kDstSecs = 3600;

struct ProbeState {
    QMutex mutex;
    std::function<bool(const QString&)> probe;
    QHash<QString, bool> cache;
};

ProbeState& probeState()
{
    static ProbeState state;
    return state;
}

bool probeDisk(const QString& directory)
{
    const QByteArray type = QStorageInfo(directory).fileSystemType().toLower();
    return type.startsWith("fat") || type == "vfat" || type == "msdos" || type == "exfat";
}

} // namespace

bool isLocalTimeVolume(const QString& directory)
{
    ProbeState& state = probeState();
    QMutexLocker locker(&state.mutex);
    if (state.probe)
        return state.probe(directory);
    if (const auto it = state.cache.constFind(directory); it != state.cache.constEnd())
        return *it;
    const bool local = probeDisk(directory);
    state.cache.insert(directory, local);
    return local;
}

void setLocalTimeVolumeProbe(std::function<bool(const QString&)> probe)
{
    ProbeState& state = probeState();
    QMutexLocker locker(&state.mutex);
    state.probe = std::move(probe);
    state.cache.clear();
}

bool sameFileDate(time_t stored, time_t onDisk, bool localTimeVolume)
{
    if (stored == onDisk)
        return true;
    if (!localTimeVolume)
        return false;
    const long long diff = std::llabs(static_cast<long long>(stored) - static_cast<long long>(onDisk));
    return diff <= kRoundingSecs || std::llabs(diff - kDstSecs) <= kRoundingSecs;
}

} // namespace eMule
