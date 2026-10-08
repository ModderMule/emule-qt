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
    const auto text = [](const char* s) {
        return QCoreApplication::translate("eMule::FileType", s);
    };
    QString type = token;
    if (type == QLatin1String("Pro") && !fileName.isEmpty()) {
        const QString byName = getFileTypeByName(fileName);
        if (byName == QLatin1String("Arc") || byName == QLatin1String("Iso"))
            type = byName;
    }
    if (type == QLatin1String("Audio"))           return text("Audio");
    if (type == QLatin1String("Video"))           return text("Video");
    if (type == QLatin1String("Image"))           return text("Picture");
    if (type == QLatin1String("Doc"))             return text("Document");
    if (type == QLatin1String("Pro"))             return text("Program");
    if (type == QLatin1String("Arc"))             return text("Archive");
    if (type == QLatin1String("Iso"))             return text("CD-Image");
    if (type == QLatin1String("EmuleCollection")) return text("Collection");
    return type;
}

} // namespace eMule
