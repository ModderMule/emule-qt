#pragma once

/// @file FileTypeFilter.h
/// @brief File types of an ed3k set filter (file-set.md §5.3 `exts`): lower case, no dot.
/// Header-only: the core, the ed3k sets and the GUI share it.

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace eMule {

/// "*.mkv", ".MKV" → "mkv"; 1-16 letters or digits, no repeats, at most 64; bad ones dropped.
inline QStringList normalizeFileTypes(const QStringList& exts)
{
    static const QRegularExpression kExt(QStringLiteral("^[a-z0-9]{1,16}$"));
    QStringList out;
    for (QString e : exts) {
        e = e.trimmed().toLower();
        while (e.startsWith(QLatin1Char('*')) || e.startsWith(QLatin1Char('.')))
            e.remove(0, 1);
        if (out.size() < 64 && kExt.match(e).hasMatch() && !out.contains(e))
            out << e;
    }
    return out;
}

/// Whether @p fileName ends in one of @p exts; "rar" also takes .r00-.r99 volumes, "zip",
/// "rar" and "7z" the .001 splits. Empty @p exts: every file.
inline bool fileTypeMatches(const QString& fileName, const QStringList& exts)
{
    if (exts.isEmpty())
        return true;
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return false;
    const QString ext = fileName.mid(dot + 1).toLower();
    if (exts.contains(ext))
        return true;
    // volumes of an archive: x.r00, x.7z.001
    static const QRegularExpression kRarVolume(QStringLiteral("^r\\d\\d$"));
    static const QRegularExpression kSplit(QStringLiteral("^\\d{3}$"));
    if (exts.contains(QLatin1String("rar")) && kRarVolume.match(ext).hasMatch())
        return true;
    return kSplit.match(ext).hasMatch()
        && (exts.contains(QLatin1String("zip")) || exts.contains(QLatin1String("rar")) || exts.contains(QLatin1String("7z")));
}

/// The default of a followed set without the author's types: video, audio, archives, images,
/// subtitles, text; no scripts or programs.
inline QStringList defaultSetFileTypes()
{
    return {
        // video
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("avi"), QStringLiteral("mov"),
        QStringLiteral("wmv"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("m2ts"),
        QStringLiteral("webm"), QStringLiteral("flv"), QStringLiteral("vob"), QStringLiteral("ogv"),
        // audio
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("aac"), QStringLiteral("wav"), QStringLiteral("wma"), QStringLiteral("ape"), QStringLiteral("mka"),
        QStringLiteral("aiff"),
        // archives
        QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("gz"),
        QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("zst"), QStringLiteral("iso"), QStringLiteral("par2"),
        QStringLiteral("sfv"),
        // images
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"),
        // subtitles, text
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"), QStringLiteral("sub"), QStringLiteral("idx"),
        QStringLiteral("txt"), QStringLiteral("nfo"),
    };
}

} // namespace eMule
