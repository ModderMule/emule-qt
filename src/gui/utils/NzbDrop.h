#pragma once

/// @file NzbDrop.h
/// @brief What counts as a droppable .nzb, in one place.
///
/// Both UsenetPanel and MainWindow accept drops, and both have to answer the
/// same question about the same QMimeData. Answering it twice is how the window
/// ends up accepting something the panel then refuses.
///
/// It is deliberately narrow: local files ending .nzb, and http(s) URLs whose
/// path does. Everything else falls through untouched, which is what keeps a
/// future `.emulecollection`, `server.met` or `ed2k:` drop free to be handled
/// somewhere else rather than silently swallowed here.

#include <QStringList>

class QMimeData;

namespace eMule::gui {

/// The .nzb-shaped things in a drop, split by how they have to be fetched.
struct NzbDropCandidates {
    /// Absolute paths to local files. Sent to the daemon as *contents*.
    QStringList files;

    /// http(s) URLs. Sent as URLs, because the daemon may be the only host that
    /// can reach them.
    QStringList urls;

    [[nodiscard]] bool isEmpty() const { return files.isEmpty() && urls.isEmpty(); }
};

/// What of @p mime is a .nzb this application should take. Empty means the drop
/// is not ours, and the event must be left alone rather than accepted.
[[nodiscard]] NzbDropCandidates nzbDropCandidates(const QMimeData* mime);

} // namespace eMule::gui
