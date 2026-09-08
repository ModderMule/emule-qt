/// @file NzbDrop.cpp
/// @brief What counts as a droppable .nzb — implementation.

#include "utils/NzbDrop.h"

#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

namespace eMule::gui {

namespace {

bool looksLikeNzb(const QString& path)
{
    return path.endsWith(QLatin1String(".nzb"), Qt::CaseInsensitive);
}

} // namespace

NzbDropCandidates nzbDropCandidates(const QMimeData* mime)
{
    NzbDropCandidates out;
    if (mime == nullptr || !mime->hasUrls())
        return out;

    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            // Existence matters here and not for a remote URL: a local path that
            // is not there cannot be fetched later, and accepting the drop would
            // promise something the panel then has to refuse with a dialog.
            if (looksLikeNzb(path) && QFileInfo(path).isFile())
                out.files.append(path);
            continue;
        }

        const QString scheme = url.scheme().toLower();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
            continue;

        // The *path* has to end in .nzb, not the whole URL: an indexer's download
        // link carries a query string after it.
        if (looksLikeNzb(url.path()))
            out.urls.append(url.toString());
    }

    return out;
}

} // namespace eMule::gui
