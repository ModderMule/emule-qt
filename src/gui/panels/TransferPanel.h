#pragma once

/// @file TransferPanel.h
/// @brief Transfer tab panel replicating the MFC Transfer window layout.
///
/// Layout (matching Transfer.png screenshot):
///   - Top half: Downloads list (QTreeView with file name, size, progress, etc.)
///   - Bottom half: Tabbed client lists (Uploading, Downloading, On Queue, Known Clients)
///   - Bottom bar: "Clients on queue: N" label
///   - Vertical splitter between top and bottom sections

#include <QWidget>

class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QSplitter;
class QTabWidget;
class QTimer;
class QTreeView;

namespace eMule {

class ClientListModel;
class DownloadListModel;
class IpcClient;

/// Full Transfer tab page matching the MFC eMule Transfer window.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(QWidget* parent = nullptr);
    ~TransferPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Switch to a client sub-tab by index (0=Uploading, 1=Downloading, 2=On Queue, 3=Known).
    void switchToSubTab(int index);

private slots:
    void onRefreshTimer();
    void onDownloadContextMenu(const QPoint& pos);

private:
    void setupUi();
    QWidget* createDownloadsSection();
    QWidget* createClientsSection();
    QTreeView* createClientView(ClientListModel* model, const QString& headerKey);
    void requestDownloads();
    void requestUploads();
    void sendDownloadAction(const QString& hash, int action);

    // Data models
    DownloadListModel* m_downloadModel = nullptr;
    ClientListModel* m_uploadingModel  = nullptr;
    ClientListModel* m_downloadingModel = nullptr;
    ClientListModel* m_onQueueModel    = nullptr;
    ClientListModel* m_knownModel      = nullptr;

    // Proxy models for sorting
    QSortFilterProxyModel* m_downloadProxy = nullptr;

    // Views
    QTreeView* m_downloadView = nullptr;
    QLabel* m_downloadsLabel  = nullptr;
    QLabel* m_queueLabel      = nullptr;

    // Bottom tabs
    QTabWidget* m_clientTabs = nullptr;

    // Splitter
    QSplitter* m_vertSplitter = nullptr;

    // Refresh timer (1.5 s)
    QTimer* m_refreshTimer = nullptr;

    // IPC link
    IpcClient* m_ipc = nullptr;

    // Context menu
    QMenu* m_downloadMenu = nullptr;
};

} // namespace eMule
