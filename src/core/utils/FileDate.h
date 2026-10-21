#pragma once

/// @file FileDate.h
/// @brief Comparing a stored file date with the one on disk.
///
/// FAT-family volumes keep local time in 2 s steps. The OS turns it into UTC
/// with the bias of the moment, so every date moves by an hour at a DST switch
/// and a date read from the cache can differ from the one read after a remount.
/// Neither is a changed file, and neither is worth a rehash (MFC has
/// AdjustNTFSDaylightFileTime for the same reason, srchybrid/OtherFunctions.cpp:2823).
/// That MFC switch, on by default in MorphXT and eMule before 0.50b, stored NTFS
/// dates an hour off while DST was active, so an exact hour counts on every volume.

#include <QString>

#include <ctime>
#include <functional>

namespace eMule {

/// Is @p directory on a volume that stores local time (FAT, exFAT)? Cached.
[[nodiscard]] bool isLocalTimeVolume(const QString& directory);

/// Tests: answer isLocalTimeVolume() from here instead of the disk. Empty restores it.
void setLocalTimeVolumeProbe(std::function<bool(const QString&)> probe);

/// Same file date? Exact or exactly one DST hour apart everywhere; on a
/// local-time volume a rounding step either side of those counts too.
[[nodiscard]] bool sameFileDate(time_t stored, time_t onDisk, bool localTimeVolume);

} // namespace eMule
