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

#include <QAbstractItemModel>
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

struct UsenetItemRow {
    QString id;
    QString name;
    UsenetRowStatus status = UsenetRowStatus::Queued;
    QString statusText;
    int priority = 0;
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

    QList<UsenetFileRow> files;

    /// Bytes per second, derived by the model from consecutive updates. The
    /// daemon does not send a per-item rate: it measures the engine as a whole,
    /// and attributing that across items would be a guess.
    qint64 speed = 0;

    /// Set by the model when it computes speed; not from the wire.
    qint64 lastDecodedBytes = 0;
    qint64 lastSampleMs = 0;
};

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

    std::vector<UsenetItemRow> m_items;
};

} // namespace eMule
