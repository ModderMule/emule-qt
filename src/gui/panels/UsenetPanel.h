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
class UsenetQueueModel;

class UsenetPanel : public QWidget {
    Q_OBJECT

public:
    explicit UsenetPanel(QWidget* parent = nullptr);
    ~UsenetPanel() override;

    void setIpcClient(IpcClient* ipc);

    /// Queue an .nzb from disk. Also the drop and menu entry point.
    void addNzbFile(const QString& path);

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

    bool m_restoringSelection = false;
};

} // namespace eMule
