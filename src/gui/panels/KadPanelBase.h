#pragma once

/// @file KadPanelBase.h
/// @brief Shared layout of one network's page in the Kad window (MFC KademliaWnd):
///   - top left: tabs "Contacts" and "Search Details" (lookup graph; with the Incoming tab
///     below shown, the graph of an incoming request instead)
///   - top right: the network's controls column
///   - bottom: tabs "Outgoing Searches" (our lookups) and "Incoming Searches" (requests
///     other nodes sent us, not MFC)
///   - a vertical splitter between top and bottom
/// Ed2kKadPanel and Ed3kKadPanel fill it; KadPanel holds both.

#include "controls/Ed3kKadModels.h"

#include <QModelIndex>
#include <QWidget>

#include <cstdint>

class QAbstractItemModel;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTreeView;

namespace eMule {

class IpcClient;
class KadIncomingGraph;
class KadLookupGraph;
class PanelPoller;

class KadPanelBase : public QWidget {
    Q_OBJECT

public:
    ~KadPanelBase() override;

    /// Connect to the IPC client: polls while connected and on screen.
    virtual void setIpcClient(IpcClient* client);

    /// 0 = Contacts, 1 = Search Details.
    void switchToSubTab(int index);
    /// 0 = Outgoing Searches, 1 = Incoming Searches.
    void switchToBottomTab(int index);

protected:
    explicit KadPanelBase(QWidget* parent = nullptr);

    /// The page: @p contacts and the lookup graph as tabs, @p controls beside them,
    /// @p searches and the incoming requests of @p network as tabs below. @p bindSplitter
    /// restores and saves the splitter.
    void buildLayout(QWidget* contacts, QWidget* controls, QWidget* searches, KadIncomingModel::Network network,
                     void (*bindSplitter)(QSplitter*));
    /// A titled list over @p model (sort role Qt::UserRole); sets @p view and @p label.
    /// An empty @p title: no header, @p label stays as it is.
    QWidget* buildListPanel(QAbstractItemModel* model, const QString& title, const QString& icon, QTreeView*& view,
                            QLabel*& label);

    /// Poll the network's data; called by the poller while connected and on screen.
    virtual void refresh() = 0;
    /// Fetch the graph's lookup; only while the Search Details tab shows.
    virtual void requestLookupHistory() = 0;
    /// Pick a lookup for the graph by itself (auto-show); default none.
    virtual void autoPickLookup() {}
    /// The connection to the core went: clear what shows.
    virtual void clearAll() = 0;
    /// The lookup id of a row of the searches list (source model row); 0: none.
    [[nodiscard]] virtual std::uint64_t searchIdAtRow(int sourceRow) const = 0;
    /// Search Details title of lookup @p id from the local list (no IPC); empty: none.
    [[nodiscard]] virtual QString lookupTitle(std::uint64_t id) const = 0;

    /// Contacts label and tab title.
    void setContactCount(int count);
    /// Title of the shown outgoing lookup; applied while the lookup graph shows.
    void setLookupTabTitle(const QString& lookupTitle);
    /// "Outgoing Searches (n)" tab title.
    void setOutgoingCount(int count);
    /// The Search Details tab shows the lookup graph (not an incoming request).
    [[nodiscard]] bool lookupTabShown() const;
    [[nodiscard]] bool contactsTabShown() const;
    /// The selected row of the searches list, 0: none.
    [[nodiscard]] std::uint64_t selectedSearchId() const;
    /// Select the row of @p sourceRow in the searches list without the selection signal.
    void markSearchRow(int sourceRow);
    [[nodiscard]] bool ipcReady() const;

    QTabWidget* m_topTabWidget = nullptr;
    QTabWidget* m_bottomTabWidget = nullptr;
    QTreeView* m_contactsView = nullptr;
    QTreeView* m_searchesView = nullptr;
    QLabel* m_contactsLabel = nullptr;
    KadLookupGraph* m_lookupGraph = nullptr;
    QSplitter* m_vertSplitter = nullptr;
    /// Lookup the graph shows; may outlive its row (MFC keeps the history while shown).
    std::uint64_t m_graphSearchId = 0;
    /// Graph lookup picked by a click: auto-show keeps it while its row stays.
    bool m_lookupPicked = false;
    PanelPoller* m_poller = nullptr;
    IpcClient* m_ipc = nullptr;

private:
    KadIncomingModel* m_incomingModel = nullptr;
    QTreeView* m_incomingView = nullptr;
    KadIncomingModel::Network m_network = KadIncomingModel::Network::Ed2k;
    QStackedWidget* m_graphStack = nullptr;
    KadIncomingGraph* m_incomingGraph = nullptr;
    QString m_lookupTitle;
    QString m_incomingTitle;
    /// Request the incoming graph shows; kept when its row is gone.
    std::uint64_t m_incomingGraphId = 0;
    /// Picked by a click: auto-show leaves it alone.
    bool m_incomingPicked = false;

private:
    void poll();
    void requestIncoming();
    /// Tab title only, while the list is hidden (no rows sent).
    void requestIncomingCount();
    void clearIncoming();
    [[nodiscard]] bool incomingTabShown() const;
    void onSearchSelectionChanged();
    void onSearchDoubleClicked(const QModelIndex& index);
    /// Search Details follows the bottom tab: lookup graph or incoming graph.
    void showGraphOf(int bottomTab);
    void updateDetailsTitle();
    void onIncomingSelectionChanged();
    /// Auto-show: the newest request that handed out contacts, unless one was clicked.
    void autoPickIncoming();
    /// Fetch the incoming graph when its request changed; graphs never change.
    void requestIncomingGraph();
    void markIncomingRow();
    [[nodiscard]] std::uint64_t selectedIncomingId() const;
};

} // namespace eMule
