#pragma once

/// @file KadPanelBase.h
/// @brief Shared layout of one network's page in the Kad window (MFC KademliaWnd):
///   - top left: tabs "Contacts" and "Search Details" (lookup graph)
///   - top right: the network's controls column
///   - bottom: the lookups list
///   - a vertical splitter between top and bottom
/// Ed2kKadPanel and Ed3kKadPanel fill it; KadPanel holds both.

#include <QModelIndex>
#include <QWidget>

#include <cstdint>

class QAbstractItemModel;
class QLabel;
class QSplitter;
class QTabWidget;
class QTreeView;

namespace eMule {

class IpcClient;
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

protected:
    explicit KadPanelBase(QWidget* parent = nullptr);

    /// The page: @p contacts and the lookup graph as tabs, @p controls beside them,
    /// @p searches below. @p bindSplitter restores and saves the splitter.
    void buildLayout(QWidget* contacts, QWidget* controls, QWidget* searches, void (*bindSplitter)(QSplitter*));
    /// A titled list over @p model (sort role Qt::UserRole); sets @p view and @p label.
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
    void setLookupTabTitle(const QString& lookupTitle);
    [[nodiscard]] bool lookupTabShown() const;
    [[nodiscard]] bool contactsTabShown() const;
    /// The selected row of the searches list, 0: none.
    [[nodiscard]] std::uint64_t selectedSearchId() const;
    /// Select the row of @p sourceRow in the searches list without the selection signal.
    void markSearchRow(int sourceRow);
    [[nodiscard]] bool ipcReady() const;

    QTabWidget* m_topTabWidget = nullptr;
    QTreeView* m_contactsView = nullptr;
    QTreeView* m_searchesView = nullptr;
    QLabel* m_contactsLabel = nullptr;
    QLabel* m_searchesLabel = nullptr;
    KadLookupGraph* m_lookupGraph = nullptr;
    QSplitter* m_vertSplitter = nullptr;
    /// Lookup the graph shows; may outlive its row (MFC keeps the history while shown).
    std::uint64_t m_graphSearchId = 0;
    PanelPoller* m_poller = nullptr;
    IpcClient* m_ipc = nullptr;

private:
    void onSearchSelectionChanged();
    void onSearchDoubleClicked(const QModelIndex& index);
};

} // namespace eMule
