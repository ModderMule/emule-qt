#pragma once

/// @file FileTypeText.h
/// @brief eD2K file type tokens as MFC names them.

#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QString>

namespace eMule {

/// MFC GetFileTypeDisplayStrFromED2KFileType (srchybrid/OtherFunctions.cpp:1639-1662).
/// @p fileName refines "Pro", the type archives and CD images are published under
/// (MFC does that when it reads the result, SearchFile.cpp:265-292). An unknown
/// token is shown as it is.
[[nodiscard]] inline QString fileTypeText(const QString& token, const QString& fileName = {})
{
    // literals marked QT_TRANSLATE_NOOP: lupdate does not look inside this lambda
    const auto text = [](const char* s) {
        return QCoreApplication::translate("eMule::FileType", s);
    };
    QString type = token;
    if (type == QLatin1String("Pro") && !fileName.isEmpty()) {
        const QString byName = getFileTypeByName(fileName);
        if (byName == QLatin1String("Arc") || byName == QLatin1String("Iso"))
            type = byName;
    }
    if (type == QLatin1String("Audio"))           return text(QT_TRANSLATE_NOOP("eMule::FileType", "Audio"));
    if (type == QLatin1String("Video"))           return text(QT_TRANSLATE_NOOP("eMule::FileType", "Video"));
    if (type == QLatin1String("Image"))           return text(QT_TRANSLATE_NOOP("eMule::FileType", "Picture"));
    if (type == QLatin1String("Doc"))             return text(QT_TRANSLATE_NOOP("eMule::FileType", "Document"));
    if (type == QLatin1String("Pro"))             return text(QT_TRANSLATE_NOOP("eMule::FileType", "Program"));
    if (type == QLatin1String("Arc"))             return text(QT_TRANSLATE_NOOP("eMule::FileType", "Archive"));
    if (type == QLatin1String("Iso"))             return text(QT_TRANSLATE_NOOP("eMule::FileType", "CD-Image"));
    if (type == QLatin1String("EmuleCollection")) return text(QT_TRANSLATE_NOOP("eMule::FileType", "Collection"));
    return type;
}

} // namespace eMule
