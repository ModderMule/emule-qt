#pragma once

/// @file FileTypeIcons.h
/// @brief The file-type icon every list column 0 draws beside a name.

#include <QIcon>
#include <QString>

namespace eMule {

/// Icon for an ED2K file-type *string* — the form `getFileTypeByName()` returns
/// ("Audio", "Video", "Doc", "Arc", …), not the enum. Anything unrecognised
/// falls back to the generic icon, so a caller need not filter first. Cached.
[[nodiscard]] QIcon fileTypeIcon(const QString& type);

/// Icon for a filename, resolving its type first. The convenient form when the
/// caller holds a name rather than a type — an archive's inner file, say.
[[nodiscard]] QIcon fileTypeIconForName(const QString& fileName);

} // namespace eMule
