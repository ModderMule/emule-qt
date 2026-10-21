#pragma once

/// @file TorrentDetailsDialog.h
/// @brief One torrent's files (with priorities), peers and trackers.
///
/// Off GetTorrentDetails, polled only while the dialog is open. Modeless, for the
/// reason UsenetDetailsDialog gives: no nested event loop for a quit to unwind.

#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QTabWidget;
class QTimer;
class QTreeWidget;

namespace eMule {

class IpcClient;

/// libtorrent file priority 0..7 as the Files tab shows it.
[[nodiscard]] QString torrentFilePriorityName(int priority);

class TorrentDetailsDialog : public QDialog {
    Q_OBJECT

public:
    TorrentDetailsDialog(IpcClient* ipc, QString torrentId, QString name,
                         QWidget* parent = nullptr);

    /// The torrent shown; the panel reuses an open dialog for the same one.
    [[nodiscard]] QString torrentId() const { return m_id; }

    /// Apply one GetTorrentDetails reply. Public for tests.
    void applyDetails(const QCborMap& details);

    /// Files tab columns.
    enum FileColumn { FileName = 0, FileSize, FileDone, FileProgress, FilePriority, FileColCount };

    /// Tab order, for tests and the panel.
    enum Tab { TabFiles = 0, TabPeers, TabTrackers };

    /// The priority levels offered by the Files tab's menu, low to high.
    static constexpr int kPriorityLevels[] = {0, 1, 4, 6, 7};

private:
    void buildUi(const QString& name);
    void poll();
    void onFilesContextMenu(const QPoint& pos);
    void setSelectedPriority(int priority);
    void applyFiles(const QCborArray& files);
    void applyPeers(const QCborArray& peers);
    void applyTrackers(const QCborArray& trackers);

    IpcClient* m_ipc = nullptr;
    QString m_id;

    QLabel* m_title = nullptr;
    QLabel* m_note = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_files = nullptr;
    QTreeWidget* m_peers = nullptr;
    QTreeWidget* m_trackers = nullptr;
    QTimer* m_timer = nullptr;

    /// Priority per file index, as last reported; what SetTorrentFilePriorities sends.
    QList<int> m_priorities;
};

} // namespace eMule
