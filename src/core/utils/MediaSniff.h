#pragma once

/// @file MediaSniff.h
/// @brief The media type of a file by its first bytes, for ed3k room media (room.md §2.6, §6):
/// the formats a room shows. Never by name or claimed type; SVG never. Header-only: daemon and
/// GUI use it.
///
/// Other small files (room info `files`): text, NZB, DLC, torrent only, by name and bytes both.

#include <QByteArray>
#include <QString>
#include <QStringDecoder>

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

/// image/* or video/* as room.md §2.6 reads a claimed type; else a file
[[nodiscard]] inline bool isMediaType(const QString& type)
{
    const auto starts = [&](QLatin1StringView p) { return type.size() > p.size() && type.startsWith(p, Qt::CaseInsensitive); };
    return starts(QLatin1StringView("image/")) || starts(QLatin1StringView("video/"));
}

/// Other small files a room may carry; nothing else is sent or opened.
enum class FileKind { None, Text, Nzb, Dlc, Torrent };

/// UTF-8 without NUL or control characters but tab, line ends, form feed
[[nodiscard]] inline bool isPlainText(const QByteArray& b)
{
    for (const char c : b) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 && u != '\t' && u != '\n' && u != '\r' && u != '\f')
            return false;
        if (u == 0x7f)
            return false;
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    [[maybe_unused]] const QString text = utf8.decode(b);
    return !utf8.hasError();
}

/// By the name's suffix, checked by the bytes; None: not one we send or open
[[nodiscard]] inline FileKind fileKind(const QByteArray& b, const QString& name)
{
    if (b.isEmpty() || !type(b).isEmpty())
        return FileKind::None;
    if (name.endsWith(u".txt", Qt::CaseInsensitive))
        return isPlainText(b) ? FileKind::Text : FileKind::None;
    if (name.endsWith(u".nzb", Qt::CaseInsensitive))
        return isPlainText(b) && b.left(64 * 1024).contains("<nzb") ? FileKind::Nzb : FileKind::None;
    if (name.endsWith(u".dlc", Qt::CaseInsensitive)) {
        // base64 text: the data and an 88-character key
        if (b.size() < 100)
            return FileKind::None;
        for (const char c : b) {
            const bool b64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
            if (!b64 && c != '\r' && c != '\n')
                return FileKind::None;
        }
        return FileKind::Dlc;
    }
    if (name.endsWith(u".torrent", Qt::CaseInsensitive))
        // bencoded dictionary with an info dictionary
        return b.startsWith('d') && b.endsWith('e') && b.contains("4:infod") ? FileKind::Torrent : FileKind::None;
    return FileKind::None;
}

/// The claimed type of a kind
[[nodiscard]] inline QString fileType(FileKind k)
{
    switch (k) {
    case FileKind::Text: return QStringLiteral("text/plain");
    case FileKind::Nzb: return QStringLiteral("application/x-nzb");
    case FileKind::Dlc: return QStringLiteral("application/x-dlc");
    case FileKind::Torrent: return QStringLiteral("application/x-bittorrent");
    case FileKind::None: break;
    }
    return {};
}

/// What a post claims (type, else the name's suffix), before the bytes are here
[[nodiscard]] inline FileKind claimedKind(const QString& type, const QString& name)
{
    if (type == u"text/plain" || (type.isEmpty() && name.endsWith(u".txt", Qt::CaseInsensitive)))
        return FileKind::Text;
    if (type == u"application/x-nzb" || (type.isEmpty() && name.endsWith(u".nzb", Qt::CaseInsensitive)))
        return FileKind::Nzb;
    if (type == u"application/x-dlc" || (type.isEmpty() && name.endsWith(u".dlc", Qt::CaseInsensitive)))
        return FileKind::Dlc;
    if (type == u"application/x-bittorrent" || (type.isEmpty() && name.endsWith(u".torrent", Qt::CaseInsensitive)))
        return FileKind::Torrent;
    return FileKind::None;
}

} // namespace eMule::MediaSniff
