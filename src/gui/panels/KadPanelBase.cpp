#include "pch.h"
#include "panels/KadPanelBase.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/KadIncomingGraph.h"
#include "controls/KadLookupGraph.h"
#include "controls/SortableItems.h"
#include "prefs/Preferences.h"
#include "utils/Appearance.h"
#include "utils/ListActivation.h"
#include "utils/PanelLayout.h"
#include "utils/PanelPoller.h"

#include "IpcMessage.h"

#include <QFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

KadPanelBase::KadPanelBase(QWidget* parent)
    : QWidget(parent)
{
    m_poller = new PanelPoller(this, [this] { poll(); });
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
            clearIncoming();
        });
    } else {
        m_poller->setEnabled(false);
        clearAll();
        clearIncoming();
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

void KadPanelBase::switchToBottomTab(int index)
{
    if (m_bottomTabWidget && index >= 0 && index < m_bottomTabWidget->count())
        m_bottomTabWidget->setCurrentIndex(index);
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

void KadPanelBase::buildLayout(QWidget* contacts, QWidget* controls, QWidget* searches, KadIncomingModel::Network network,
                               void (*bindSplitter)(QSplitter*))
{
    m_network = network;
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

    // Search Details tab with lookup graph (MFC IDS_LOOKUPGRAPH, icon "FriendSlot"); the
    // incoming graph takes its place while the Incoming tab shows
    m_lookupGraph = new KadLookupGraph;
    m_incomingGraph = new KadIncomingGraph;
    m_graphStack = new QStackedWidget;
    m_graphStack->addWidget(m_lookupGraph);
    m_graphStack->addWidget(m_incomingGraph);
    if (thePrefs.useOriginalIcons())
        m_topTabWidget->addTab(m_graphStack, skinIcon(QStringLiteral(":/icons/FriendSlot.ico")), tr("▸ Search Details"));
    else
        m_topTabWidget->addTab(m_graphStack, tr("▸ Search Details"));
    for (KadGraphBase* graph : {static_cast<KadGraphBase*>(m_lookupGraph), static_cast<KadGraphBase*>(m_incomingGraph)}) {
        graph->setAutoShowLookups(theUiState.kadAutoShowLookups());
        connect(graph, &KadGraphBase::autoShowLookupsToggled, this, [this](bool on) {
            theUiState.setKadAutoShowLookups(on);
            m_lookupGraph->setAutoShowLookups(on);
            m_incomingGraph->setAutoShowLookups(on);
            if (on) {
                m_incomingPicked = false;
                m_lookupPicked = false;
                autoPickLookup();
                autoPickIncoming();
                requestIncomingGraph();
            }
        });
    }

    topSplitter->addWidget(m_topTabWidget);
    // scrolls, so its full height doesn't pin the top pane's minimum (like the server panel)
    topSplitter->addWidget(PanelLayout::scrollableColumn(controls));
    topSplitter->setStretchFactor(0, 3);
    topSplitter->setStretchFactor(1, 1);

    // Bottom: our lookups and the requests other nodes sent us
    m_incomingModel = new KadIncomingModel(network, this);
    QLabel* noLabel = nullptr;
    QWidget* incoming = buildListPanel(m_incomingModel, {}, {}, m_incomingView, noLabel);
    auto* incomingView = static_cast<ListTreeView*>(m_incomingView);
    incomingView->setDefaultSort(KadIncomingModel::ColTime, Qt::DescendingOrder);
    // Time, Type, Key, Name, From, Answer
    incomingView->bindColumns(network == KadIncomingModel::Network::Ed2k ? QStringLiteral("kadIncoming")
                                                                         : QStringLiteral("ed3kKadIncoming"),
                              {70, 120, 240, 240, network == KadIncomingModel::Network::Ed2k ? 40 : 290, 160});
    m_bottomTabWidget = new QTabWidget;
    m_bottomTabWidget->setDocumentMode(true);
    const QIcon searchIcon = thePrefs.useOriginalIcons() ? skinIcon(QStringLiteral(":/icons/KadCurrentSearches.ico")) : QIcon();
    m_bottomTabWidget->addTab(searches, searchIcon, tr("▸ Outgoing Searches (0)"));
    m_bottomTabWidget->addTab(incoming, searchIcon, tr("▸ Incoming Searches (0)"));
    connect(m_bottomTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        showGraphOf(index);
        if (index == 1)
            requestIncoming();   // filled only while shown
        else
            requestLookupHistory();
    });

    m_vertSplitter->addWidget(topSplitter);
    m_vertSplitter->addWidget(m_bottomTabWidget);
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 1);
    PanelLayout::setPaneMinHeights(m_vertSplitter);
    if (bindSplitter)
        bindSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter);

    // the graph follows the list selection, shown or not (MFC OnListModifiedSearchlist)
    connect(m_searchesView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &KadPanelBase::onSearchSelectionChanged);
    connect(m_searchesView, &QAbstractItemView::doubleClicked, this, &KadPanelBase::onSearchDoubleClicked);
    // click on an already selected row: no selection change, still a pick; the graph may
    // show another lookup (auto-show doesn't mark its pick while Contacts is shown)
    connect(m_searchesView, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
            onSearchSelectionChanged();
    });
    connect(m_incomingView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &KadPanelBase::onIncomingSelectionChanged);
    connect(m_incomingView, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
            m_topTabWidget->setCurrentIndex(1);
    });
    // switching to the Search Details tab fetches its lookup
    connect(m_topTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            autoPickLookup();
            requestLookupHistory();
            requestIncomingGraph();
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
    if (!title.isEmpty()) {
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
    }

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
    m_lookupTitle = lookupTitle;
    updateDetailsTitle();
}

void KadPanelBase::setOutgoingCount(int count)
{
    if (m_bottomTabWidget)
        m_bottomTabWidget->setTabText(0, tr("▸ Outgoing Searches (%1)").arg(count));
}

bool KadPanelBase::lookupTabShown() const
{
    return m_topTabWidget && m_topTabWidget->currentIndex() == 1 && !incomingTabShown();
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

void KadPanelBase::poll()
{
    refresh();
    if (incomingTabShown())
        requestIncoming();
    else
        requestIncomingCount();
}

void KadPanelBase::requestIncomingCount()
{
    if (!ipcReady() || !m_incomingModel)
        return;
    const auto type = m_network == KadIncomingModel::Network::Ed2k ? Ipc::IpcMsgType::GetKadIncoming
                                                                   : Ipc::IpcMsgType::GetEd3kKadIncoming;
    m_ipc->sendPoll(this, Ipc::IpcMessage(type).append(true), [this](const Ipc::IpcMessage& resp) {
        if (incomingTabShown())
            return;   // the full list took over
        const bool ok = resp.type() == Ipc::IpcMsgType::Result && resp.fieldBool(0);
        // an old core ignores countOnly and sends the rows
        const QCborValue v = ok ? resp.field(1) : QCborValue();
        const qint64 count = v.isArray() ? v.toArray().size() : v.toInteger();
        m_bottomTabWidget->setTabText(1, tr("▸ Incoming Searches (%1)").arg(count));
    });
}

void KadPanelBase::requestIncoming()
{
    if (!ipcReady() || !m_incomingModel)
        return;
    const auto type = m_network == KadIncomingModel::Network::Ed2k ? Ipc::IpcMsgType::GetKadIncoming
                                                                   : Ipc::IpcMsgType::GetEd3kKadIncoming;
    m_ipc->sendPoll(this, Ipc::IpcMessage(type), [this](const Ipc::IpcMessage& resp) {
        if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0)) {
            clearIncoming();
            return;
        }
        const int scroll = m_incomingView->verticalScrollBar()->value();
        m_incomingModel->setIncoming(resp.fieldArray(1));
        markIncomingRow();   // the reset dropped the selection
        m_incomingView->verticalScrollBar()->setValue(scroll);
        m_bottomTabWidget->setTabText(1, tr("▸ Incoming Searches (%1)").arg(m_incomingModel->count()));
        autoPickIncoming();
        requestIncomingGraph();
    });
}

void KadPanelBase::clearIncoming()
{
    if (!m_incomingModel)
        return;
    m_incomingModel->clear();
    m_bottomTabWidget->setTabText(1, tr("▸ Incoming Searches (0)"));
    m_incomingGraphId = 0;
    m_incomingPicked = false;
    m_incomingTitle.clear();
    m_incomingGraph->clear();
    updateDetailsTitle();
}

bool KadPanelBase::incomingTabShown() const
{
    return m_bottomTabWidget && m_bottomTabWidget->currentIndex() == 1;
}

void KadPanelBase::showGraphOf(int bottomTab)
{
    m_graphStack->setCurrentIndex(bottomTab == 1 ? 1 : 0);
    updateDetailsTitle();
}

void KadPanelBase::updateDetailsTitle()
{
    // MFC CKademliaWnd::UpdateButtonTitle; the incoming graph names its request
    const QString title = incomingTabShown() ? m_incomingTitle : m_lookupTitle;
    m_topTabWidget->setTabText(1, title.isEmpty() ? tr("▸ Search Details") : tr("▸ Search Details (%1)").arg(title));
}

void KadPanelBase::onIncomingSelectionChanged()
{
    const std::uint64_t id = selectedIncomingId();
    if (id == 0)
        return;
    m_incomingPicked = true;
    if (id == m_incomingGraphId)
        return;
    m_incomingGraphId = id;
    requestIncomingGraph();
}

void KadPanelBase::autoPickIncoming()
{
    if (!theUiState.kadAutoShowLookups() || !m_incomingModel)
        return;
    if (m_incomingPicked && m_incomingModel->rowOf(m_incomingGraphId) >= 0)
        return;
    const int row = m_incomingModel->newestWithContacts();
    if (row < 0)
        return;
    m_incomingPicked = false;
    m_incomingGraphId = m_incomingModel->idAt(row);
}

void KadPanelBase::requestIncomingGraph()
{
    if (!ipcReady() || !incomingTabShown())
        return;
    const int row = m_incomingModel->rowOf(m_incomingGraphId);
    if (m_incomingGraphId == 0 || row < 0 || m_incomingGraph->requestId() == m_incomingGraphId)
        return;   // a request gone from the list keeps its last picture
    // the title at once, the picture only while it shows
    m_incomingTitle = m_incomingModel->titleAt(row);
    updateDetailsTitle();
    if (m_topTabWidget->currentIndex() != 1)
        return;
    Ipc::IpcMessage req(m_network == KadIncomingModel::Network::Ed2k ? Ipc::IpcMsgType::GetKadIncomingGraph
                                                                     : Ipc::IpcMsgType::GetEd3kKadIncomingGraph);
    req.append(static_cast<qint64>(m_incomingGraphId));
    const std::uint64_t id = m_incomingGraphId;
    const QCborMap rowData = m_incomingModel->row(row);
    const QString answer = m_incomingModel->answerAt(row);
    m_incomingGraph->setLoading(true);
    m_ipc->sendPoll(this, std::move(req), [this, id, rowData, answer](const Ipc::IpcMessage& resp) {
        if (id != m_incomingGraphId)
            return;
        const QCborMap graph = resp.fieldMap(1);
        if (resp.type() == Ipc::IpcMsgType::Result && resp.fieldBool(0) && graph.value(QStringLiteral("found")).toBool())
            m_incomingGraph->setRequest(rowData, graph, m_incomingTitle, answer);
        else
            m_incomingGraph->setLoading(false);
    });
}

void KadPanelBase::markIncomingRow()
{
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_incomingView->model());
    const int row = m_incomingPicked ? m_incomingModel->rowOf(m_incomingGraphId) : -1;
    if (!proxy || row < 0)
        return;
    const QModelIndex idx = proxy->mapFromSource(m_incomingModel->index(row, 0));
    const QSignalBlocker block(m_incomingView->selectionModel());
    m_incomingView->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_incomingView->viewport()->update();
}

std::uint64_t KadPanelBase::selectedIncomingId() const
{
    const auto selection = m_incomingView->selectionModel()->selectedRows();
    if (selection.isEmpty())
        return 0;
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_incomingView->model());
    return proxy ? m_incomingModel->idAt(proxy->mapToSource(selection.first()).row()) : 0;
}

void KadPanelBase::onSearchSelectionChanged()
{
    const std::uint64_t id = selectedSearchId();
    if (id == 0)
        return;
    m_lookupPicked = true;   // auto-show marks rows with the signal blocked
    if (id == m_graphSearchId)
        return;
    m_graphSearchId = id;
    // title and blank graph at once, the picture follows with the reply (MFC UpdateButtonTitle)
    setLookupTabTitle(lookupTitle(id));
    m_lookupGraph->clear(true);
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
