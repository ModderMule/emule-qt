#pragma once

/// @file VersionedRecordFile.h
/// @brief Base for small stores kept on disk as a version byte plus a body.

#include "utils/Types.h"

#include <QString>

#include <functional>

namespace eMule {

class SafeFile;

/// Load/save scaffolding shared by the small record stores (kadpublish.dat, kadbuddies.dat):
/// a leading `u8` version, written to `.tmp` and committed with commitAndReplace(), which
/// keeps the previous file as `.bak`.
class VersionedRecordFile {
protected:
    /// Open @p path, check the version byte, hand the rest to @p readBody. False for a
    /// missing or foreign file, or when @p readBody throws (logged as a warning). Parse
    /// into locals and keep them only on true, so nothing half-read survives.
    static bool readVersioned(const QString& path, uint8 version,
                              const std::function<void(SafeFile&)>& readBody);

    /// Write the version byte and @p writeBody to `path.tmp`, then commit over @p path.
    /// False on any failure (logged); the previous file stays in place.
    static bool writeVersioned(const QString& path, uint8 version,
                               const std::function<void(SafeFile&)>& writeBody);
};

} // namespace eMule
