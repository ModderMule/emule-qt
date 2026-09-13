#pragma once

/// @file IndexerFeed.h
/// @brief One saved feed: a query or an RSS URL, polled on a schedule.
///
/// Lives in core/prefs beside IndexerConfig and for the same reason — it is a
/// stored preference first, and a URL feed carries an API key that Preferences
/// owns the encryption of. src/indexer/IndexerFeed.h aliases it into
/// eMule::indexer, where the polling and the matching live.
///
/// A feed is the first thing in this application that spends money unattended,
/// so two of these fields exist purely to bound that: `intervalMinutes` has a
/// floor no dialog can undercut, and `grabExisting` is off, which is what makes
/// a new feed's first poll record what it saw and add nothing.

#include "prefs/DownloadCategory.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtTypes>

namespace eMule {

/// Where a feed's rows come from.
enum class IndexerFeedKind : quint8 {
    /// A stored query run against the configured indexer accounts. The API key
    /// stays where it already is, encrypted, and never reaches the GUI.
    SavedSearch = 0,

    /// An RSS link the user copied off an indexer's own website. Convenient, and
    /// the reason `url` is treated as a credential: that link embeds the key.
    Url = 1,
};

[[nodiscard]] QString indexerFeedKindToString(IndexerFeedKind kind);
[[nodiscard]] IndexerFeedKind indexerFeedKindFromString(const QString& text);

/// A saved feed. Plain value type; `name` is the identity.
struct IndexerFeed {
    /// Display name, and the identity: it keys the seen-set sidecar and is what
    /// PollIndexerFeedNow addresses. Must be unique and non-empty.
    QString name;

    IndexerFeedKind kind = IndexerFeedKind::SavedSearch;

    /// Skipped by the poller when false, without losing its history.
    bool enabled = true;

    // -- SavedSearch --------------------------------------------------------

    /// Keywords. Empty is legitimate: with categories set it means "everything
    /// new in these categories", which is how a whole-category feed is written.
    QString query;

    /// Newznab category ids. Empty means every category.
    QList<int> categories;

    /// Account names to ask. Empty means every enabled account that serves
    /// Usenet — which is also why adding an account later is a change the
    /// poller has to notice; see IndexerFeedStore's seededAccounts.
    QStringList indexers;

    // -- Url ----------------------------------------------------------------

    /// The feed URL, plaintext in memory and `urlEnc` in the YAML.
    ///
    /// Held to the same rules as IndexerConfig::apiKey because it contains one:
    /// newznab's RSS endpoint takes the key as `apikey=` or `r=`, so this string
    /// is a credential that happens to look like a URL. It is redacted before it
    /// reaches a log line or the GUI.
    QString url;

    // -- Filters ------------------------------------------------------------
    //
    // All optional, all applied to a row the feed has not acted on before.
    // Filtering costs no network, so a row that fails one is simply not
    // recorded — which is what lets a relaxed filter pick up whatever the
    // indexer still lists.

    /// Regex the title must match. Empty accepts everything.
    QString accept;

    /// Regex the title must not match. Wins over `accept`.
    QString reject;

    /// Bounds in bytes, inclusive. 0 for `maxSize` means no ceiling. A row whose
    /// indexer reported no size is not filtered by either — see feedAccepts().
    qint64 minSize = 0;
    qint64 maxSize = 0;

    /// Ceiling on a row's age in days. 0 means no limit, and a row with no date
    /// is not filtered by it.
    int maxAgeDays = 0;

    // -- Schedule -----------------------------------------------------------

    /// Minutes between polls, clamped to kMinIntervalMinutes by
    /// Preferences::setIndexerFeeds() rather than only by the dialog: a
    /// hand-edited file naming one minute would get the user's account banned.
    int intervalMinutes = 30;

    /// Act on the very first poll instead of only recording what it returned.
    ///
    /// Off by default, and that default is the whole safety argument: to a feed
    /// that has never run, every release the indexer still holds is new, so a
    /// first poll that acts downloads the entire retention window for the query.
    bool grabExisting = false;

    // -- Destination ---------------------------------------------------------

    /// Index into `Preferences::categories()` for everything this feed queues,
    /// 0 being the implicit "All".
    ///
    /// Named in full because `categories` two fields up is the *newznab* id list
    /// this feed searches, and a `category:` sitting beside a `categories:` in
    /// the same YAML map would read as its singular.
    ///
    /// Deliberately **not** clamped against `categoryCount()` by
    /// `Preferences::sanitizeFeeds()`: that would make a feed's category depend
    /// on the order in which `load()` reads its blocks, and a load-order slip
    /// would silently reset every feed to "All". An index the list no longer
    /// holds is already harmless where it is used --
    /// `incomingDirForCategory()` falls back -- and `SetCategories` is what
    /// keeps it correct, because a *feed* pointing at a deleted category costs
    /// every release it ever matches, not just one.
    int downloadCategory = 0;

    /// Minutes. Public newznab terms commonly cap RSS at one request per quarter
    /// hour, and a feed is not a search — nothing is lost by being slow.
    static constexpr int kMinIntervalMinutes = 15;

    /// Identity, case-insensitive so a rename that only changes case keeps the
    /// feed's history.
    [[nodiscard]] QString key() const { return name.trimmed().toCaseFolded(); }

    /// Filename-safe form of the name, for the seen-set sidecar.
    [[nodiscard]] QString slug() const;

    [[nodiscard]] bool isValid() const;

    /// Whether this feed has anything to ask. A saved search always does; a URL
    /// feed needs a usable http(s) URL.
    [[nodiscard]] QUrl feedUrl() const;
};

/// Renumber every feed's download category after the list was edited.
/// Returns whether anything changed. See `remapCategoryIndex()`.
///
/// A feed is the worst of the four stores to get this wrong on: a queue item
/// pointing at a deleted category costs one release in the wrong folder, a feed
/// pointing at one costs **every release it ever matches**, for as long as it
/// runs.
[[nodiscard]] bool remapFeedCategories(QList<IndexerFeed>& feeds,
                                       const QHash<uint32, uint32>& oldToNew);

} // namespace eMule
