#include "pch.h"

/// @file FileTypeIcons.cpp
/// @brief The file-type icon every list column 0 draws beside a name.

#include "utils/FileTypeIcons.h"

#include "utils/OtherFunctions.h"

#include <QHash>

namespace eMule {

QIcon fileTypeIcon(const QString& type)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(type);
    if (it != cache.end())
        return *it;

    QString path;
    if (type == u"Audio")
        path = QStringLiteral(":/icons/FileTypeAudio.ico");
    else if (type == u"Video")
        path = QStringLiteral(":/icons/FileTypeVideo.ico");
    else if (type == u"Image")
        path = QStringLiteral(":/icons/FileTypePicture.ico");
    else if (type == u"Doc")
        path = QStringLiteral(":/icons/FileTypeDocument.ico");
    else if (type == u"Pro")
        path = QStringLiteral(":/icons/FileTypeProgram.ico");
    else if (type == u"Arc")
        path = QStringLiteral(":/icons/FileTypeArchive.ico");
    else if (type == u"Iso")
        path = QStringLiteral(":/icons/FileTypeCDImage.ico");
    else if (type == u"EmuleCollection")
        path = QStringLiteral(":/icons/emuleCollectionFileType.ico");
    else
        path = QStringLiteral(":/icons/FileTypeAny.ico");

    QIcon icon(path);
    cache.insert(type, icon);
    return icon;
}

QIcon fileTypeIconForName(const QString& fileName)
{
    return fileTypeIcon(getFileTypeByName(fileName));
}

} // namespace eMule
