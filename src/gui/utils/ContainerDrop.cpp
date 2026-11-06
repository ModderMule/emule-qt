/// @file ContainerDrop.cpp
/// @brief Link container files — implementation.

#include "utils/ContainerDrop.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

namespace eMule::gui {

bool isContainerFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QLatin1String("dlc"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("rsdf"), Qt::CaseInsensitive) == 0
           || suffix.compare(QLatin1String("ccf"), Qt::CaseInsensitive) == 0;
}

QStringList containerDropFiles(const QMimeData* mime)
{
    QStringList out;
    if (mime == nullptr || !mime->hasUrls())
        return out;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (isContainerFilePath(path) && QFileInfo(path).isFile())
            out << path;
    }
    return out;
}

QString containerFileFilter()
{
    return QCoreApplication::translate("ContainerDrop", "Link containers (*.dlc *.rsdf *.ccf)");
}

} // namespace eMule::gui
