#pragma once

/// @file TorrentPanel.h
/// @brief The Torrents tab: the BitTorrent download list.
///
/// A standalone tab like UsenetPanel. The GUI links eMule::Core and eMule::Ipc
/// only, never eMule::Bt: every row comes off the wire as CBOR (BtBridge.cpp).
/// Polls GetTorrents while visible and upserts rows on the 950-954 pushes.

#include <QCborMap>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "ipc/IpcMessage.h"

class QAction;
class QLabel;
class QMimeData;
class QPushButton;
class QSortFilterProxyModel;
class QTreeView;

namespace eMule {

class IpcClient;
class PanelPoller;
class TorrentDetailsDialog;
class TorrentListModel;
struct TorrentRow;

class TorrentPanel : public QWidget {
    Q_OBJECT

public:
    explicit TorrentPanel(QWidget* parent = nullptr);
    ~TorrentPanel() override;

    void setIpcClient(IpcClient* ipc);

    /// Add .torrent files from disk without asking (drop, Finder, command line).
    /// The bytes travel, not the path.
    void addTorrentFiles(const QStringList& paths);

    /// Add BitTorrent magnets without asking (a clicked or pasted link).
    void addMagnets(const QStringList& magnets);

    /// Ask for magnets (one per line), save path and "start paused".
    /// @p prefill lands in the box, for a paste that wants a second look.
    void promptAddMagnets(const QStringList& prefill = {});

    /// Pick .torrent files, then the same save path / paused choice.
    void promptAddTorrentFiles();

    /// Take every local .torrent in @p mime. False when there was none.
    bool acceptTorrentDrop(const QMimeData* mime);

    /// "Add Magnet Link…", shared with the Tools menu.
    [[nodiscard]] QAction* addMagnetAction() const { return m_addMagnetAction; }

    /// The engine as last reported by GetBtStatus / PushBtStatus.
    [[nodiscard]] bool engineRunning() const;

    [[nodiscard]] TorrentListModel* model() const { return m_model; }

signals:
    /// Open Options at the BitTorrent page.
    void optionsRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUi();
    void refresh();
    void requestStatus();
    void applyStatus(const QCborMap& status);

    [[nodiscard]] QStringList selectedIds() const;
    [[nodiscard]] const TorrentRow* currentRow() const;

    void onContextMenu(const QPoint& pos);
    /// Pause/Resume/Recheck/Reannounce: one [id] request per selected torrent.
    void sendForSelection(Ipc::IpcMsgType type);
    void onRemove();
    void onOpenFolder();
    void onCopyMagnet();
    void onPasteMagnets();
    void showDetails(const QString& id);

    void updateActions();
    void updateSummary();

    IpcClient* m_ipc = nullptr;
    PanelPoller* m_poller = nullptr;

    QTreeView* m_view = nullptr;
    TorrentListModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QLabel* m_summary = nullptr;
    QPushButton* m_optionsButton = nullptr;

    QAction* m_addFileAction = nullptr;
    QAction* m_addMagnetAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_recheckAction = nullptr;
    QAction* m_reannounceAction = nullptr;
    QAction* m_openFolderAction = nullptr;
    QAction* m_copyMagnetAction = nullptr;
    QAction* m_detailsAction = nullptr;

    QCborMap m_status;
    bool m_statusKnown = false;

    QPointer<TorrentDetailsDialog> m_detailsDialog;
};

} // namespace eMule
