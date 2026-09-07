#pragma once

/// @file DownloadCategory.h
/// @brief One download category — MFC's `Category_Struct`.
///
/// Lives in core/prefs for the same reason NewsServer and HttpCacheServerConfig
/// do: it is a stored preference first and a domain object second. The file
/// format, the path validation and the "which index is which" rules are all
/// Preferences' business, and the download queue only ever asks it questions.
///
/// Ported from `srchybrid/Preferences.h:114-128`. Field-for-field, including
/// the four members this port persists but does not yet consult (`filter`,
/// `filterNeg`, `care4all`, `downloadInAlphabeticalOrder`) — writing them from
/// the start means the per-category *view filter* can land later without a
/// preferences.yml migration.
///
/// **Identity is the list index**, as in MFC. `part.met` stores `FT_CATEGORY`
/// as an index (`PartFile.cpp` load/save), so removing or reordering a category
/// renumbers every download that pointed past it — see
/// `DownloadQueue::resetCatParts()` / `moveCat()`. Index 0 is the implicit
/// "All" category: it always exists and its `incomingPath` is always empty,
/// which is what makes it resolve to the global incoming dir.

#include <QString>
#include <QtTypes>

namespace eMule {

/// "No colour chosen — use the system default text colour."
///
/// MFC's `CLR_NONE`, and `CPreferences::GetCatColor` reads it exactly this way
/// (`srchybrid/Preferences.cpp:2554-2563`). Kept as the same bit pattern
/// because `Category.ini` round-trips it through a signed int as -1, and a
/// user importing one should not silently acquire a white category.
inline constexpr quint32 kCategoryColorAuto = 0xFFFFFFFFu;

/// Cap on the category list. MFC has none; a bound keeps a hand-edited or
/// hostile preferences.yml from turning every `shouldBeShared()` call — which
/// runs once per file per share scan — into an unbounded loop.
inline constexpr int kMaxCategories = 64;

/// A download category. Plain value type — copied freely, no identity of its
/// own beyond its position in `Preferences::categories()`.
struct DownloadCategory {
    QString title;

    /// Where this category's finished downloads land, and a permanently shared
    /// directory while it is set. Empty means "the global incoming dir" — which
    /// is always the case for index 0, and is also what an invalid path falls
    /// back to. Never read directly: ask `Preferences::incomingDirForCategory()`.
    QString incomingPath;

    QString comment;

    /// Auto-categorisation pattern: `'|'`-separated substrings (wildcards `*`
    /// and `?` allowed), or one regular expression when `autocatIsRegexp`.
    /// Applied to new downloads only — see `DownloadQueue::applyAutoCategory()`.
    QString autocat;

    /// The view filter's regular expression (MFC filter mode 18). Persisted so
    /// the format is stable; nothing consults it yet.
    QString regexp;

    quint32 color = kCategoryColorAuto;

    /// MFC's `a4afPriority`. Ranks which paused file resumes first
    /// (`PartFile::rightFileHasHigherPrio`), *not* the download priority of the
    /// files in it. Values are the `kPr*` constants from KnownFile.h; the
    /// literal avoids dragging that header into Preferences.h.
    quint8 prio = 1; ///< kPrNormal

    // -- Persisted, not yet consulted -----------------------------------------
    // MFC's per-category view filter (0 = none, 1 = uncategorised, 2..16 status
    // and file-type modes, 18 = regexp) and its two modifiers.
    int  filter = 0;
    bool filterNeg = false;
    bool care4all = false;
    bool downloadInAlphabeticalOrder = false;

    bool autocatIsRegexp = false;

    /// Whether this entry is worth keeping. A category with no title is a
    /// tab the user cannot identify, which is the one hard requirement;
    /// everything else has a working default.
    [[nodiscard]] bool isValid() const { return !title.isEmpty(); }

    /// What to show when the user has not named the category. MFC seeds a new
    /// one with the literal "?" (`srchybrid/TransferWnd.cpp:1117`) and lets the
    /// dialog overwrite it; this is the display-time equivalent.
    [[nodiscard]] QString displayName() const
    {
        return title.isEmpty() ? QStringLiteral("?") : title;
    }
};

} // namespace eMule
