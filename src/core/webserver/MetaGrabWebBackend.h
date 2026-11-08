#pragma once

/// @file MetaGrabWebBackend.h
/// @brief Grab a meta row (Usenet, torrent, web) of a web-page search.
///
/// The metafile lives behind the answering server's Meta API, which only the
/// daemon talks to (MetaSearchService); DaemonApp installs the implementation.
/// No REST routes use it.

#include "utils/Types.h"

#include <QList>
#include <QString>

#include <array>
#include <functional>

namespace eMule {

class MetaGrabWebBackend {
public:
    virtual ~MetaGrabWebBackend() = default;

    /// Fetch the metafile of @p rows (ticked rows of one release, the first fetched) and
    /// hand it to its engine: with the selected-file-only prefs, file rows keep their own
    /// files. A container's links go to Hoster. @p done runs once, later.
    virtual void grab(uint32 searchID, const QList<std::array<uint8, 16>>& rows,
                      std::function<void(bool ok, const QString& message)> done) = 0;
};

} // namespace eMule
