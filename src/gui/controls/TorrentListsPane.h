#pragma once

/// @file TorrentListsPane.h
/// @brief The lists of the selected torrent under the Torrents list: Peers, Trackers
///        and Files, as in Transmission's inspector.
///
/// Off GetTorrentDetails, polled only while visible. Rows update in place so the
/// selection survives a poll. The non-list pages (Info, Activity, Options) are in
/// TorrentDetailsDialog.

#include <QCborMap>
#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "controls/TorrentListModel.h"   // torrentFilePriorityName

class QCborArray;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;
class PanelPoller;

/// One line per letter of Transmission's peer status @p flags, for a tooltip.
[[nodiscard]] QString torrentPeerFlagsTooltip(const QString& flags);

class TorrentListsPane : public QWidget {
    Q_OBJECT

public:
    enum Tab { TabPeers = 0, TabTrackers, TabFiles };
    enum PeerColumn {
        PeerLock = 0, PeerUp, PeerDown, PeerProgress, PeerStatus, PeerAddress, PeerClient,
        PeerDownloaded, PeerUploaded, PeerSource, PeerColCount
    };
    enum TrackerColumn {
        TrackerUrl = 0, TrackerTier, TrackerStatus, TrackerNext, TrackerSeeds, TrackerPeers,
        TrackerDownloaded, TrackerColCount
    };
    enum FileColumn { FileName = 0, FileSize, FileProgress, FileDownload, FilePriority, FileColCount };

    explicit TorrentListsPane(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc);

    /// The torrent to show; empty shows the "select a torrent" note.
    void setTorrentId(const QString& id);
    [[nodiscard]] QString torrentId() const { return m_id; }

    /// Apply one GetTorrentDetails reply. Public for tests.
    void applyDetails(const QCborMap& details);

    [[nodiscard]] QTabWidget* tabs() const { return m_tabs; }
    [[nodiscard]] QTreeWidget* peersTree() const { return m_peers; }
    [[nodiscard]] QTreeWidget* trackersTree() const { return m_trackers; }
    [[nodiscard]] QTreeWidget* filesTree() const { return m_files; }

    /// The priorities a Files edit would send, one per file. Public for tests.
    [[nodiscard]] QList<int> prioritiesFromChecks() const;

signals:
    /// A Files or Trackers edit went to the daemon.
    void changed();

private:
    void buildUi();
    QWidget* buildTrackersPage();
    void poll();
    void showNote(const QString& text);

    void applyPeers(const QCborArray& peers);
    void applyTrackers(const QCborArray& trackers);
    void applyFiles(const QCborArray& files);
    QTreeWidgetItem* folderItem(const QString& path, QHash<QString, QTreeWidgetItem*>& folders);
    void sumFolders();
    void updateTrackerVisibility();

    void onFilesContextMenu(const QPoint& pos);
    void onFileItemChanged(QTreeWidgetItem* item, int column);
    void setSelectedPriority(int priority);
    void sendPriorities(const QList<int>& priorities);
    void onAddTrackers();
    void onRemoveTrackers();
    void onReannounce();
    void sendTrackerRequest(int type, const QStringList& urls);

    IpcClient* m_ipc = nullptr;
    PanelPoller* m_poller = nullptr;
    QString m_id;

    QStackedWidget* m_stack = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_peers = nullptr;
    QTreeWidget* m_trackers = nullptr;
    QTreeWidget* m_files = nullptr;
    QCheckBox* m_showBackup = nullptr;
    QToolButton* m_removeTracker = nullptr;

    /// libtorrent priority per file index, as last reported
    QList<int> m_priorities;
    /// file rows are being filled from a reply, not edited
    bool m_updating = false;
    bool m_checkSendQueued = false;
};

} // namespace eMule
