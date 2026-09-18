#include "pch.h"
#include "panels/UsenetPanel.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/UsenetFileCheckList.h"
#include "controls/UsenetProgressDelegate.h"
#include "controls/UsenetQueueModel.h"
#include "utils/PanelPoller.h"
#include "dialogs/UsenetArchiveEntryDialog.h"
#include "dialogs/UsenetDetailsDialog.h"
#include "utils/MenuUtils.h"
#include "utils/NzbAdd.h"
#include "utils/NzbDrop.h"
#include "utils/OtherFunctions.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include "utils/PreviewLauncher.h"
#include "dialogs/AddNzbFilesDialog.h"
#include "dialogs/AddNzbUrlDialog.h"
#include "utils/ListActivation.h"
#include "utils/StatusBarNotifier.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include "controls/CategoryFilterProxy.h"
#include "controls/CategoryTabBar.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QSortFilterProxyModel>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

UsenetPanel::UsenetPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    m_poller = new PanelPoller(this, [this] { refresh(); });
}

UsenetPanel::~UsenetPanel() = default;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void UsenetPanel::setStreamToken(const QString& token)
{
    m_streamToken = token;
    // "Open Incoming Folder" on a category tab needs it against a remote core.
    m_categoryTabBar->setStreamToken(token);
}

void UsenetPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    m_categoryTabBar->setIpcClient(ipc);
    if (!m_ipc)
        return;

    // The list can change from the Transfers tab or another GUI, and this tab
    // has to relabel its column and rebuild its tabs when it does.
    connect(m_ipc, &IpcClient::categoriesChanged, this,
            [this] { m_categoryTabBar->requestCategories(); });

    auto enable = [this] {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
        // Inside `enable`, not beside it: setIpcClient() runs before the
        // handshake, and requestCategories() on a client that is not connected
        // yet returns having asked nothing — leaving the bar showing "All" for
        // the rest of the session.
        m_categoryTabBar->requestCategories();
    };

    if (m_ipc->isConnected()) {
        enable();
    } else {
        connect(m_ipc, &IpcClient::connected, this, enable);
    }

    connect(m_ipc, &IpcClient::disconnected, this, [this] {
        m_poller->setEnabled(false);
        m_model->clear();
        m_maxDownloadKb = m_usenetLimitKb = m_ed2kBudgetKb = 0;
        updateSummary();
        updateEngineAction();
    });
    connect(m_ipc, &IpcClient::connected, this, &UsenetPanel::updateEngineAction);
    connect(m_ipc, &IpcClient::usenetEnginePausedChanged, this,
            &UsenetPanel::updateEngineAction);
    updateEngineAction();

    // The split moves with eD2K's demand, not with anything in this queue, so it
    // cannot ride the item pushes. The stats push comes about once a second.
    connect(m_ipc, &IpcClient::statsUpdated, this, [this](const Ipc::IpcMessage& msg) {
        const QCborMap stats = msg.fieldMap(0);
        const QCborValue ceiling = stats.value(QStringLiteral("maxDownloadKb"));
        if (!ceiling.isInteger())
            return;
        const qint64 usenet = stats.value(QStringLiteral("usenetLimitKb")).toInteger();
        const qint64 ed2k = stats.value(QStringLiteral("ed2kBudgetKb")).toInteger();
        if (ceiling.toInteger() == m_maxDownloadKb && usenet == m_usenetLimitKb
            && ed2k == m_ed2kBudgetKb) {
            return;
        }
        m_maxDownloadKb = ceiling.toInteger();
        m_usenetLimitKb = usenet;
        m_ed2kBudgetKb = ed2k;
        updateSummary();
    });

    // Pull the next poll forward rather than refetching here. Progress on a
    // 10 000-article release is a great many events, and the daemon has already
    // coalesced them per item — refetching the whole queue for each one would put
    // the quadratic cost straight back.
    connect(m_ipc, &IpcClient::usenetItemUpdated, this, [this](const Ipc::IpcMessage& msg) {
        const QCborMap row = msg.fieldMap(0);
        if (!row.isEmpty())
            m_model->upsertItem(usenetRowFromCbor(row));
        updateSummary();
        updateActions();
    });

    connect(m_ipc, &IpcClient::usenetItemRemoved, this, [this](const Ipc::IpcMessage& msg) {
        m_model->removeItem(msg.fieldString(0));
        updateSummary();
        updateActions();
    });

    connect(m_ipc, &IpcClient::usenetItemFinished, this, [this](const Ipc::IpcMessage& msg) {
        const QString id = msg.fieldString(0);
        const QString message = msg.fieldString(2);
        const auto* row = m_model->findById(id);
        StatusBarNotifier::post(row ? QStringLiteral("%1 — %2").arg(row->name, message)
                                    : message);
        m_poller->nudge();
    });
}

void UsenetPanel::addNzbFile(const QString& path, const NzbAddChoices& choices)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add NZB"),
                             tr("Not connected to the eMule core."));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Add NZB"),
                             tr("Cannot read %1.").arg(QFileInfo(path).fileName()));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    // The file's contents travel, not its path: the daemon may be on another
    // machine, where that path resolves to something else or to nothing.
    const QString name = QFileInfo(path).completeBaseName();

    // Every GUI file route funnels here — the file dialog, a panel drop, a window
    // drop, a Finder open and the command line — so this one call gives all five
    // the "you already downloaded this, again?" question.
    gui::sendNzbAdd(m_ipc, this, name,
        [data, name, choices](bool force) {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzb);
            msg.append(QCborValue(data));
            msg.append(name);
            msg.append(false);   // automatic — a person asked for this one
            msg.append(force);
            msg.append(choices.password);   // archive passphrase, usually empty
            msg.append(qint64(choices.category));
            msg.append(qint64(choices.priority));
            msg.append(choices.paused);
            QCborArray skipped;
            for (const int f : choices.skippedFiles)
                skipped.append(f);
            msg.append(QCborValue(skipped));
            return msg;
        },
        [this](bool added) {
            if (added)
                m_poller->refreshNow();
        });
}

bool UsenetPanel::acceptNzbDrop(const QMimeData* mime)
{
    const auto candidates = gui::nzbDropCandidates(mime);
    if (candidates.isEmpty())
        return false;

    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add NZB"), tr("Not connected to the eMule core."));
        return true;   // ours, and refused with a reason — not something to pass on
    }

    for (const QString& path : candidates.files)
        addNzbFile(path);

    for (const QString& url : candidates.urls) {
        gui::sendNzbAdd(m_ipc, this, url,
            [url](bool force) {
                Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzbUrl);
                msg.append(url);
                msg.append(false);   // automatic
                msg.append(force);
                return msg;
            },
            [this](bool added) {
                if (added)
                    m_poller->refreshNow();
            });
    }

    return true;
}

void UsenetPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (gui::nzbDropCandidates(event->mimeData()).isEmpty())
        return;   // not ours: leave the event unaccepted so it can fall through
    event->acceptProposedAction();
}

void UsenetPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (gui::nzbDropCandidates(event->mimeData()).isEmpty())
        return;
    event->acceptProposedAction();
}

void UsenetPanel::dropEvent(QDropEvent* event)
{
    if (acceptNzbDrop(event->mimeData()))
        event->acceptProposedAction();
}

/// The user's categories in their own order, index 0 first — what the add
/// dialogs put in their category box. Taken from the tab strip the panel already
/// keeps in sync rather than asking the daemon again, which would race it.
QStringList UsenetPanel::categoryChoices() const
{
    QStringList names;
    const int count = int(m_categoryTabBar->categories().size());
    names.reserve(count);
    for (int i = 0; i < count; ++i)
        names.append(m_categoryTabBar->categoryTitle(i));
    return names;
}

void UsenetPanel::promptAddNzbUrl()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add NZB from URL"),
                             tr("Not connected to the eMule core."));
        return;
    }

    AddNzbUrlDialog dlg(m_ipc, categoryChoices(), this);
    if (dlg.exec() == QDialog::Accepted)
        m_poller->refreshNow();
}

// ---------------------------------------------------------------------------
// Private — construction
// ---------------------------------------------------------------------------

void UsenetPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));

    m_addAction = toolbar->addAction(menuIcon("ListAdd.ico"), tr("Add NZB…"),
                                     this, &UsenetPanel::onAddNzb);
    toolbar->addSeparator();
    m_pauseAction = toolbar->addAction(menuIcon("Pause.ico"), tr("Pause"),
                                       this, &UsenetPanel::onPause);
    // Start.ico, not Resume.ico: the latter has never existed, and asking for it
    // yields a valid null icon that renders as nothing. tst_MenuIcons pins it.
    m_resumeAction = toolbar->addAction(menuIcon("Start.ico"), tr("Resume"),
                                        this, &UsenetPanel::onResume);
    m_removeAction = toolbar->addAction(menuIcon("ListRemove.ico"), tr("Remove"),
                                        this, [this] { onRemove(false); });
    toolbar->addSeparator();
    // The engine, not the selection: nothing new starts, and no release's
    // status changes — so Resume All cannot wake what the user paused one by one.
    m_pauseAllAction = toolbar->addAction(menuIcon("Pause.ico"), tr("Pause All"),
                                          this, &UsenetPanel::onToggleEnginePause);

    // Context menu and the Tools menu, but not the toolbar: MFC parity argues
    // against a second Add button, and one QAction shared by both menus is what
    // keeps their text and state from drifting apart.
    m_addUrlAction = new QAction(menuIcon("DirectDownload.ico"),
                                 tr("Add NZB from URL…"), this);
    connect(m_addUrlAction, &QAction::triggered, this, &UsenetPanel::promptAddNzbUrl);

    // Context menu only — a toolbar entry would be enabled for most of a
    // release's life and do nothing, because most posts are RAR sets.
    m_previewAction = new QAction(menuIcon("Preview.ico"), tr("Preview"), this);
    connect(m_previewAction, &QAction::triggered, this, &UsenetPanel::onPreview);

    // Context menu only, and for the same reason: a release queued a week ago is
    // a different question from the one answered when it was added, but it is
    // not something anybody wants a toolbar button for.
    m_checkAction = new QAction(menuIcon("ServerInfo.ico"),
                                tr("Check Availability"), this);
    connect(m_checkAction, &QAction::triggered, this,
            &UsenetPanel::onCheckAvailability);
    // Header row: the toolbar, and the category tabs right-aligned beside it —
    // the same arrangement the Transfers tab uses, through the same widget.
    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(2);
    headerRow->addWidget(toolbar, 1);

    m_categoryTabBar = new CategoryTabBar;
    connect(m_categoryTabBar, &CategoryTabBar::currentCategoryChanged, this, [this](int cat) {
        m_categoryProxy->setCategoryFilter(cat);
    });
    connect(m_categoryTabBar, &CategoryTabBar::categoriesReloaded, this, [this] {
        m_model->setCategoryNames(m_categoryTabBar->categoryNames());
    });
    connect(m_categoryTabBar, &CategoryTabBar::menuRequested, this,
            &UsenetPanel::populateCategoryMenu);
    headerRow->addWidget(m_categoryTabBar);

    layout->addLayout(headerRow);

    auto* view = new ListTreeView(this);
    m_view = view;
    m_model = new UsenetQueueModel(this);

    // A file row's checkbox. The row follows the push, not the click.
    connect(m_model, &UsenetQueueModel::fileSkipRequested, this,
            [this](const QString& itemId, int fileIndex, bool skipped) {
        UsenetFileCheckList::sendSkip(m_ipc, this, itemId, {fileIndex}, skipped);
    });

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    // Qt::UserRole, as on Transfers: the model's display strings are formatted
    // ("1.40 GB", "7%", "612.30 KB/s") and sorting those as text is worse than
    // not sorting at all.
    m_proxy->setSortRole(Qt::UserRole);
    m_proxy->setDynamicSortFilter(true);

    // Stacked on the sort proxy, exactly as on the Transfers tab. It reads the
    // category off the row, so the order of the two proxies cannot matter.
    m_categoryProxy = new CategoryFilterProxy(this);
    m_categoryProxy->setSourceModel(m_proxy);
    // The outer proxy is the one bound to the view, so this is the sort role that
    // actually governs; both are set so the pair can never disagree.
    m_categoryProxy->setSortRole(Qt::UserRole);

    m_view->setModel(m_categoryProxy);
    m_view->setItemDelegateForColumn(UsenetQueueModel::ColProgress,
                                     new UsenetProgressDelegate(m_view));
    m_view->setRootIsDecorated(true);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSortingEnabled(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // bindColumns only after setModel(): a header with no sections cannot take a
    // restore, and caching that empty state would destroy the saved layout.
    view->bindColumns(QStringLiteral("usenetQueue"), {280, 80, 110, 160, 85, 85, 60, 60, 90});

    connect(m_view, &QWidget::customContextMenuRequested,
            this, &UsenetPanel::onContextMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] {
        // restoreSelection() clears and re-selects, so this fires twice mid-restore
        // against a half-built selection. The final state is applied right after.
        if (!m_restoringSelection)
            updateActions();
    });

    connect(m_view, &QTreeView::doubleClicked, this, &UsenetPanel::activateRow);

    // Enter mirrors the double-click, Alt+Enter always opens Details — the same
    // pairing TransferPanel and SharedFilesPanel use. Both resolve the row from
    // the view at the moment they run, never from a captured index: this list
    // refreshes about once a second.
    bindListActivation(m_view,
                       [this](const QModelIndex& index) { activateRow(index); },
                       [this](const QModelIndex& index) {
                           if (!index.isValid())
                               return;
                           QModelIndex src = toSourceIndex(index);
                           if (m_model->isFileRow(src))
                               src = src.parent();
                           const QString id = m_model->idAt(src.row());
                           if (!id.isEmpty())
                               showDetails(id);
                       });

    layout->addWidget(m_view, 1);

    m_summary = new QLabel(this);
    m_summary->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_summary);

    // Drops land here as well as on the main window; see acceptNzbDrop().
    setAcceptDrops(true);
    updateActions();
    updateEngineAction();
    updateSummary();
}

// ---------------------------------------------------------------------------
// Private — data
// ---------------------------------------------------------------------------

void UsenetPanel::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetUsenetQueue),
                       [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        applyQueue(resp.fieldArray(1));
    });
}

void UsenetPanel::applyQueue(const QCborArray& rows)
{
    QList<UsenetItemRow> items;
    items.reserve(rows.size());
    for (const auto& v : rows) {
        if (v.isMap())
            items.append(usenetRowFromCbor(v.toMap()));
    }

    // The model updates incrementally, so the view's own state usually survives.
    // Save and restore anyway: an arrival or a departure still moves rows, and a
    // sort on a changing column reorders them under the user.
    const SelectionState state = saveSelection();
    m_model->setItems(items);
    restoreSelection(state);

    updateSummary();
    updateActions();
}

UsenetPanel::SelectionState UsenetPanel::saveSelection() const
{
    SelectionState state;
    state.scrollValue = m_view->verticalScrollBar()->value();

    state.ids = selectedItemIds();

    const QModelIndex current = m_view->selectionModel()->currentIndex();
    if (current.isValid()) {
        QModelIndex src = toSourceIndex(current);
        if (m_model->isFileRow(src))
            src = src.parent();
        state.currentId = m_model->idAt(src.row());
    }

    for (int row = 0; row < m_model->itemCount(); ++row) {
        const QModelIndex proxyIdx = fromSourceIndex(m_model->index(row, 0));
        if (proxyIdx.isValid() && m_view->isExpanded(proxyIdx))
            state.expandedIds << m_model->idAt(row);
    }

    return state;
}

void UsenetPanel::restoreSelection(const SelectionState& state)
{
    for (int row = 0; row < m_model->itemCount(); ++row) {
        if (!state.expandedIds.contains(m_model->idAt(row)))
            continue;
        const QModelIndex proxyIdx = fromSourceIndex(m_model->index(row, 0));
        if (proxyIdx.isValid())
            m_view->setExpanded(proxyIdx, true);
    }

    if (state.ids.isEmpty()) {
        m_view->verticalScrollBar()->setValue(state.scrollValue);
        return;
    }

    QItemSelection selection;
    QModelIndex currentIdx;
    for (int row = 0; row < m_model->itemCount(); ++row) {
        const QString id = m_model->idAt(row);
        if (!state.ids.contains(id))
            continue;

        const QModelIndex proxyIdx = fromSourceIndex(m_model->index(row, 0));
        if (!proxyIdx.isValid())
            continue;

        // selectedRows() only reports a row when every model column is selected,
        // so select the whole row rather than just column 0.
        selection.select(proxyIdx,
                         m_categoryProxy->index(proxyIdx.row(),
                                                UsenetQueueModel::ColCount - 1,
                                                proxyIdx.parent()));
        if (id == state.currentId)
            currentIdx = proxyIdx;
    }

    if (selection.isEmpty()) {
        m_view->verticalScrollBar()->setValue(state.scrollValue);
        return;
    }
    if (!currentIdx.isValid())
        currentIdx = selection.indexes().constFirst();

    m_restoringSelection = true;
    // setCurrentIndex() on the view would ClearAndSelect and collapse the whole
    // selection to one row, so go through the selection model.
    m_view->selectionModel()->setCurrentIndex(currentIdx, QItemSelectionModel::NoUpdate);
    m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    m_restoringSelection = false;
    updateActions();

    m_view->verticalScrollBar()->setValue(state.scrollValue);
}

QStringList UsenetPanel::selectedItemIds() const
{
    QStringList ids;
    const auto rows = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex& proxyIdx : rows) {
        if (proxyIdx.column() != 0)
            continue;
        QModelIndex src = toSourceIndex(proxyIdx);
        // Every action here acts on the NZB, so a selected file row resolves up.
        if (m_model->isFileRow(src))
            src = src.parent();
        const QString id = m_model->idAt(src.row());
        if (!id.isEmpty() && !ids.contains(id))
            ids << id;
    }
    return ids;
}

// ---------------------------------------------------------------------------
// Private — actions
// ---------------------------------------------------------------------------

void UsenetPanel::onContextMenu(const QPoint& pos)
{
    const QStringList ids = selectedItemIds();

    QMenu menu(this);
    menu.addAction(m_addAction);
    menu.addAction(m_addUrlAction);
    if (!ids.isEmpty()) {
        menu.addSeparator();
        menu.addAction(m_pauseAction);
        menu.addAction(m_resumeAction);
        menu.addSeparator();

        // Built from usenet::kUsenetPriorityLevels rather than spelled out, so
        // the menu, the add dialogs and the column cannot drift apart. Five
        // levels share three icons: eMule ships none for "very", and MFC's own
        // download list has only the three.
        auto* priorityMenu = menu.addMenu(menuIcon("FilePriority.ico"), tr("Priority"));
        for (const int level : kUsenetPriorityLevels) {
            const char* icon = level > 0   ? "PriorityHigh.ico"
                               : level < 0 ? "PriorityLow.ico"
                                           : "PriorityNormal.ico";
            priorityMenu->addAction(menuIcon(icon), usenetPriorityName(level), this,
                                    [this, level] { onSetPriority(level); });
        }

        // Batch over the whole selection, as the Transfers tab does.
        auto* catMenu = menu.addMenu(menuIcon("Category.ico"), tr("Assign To Category"));
        // Index 0 is not a category to assign *to* — picking it takes the release
        // out of whatever category it is in, which is why MFC labels it this way.
        catMenu->addAction(tr("No category"), this,
                           [this, ids] { sendSetCategory(ids, 0); });
        const int catCount = int(m_categoryTabBar->categories().size());
        if (catCount > 1)
            catMenu->addSeparator();
        for (int i = 1; i < catCount; ++i) {
            catMenu->addAction(m_categoryTabBar->categoryTitle(i), this,
                               [this, ids, i] { sendSetCategory(ids, i); });
        }
        catMenu->setEnabled(catCount > 1);

        menu.addSeparator();
        // One item only: a passphrase belongs to a release, and setting the same
        // one on four selected downloads is almost always a mistake.
        auto* pwAct = menu.addAction(menuIcon("Security.ico"), tr("Set Password…"), this,
                                     &UsenetPanel::onSetPassword);
        pwAct->setEnabled(ids.size() == 1);

        menu.addSeparator();
        menu.addAction(m_checkAction);
        menu.addAction(m_previewAction);

        // The file rows' checkboxes, for a whole selection at once.
        QHash<QString, QList<int>> toSkip;
        QHash<QString, QList<int>> toFetch;
        for (const QModelIndex& proxyIdx : m_view->selectionModel()->selectedIndexes()) {
            if (proxyIdx.column() != 0)
                continue;
            QString ownerId;
            const UsenetFileRow* f = m_model->fileAt(toSourceIndex(proxyIdx), &ownerId);
            const UsenetItemRow* owner = f ? m_model->findById(ownerId) : nullptr;
            if (!f || f->isPar2 || !owner || !usenetItemAcceptsSkip(owner->status))
                continue;
            (f->skipped ? toFetch : toSkip)[ownerId].append(f->index);
        }
        if (!toSkip.isEmpty() || !toFetch.isEmpty()) {
            menu.addSeparator();
            auto* fetchAct = menu.addAction(menuIcon("Download.ico"), tr("Download Selected Files"),
                                            this, [this, toFetch] {
                for (auto it = toFetch.cbegin(); it != toFetch.cend(); ++it)
                    UsenetFileCheckList::sendSkip(m_ipc, this, it.key(), it.value(), false);
            });
            fetchAct->setEnabled(!toFetch.isEmpty());
            auto* skipAct = menu.addAction(menuIcon("Pause.ico"), tr("Skip Selected Files"),
                                           this, [this, toSkip] {
                for (auto it = toSkip.cbegin(); it != toSkip.cend(); ++it)
                    UsenetFileCheckList::sendSkip(m_ipc, this, it.key(), it.value(), true);
            });
            skipAct->setEnabled(!toSkip.isEmpty());
        }

        // What the double-click does, spelled out. Resolved from the row under
        // the cursor, so a file row offers *that* file and an item row its payload.
        const QModelIndex src = toSourceIndex(m_view->currentIndex());
        QString ownerId;
        const UsenetFileRow* fileRow = m_model->fileAt(src, &ownerId);
        const UsenetItemRow* itemRow = m_model->findById(ids.first());

        const bool openable = fileRow ? !fileRow->finalPath.isEmpty()
                                      : (itemRow && itemRow->status == UsenetRowStatus::Complete
                                         && !itemRow->publishedFiles.isEmpty());

        auto* openAct = menu.addAction(menuIcon("FileOpen.ico"), tr("Open File"), this,
                                       [this, fileRow, ownerId, ids] {
            if (fileRow)
                openUsenetFile(ownerId, fileRow->index);
            else
                openUsenetFile(ids.first(), -1);
        });
        // One selection only: "open" names one file, and doing it for four rows
        // at once would spawn four windows nobody asked for.
        openAct->setEnabled(openable && ids.size() == 1);

        menu.addAction(menuIcon("FolderOpen.ico"), tr("Open Folder"), this,
                       &UsenetPanel::onOpenFolder);
        menu.addAction(menuIcon("FileInfo.ico"), tr("Details…"), this,
                       [this, ids] { showDetails(ids.first()); });

        // Bold, so the entry the double-click runs is visible as such.
        setMenuDefaultAction(&menu, openAct->isEnabled() ? openAct : nullptr);

        menu.addSeparator();
        menu.addAction(m_removeAction);
        menu.addAction(menuIcon("Delete.ico"), tr("Remove and Delete Files"), this,
                       [this] { onRemove(true); });
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void UsenetPanel::onAddNzb()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add NZB"), QString(), tr("NZB files (*.nzb);;All files (*)"));
    if (paths.isEmpty())
        return;

    // A second dialog, and worth it: the passphrase is the one thing about a
    // release that a file picker cannot express, and supplying it here is the
    // difference between a download that finishes and one that fails and has to
    // be retried. A *drop* stays silent — that is the quick path, and the
    // context menu's Set Password… covers it afterwards.
    AddNzbFilesDialog dlg(m_ipc, paths, categoryChoices(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    for (const QString& path : paths) {
        const NzbAddChoices choices{dlg.archivePassword(), dlg.category(), dlg.priority(),
                                    dlg.paused(), dlg.skippedFiles(path)};
        addNzbFile(path, choices);
    }
}

void UsenetPanel::onSetPassword()
{
    const QStringList ids = selectedItemIds();
    if (ids.size() != 1 || !m_ipc || !m_ipc->isConnected())
        return;

    const UsenetItemRow* row = m_model->findById(ids.first());
    const bool has = row && row->hasPassword;

    bool ok = false;
    // Never pre-filled with the stored value: the daemon does not send it, on
    // purpose — the same one-way contract the news-server passwords keep. The
    // placeholder says whether there is one without showing it.
    QInputDialog dlg(this);
    dlg.setWindowTitle(tr("Set Password"));
    dlg.setLabelText(tr("Archive password for \"%1\":")
                         .arg(row ? row->name : tr("this download")));
    dlg.setInputMode(QInputDialog::TextInput);
    dlg.setTextEchoMode(QLineEdit::Password);
    dlg.setTextValue(QString());
    ok = dlg.exec() == QDialog::Accepted;
    if (!ok)
        return;
    const QString password = dlg.textValue();

    // An empty box clears a stored password rather than doing nothing, which is
    // how a user takes back a wrong guess. Confirmed, because the same gesture
    // is what somebody who opened the dialog by mistake makes.
    if (password.isEmpty() && has) {
        if (QMessageBox::question(this, tr("Set Password"),
                                  tr("Remove the stored password for this download?"),
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetItemPassword);
    msg.append(ids.first());
    msg.append(password);
    m_ipc->sendRequest(msg, [this](const Ipc::IpcMessage& reply) {
        if (!reply.isValid())
            return;   // connection dropped
        if (!reply.fieldBool(0)) {
            StatusBarNotifier::post(tr("Could not set the password."));
            return;
        }
        // Setting it on a failed item retries the unpack daemon-side, so the row
        // is about to change status; refresh rather than wait for the poll.
        m_poller->refreshNow();
    });
}

void UsenetPanel::onPause()
{
    if (!m_ipc)
        return;
    for (const QString& id : selectedItemIds()) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::PauseUsenetItem);
        msg.append(id);
        m_ipc->sendRequest(msg, [](const Ipc::IpcMessage&) {});
    }
    m_poller->refreshNow();
}

void UsenetPanel::onResume()
{
    if (!m_ipc)
        return;
    for (const QString& id : selectedItemIds()) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::ResumeUsenetItem);
        msg.append(id);
        m_ipc->sendRequest(msg, [](const Ipc::IpcMessage&) {});
    }
    m_poller->refreshNow();
}

void UsenetPanel::onRemove(bool deleteFiles)
{
    if (!m_ipc)
        return;
    const QStringList ids = selectedItemIds();
    if (ids.isEmpty())
        return;

    if (deleteFiles) {
        const auto answer = QMessageBox::question(
            this, tr("Remove Downloads"),
            tr("Remove %n download(s) and delete the files already fetched?",
               nullptr, int(ids.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    for (const QString& id : ids) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::RemoveUsenetItem);
        msg.append(id);
        msg.append(deleteFiles);
        m_ipc->sendRequest(msg, [](const Ipc::IpcMessage&) {});
    }
    m_poller->refreshNow();
}

void UsenetPanel::onSetPriority(int priority)
{
    if (!m_ipc)
        return;
    for (const QString& id : selectedItemIds()) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetItemPriority);
        msg.append(id);
        msg.append(qint64(priority));
        m_ipc->sendRequest(msg, [](const Ipc::IpcMessage&) {});
    }
    m_poller->refreshNow();
}

void UsenetPanel::onOpenFolder()
{
    const QStringList ids = selectedItemIds();
    if (ids.isEmpty())
        return;

    const auto* row = m_model->findById(ids.first());
    if (!row)
        return;

    // finalPath is a path on the *core's* filesystem. It only means anything to
    // this machine's file manager when the core runs here; otherwise the browse
    // page is the only way to reach what was published.
    if (!m_ipc || !m_ipc->isLocalConnection()) {
        openIncomingFolder(m_ipc, m_streamToken);
        return;
    }

    for (const auto& f : row->files) {
        if (f.finalPath.isEmpty())
            continue;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(f.finalPath).absolutePath()));
        return;
    }

    StatusBarNotifier::post(tr("Nothing has completed yet for \"%1\".").arg(row->name));
}

void UsenetPanel::activateRow(const QModelIndex& proxyIndex)
{
    if (!proxyIndex.isValid())
        return;

    const QModelIndex src = toSourceIndex(proxyIndex);

    // A file row names one file. It opens when the release published it under
    // its own name; a volume that was consumed into an archive never was, and
    // the details view is the useful answer for those.
    QString ownerId;
    if (const UsenetFileRow* f = m_model->fileAt(src, &ownerId)) {
        if (!f->finalPath.isEmpty())
            openUsenetFile(ownerId, f->index);
        else
            showDetails(ownerId);
        return;
    }

    const QString id = m_model->idAt(src.row());
    if (id.isEmpty())
        return;

    const UsenetItemRow* row = m_model->findById(id);
    if (row && row->status == UsenetRowStatus::Complete) {
        openUsenetFile(id, -1);
        return;
    }

    // Anything unfinished keeps Qt's expand-on-double-click. The child rows are
    // the release's files, and opening them is what a double-click here has
    // always meant.
}

void UsenetPanel::openUsenetFile(const QString& itemId, int fileIndex)
{
    const UsenetItemRow* row = m_model->findById(itemId);
    if (!row)
        return;

    QString path;
    QString relPath;
    QString name;

    if (fileIndex >= 0) {
        for (const auto& f : row->files) {
            if (f.index != fileIndex)
                continue;
            path = f.finalPath;
            name = f.name;
            // The queue row carries no per-file relPath; match the published
            // entry by the name the daemon gave it.
            for (const auto& pf : row->publishedFiles) {
                if (pf.path == f.finalPath) {
                    relPath = pf.relPath;
                    break;
                }
            }
            break;
        }
    } else {
        // The item's payload: the biggest thing it published that is not a
        // recovery volume. Same rule previewTarget() uses, and for the same
        // reason — a release carries a sample and sometimes a trailer.
        const UsenetPublishedFile* best = nullptr;
        for (const auto& pf : row->publishedFiles) {
            if (pf.name.endsWith(QLatin1String(".par2"), Qt::CaseInsensitive))
                continue;
            if (!best || pf.size > best->size)
                best = &pf;
        }
        if (best) {
            path = best->path;
            relPath = best->relPath;
            name = best->name;
        }
    }

    if (path.isEmpty()) {
        StatusBarNotifier::post(tr("Nothing has completed yet for \"%1\".").arg(row->name));
        return;
    }

    // Local core: the recorded path is a path on this machine's filesystem, so
    // the file manager's own handler is the right one.
    if (m_ipc && m_ipc->isLocalConnection()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    // Remote core: the bytes live on the daemon's disk and only its web server
    // can reach them. A file outside the incoming folder has no browse address
    // at all, so fall back to the listing rather than build a URL that 404s.
    if (relPath.isEmpty()) {
        openIncomingFolder(m_ipc, m_streamToken);
        return;
    }

    const ED2KFileType type = getED2KFileTypeID(name);
    const bool play = type == ED2KFileType::Video || type == ED2KFileType::Audio;
    openIncomingFileInBrowser(m_ipc, m_streamToken, relPath, play);
}

void UsenetPanel::showDetails(const QString& itemId)
{
    if (itemId.isEmpty())
        return;

    if (m_detailsDialog) {
        m_detailsDialog->raise();
        m_detailsDialog->activateWindow();
        return;
    }

    auto* dialog = new UsenetDetailsDialog(m_ipc, itemId, m_streamToken, this);
    m_detailsDialog = dialog;
    dialog->show();
}

QPair<QString, int> UsenetPanel::previewTarget() const
{
    // A file row names itself.
    const QModelIndex current = m_view->currentIndex();
    if (current.isValid()) {
        const QModelIndex src = toSourceIndex(current);
        QString ownerId;
        if (const UsenetFileRow* f = m_model->fileAt(src, &ownerId))
            return {f->previewable ? ownerId : QString(), f->previewable ? f->index : -1};
    }

    // An item row: the biggest thing in it that can be played. A release
    // carries a sample and sometimes a trailer, and neither is what was asked
    // for.
    const QStringList ids = selectedItemIds();
    if (ids.isEmpty())
        return {QString(), -1};

    const UsenetItemRow* row = m_model->findById(ids.first());
    if (!row)
        return {QString(), -1};

    const UsenetFileRow* best = nullptr;
    for (const auto& f : row->files) {
        if (!f.previewable)
            continue;
        if (!best || f.size > best->size)
            best = &f;
    }
    if (!best)
        return {QString(), -1};
    return {row->id, best->index};
}

void UsenetPanel::onCheckAvailability()
{
    const QStringList ids = selectedItemIds();
    if (!m_ipc || ids.isEmpty())
        return;

    for (const QString& id : ids) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::CheckUsenetItem);
        msg.append(id);
        m_ipc->sendRequest(msg, [this](const Ipc::IpcMessage& resp) {
            // A refusal here is "not right now", not "this release is bad", so
            // it goes to the status bar rather than into a dialog.
            if (!resp.fieldBool(0))
                StatusBarNotifier::post(resp.fieldString(1), 5000);
            m_poller->refreshNow();
        });
    }
}

void UsenetPanel::onPreview()
{
    const auto [itemId, fileIndex] = previewTarget();
    if (itemId.isEmpty() || fileIndex < 0) {
        // A refusal the daemon can explain — a compressed or solid archive —
        // deserves the explanation rather than the generic line.
        const QString note = previewNote();
        StatusBarNotifier::post(note.isEmpty() ? tr("Nothing here can be previewed yet.")
                                               : note);
        return;
    }

    // A release may hold several playable files. Asking which is a round trip,
    // so the dialog is constructed hidden and shows itself only if the answer
    // turns out to be "more than one" — an ordinary single-video release plays
    // without a window ever appearing. See UsenetArchiveEntryDialog.
    if (m_entryDialog) {
        m_entryDialog->raise();
        m_entryDialog->activateWindow();
        return;
    }

    auto* dialog = new UsenetArchiveEntryDialog(m_ipc, itemId, fileIndex, this);
    m_entryDialog = dialog;
    connect(dialog, &UsenetArchiveEntryDialog::entryChosen, this,
            [this, itemId, fileIndex](int entry) { launchEntry(itemId, fileIndex, entry); });
}

void UsenetPanel::launchEntry(const QString& itemId, int fileIndex, int entry)
{
    const QString url = daemonUsenetStreamUrl(m_ipc, itemId, fileIndex, m_streamToken, entry);
    if (url.isEmpty()) {
        StatusBarNotifier::post(
            tr("Preview is unavailable — the daemon's web server is not running."));
        return;
    }

    launchPreview(url);
}

void UsenetPanel::updateActions()
{
    const QStringList ids = selectedItemIds();
    const bool any = !ids.isEmpty();

    m_pauseAction->setEnabled(any);
    m_resumeAction->setEnabled(any);
    m_removeAction->setEnabled(any);
    m_checkAction->setEnabled(any);

    // Both halves matter: the daemon has to say the file is playable *and* the
    // stream token has to have arrived, or the action offers a URL that cannot
    // be built.
    const auto [previewId, previewIndex] = previewTarget();
    m_previewAction->setEnabled(!previewId.isEmpty() && previewIndex >= 0
                                && !m_streamToken.isEmpty());

    // On a disabled action the tooltip is the only place the reason can go.
    const QString note = m_previewAction->isEnabled() ? QString() : previewNote();
    m_previewAction->setToolTip(note);
}

QString UsenetPanel::previewNote() const
{
    const QModelIndex current = m_view->currentIndex();
    if (current.isValid()) {
        const QModelIndex src = toSourceIndex(current);
        if (const UsenetFileRow* f = m_model->fileAt(src))
            return f->previewNote;
    }

    const QStringList ids = selectedItemIds();
    if (ids.isEmpty())
        return {};
    const UsenetItemRow* row = m_model->findById(ids.first());
    if (!row)
        return {};

    // One note for the whole set: every volume of a refused archive carries the
    // same one, so the first is as good as any.
    for (const auto& f : row->files) {
        if (!f.previewNote.isEmpty())
            return f.previewNote;
    }
    return {};
}

void UsenetPanel::updateSummary()
{
    const int count = m_model->itemCount();
    if (count == 0) {
        m_summary->setText(tr("No Usenet downloads. Use \"Add NZB…\" to queue one."));
        m_summary->setToolTip({});
        return;
    }

    int active = 0;
    qint64 total = 0;
    qint64 done = 0;
    QString stalled;
    for (int i = 0; i < count; ++i) {
        const auto* row = m_model->findById(m_model->idAt(i));
        if (!row)
            continue;
        // The reason is queue-level, so every stalled row carries the same
        // sentence and the first is as good as any.
        if (stalled.isEmpty() && !row->stalledReason.isEmpty())
            stalled = row->stalledReason;
        // Post-processing counts as active: the item is still working, and a
        // summary that says "0 active" while a repair is running reads as a
        // stall.
        if (row->status == UsenetRowStatus::Downloading
            || row->status == UsenetRowStatus::Queued
            || isPostProcessing(row->status)) {
            ++active;
        }
        total += row->totalBytes;
        done += row->decodedBytes;
    }

    const int percent = total > 0 ? int(done * 100 / total) : 0;
    QString text = tr("%1 download(s), %2 active — %3% complete")
                       .arg(count)
                       .arg(active)
                       .arg(percent);

    // A queue that has stopped must say so here too. The Status column carries
    // it per row, but a user watching the totals sit still is looking at this
    // line, and "0 active" on its own reads as a bug.
    if (!stalled.isEmpty())
        text += tr(" — %1").arg(stalled);

    // Only with a limit set and something to download: otherwise there is no
    // split, and a cap on an idle queue answers a question nobody asked.
    QString splitTip;
    if (active > 0 && m_maxDownloadKb > 0) {
        if (m_usenetLimitKb < m_maxDownloadKb) {
            text += tr(" — limited to %1 KB/s while eD2K downloads").arg(m_usenetLimitKb);
        }
        splitTip = tr("Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.\n"
                      "Whichever network is idle lends its share to the other.")
                       .arg(m_maxDownloadKb)
                       .arg(m_usenetLimitKb)
                       .arg(m_ed2kBudgetKb);
    }

    m_summary->setText(text);
    m_summary->setToolTip(splitTip);

    if (!stalled.isEmpty() && stalled != m_lastStallNotice) {
        StatusBarNotifier::post(tr("Usenet: %1").arg(stalled), 6000);
    } else if (stalled.isEmpty() && !m_lastStallNotice.isEmpty()) {
        StatusBarNotifier::post(tr("Usenet: downloading again."), 4000);
    }
    m_lastStallNotice = stalled;
}

void UsenetPanel::onToggleEnginePause()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    // The label follows the push, not the click, so a refused or lost request
    // cannot leave the button lying.
    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetPaused);
    msg.append(!m_ipc->usenetEnginePaused());
    m_ipc->sendRequest(std::move(msg));
}

void UsenetPanel::updateEngineAction()
{
    const bool paused = m_ipc && m_ipc->usenetEnginePaused();
    m_pauseAllAction->setText(paused ? tr("Resume All") : tr("Pause All"));
    m_pauseAllAction->setIcon(paused ? menuIcon("Start.ico") : menuIcon("Pause.ico"));
    m_pauseAllAction->setToolTip(
        paused ? tr("Let every Usenet download continue")
               : tr("Stop starting new Usenet articles. Nothing is removed, and each "
                    "release keeps its own state."));
    m_pauseAllAction->setEnabled(m_ipc && m_ipc->isConnected());
}

QModelIndex UsenetPanel::toSourceIndex(const QModelIndex& viewIndex) const
{
    // Two proxies now sit between the view and the model — the sort proxy, and
    // the category filter on top of it. Mapping through only one silently yields
    // the wrong row rather than an invalid index, which is precisely the bug the
    // Transfers tab's filter used to have.
    if (!viewIndex.isValid())
        return {};
    return m_proxy->mapToSource(m_categoryProxy->mapToSource(viewIndex));
}

QModelIndex UsenetPanel::fromSourceIndex(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid())
        return {};
    // Invalid at the second hop is normal, not an error: it means the row is
    // filtered out by the current category tab.
    return m_categoryProxy->mapFromSource(m_proxy->mapFromSource(sourceIndex));
}

void UsenetPanel::populateCategoryMenu(QMenu* menu, int index)
{
    if (!menu)
        return;

    menu->addAction(menuIcon("Pause.ico"), tr("Pause"), this, [this, index] {
        sendCategoryStatus(index, Ipc::CategoryAction::Pause);
    });
    menu->addAction(menuIcon("Start.ico"), tr("Resume"), this, [this, index] {
        sendCategoryStatus(index, Ipc::CategoryAction::Resume);
    });
    // No Stop and no Resume-next: Stop keeps an ED2K file and drops its sources,
    // and Usenet has no sources; Resume-next ranks paused files by the category's
    // a4af priority, which is an ED2K concept. The daemon refuses both.
    menu->addAction(menuIcon("Delete.ico"), tr("Cancel"), this, [this, index] {
        // MFC asks before a cancel (IDS_Q_CANCELDL) and so does this: it deletes
        // every article already fetched for those releases, and there is no undo.
        if (QMessageBox::question(
                this, tr("Cancel"),
                tr("Remove every Usenet download in \"%1\" and delete its files?")
                    .arg(m_categoryTabBar->categoryTitle(index)))
            == QMessageBox::Yes)
        {
            sendCategoryStatus(index, Ipc::CategoryAction::Cancel);
        }
    });
}

void UsenetPanel::sendCategoryStatus(int index, Ipc::CategoryAction action)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetCategoryStatus);
    msg.append(static_cast<qint64>(index));
    msg.append(static_cast<qint64>(action));
    m_ipc->sendRequest(msg, [this](const Ipc::IpcMessage& resp) {
        if (!resp.isValid())
            return;   // connection dropped
        if (!resp.fieldBool(0)) {
            StatusBarNotifier::post(
                tr("Could not apply that to the category: %1").arg(resp.field(1).toString()));
        }
        m_poller->refreshNow();
    });
}

void UsenetPanel::sendSetCategory(const QStringList& ids, int category)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    for (const QString& id : ids) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetItemCategory);
        msg.append(id);
        msg.append(static_cast<qint64>(category));
        m_ipc->sendRequest(msg, [](const Ipc::IpcMessage&) {});
    }
    // The daemon pushes the changed rows, but a refresh here is what makes the
    // release leave the tab it was filtered into while the user is watching.
    m_poller->refreshNow();
}

} // namespace eMule
