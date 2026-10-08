#pragma once

/// @file CategoryView.h
/// @brief A category's view filter: which downloads its tab shows.
///
/// Used by the GUI's CategoryFilterProxy for the list, and by the download queue for
/// what a bulk action on the "All" tab covers (MFC applies the same test in both:
/// CPartFile::CheckShowItemInGivenCat). Header-only so GUI tests need no core source.

#include "prefs/DownloadCategory.h"
#include "utils/OtherFunctions.h"

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace eMule {

/// What a category's view filter needs to know about a download.
struct CategoryRowFacts {
    enum State { Other, Waiting, Transferring, Erroneous, Paused };

    int category = 0;
    QString fileName;
    bool unfinished = true;      ///< MFC IsPartFile: not completed yet
    State state = Other;
    bool seenComplete = false;   ///< every part had a source at some time
};

/// MFC's view filter modes (srchybrid/TransferWnd.cpp:706-748).
namespace CategoryViewFilter {
enum : int {
    All = 0, Uncategorized = 1, Incomplete = 2, Completed = 3, Waiting = 4, Downloading = 5,
    Erroneous = 6, Paused = 7, SeenComplete = 8,
    Video = 10, Audio = 11, Archive = 12, CDImage = 13, Document = 14, Picture = 15,
    Program = 16, RegExp = 18, Collection = 20
};
}

/// Whether a download belongs on the tab of category @p inCategory — port of MFC
/// CPartFile::CheckShowItemInGivenCat (srchybrid/PartFile.cpp:5055-5122).
[[nodiscard]] inline bool categoryShowsRow(const QList<DownloadCategory>& categories,
                                           int inCategory, const CategoryRowFacts& row)
{
    namespace F = CategoryViewFilter;   // not `using`: Collection is a class too

    // No list yet (or a tab past its end): membership alone
    if (inCategory < 0 || inCategory >= categories.size())
        return inCategory == 0 || row.category == inCategory;

    const DownloadCategory& cat = categories.at(inCategory);
    const int filter = cat.filter;
    if (row.category == inCategory && filter == F::All)
        return true;
    if (inCategory > 0 && row.category != inCategory && !cat.care4all)
        return false;

    bool shown = filter <= F::All;
    // The status modes say nothing about a finished file
    if (!shown && (filter < F::Waiting || filter > F::SeenComplete || row.unfinished)) {
        const auto typeIs = [&row](ED2KFileType type) {
            return getED2KFileTypeID(row.fileName) == type;
        };
        switch (filter) {
        case F::Uncategorized: shown = row.category == 0; break;
        case F::Incomplete:    shown = row.unfinished; break;
        case F::Completed:     shown = !row.unfinished; break;
        case F::Waiting:       shown = row.state == CategoryRowFacts::Waiting; break;
        case F::Downloading:   shown = row.state == CategoryRowFacts::Transferring; break;
        case F::Erroneous:     shown = row.state == CategoryRowFacts::Erroneous; break;
        case F::Paused:        shown = row.state == CategoryRowFacts::Paused; break;
        case F::SeenComplete:  shown = row.seenComplete; break;
        case F::Video:         shown = typeIs(ED2KFileType::Video); break;
        case F::Audio:         shown = typeIs(ED2KFileType::Audio); break;
        case F::Archive:       shown = typeIs(ED2KFileType::Archive); break;
        case F::CDImage:       shown = typeIs(ED2KFileType::CDImage); break;
        case F::Document:      shown = typeIs(ED2KFileType::Document); break;
        case F::Picture:       shown = typeIs(ED2KFileType::Image); break;
        case F::Program:       shown = typeIs(ED2KFileType::Program); break;
        case F::Collection:    shown = typeIs(ED2KFileType::EmuleCollection); break;
        case F::RegExp: {
            // The whole name, case as written (MFC RegularExpressionMatch: regex_match)
            const QRegularExpression re(QRegularExpression::anchoredPattern(cat.regexp));
            shown = re.isValid() && re.match(row.fileName).hasMatch();
            break;
        }
        default: break;
        }
    }
    return cat.filterNeg ? !shown : shown;
}

} // namespace eMule
