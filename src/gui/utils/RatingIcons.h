#pragma once

/// @file RatingIcons.h
/// @brief Column-0 marks: eMule's comment/rating icon, and the fake-file mark.
///
/// Two independent signals share the cell. The rating mark is what other users
/// said about the file; the red exclamation is what its own bytes say, and only
/// the first is an opinion the "indicate ratings" preference governs.

// ratingLabel() -- MFC GetRateString in full -- lives in core, where the web UI can
// name the same six values too. Pulled in here so the marks header stays the one
// include a list needs.
#include "utils/OtherFunctions.h"

#include <QIcon>
#include <QString>

namespace eMule {

/// Which rating mark a row shows. Values are MFC's rating numbers so the icon
/// name falls straight out of them; Spam is the search list's substitute, which
/// MFC draws *instead of* the rating (srchybrid/SearchListCtrl.cpp:1276-1278).
enum class FileMark : int {
    None = -1,      ///< nothing to say — draw no mark at all
    NotRated = 0,
    Fake,           ///< MFC's "Invalid / Corrupt / Fake"
    Poor,
    Fair,
    Good,
    Excellent,
    KadSearching,   ///< a Kad note lookup is running: userRating(true) == 6
    Spam,
};

/// The rating as words, matching MFC GetRateString() (OtherFunctions.cpp:787).
/// Empty for 0 and for the out-of-band 6, neither of which names a quality.
/// This is the tooltip form: a row reading "Rating: Not rated" is noise.
[[nodiscard]] QString ratingText(int userRating);

/// The FileRating0-5 art for a rating, for the same two places. Null outside 0-5, or
/// when the user has turned original icons off.
[[nodiscard]] QIcon ratingIcon(int rating);

/// MFC's predicate: ShowRatingIndicator() && (HasComment() || HasRating() ||
/// IsKadCommentSearchRunning()) -- srchybrid/DownloadListCtrl.cpp:407.
/// @p userRating is the wire value, i.e. already 6 while a Kad lookup runs.
/// Returns None when the preference is off, so callers need not check it.
[[nodiscard]] FileMark ratingMark(bool hasComment, int userRating);

/// The whole of column 0: file-type icon, then the red "not what it claims"
/// mark, then the rating mark.
///
/// MFC draws these one after another in DrawFileItem; composing a single pixmap
/// gets the same result through the plain QStyledItemDelegate, which is what the
/// name column uses in every one of our lists. Cached — a few dozen combinations
/// exist at most.
///
/// @p ownComment is *your* comment or rating on the file, which MFC marks quite
/// separately from everyone else's: a small overlay on the type icon rather than a
/// mark of its own (srchybrid/SharedFilesCtrl.cpp:561-562), and shown whether or not
/// the rating indicator is switched on, since your own note is not an opinion the
/// preference governs. Only the shared list has anything to say here — a download or
/// a search hit is not a file you publish.
[[nodiscard]] QIcon fileMarksIcon(const QString& fileType, bool containerSuspect,
                                  bool ownComment, FileMark mark);

/// The lines that explain those marks, for the lists that draw them. Empty when the
/// row has nothing to add, otherwise each line starts with its own newline so a
/// caller can append it straight onto its tooltip.
///
/// One implementation because a mark that means different things in two lists is
/// worse than no mark: the download list and the shared list draw the same cell and
/// must read the same. The container sentence itself comes from core
/// (containerWarningText), which the web listings use too.
[[nodiscard]] QString fileMarksTooltip(const QString& fileName, bool containerSuspect,
                                       const QString& containerActual,
                                       bool hasComment, int userRating);

} // namespace eMule
