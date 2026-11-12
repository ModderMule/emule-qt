#include "pch.h"
#include "panels/KadPanelBase.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/KadLookupGraph.h"
#include "controls/SortableItems.h"
#include "prefs/Preferences.h"
#include "utils/Appearance.h"
#include "utils/ListActivation.h"
#include "utils/PanelPoller.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

KadPanelBase::KadPanelBase(QWidget* parent)
    : QWidget(parent)
{
    m_poller = new PanelPoller(this, [this] { refresh(); });
}

KadPanelBase::~KadPanelBase() = default;

void KadPanelBase::setIpcClient(IpcClient* client)
{
    m_ipc = client;
    if (m_ipc && m_ipc->isConnected()) {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
    } else if (m_ipc) {
        connect(m_ipc, &IpcClient::connected, this, [this] {
            m_poller->setInterval(m_ipc->pollingInterval());
            m_poller->setEnabled(true);
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this] {
            m_poller->setEnabled(false);
            clearAll();
        });
    } else {
        m_poller->setEnabled(false);
        clearAll();
    }
}

void KadPanelBase::switchToSubTab(int index)
{
    if (!m_topTabWidget || index < 0 || index >= m_topTabWidget->count())
        return;
    m_topTabWidget->setCurrentIndex(index);
    if (index == 1)
        autoPickLookup();
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

void KadPanelBase::buildLayout(QWidget* contacts, QWidget* controls, QWidget* searches, void (*bindSplitter)(QSplitter*))
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(0);

    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    useThemedSplitterHandles(m_vertSplitter);

    // Top section: tab widget (left) + controls panel (right)
    auto* topSplitter = new QSplitter(Qt::Horizontal);

    m_topTabWidget = new QTabWidget;
    m_topTabWidget->setDocumentMode(true);
    if (thePrefs.useOriginalIcons())
        m_topTabWidget->addTab(contacts, skinIcon(QStringLiteral(":/icons/KadContactList.ico")), tr("▸ Contacts (0)"));
    else
        m_topTabWidget->addTab(contacts, tr("▸ Contacts (0)"));

    // Search Details tab with lookup graph (MFC IDS_LOOKUPGRAPH, icon "FriendSlot")
    m_lookupGraph = new KadLookupGraph;
    m_lookupGraph->setAutoShowLookups(theUiState.kadAutoShowLookups());
    if (thePrefs.useOriginalIcons())
        m_topTabWidget->addTab(m_lookupGraph, skinIcon(QStringLiteral(":/icons/FriendSlot.ico")), tr("▸ Search Details"));
    else
        m_topTabWidget->addTab(m_lookupGraph, tr("▸ Search Details"));
    connect(m_lookupGraph, &KadLookupGraph::autoShowLookupsToggled, this, [this](bool on) {
        theUiState.setKadAutoShowLookups(on);
        if (on)
            autoPickLookup();
    });

    topSplitter->addWidget(m_topTabWidget);
    topSplitter->addWidget(controls);
    topSplitter->setStretchFactor(0, 3);
    topSplitter->setStretchFactor(1, 1);

    m_vertSplitter->addWidget(topSplitter);
    m_vertSplitter->addWidget(searches);
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 1);
    if (bindSplitter)
        bindSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter);

    // the graph follows the list selection, shown or not (MFC OnListModifiedSearchlist)
    connect(m_searchesView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &KadPanelBase::onSearchSelectionChanged);
    connect(m_searchesView, &QAbstractItemView::doubleClicked, this, &KadPanelBase::onSearchDoubleClicked);
    // switching to the Search Details tab fetches its lookup
    connect(m_topTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            autoPickLookup();
            requestLookupHistory();
        } else if (index == 0 && m_poller) {
            m_poller->refreshNow();   // contacts list is only filled while shown
        }
    });
}

QWidget* KadPanelBase::buildListPanel(QAbstractItemModel* model, const QString& title, const QString& icon, QTreeView*& view,
                                      QLabel*& label)
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Section header matching MFC style: icon + bold text
    auto* header = new QHBoxLayout;
    header->setSpacing(4);
    if (!icon.isEmpty() && thePrefs.useOriginalIcons()) {
        auto* iconLabel = new QLabel;
        setSkinPixmap(iconLabel, icon);
        header->addWidget(iconLabel);
    }
    label = new QLabel(title);
    QFont boldFont = label->font();
    boldFont.setBold(true);
    label->setFont(boldFont);
    header->addWidget(label);
    header->addStretch();
    layout->addLayout(header);

    auto* proxy = new SortValueProxy(this);
    proxy->setSourceModel(model);
    proxy->setSortRole(Qt::UserRole);
    auto* list = new ListTreeView;
    view = list;
    view->setModel(proxy);
    view->setRootIsDecorated(false);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setUniformRowHeights(true);
    view->header()->setStretchLastSection(true);
    bindListKeys(view, {.find = true});   // Ctrl+F / F3 (general-purpose find)
    layout->addWidget(view);
    return widget;
}

void KadPanelBase::setContactCount(int count)
{
    m_contactsLabel->setText(tr("▸ Contacts (%1)").arg(count));
    m_topTabWidget->setTabText(0, tr("▸ Contacts (%1)").arg(count));
}

void KadPanelBase::setLookupTabTitle(const QString& lookupTitle)
{
    // MFC CKademliaWnd::UpdateButtonTitle
    m_topTabWidget->setTabText(1, lookupTitle.isEmpty() ? tr("▸ Search Details")
                                                        : tr("▸ Search Details (%1)").arg(lookupTitle));
}

bool KadPanelBase::lookupTabShown() const
{
    return m_topTabWidget && m_topTabWidget->currentIndex() == 1;
}

bool KadPanelBase::contactsTabShown() const
{
    return m_topTabWidget && m_topTabWidget->currentIndex() == 0;
}

std::uint64_t KadPanelBase::selectedSearchId() const
{
    const auto selection = m_searchesView->selectionModel()->selectedRows();
    if (selection.isEmpty())
        return 0;
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_searchesView->model());
    return proxy ? searchIdAtRow(proxy->mapToSource(selection.first()).row()) : 0;
}

void KadPanelBase::markSearchRow(int sourceRow)
{
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_searchesView->model());
    if (!proxy || sourceRow < 0)
        return;
    const QModelIndex idx = proxy->mapFromSource(proxy->sourceModel()->index(sourceRow, 0));
    const QSignalBlocker block(m_searchesView->selectionModel());
    m_searchesView->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_searchesView->viewport()->update();
}

bool KadPanelBase::ipcReady() const
{
    return m_ipc && m_ipc->isConnected();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void KadPanelBase::onSearchSelectionChanged()
{
    const std::uint64_t id = selectedSearchId();
    if (id == 0 || id == m_graphSearchId)
        return;
    m_graphSearchId = id;
    // title and blank graph at once, the picture follows with the reply (MFC UpdateButtonTitle)
    setLookupTabTitle(lookupTitle(id));
    m_lookupGraph->clear();
    requestLookupHistory();
}

void KadPanelBase::onSearchDoubleClicked(const QModelIndex& index)
{
    // Show this lookup and stop switching away from it (MFC OnNMDblclkSearchlist)
    if (!index.isValid())
        return;
    m_graphSearchId = selectedSearchId();
    theUiState.setKadAutoShowLookups(false);
    m_lookupGraph->setAutoShowLookups(false);
    m_topTabWidget->setCurrentIndex(1);
    requestLookupHistory();
}

} // namespace eMule
