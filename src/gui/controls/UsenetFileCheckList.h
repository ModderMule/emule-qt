#pragma once

/// @file UsenetFileCheckList.h
/// @brief Which files of a release download: checkboxes on a file table.
///
/// The release details dialog and the Add NZB file chooser both show a table of
/// NZB files with a box per row and Select All / Select None / Invert Selection
/// over it. The check logic — locked PAR2 rows, an archive set toggling as one,
/// telling a click from a rebuild — is the same in both, so it lives here once.
///
/// Rows are either top-level (the details dialog) or children of one row per
/// .nzb (the chooser); a bulk action and a set toggle stay within one parent.

#include <QList>
#include <QObject>
#include <QString>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace eMule {

class IpcClient;

class UsenetFileCheckList : public QObject {
    Q_OBJECT

public:
    /// Boxes go in @p column of @p tree, which also parents this object.
    UsenetFileCheckList(QTreeWidget* tree, int column);

    /// Select All / Select None / Invert Selection, parented to @p parent.
    [[nodiscard]] QWidget* createButtonRow(QWidget* parent);

    /// Make @p item a file row. A @p locked row shows its box but cannot be
    /// changed — PAR2 files. Rows sharing a non-empty @p setKey toggle together.
    void bindRow(QTreeWidgetItem* item, int fileIndex, bool checked, bool locked,
                 const QString& setKey = {});

    /// Whether the user may change anything now. Applies to rows bound later too.
    void setEditable(bool editable);

    /// Unchecked file indices among @p parent's children, or the top-level rows.
    [[nodiscard]] QList<int> uncheckedIndices(QTreeWidgetItem* parent = nullptr) const;

    /// Whether an unlocked row under @p parent is still checked — what keeps a
    /// release from being queued with nothing to download.
    [[nodiscard]] bool anyUnlockedChecked(QTreeWidgetItem* parent = nullptr) const;

    /// Send SetUsenetFilesSkipped for one queued release. A refusal is a sentence
    /// from the daemon and goes to the status bar.
    static void sendSkip(IpcClient* ipc, QObject* context, const QString& itemId,
                         const QList<int>& fileIndices, bool skipped);

signals:
    /// The user changed rows under one parent. Never emitted by bindRow().
    void skipChanged(const QList<int>& fileIndices, bool skipped);

private:
    enum class Bulk { All, None, Invert };

    void onItemChanged(QTreeWidgetItem* item, int column);
    void applyBulk(Bulk mode);

    /// Bound rows under @p parent, or at the top level.
    [[nodiscard]] QList<QTreeWidgetItem*> fileRows(QTreeWidgetItem* parent) const;

    /// Parents whose children are file rows; nullptr stands for the top level.
    [[nodiscard]] QList<QTreeWidgetItem*> groups() const;

    void setChecked(QTreeWidgetItem* item, bool checked);
    void applyFlags(QTreeWidgetItem* item);

    QTreeWidget* m_tree = nullptr;
    int m_column = 0;
    bool m_editable = true;

    /// Set while this class writes check states, so its own writes are not
    /// mistaken for clicks.
    bool m_updating = false;

    QList<QPushButton*> m_buttons;
};

} // namespace eMule
