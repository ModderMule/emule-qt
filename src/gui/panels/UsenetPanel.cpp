#include "pch.h"
#include "panels/UsenetPanel.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/UsenetQueueModel.h"
#include "utils/PanelPoller.h"
#include "dialogs/UsenetArchiveEntryDialog.h"
#include "utils/PreviewLauncher.h"
#include "utils/StatusBarNotifier.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

/// Mirrors usenet::UsenetItemStatus. Read as an int off the wire, because the GUI
/// does not link eMule::Usenet.
[[nodiscard]] UsenetRowStatus statusFromInt(int v)
{
    switch (v) {
    case 1: return UsenetRowStatus::Downloading;
    case 2: return UsenetRowStatus::Paused;
    case 3: return UsenetRowStatus::Complete;
    case 4: return UsenetRowStatus::Failed;
    case 5: return UsenetRowStatus::Verifying;
    case 6: return UsenetRowStatus::Repairing;
    case 7: return UsenetRowStatus::Unpacking;
    default: return UsenetRowStatus::Queued;
    }
}

[[nodiscard]] UsenetItemRow rowFromCbor(const QCborMap& m)
{
    UsenetItemRow r;
    r.id = m.value(QStringLiteral("id")).toString();
    r.name = m.value(QStringLiteral("name")).toString();
    r.status = statusFromInt(int(m.value(QStringLiteral("status")).toInteger()));
    r.statusText = m.value(QStringLiteral("statusText")).toString();
    r.postPercent = m.value(QStringLiteral("postPercent")).toInteger(0);
    r.postDetail = m.value(QStringLiteral("postDetail")).toString();
    r.priority = int(m.value(QStringLiteral("priority")).toInteger());
    r.percent = int(m.value(QStringLiteral("percent")).toInteger());
    r.totalBytes = m.value(QStringLiteral("totalBytes")).toInteger();
    r.decodedBytes = m.value(QStringLiteral("decodedBytes")).toInteger();
    r.segmentCount = int(m.value(QStringLiteral("segmentCount")).toInteger());
    r.doneSegments = int(m.value(QStringLiteral("doneSegments")).toInteger());
    r.missingSegments = int(m.value(QStringLiteral("missingSegments")).toInteger());
    r.error = m.value(QStringLiteral("error")).toString();

    const QCborArray files = m.value(QStringLiteral("files")).toArray();
    r.files.reserve(files.size());
    for (const auto& fv : files) {
        const QCborMap fm = fv.toMap();
        UsenetFileRow f;
        f.name = fm.value(QStringLiteral("name")).toString();
        f.size = fm.value(QStringLiteral("size")).toInteger();
        f.percent = int(fm.value(QStringLiteral("percent")).toInteger());
        f.finalPath = fm.value(QStringLiteral("finalPath")).toString();
        f.isPar2 = fm.value(QStringLiteral("isPar2")).toBool();
        f.missingSegments = int(fm.value(QStringLiteral("missingSegments")).toInteger());
        f.index = int(fm.value(QStringLiteral("index")).toInteger(-1));
        f.previewable = fm.value(QStringLiteral("previewable")).toBool();
        f.previewNote = fm.value(QStringLiteral("previewNote")).toString();
        r.files.append(f);
    }
    return r;
}

} // namespace

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

void UsenetPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    if (!m_ipc)
        return;

    auto enable = [this] {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
    };

    if (m_ipc->isConnected()) {
        enable();
    } else {
        connect(m_ipc, &IpcClient::connected, this, enable);
    }

    connect(m_ipc, &IpcClient::disconnected, this, [this] {
        m_poller->setEnabled(false);
        m_model->clear();
        updateSummary();
    });

    // Pull the next poll forward rather than refetching here. Progress on a
    // 10 000-article release is a great many events, and the daemon has already
    // coalesced them per item — refetching the whole queue for each one would put
    // the quadratic cost straight back.
    connect(m_ipc, &IpcClient::usenetItemUpdated, this, [this](const Ipc::IpcMessage& msg) {
        const QCborMap row = msg.fieldMap(0);
        if (!row.isEmpty())
            m_model->upsertItem(rowFromCbor(row));
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

void UsenetPanel::addNzbFile(const QString& path)
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
    Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzb);
    msg.append(QCborValue(data));
    msg.append(QFileInfo(path).completeBaseName());

    m_ipc->sendRequest(msg, [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0)) {
            QMessageBox::warning(this, tr("Add NZB"), resp.fieldString(1));
            return;
        }
        m_poller->refreshNow();
    });
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

    m_addAction = toolbar->addAction(tr("Add NZB…"), this, &UsenetPanel::onAddNzb);
    toolbar->addSeparator();
    m_pauseAction = toolbar->addAction(tr("Pause"), this, &UsenetPanel::onPause);
    m_resumeAction = toolbar->addAction(tr("Resume"), this, &UsenetPanel::onResume);
    m_removeAction = toolbar->addAction(tr("Remove"), this, [this] { onRemove(false); });

    // Context menu only — a toolbar entry would be enabled for most of a
    // release's life and do nothing, because most posts are RAR sets.
    m_previewAction = new QAction(tr("Preview"), this);
    connect(m_previewAction, &QAction::triggered, this, &UsenetPanel::onPreview);
    layout->addWidget(toolbar);

    auto* view = new ListTreeView(this);
    m_view = view;
    m_model = new UsenetQueueModel(this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::DisplayRole);
    m_proxy->setDynamicSortFilter(true);

    m_view->setModel(m_proxy);
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
    view->bindColumns(QStringLiteral("usenetQueue"), {280, 80, 75, 160, 85, 85, 60});

    connect(m_view, &QWidget::customContextMenuRequested,
            this, &UsenetPanel::onContextMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] {
        // restoreSelection() clears and re-selects, so this fires twice mid-restore
        // against a half-built selection. The final state is applied right after.
        if (!m_restoringSelection)
            updateActions();
    });

    layout->addWidget(m_view, 1);

    m_summary = new QLabel(this);
    m_summary->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(m_summary);

    setAcceptDrops(false);
    updateActions();
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
            items.append(rowFromCbor(v.toMap()));
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
        QModelIndex src = m_proxy->mapToSource(current);
        if (m_model->isFileRow(src))
            src = src.parent();
        state.currentId = m_model->idAt(src.row());
    }

    for (int row = 0; row < m_model->itemCount(); ++row) {
        const QModelIndex proxyIdx = m_proxy->mapFromSource(m_model->index(row, 0));
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
        const QModelIndex proxyIdx = m_proxy->mapFromSource(m_model->index(row, 0));
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

        const QModelIndex proxyIdx = m_proxy->mapFromSource(m_model->index(row, 0));
        if (!proxyIdx.isValid())
            continue;

        // selectedRows() only reports a row when every model column is selected,
        // so select the whole row rather than just column 0.
        selection.select(proxyIdx,
                         m_proxy->index(proxyIdx.row(), UsenetQueueModel::ColCount - 1,
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
        QModelIndex src = m_proxy->mapToSource(proxyIdx);
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
    if (!ids.isEmpty()) {
        menu.addSeparator();
        menu.addAction(m_pauseAction);
        menu.addAction(m_resumeAction);
        menu.addSeparator();

        auto* priorityMenu = menu.addMenu(tr("Priority"));
        priorityMenu->addAction(tr("High"), this, [this] { onSetPriority(1); });
        priorityMenu->addAction(tr("Normal"), this, [this] { onSetPriority(0); });
        priorityMenu->addAction(tr("Low"), this, [this] { onSetPriority(-1); });

        menu.addSeparator();
        menu.addAction(m_previewAction);
        menu.addAction(tr("Open Folder"), this, &UsenetPanel::onOpenFolder);
        menu.addSeparator();
        menu.addAction(m_removeAction);
        menu.addAction(tr("Remove and Delete Files"), this, [this] { onRemove(true); });
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void UsenetPanel::onAddNzb()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add NZB"), QString(), tr("NZB files (*.nzb);;All files (*)"));
    if (!path.isEmpty())
        addNzbFile(path);
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

    for (const auto& f : row->files) {
        if (f.finalPath.isEmpty())
            continue;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(f.finalPath).absolutePath()));
        return;
    }

    StatusBarNotifier::post(tr("Nothing has completed yet for \"%1\".").arg(row->name));
}

QPair<QString, int> UsenetPanel::previewTarget() const
{
    // A file row names itself.
    const QModelIndex current = m_view->currentIndex();
    if (current.isValid()) {
        const QModelIndex src = m_proxy->mapToSource(current);
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
        const QModelIndex src = m_proxy->mapToSource(current);
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
        return;
    }

    int active = 0;
    qint64 total = 0;
    qint64 done = 0;
    for (int i = 0; i < count; ++i) {
        const auto* row = m_model->findById(m_model->idAt(i));
        if (!row)
            continue;
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
    m_summary->setText(tr("%1 download(s), %2 active — %3% complete")
                           .arg(count)
                           .arg(active)
                           .arg(percent));
}

} // namespace eMule
