#pragma once

/// @file MediaSniff.h
/// @brief The media type of a file by its first bytes, for ed3k room media (room.md §2.6, §6):
/// the formats a room shows. Never by name or claimed type; SVG never. Header-only: daemon and
/// GUI use it.

#include <QByteArray>
#include <QString>

namespace eMule::MediaSniff {

/// "image/png", "image/jpeg", "image/gif", "image/webp", "video/mp4", "video/webm"; empty: none
[[nodiscard]] inline QString type(const QByteArray& b)
{
    if (b.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral("image/png");
    if (b.startsWith("\xff\xd8\xff"))
        return QStringLiteral("image/jpeg");
    if (b.startsWith("GIF87a") || b.startsWith("GIF89a"))
        return QStringLiteral("image/gif");
    if (b.size() >= 12 && b.startsWith("RIFF") && b.mid(8, 4) == "WEBP")
        return QStringLiteral("image/webp");
    if (b.size() >= 12 && b.mid(4, 4) == "ftyp")
        return QStringLiteral("video/mp4");
    if (b.startsWith("\x1a\x45\xdf\xa3"))
        return QStringLiteral("video/webm");
    return {};
}

[[nodiscard]] inline bool isImage(const QString& type)
{
    return type == u"image/png" || type == u"image/jpeg" || type == u"image/gif" || type == u"image/webp";
}

[[nodiscard]] inline bool isVideo(const QString& type)
{
    return type == u"video/mp4" || type == u"video/webm";
}

} // namespace eMule::MediaSniff
