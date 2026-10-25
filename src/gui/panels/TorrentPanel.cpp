#include "pch.h"
/// @file TorrentPanel.cpp
/// @brief The Torrents tab — implementation.

#include "panels/TorrentPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "controls/TorrentFilterProxy.h"
#include "controls/TorrentListModel.h"
#include "controls/TorrentListsPane.h"
#include "controls/TorrentProgressDelegate.h"
#include "dialogs/AddTorrentDialog.h"
#include "dialogs/TorrentDetailsDialog.h"
#include "utils/Clipboard.h"
#include "utils/ListActivation.h"
#include "utils/MenuUtils.h"
#include "utils/PanelPoller.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"
#include "utils/TorrentAdd.h"
#include "utils/TorrentLinks.h"
#include "utils/ViewNavigation.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <utility>
#include <memory>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

TorrentPanel::TorrentPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    m_poller = new PanelPoller(this, [this] { refresh(); });
}

TorrentPanel::~TorrentPanel() = default;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void TorrentPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    m_pane->setIpcClient(ipc);
    if (!m_ipc)
        return;

    auto enable = [this] {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
        requestStatus();
    };
    if (m_ipc->isConnected())
        enable();
    else
        connect(m_ipc, &IpcClient::connected, this, enable);

    connect(m_ipc, &IpcClient::disconnected, this, [this] {
        m_poller->setEnabled(false);
        m_model->clear();
        m_status = {};
        m_statusKnown = false;
        updateSummary();
        updateActions();
    });

    // Rows ride the pushes; the poll only catches what a push missed
    auto upsert = [this](const IpcMessage& msg) {
        const QCborMap row = msg.fieldMap(0);
        if (!row.isEmpty())
            m_model->upsertRow(torrentRowFromCbor(row));
        updateSummary();
        updateActions();
    };
    connect(m_ipc, &IpcClient::torrentAdded, this, upsert);
    connect(m_ipc, &IpcClient::torrentUpdated, this, upsert);
    connect(m_ipc, &IpcClient::torrentRemoved, this, [this](const IpcMessage& msg) {
        m_model->removeRow(msg.fieldString(0));
        updateSummary();
        updateActions();
    });
    connect(m_ipc, &IpcClient::torrentFinished, this, [this](const IpcMessage& msg) {
        const TorrentRow* row = m_model->findById(msg.fieldString(0));
        StatusBarNotifier::post(tr("Torrent download completed: %1")
                                    .arg(row ? row->name : msg.fieldString(0)),
                                6000);
        m_poller->nudge();
    });
    connect(m_ipc, &IpcClient::btStatusChanged, this, [this](const IpcMessage& msg) {
        applyStatus(msg.fieldMap(0));
        m_poller->nudge();
    });
}

void TorrentPanel::addTorrentFiles(const QStringList& paths)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add Torrent"), tr("Not connected to the eMule core."));
        return;
    }
    for (const QString& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Add Torrent"),
                                 tr("Cannot read %1.").arg(QFileInfo(path).fileName()));
            continue;
        }
        // The bytes travel: the daemon may be on another machine
        addTorrentData(QFileInfo(path).fileName(), file.readAll());
    }
}

void TorrentPanel::addTorrentData(const QString& name, const QByteArray& data)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add Torrent"), tr("Not connected to the eMule core."));
        return;
    }
    gui::sendTorrentAdd(m_ipc, this, name, gui::torrentFileRequest(data, {}, false),
                        [this](bool added) {
                            if (added)
                                m_poller->refreshNow();
                        });
}

void TorrentPanel::addMagnets(const QStringList& magnets)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add Torrent"), tr("Not connected to the eMule core."));
        return;
    }
    for (const QString& uri : magnets) {
        gui::sendTorrentAdd(m_ipc, this, gui::magnetDisplayName(uri),
                            gui::magnetRequest(uri, {}, false), [this](bool added) {
                                if (added)
                                    m_poller->refreshNow();
                            });
    }
}

void TorrentPanel::promptAddMagnets(const QStringList& prefill)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add Magnet Links"), tr("Not connected to the eMule core."));
        return;
    }
    AddTorrentDialog dlg(m_ipc, AddTorrentDialog::Mode::Magnets, prefill, this);
    if (dlg.exec() == QDialog::Accepted)
        m_poller->refreshNow();
}

void TorrentPanel::promptAddTorrentFiles()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Add Torrent Files"), tr("Not connected to the eMule core."));
        return;
    }
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Torrent Files"), QString(),
        tr("Torrent files (*.torrent);;All files (*)"));
    if (paths.isEmpty())
        return;
    AddTorrentDialog dlg(m_ipc, AddTorrentDialog::Mode::Files, paths, this);
    if (dlg.exec() == QDialog::Accepted)
        m_poller->refreshNow();
}

bool TorrentPanel::acceptTorrentDrop(const QMimeData* mime)
{
    const QStringList files = gui::torrentDropFiles(mime);
    if (files.isEmpty())
        return false;
    addTorrentFiles(files);
    return true;
}

void TorrentPanel::switchToSubTab(int tab)
{
    if (m_proxy->rowCount() == 0) {
        m_pendingSubTab = tab;
        return;
    }
    m_view->setCurrentIndex(m_proxy->index(0, 0));
    if (tab >= TorrentListsPane::TabPeers && tab <= TorrentListsPane::TabFiles) {
        m_pane->tabs()->setCurrentIndex(tab);
        return;
    }
    const QString id = m_model->idAt(m_proxy->mapToSource(m_proxy->index(0, 0)).row());
    showDetails(id);
    if (m_detailsDialog)
        m_detailsDialog->tabs()->setCurrentIndex(std::clamp(tab - 3, 0, int(TorrentDetailsDialog::TabOptions)));
}

bool TorrentPanel::engineRunning() const
{
    return m_status.value(QStringLiteral("running")).toBool();
}

void TorrentPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!gui::torrentDropFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void TorrentPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!gui::torrentDropFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void TorrentPanel::dropEvent(QDropEvent* event)
{
    if (acceptTorrentDrop(event->mimeData()))
        event->acceptProposedAction();
}

// ---------------------------------------------------------------------------
// Private — construction
// ---------------------------------------------------------------------------

void TorrentPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));

    m_addFileAction = toolbar->addAction(menuIcon("ListAdd.ico"), tr("Add Torrent…"), this,
                                         &TorrentPanel::promptAddTorrentFiles);
    m_addMagnetAction = toolbar->addAction(menuIcon("DirectDownload.ico"), tr("Add Magnet Link…"),
                                           this, [this] { promptAddMagnets(); });
    toolbar->addSeparator();
    m_pauseAction = toolbar->addAction(menuIcon("Pause.ico"), tr("Pause"), this,
                                       [this] { sendForSelection(IpcMsgType::PauseTorrent); });
    m_resumeAction = toolbar->addAction(menuIcon("Start.ico"), tr("Resume"), this,
                                        [this] { sendForSelection(IpcMsgType::ResumeTorrent); });
    m_removeAction = toolbar->addAction(menuIcon("ListRemove.ico"), tr("Remove…"), this,
                                        &TorrentPanel::onRemove);
    toolbar->addSeparator();
    m_detailsAction = toolbar->addAction(menuIcon("FileInfo.ico"), tr("Details…"), this, [this] {
        const QStringList ids = selectedIds();
        if (!ids.isEmpty())
            showDetails(ids.first());
    });

    // Context menu only
    m_recheckAction = new QAction(menuIcon("HardDisk.ico"), tr("Force Recheck"), this);
    connect(m_recheckAction, &QAction::triggered, this,
            [this] { sendForSelection(IpcMsgType::RecheckTorrent); });
    m_reannounceAction = new QAction(menuIcon("ServersUpdate.ico"), tr("Force Reannounce"), this);
    connect(m_reannounceAction, &QAction::triggered, this,
            [this] { sendForSelection(IpcMsgType::ReannounceTorrent); });
    m_openFolderAction = new QAction(menuIcon("FolderOpen.ico"), tr("Open Folder"), this);
    connect(m_openFolderAction, &QAction::triggered, this, &TorrentPanel::onOpenFolder);
    m_copyMagnetAction = new QAction(menuIcon("Copy.ico"), tr("Copy Magnet Link"), this);
    connect(m_copyMagnetAction, &QAction::triggered, this, &TorrentPanel::onCopyMagnet);

    layout->addWidget(toolbar);
    layout->addWidget(createFilterBar());

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setHandleWidth(4);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    auto* view = new ListTreeView(m_splitter);
    m_view = view;
    m_model = new TorrentListModel(this);
    m_proxy = new TorrentFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilter(static_cast<TorrentFilterProxy::Filter>(
        std::clamp(theUiState.torrentFilter(), 0, int(TorrentFilterProxy::FilterCount) - 1)));
    m_proxy->setSortRole(Qt::UserRole);   // raw values, not "1.40 GB"
    m_proxy->setDynamicSortFilter(true);

    m_view->setModel(m_proxy);
    m_view->setItemDelegateForColumn(TorrentListModel::ColProgress,
                                     new TorrentProgressDelegate(m_view));
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSortingEnabled(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    view->setDescendingFirst({TorrentListModel::ColSize, TorrentListModel::ColDone,
                              TorrentListModel::ColProgress, TorrentListModel::ColDownSpeed,
                              TorrentListModel::ColUpSpeed, TorrentListModel::ColSeeds,
                              TorrentListModel::ColPeers, TorrentListModel::ColRatio,
                              TorrentListModel::ColAdded});
    // After setModel(): a header without sections cannot take a restore
    view->bindColumns(QStringLiteral("torrentList"),
                      {260, 80, 80, 150, 110, 80, 80, 75, 75, 50, 90, 120});

    connect(m_view, &QWidget::customContextMenuRequested, this, &TorrentPanel::onContextMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &TorrentPanel::onSelectionChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &TorrentPanel::onSelectionChanged);

    // Double click / Enter / Alt+Enter: Details, as on the MFC download list
    auto details = [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        const QString id = m_model->idAt(m_proxy->mapToSource(index).row());
        if (!id.isEmpty())
            showDetails(id);
    };
    connect(m_view, &QTreeView::doubleClicked, this, details);
    ListKeyHandlers keys;
    keys.activate = details;
    keys.details = details;
    keys.remove = [this] { onRemove(); };
    keys.copy = [this] { onCopyMagnet(); };
    keys.paste = [this] { onPasteMagnets(); };
    bindListKeys(m_view, std::move(keys));

    m_pane = new TorrentListsPane(m_splitter);
    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_pane);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    theUiState.bindTorrentSplitter(m_splitter);
    layout->addWidget(m_splitter, 1);

    // Status line: engine state, and the way to Options when it is off
    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(6, 2, 6, 2);
    m_summary = new QLabel(this);
    statusRow->addWidget(m_summary, 1);
    m_optionsButton = new QPushButton(menuIcon("Preferences.ico"), tr("BitTorrent Options…"), this);
    m_optionsButton->setVisible(false);
    connect(m_optionsButton, &QPushButton::clicked, this, &TorrentPanel::optionsRequested);
    statusRow->addWidget(m_optionsButton);
    layout->addLayout(statusRow);

    setAcceptDrops(true);
    updateActions();
    updateSummary();
}

// ---------------------------------------------------------------------------
// Private — data
// ---------------------------------------------------------------------------

void TorrentPanel::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetTorrents), [this](const IpcMessage& resp) {
        if (!resp.isValid() || !resp.fieldBool(0))
            return;
        QList<TorrentRow> rows;
        for (const auto& v : resp.fieldArray(1)) {
            if (v.isMap())
                rows.append(torrentRowFromCbor(v.toMap()));
        }
        m_model->setRows(rows);
        updateSummary();
        onSelectionChanged();
        if (m_pendingSubTab >= 0 && m_proxy->rowCount() > 0)
            switchToSubTab(std::exchange(m_pendingSubTab, -1));
    });
    // Start/stop is pushed, DHT size is not: refresh it with the list
    requestStatus();
}

void TorrentPanel::requestStatus()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetBtStatus), [this](const IpcMessage& resp) {
        if (resp.isValid() && resp.fieldBool(0))
            applyStatus(resp.fieldMap(1));
    });
}

void TorrentPanel::applyStatus(const QCborMap& status)
{
    m_status = status;
    m_statusKnown = true;
    updateSummary();
    updateActions();
}

QStringList TorrentPanel::selectedIds() const
{
    QStringList ids;
    for (const QModelIndex& index : m_view->selectionModel()->selectedRows()) {
        const QString id = m_model->idAt(m_proxy->mapToSource(index).row());
        if (!id.isEmpty())
            ids << id;
    }
    return ids;
}

const TorrentRow* TorrentPanel::currentRow() const
{
    const QStringList ids = selectedIds();
    return ids.isEmpty() ? nullptr : m_model->findById(ids.first());
}

// ---------------------------------------------------------------------------
// Private — actions
// ---------------------------------------------------------------------------

void TorrentPanel::onContextMenu(const QPoint& pos)
{
    const QStringList ids = selectedIds();

    QMenu menu(this);
    if (!ids.isEmpty()) {
        menu.addAction(m_pauseAction);
        menu.addAction(m_resumeAction);
        menu.addSeparator();
        menu.addAction(m_recheckAction);
        menu.addAction(m_reannounceAction);
        menu.addSeparator();
        menu.addAction(m_openFolderAction);
        menu.addAction(m_copyMagnetAction);
        menu.addAction(m_detailsAction);
        setMenuDefaultAction(&menu, m_detailsAction);
        menu.addSeparator();
        menu.addAction(m_removeAction);
        menu.addSeparator();
    }
    menu.addAction(m_addFileAction);
    menu.addAction(m_addMagnetAction);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void TorrentPanel::sendForSelection(IpcMsgType type)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    for (const QString& id : selectedIds()) {
        IpcMessage msg(type);
        msg.append(id);
        m_ipc->sendRequest(std::move(msg), [](const IpcMessage& resp) {
            if (resp.isValid() && resp.type() == IpcMsgType::Error)
                StatusBarNotifier::post(tr("BitTorrent: %1").arg(resp.fieldString(1)), 5000);
        });
    }
    m_poller->refreshNow();
}

void TorrentPanel::onRemove()
{
    const QStringList ids = selectedIds();
    if (ids.isEmpty() || !m_ipc || !m_ipc->isConnected())
        return;

    const TorrentRow* first = m_model->findById(ids.first());
    QMessageBox box(QMessageBox::Question, tr("Remove Torrents"),
                    ids.size() == 1 && first
                        ? tr("Remove \"%1\" from the list?").arg(first->name)
                        : tr("Remove %n torrent(s) from the list?", nullptr, int(ids.size())),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    auto* deleteFiles = new QCheckBox(tr("Also delete the downloaded files"), &box);
    box.setCheckBox(deleteFiles);
    if (box.exec() != QMessageBox::Yes)
        return;

    const bool del = deleteFiles->isChecked();
    for (const QString& id : ids) {
        IpcMessage msg(IpcMsgType::RemoveTorrent);
        msg.append(id);
        msg.append(del);
        m_ipc->sendRequest(std::move(msg));
    }
    m_poller->refreshNow();
}

void TorrentPanel::onOpenFolder()
{
    const TorrentRow* row = currentRow();
    if (!row || row->savePath.isEmpty())
        return;

    // savePath is on the core's disk: only a local core can open it here
    if (!m_ipc || !m_ipc->isLocalConnection()) {
        StatusBarNotifier::post(tr("Saved on the core's machine: %1").arg(row->savePath), 6000);
        return;
    }
    // A multi-file torrent lives in a folder named after it
    const QString inner = QDir(row->savePath).filePath(row->name);
    const QString folder = row->numFiles > 1 && QFileInfo(inner).isDir() ? inner : row->savePath;
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void TorrentPanel::onCopyMagnet()
{
    QStringList links;
    for (const QString& id : selectedIds()) {
        const TorrentRow* row = m_model->findById(id);
        if (!row)
            continue;
        const QString link = gui::torrentMagnetLink(row->hashV1, row->name);
        if (!link.isEmpty())
            links << link;
    }
    if (links.isEmpty()) {
        StatusBarNotifier::post(tr("No magnet link: v2-only torrents have no v1 info hash."), 5000);
        return;
    }
    eMule::Clipboard::setText(links.join(QLatin1Char('\n')));
    StatusBarNotifier::post(tr("Copied %n magnet link(s).", nullptr, int(links.size())), 3000);
}

void TorrentPanel::onPasteMagnets()
{
    const QStringList magnets = gui::bitTorrentMagnetsIn(QApplication::clipboard()->text());
    if (!magnets.isEmpty())
        promptAddMagnets(magnets);
}

void TorrentPanel::showDetails(const QString& id)
{
    if (id.isEmpty())
        return;
    if (m_detailsDialog) {
        // one inspector, as in Transmission: it follows the torrent asked for
        m_detailsDialog->setTorrentId(id);
        m_detailsDialog->setWalker(makeWalker(id));
        m_detailsDialog->raise();
        m_detailsDialog->activateWindow();
        return;
    }
    auto* dialog = new TorrentDetailsDialog(m_ipc, m_model, id, this);
    dialog->setWalker(makeWalker(id));
    m_detailsDialog = dialog;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

DetailWalker TorrentPanel::makeWalker(const QString& id)
{
    // Anchored by id: the model merges rows on each poll, an index would go stale
    auto anchor = std::make_shared<QString>(id);
    auto indexOf = [this](const QString& key) -> QModelIndex {
        const int row = m_model->rowOf(key);
        return row < 0 ? QModelIndex{} : ViewNav::fromSource(m_view, m_model->index(row, 0));
    };
    DetailWalker walker;
    walker.step = [this, anchor, indexOf](int delta) -> QString {
        const QModelIndex to = ViewNav::step(m_view, indexOf(*anchor), delta, &ViewNav::isTopLevel);
        if (!to.isValid())
            return {};
        *anchor = m_model->idAt(ViewNav::toSource(to).row());
        return *anchor;
    };
    walker.canStep = [this, anchor, indexOf](int delta) {
        return ViewNav::peekStep(m_view, indexOf(*anchor), delta, &ViewNav::isTopLevel).isValid();
    };
    return walker;
}

QWidget* TorrentPanel::createFilterBar()
{
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(2);
    m_filterGroup = new QButtonGroup(bar);
    m_filterGroup->setExclusive(true);
    for (int f = 0; f < TorrentFilterProxy::FilterCount; ++f) {
        auto* b = new QToolButton(bar);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setText(TorrentFilterProxy::filterName(static_cast<TorrentFilterProxy::Filter>(f)));
        m_filterGroup->addButton(b, f);
        row->addWidget(b);
    }
    row->addStretch(1);
    if (auto* b = m_filterGroup->button(std::clamp(theUiState.torrentFilter(), 0,
                                                   int(TorrentFilterProxy::FilterCount) - 1)))
        b->setChecked(true);
    connect(m_filterGroup, &QButtonGroup::idClicked, this, &TorrentPanel::setFilter);
    return bar;
}

void TorrentPanel::setFilter(int filter)
{
    m_proxy->setFilter(static_cast<TorrentFilterProxy::Filter>(filter));
    theUiState.setTorrentFilter(filter);
    updateSummary();
    onSelectionChanged();
}

void TorrentPanel::updateFilterCounts()
{
    std::array<int, TorrentFilterProxy::FilterCount> counts{};
    for (int i = 0; i < m_model->count(); ++i) {
        const TorrentRow* r = m_model->rowAt(i);
        if (!r)
            continue;
        for (int f = 0; f < TorrentFilterProxy::FilterCount; ++f)
            if (TorrentFilterProxy::matches(*r, static_cast<TorrentFilterProxy::Filter>(f)))
                ++counts[static_cast<std::size_t>(f)];
    }
    for (int f = 0; f < TorrentFilterProxy::FilterCount; ++f) {
        auto* b = m_filterGroup->button(f);
        if (!b)
            continue;
        const QString text = QStringLiteral("%1 (%2)")
                                 .arg(TorrentFilterProxy::filterName(static_cast<TorrentFilterProxy::Filter>(f)))
                                 .arg(counts[static_cast<std::size_t>(f)]);
        if (b->text() != text)
            b->setText(text);
    }
}

void TorrentPanel::onSelectionChanged()
{
    updateActions();
    // the pane follows the current row; with several selected and none current, the first
    QString id;
    const QModelIndex current = m_view->selectionModel()->currentIndex();
    if (current.isValid() && m_view->selectionModel()->isRowSelected(current.row(), current.parent()))
        id = m_model->idAt(m_proxy->mapToSource(current).row());
    if (id.isEmpty()) {
        const QStringList ids = selectedIds();
        id = ids.isEmpty() ? QString() : ids.first();
    }
    m_pane->setTorrentId(id);
}

void TorrentPanel::updateActions()
{
    const QStringList ids = selectedIds();
    const bool any = !ids.isEmpty();
    const bool running = engineRunning();
    const bool connected = m_ipc && m_ipc->isConnected();

    bool canPause = false;
    bool canResume = false;
    bool hasMagnet = false;
    for (const QString& id : ids) {
        const TorrentRow* row = m_model->findById(id);
        if (!row)
            continue;
        (row->paused ? canResume : canPause) = true;
        hasMagnet = hasMagnet || !row->hashV1.isEmpty();
    }

    m_addFileAction->setEnabled(connected);
    m_addMagnetAction->setEnabled(connected);
    m_pauseAction->setEnabled(running && canPause);
    m_resumeAction->setEnabled(running && canResume);
    m_removeAction->setEnabled(running && any);
    m_recheckAction->setEnabled(running && any);
    m_reannounceAction->setEnabled(running && any);
    m_openFolderAction->setEnabled(any);
    m_copyMagnetAction->setEnabled(hasMagnet);
    m_detailsAction->setEnabled(running && any);
}

void TorrentPanel::updateSummary()
{
    updateFilterCounts();
    const bool connected = m_ipc && m_ipc->isConnected();
    const bool available = m_status.value(QStringLiteral("available")).toBool(true);
    const bool running = engineRunning();

    QString notice;
    if (!connected) {
        m_summary->setText(tr("Not connected to the eMule core."));
    } else if (!m_statusKnown) {
        m_summary->setText(tr("Asking the core for the BitTorrent state…"));
    } else if (!available) {
        notice = tr("This eMule core was built without BitTorrent support.");
        m_summary->setText(notice);
    } else if (!running) {
        notice = tr("BitTorrent is off — enable it in Options.");
        m_summary->setText(notice);
    } else {
        // Rates summed from the rows: they move with every push, the status does not
        int active = 0;
        qint64 down = 0;
        qint64 up = 0;
        for (int i = 0; i < m_model->count(); ++i) {
            const TorrentRow* r = m_model->rowAt(i);
            if (!r)
                continue;
            if (torrentStateActive(r->state))
                ++active;
            down += r->rateDown;
            up += r->rateUp;
        }
        const qint64 port = m_status.value(QStringLiteral("listenPort")).toInteger();
        const bool listening = m_status.value(QStringLiteral("listening")).toBool();
        const qint64 dht = m_status.value(QStringLiteral("dhtNodes")).toInteger();
        m_summary->setText(
            tr("%n torrent(s), %1 active — Down: %2, Up: %3 — DHT: %4 nodes — Port %5 %6", nullptr,
               m_model->count())
                .arg(active)
                .arg(formatByteRate(down), formatByteRate(up))
                .arg(dht)
                .arg(port)
                .arg(listening ? tr("(listening)") : tr("(not listening)")));
        if (m_model->count() == 0)
            notice = tr("No torrents. Use \"Add Torrent…\" or \"Add Magnet Link…\" to start one.");
        else if (m_proxy->rowCount() == 0)
            notice = tr("No torrents match this filter.");
    }

    m_optionsButton->setVisible(connected && m_statusKnown && available && !running);
    // Drawn centred in the empty list by AbstractListView
    m_view->setProperty("emptyNotice", notice);
    m_view->viewport()->update();
}

} // namespace eMule
