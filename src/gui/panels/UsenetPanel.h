#pragma once

/// @file UsenetPanel.h
/// @brief The Usenet tab: the NZB download queue.
///
/// A standalone panel, and it stays one. Merging it into the shared transfer list
/// is a separate refactor for after the whole Usenet module is built and tested —
/// nothing here is shaped toward that, and TransferPanel and DownloadListModel are
/// untouched.
///
/// The GUI links eMule::Core and eMule::Ipc only, never eMule::Usenet, so
/// everything here comes off the wire as CBOR.

#include <QPointer>
#include <QWidget>

#include "ipc/IpcMessage.h"

class QAction;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QTreeView;

namespace eMule {

class IpcClient;
class PanelPoller;
class UsenetArchiveEntryDialog;
class UsenetQueueModel;

class UsenetPanel : public QWidget {
    Q_OBJECT

public:
    explicit UsenetPanel(QWidget* parent = nullptr);
    ~UsenetPanel() override;

    void setIpcClient(IpcClient* ipc);

    /// Queue an .nzb from disk. Also the drop and menu entry point.
    void addNzbFile(const QString& path);

    /// The daemon's per-process preview-stream token, handed out with the stats
    /// poll. Empty means the web server is not up, and Preview stays disabled —
    /// the same contract TransferPanel, SearchPanel and SharedFilesPanel use.
    void setStreamToken(const QString& token) { m_streamToken = token; }

private:
    /// The queue view's state, all of which a model change can disturb.
    struct SelectionState {
        QStringList ids;
        QString currentId;
        QStringList expandedIds;
        int scrollValue = 0;
    };

    void setupUi();
    void refresh();
    void applyQueue(const QCborArray& rows);

    [[nodiscard]] SelectionState saveSelection() const;
    void restoreSelection(const SelectionState& state);

    /// Item ids under the selection. A selected *file* row resolves up to its
    /// parent NZB, because every action here acts on the item.
    [[nodiscard]] QStringList selectedItemIds() const;

    void onContextMenu(const QPoint& pos);
    void onAddNzb();
    void onPause();
    void onResume();
    void onRemove(bool deleteFiles);
    void onSetPriority(int priority);
    void onOpenFolder();
    void onPreview();

    /// The file a Preview should stream, as (item id, index in the NZB).
    ///
    /// A selected *file* row names itself. A selected item row resolves to its
    /// largest previewable file, which for a release is the feature rather than
    /// the sample. Returns index -1 when nothing in the selection can be played.
    [[nodiscard]] QPair<QString, int> previewTarget() const;

    /// Build the preview URL for one inner file and hand it to the player.
    void launchEntry(const QString& itemId, int fileIndex, int entry);

    /// Why the current selection cannot be previewed, when the daemon said so.
    /// Empty when the answer is "not yet" rather than "not ever".
    [[nodiscard]] QString previewNote() const;

    void updateActions();
    void updateSummary();

    IpcClient* m_ipc = nullptr;
    PanelPoller* m_poller = nullptr;

    /// A ListTreeView, held as its base the way TransferPanel does: the column
    /// persistence is bound once at construction and never needed again.
    QTreeView* m_view = nullptr;
    UsenetQueueModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLabel* m_summary = nullptr;

    QAction* m_addAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_removeWithFilesAction = nullptr;
    QAction* m_openFolderAction = nullptr;
    QAction* m_previewAction = nullptr;

    QString m_streamToken;

    /// The chooser, while one is open. QPointer because the dialog is modeless
    /// and deletes itself on close.
    QPointer<UsenetArchiveEntryDialog> m_entryDialog;

    bool m_restoringSelection = false;
};

} // namespace eMule
