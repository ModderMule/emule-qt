#pragma once

/// @file UsenetQueueModel.h
/// @brief Two-level model for the Usenet queue: NZB -> files.
///
/// Same parent/child encoding as DownloadListModel, and for the same reason: an
/// `internalId` of 0 marks a top-level row, and `parentRow + 1` marks a child.
/// Because that encoding stores the parent's *row index*, a removal has to run
/// bottom-up or every surviving child's id points at the wrong parent.
///
/// Updates are incremental rather than a reset. beginResetModel() destroys the
/// view's selection, expansion and scroll position, and this list refreshes
/// roughly once a second — resetting would make a queue impossible to interact
/// with while it runs.

#include "controls/CategoryFilterProxy.h"

#include <QAbstractItemModel>

#include <array>
#include <QCborMap>
#include <QList>
#include <QString>

#include <vector>

namespace eMule {

/// Mirrors usenet::UsenetItemStatus. Duplicated rather than included because the
/// GUI links eMule::Core and eMule::Ipc only — never eMule::Usenet — so it reads
/// these off the wire as plain ints.
enum class UsenetRowStatus : int {
    Queued = 0,
    Downloading = 1,
    Paused = 2,
    Complete = 3,
    Failed = 4,

    // Post-processing (phase 4). The daemon also sends `statusText`, so the
    // Status column reads correctly without this enum; what needs the values is
    // the colouring and whether Pause/Resume apply.
    Verifying = 5,
    Repairing = 6,
    Unpacking = 7,

    /// Asking the servers whether they still hold the release, before anything
    /// is spent on it. Not a download: an item here is deliberately not counted
    /// as active by the daemon's bandwidth split either.
    Checking = 8,
};

/// Whether the item is in the post-processing pipeline rather than downloading.
[[nodiscard]] inline bool isPostProcessing(UsenetRowStatus s)
{
    return s == UsenetRowStatus::Verifying || s == UsenetRowStatus::Repairing
        || s == UsenetRowStatus::Unpacking;
}

struct UsenetFileRow {
    QString name;
    qint64 size = 0;
    int percent = 0;
    QString finalPath;
    bool isPar2 = false;
    int missingSegments = 0;

    /// Position in the NZB. What the preview URL addresses — deliberately not
    /// the row's index in this list, which a filter or a sort could move.
    int index = -1;

    /// Whether the daemon says a preview is worth offering: media, not PAR2,
    /// and enough downloaded from byte 0 to stream. The rule lives daemon-side
    /// because it needs the real post-yEnc filename and the contiguous-prefix
    /// length, neither of which reaches the GUI.
    bool previewable = false;

    /// Why not, when `previewable` is false and there is something to say — a
    /// compressed or encrypted archive can never be streamed, and a greyed-out
    /// menu entry with no explanation is the failure mode phase 6b set out to
    /// avoid. Empty when the answer is simply "not yet".
    QString previewNote;
};

/// One file the release actually put on disk, as the daemon resolved it.
///
/// Deliberately not derived from UsenetFileRow: an unpacked release publishes the
/// *extracted* members, which are not NZB files at all, so the two lists have
/// different lengths and no positional correspondence.
struct UsenetPublishedFile {
    QString name;
    QString path;      ///< absolute, on the *core's* filesystem
    /// Relative to the core's incoming directory, for the browse route against a
    /// remote core. Empty when the file landed outside it, which no URL can reach.
    QString relPath;
    qint64  size = 0;
};

struct UsenetItemRow {
    QString id;
    QString name;
    UsenetRowStatus status = UsenetRowStatus::Queued;
    QString statusText;
    int priority = 0;

    /// Index into the daemon's category list, 0 being "All". The GUI never
    /// resolves it to a folder -- that happens at completion, daemon-side.
    int category = 0;

    int percent = 0;
    qint64 totalBytes = 0;
    qint64 decodedBytes = 0;
    int segmentCount = 0;
    int doneSegments = 0;
    int missingSegments = 0;
    QString error;

    /// Progress of the current post-processing stage, 0-100, and what it is
    /// working on. `percent` cannot stand in: it counts segments, and a repair
    /// moves none, so it sits at 100% for the whole pass.
    int postPercent = 0;
    QString postDetail;

    /// Why a queued item is standing still — an allowance spent, a probe still
    /// running. Shown in the Status column the way postDetail is, and
    /// deliberately not `error`: waiting for a billing day has failed nothing.
    QString stalledReason;

    /// How much of the release looks obtainable. **-1 means not assessed**,
    /// which is not the same as 100 and must never render as it.
    int healthPercent = -1;
    qint64 healthMissingBytes = 0;
    qint64 healthRecoveryBytes = 0;

    /// Whether a server was actually asked. False means the figure is the NZB's
    /// own arithmetic — a weaker claim, and the tooltip says so.
    bool healthProbed = false;

    /// A passphrase is stored for this release. The value itself never crosses
    /// the wire, which is why this is a bool — the same one-way contract the
    /// news-server passwords keep.
    bool hasPassword = false;

    /// The release is password-protected and no password we have opens it. A
    /// flag rather than a match on `error`, which is translated.
    bool passwordRequired = false;

    QList<UsenetFileRow> files;

    /// What completion published. Empty until then — and empty is also what an
    /// item restored from a sidecar written before publishedPaths existed
    /// reports, since it went in as an optional key rather than a version bump.
    QList<UsenetPublishedFile> publishedFiles;

    /// Bytes per second, derived by the model from consecutive updates. The
    /// daemon does not send a per-item rate: it measures the engine as a whole,
    /// and attributing that across items would be a guess.
    qint64 speed = 0;

    /// Set by the model when it computes speed; not from the wire.
    qint64 lastDecodedBytes = 0;
    qint64 lastSampleMs = 0;
};

/// Mirrors usenet::UsenetItemStatus. Read as an int off the wire, because the GUI
/// does not link eMule::Usenet.
[[nodiscard]] UsenetRowStatus usenetStatusFromInt(int v);

/// Decode one queue row. Lives here rather than in UsenetPanel because the
/// details dialog decodes the same shape off GetUsenetItemDetails, and a wire
/// decoder that exists twice drifts the moment one field is added.
[[nodiscard]] UsenetItemRow usenetRowFromCbor(const QCborMap& m);

/// The five levels the daemon accepts, highest first — menu order, and the order
/// the queue runs them in.
///
/// Duplicated from `usenet::kUsenetPriorityLevels` because the GUI does not link
/// eMule::Usenet and the wire carries a bare int. The daemon clamps what it is
/// sent, so the worst a drift here can do is offer a level that comes back as
/// its nearest neighbour.
inline constexpr std::array<int, 5> kUsenetPriorityLevels{2, 1, 0, -1, -2};

/// A level's name, for the menu and the Priority column. Anything outside the
/// five reads as the nearest one.
[[nodiscard]] QString usenetPriorityName(int priority);

class UsenetQueueModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColSize,
        ColProgress,
        ColStatus,
        ColSpeed,
        ColRemaining,
        ColPriority,

        /// Its own column rather than another clause on the Status cascade,
        /// which is first-match-wins: a health clause appended there would be
        /// invisible on exactly the failed, post-processing and stalled items
        /// where it is most worth reading.
        ColHealth,

        /// Appended after Health rather than slotted beside Priority: the column
        /// order is what uistate.yml stores widths against, and inserting one in
        /// the middle would shift every saved width by one.
        ColCategory,

        ColCount
    };

    explicit UsenetQueueModel(QObject* parent = nullptr);

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Replace the whole queue, incrementally. Rows that survive are updated in
    /// place; only genuine arrivals and departures move.
    void setItems(const QList<UsenetItemRow>& items);

    /// Apply one pushed row, inserting it if it is new.
    void upsertItem(const UsenetItemRow& item);

    void removeItem(const QString& id);

    /// Category titles by index, so the Category column can show a name instead
    /// of a number. Same contract as DownloadListModel's.
    void setCategoryNames(QStringList names);
    void clear();

    [[nodiscard]] bool isFileRow(const QModelIndex& index) const;

    /// The file a child row names, or null for a top-level row. @p itemId, when
    /// given, receives the id of the NZB it belongs to — a file is only ever
    /// addressable as (item, index), never on its own.
    [[nodiscard]] const UsenetFileRow* fileAt(const QModelIndex& index,
                                              QString* itemId = nullptr) const;
    [[nodiscard]] QString idAt(int row) const;
    [[nodiscard]] const UsenetItemRow* findById(const QString& id) const;
    [[nodiscard]] int itemCount() const { return int(m_items.size()); }

private:
    void applyInto(UsenetItemRow& target, const UsenetItemRow& incoming);
    [[nodiscard]] int rowOf(const QString& id) const;

    /// Where a release stands, as an order rather than a word. The enum above is
    /// wire order and reads as nonsense in a column; this is the same
    /// most-finished-first convention DownloadListModel::statusRank uses.
    [[nodiscard]] static int statusRank(const UsenetItemRow& item);

    std::vector<UsenetItemRow> m_items;
    QStringList m_categoryNames;
};

} // namespace eMule
