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

#include <QModelIndex>
#include <QPointer>
#include <QWidget>

#include "ipc/IpcMessage.h"

class QAction;
class QMimeData;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QTreeView;

namespace eMule {

class CategoryFilterProxy;
class CategoryTabBar;
class IpcClient;
class PanelPoller;
class UsenetArchiveEntryDialog;
class UsenetDetailsDialog;
class UsenetQueueModel;

/// What an Add dialog asked for, or nothing when the path does not ask — a drop,
/// a Finder open, the command line. The defaults reproduce those exactly: no
/// category (so auto-categorisation runs), normal priority, not paused.
///
/// Outside the panel class because a default argument cannot use a member
/// initializer of the class it is declared in.
struct NzbAddChoices {
    QString password;
    int category = 0;
    int priority = 0;
    bool paused = false;

    /// NZB file indices unchecked in Choose Files…; the daemon widens them to
    /// whole archive sets.
    QList<int> skippedFiles;
};

class UsenetPanel : public QWidget {
    Q_OBJECT

public:
    explicit UsenetPanel(QWidget* parent = nullptr);
    ~UsenetPanel() override;

    void setIpcClient(IpcClient* ipc);

    /// Queue an .nzb from disk. Also the drop and menu entry point.
    /// @p password is an archive passphrase for this release, usually empty.
    /// Every file route funnels here, and only the Add NZB dialog has one to
    /// offer — a drop is a quick action, and the context menu can set it after.
    void addNzbFile(const QString& path, const NzbAddChoices& choices = {});

    [[nodiscard]] QStringList categoryChoices() const;

    /// Ask for .nzb URLs and queue them. The daemon does the downloading, so a
    /// link only its network can reach still works — see IpcProtocol.h,
    /// AddNzbUrl.
    void promptAddNzbUrl();

    /// The "Add NZB from URL…" action, so the Tools menu can show the very same
    /// one rather than a copy that drifts from it.
    [[nodiscard]] QAction* addNzbUrlAction() const { return m_addUrlAction; }

    /// The daemon's per-process preview-stream token, handed out with the stats
    /// poll. Empty means the web server is not up, and Preview stays disabled —
    /// the same contract TransferPanel, SearchPanel and SharedFilesPanel use.
    void setStreamToken(const QString& token);

protected:
    // Drops are accepted here and in MainWindow, so a drop works wherever the
    // user happens to be. Both defer to nzbDropCandidates() rather than each
    // deciding for itself what a droppable .nzb is.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public:
    /// Queue everything .nzb-shaped in @p mime. Returns false when there was
    /// nothing of ours in it, so a caller can leave the event alone.
    bool acceptNzbDrop(const QMimeData* mime);

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

    /// Map a view index down to the model, and back. Two proxies sit in between
    /// — the sort proxy and the category filter on top of it — and going through
    /// only one yields the *wrong row* rather than an invalid index.
    [[nodiscard]] QModelIndex toSourceIndex(const QModelIndex& viewIndex) const;
    [[nodiscard]] QModelIndex fromSourceIndex(const QModelIndex& sourceIndex) const;

    /// The Usenet half of a category tab's context menu — CategoryTabBar owns
    /// the entries that edit the category itself.
    void populateCategoryMenu(QMenu* menu, int index);
    void sendCategoryStatus(int index, Ipc::CategoryAction action);
    void sendSetCategory(const QStringList& ids, int category);

    void onContextMenu(const QPoint& pos);
    void onAddNzb();
    void onSetPassword();
    void onPause();
    void onResume();
    void onRemove(bool deleteFiles);
    void onSetPriority(int priority);
    void onOpenFolder();
    void onPreview();
    void onCheckAvailability();
    void onToggleEnginePause();
    /// Pause All / Resume All, from IpcClient::usenetEnginePaused().
    void updateEngineAction();

    /// What a double-click or Enter does on @p index — the single place that
    /// decision lives, so the mouse and the keyboard cannot diverge.
    void activateRow(const QModelIndex& proxyIndex);

    /// Open one published file with whatever can reach it: the OS default
    /// application when the core runs on this machine, the daemon's own web page
    /// when it does not.
    ///
    /// @p fileIndex names a file row's position in the NZB; -1 means "the item's
    /// payload", which resolves to its largest non-PAR2 published file — a
    /// release carries a sample and sometimes a trailer, and neither is what was
    /// asked for.
    void openUsenetFile(const QString& itemId, int fileIndex);

    /// Show the release details dialog, raising the open one rather than
    /// stacking a second.
    void showDetails(const QString& itemId);

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

    /// Sits on top of m_proxy and is what the view is actually bound to, so
    /// every view index needs both hops — toSourceIndex()/fromSourceIndex().
    CategoryFilterProxy* m_categoryProxy = nullptr;

    CategoryTabBar* m_categoryTabBar = nullptr;
    QLabel* m_summary = nullptr;

    QAction* m_addAction = nullptr;
    QAction* m_addUrlAction = nullptr;

    /// Last quota-stall sentence shown in the status bar. The panel polls four
    /// times a second, so posting on every refresh would repeat the notice
    /// forever; this makes it fire on the transition only.
    QString m_lastStallNotice;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_previewAction = nullptr;
    QAction* m_checkAction = nullptr;
    QAction* m_pauseAllAction = nullptr;

    QString m_streamToken;

    /// The live download split, off the stats push; KB/s, 0 = unlimited. Lets
    /// the summary say why the rate stops where it does.
    qint64 m_maxDownloadKb = 0;
    qint64 m_usenetLimitKb = 0;
    qint64 m_ed2kBudgetKb = 0;

    /// The chooser, while one is open. QPointer because the dialog is modeless
    /// and deletes itself on close.
    QPointer<UsenetArchiveEntryDialog> m_entryDialog;

    /// The details window, on the same terms and for the same reason.
    QPointer<UsenetDetailsDialog> m_detailsDialog;

    bool m_restoringSelection = false;
};

} // namespace eMule
