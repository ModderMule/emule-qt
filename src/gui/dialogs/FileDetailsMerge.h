#pragma once

/// @file FileDetailsMerge.h
/// @brief The details of several files as one map, for the combined detail sheet.
///
/// MFC's CFileDetailDialog shows a multi-selection in one sheet: what adds up is
/// summed, media fields survive only where all files agree, comments and links are
/// listed for all (FileDetailDialogInfo.cpp:228-327, FileInfoDialog.cpp:535-600,
/// CommentDialogLst.cpp:128-180, ED2kLinkDlg.cpp:122-185).

#include <QCborArray>
#include <QCborMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace eMule {

[[nodiscard]] inline QCborMap mergeFileDetails(const QList<QCborMap>& files)
{
    const auto num = [](const QCborMap& m, const char* key) {
        return m.value(QLatin1StringView(key)).toInteger();
    };
    const auto str = [](const QCborMap& m, const char* key) {
        return m.value(QLatin1StringView(key)).toString();
    };

    QCborMap merged;
    merged.insert(QLatin1StringView("multiCount"), files.size());

    for (const char* key : {"fileSize", "completedSize", "downTransferred", "sourceCount",
                            "transferringSrcCount", "a4afSourceCount", "mediaLength"}) {
        qint64 sum = 0;
        for (const QCborMap& f : files)
            sum += num(f, key);
        merged.insert(QLatin1StringView(key), sum);
    }
    const qint64 size = num(merged, "fileSize");
    merged.insert(QLatin1StringView("percentCompleted"),
                  size > 0 ? 100.0 * static_cast<double>(num(merged, "completedSize"))
                                 / static_cast<double>(size)
                           : 0.0);

    for (const char* key : {"mediaArtist", "mediaAlbum", "mediaTitle", "mediaCodec"}) {
        const QString first = files.isEmpty() ? QString() : str(files.first(), key);
        const bool same = std::ranges::all_of(files, [&](const QCborMap& f) {
            return str(f, key) == first;
        });
        merged.insert(QLatin1StringView(key), same ? first : QString());
    }
    {
        const qint64 first = files.isEmpty() ? 0 : num(files.first(), "mediaBitrate");
        const bool same = std::ranges::all_of(files, [&](const QCborMap& f) {
            return num(f, "mediaBitrate") == first;
        });
        merged.insert(QLatin1StringView("mediaBitrate"), same ? first : qint64{0});
    }

    QCborArray comments;
    QStringList links;
    bool searching = false;
    for (const QCborMap& f : files) {
        for (const QCborValue& c : f.value(QLatin1StringView("comments")).toArray())
            comments.append(c);
        if (const QString link = str(f, "ed2kLink"); !link.isEmpty())
            links << link;
        searching = searching || f.value(QLatin1StringView("notesSearchRunning")).toBool();
    }
    merged.insert(QLatin1StringView("comments"), comments);
    merged.insert(QLatin1StringView("ed2kLink"), links.join(QLatin1Char('\n')));
    merged.insert(QLatin1StringView("notesSearchRunning"), searching);
    merged.insert(QLatin1StringView("canComment"), false);
    return merged;
}

} // namespace eMule
